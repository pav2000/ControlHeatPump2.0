/*
 * Copyright (c) 2016-2026 by Pavel Panfilov <firstlast2007@gmail.com>  telegram @pav20000
 * &                       by Vadim Kulakov vad7@yahoo.com, vad711
 * "Народный контроллер 2.0" для тепловых насосов.
 * Данное программное обеспечение предназначено для управления
 * различными типами тепловых насосов для отопления и ГВС.
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 */
/*
 * Датчики температуры: отдельная единица трансляции.
 *
 * SensorTemp.h тянет Constant.h, а с ним Config.h, поэтому #ifdef SENSOR_IP,
 * #ifdef DEMO и #if T_NUMSAMLES разрешаются так же, как в главной единице
 * трансляции. Проверено сверкой размеров методов.
 */
#include "SensorTemp.h"
#include "../core/SettingsLegacy.h"   // чтение старых форматов, файл к удалению
#include "Hardware.h"      // rtcSAM3X8, addressToHex
#include "../core/HeatPump.h"      // HP, set_Error
#include "../log/Journal.h"       // journal
#include "../core/Rtos.h"          // _delay, замки шин
#include "../net/ParamRegistry.h"   // таблица параметров веб-API (внизу файла)
#include "BusOneWire.h"            // OW_scanTable
// ---------------------------------------------------------------------------
// ---- Цифровые датчики температуры ----
 // Инициализация на входе номер датчика
void sensorTemp::initTemp(int sensor)
    { 
      err=OK;                                  // ошибка датчика (работа)
      numErrorRead=0;                          // Ошибок нет
      sumErrorRead=0;                          // ошибок нет
      cfg.number = sensor;
      minTemp=TempSetup[sensor].min;                 // минимальная разрешенная температура
      maxTemp=TempSetup[sensor].max;                 // максимальная разрешенная температура
      cfg.errTemp=TempSetup[sensor].err;                 // статическая ошибка датчика
      cfg.testTemp=TempSetup[sensor].test;               // Значение при тестировании
      lastTemp=STARTTEMP;                      // последняя считанная температура с датчика по умолчанию абсолютный ноль
      Temp=0;                                  // температура датчика (обработанная)
      flags=0x00;                              // Сбросить все флаги

      if(TempSetup[sensor].cfg_flags & 1) SETBIT1(flags, fPresent); // наличие датчика в текушей конфигурации
      memset(cfg.address, 0, sizeof(cfg.address));	   // обнуление адрес датчика
      busOneWire = NULL;
      testMode = NORMAL;                         // Значение режима тестирования
#if T_NUMSAMLES > 1
      memset(t, 0, sizeof(t));	   			   // обнуление буфера значений
      sum=0;
      last=0;
#endif
      nGap=0;                                  // Счечик "разорванных" данных  - требуется для фильтрации помехи
      note=(char*)TempSetup[sensor].note;            // присвоить наименование датчика
      name=(char*)TempSetup[sensor].name;            // присвоить имя датчика

     // Удаленные устройства  #ifdef SENSOR_IP
     #ifdef SENSOR_IP
      devIP=NULL;                             // Ссылка на привязаный датчик (класс) если NULL привявязки нет
     #endif
 }


// Чтение датчиков температуры, возвращает код ошибки, делает все преобразования
int8_t sensorTemp::Read() 
{  
	if(!(GETBIT(flags, fPresent))) return OK;          // датчик запрещен в конфигурации ничего не делаем
	if(testMode!=NORMAL) lastTemp=cfg.testTemp;             // В режиме теста присвоить значение теста
	else {                                              // Чтение датчиков
#ifdef DEMO
		if (strcmp(name,"TBOILER")==0) lastTemp=3000;       // В демо бойлер всегда 30 градусов: ниже порога догрева ТЭНом (TEMPR), иначе ТН ГВС не греет
		else lastTemp=random(101,1190);                     // В демо режиме генерим значения
#else   // чтение датчика
		if(!(GETBIT(flags,fAddress))) { // Адрес не установлен
			if(cfg.number == TCOMP) { // эти датчики должны быть привязаны
				set_Error(err = ERR_ADDRESS, name);
				return err;
			} else if(cfg.number == TIN) return OK; // Этот может получать значения от других датчиков
			else lastTemp = cfg.testTemp; // Если датчик не привязан, то присвоить значение теста
		} else {
			{
				int16_t ttemp;
				err = busOneWire->Read(cfg.address, ttemp);
				if(err != OK) {
					sumErrorRead++;
					if(!(err == ERR_ONEWIRE_CRC && get_setup_flag(fTEMP_ignory_CRC))) {
						if(!get_setup_flag(fTEMP_dont_log_errors)) {
							journal.jprintf_time("%s: Error ", name);
							if(err == ERR_ONEWIRE_CRC || (err == ERR_ONEWIRE_RW && busOneWire->get_rawErr() == OW_RAW_BADDATA)) { // Ошибка CRC или плохие данные, но успели прочитать температуру
								journal.jprintf("%s (%d). t=%.2d, prev=%.2d\n", err == ERR_ONEWIRE_CRC ? "CRC" : "bad data", err == ERR_ONEWIRE_CRC ? err : 0, ttemp, lastTemp);
							} else if(err == ERR_ONEWIRE_RW) journal.jprintf("read (%d)\n", busOneWire->get_rawErr());   // код драйвера обмена
							else journal.jprintf("%s (%d)\n", err == ERR_ONEWIRE ? "RESET" : "read", err);
						}
						if(++numErrorRead == 0) numErrorRead--;
						if(numErrorRead > NUM_READ_TEMP_ERR && !get_setup_flag(fTEMP_ignory_errors)) { // Слишком много ошибок чтения подряд - ошибка!
							int8_t code = Error_TempReadCode(err);
							if(code != err) journal.jprintf_time("%s: read errors limit, last %d\n", name, err);   // причина - в журнал, итоговый код общий
							set_Error(code, name);
						}
						return err;
					}
				}
				numErrorRead = 0; // Сброс счетчика ошибок
				// Защита от скачков
				if ((lastTemp==STARTTEMP)||(abs(lastTemp-ttemp) < (get_setup_flag(fTEMP_ignory_CRC) ? GAP_TEMP_VAL_CRC : GAP_TEMP_VAL))) {
					lastTemp=ttemp; nGap=0; // Первая итерация или нет скачка Штатная ситуация
				} else { // Данные сильно отличаются от предыдущих "СКАЧОК"
				   nGap++;
				   if (nGap > (get_setup_flag(fTEMP_ignory_CRC) ? GAP_NUMBER_CRC : GAP_NUMBER)) { // Больше максимальной длительности данные используем, счетчик сбрасываем
					   nGap = 0;
					   lastTemp = ttemp;
				   }
				   if(nGap == 0 || !get_setup_flag(fTEMP_dont_log_errors))
					   journal.jprintf_time("GAP %s t=%.2d, %s\n", name, ttemp, nGap == 0 ? "accept" : "skip");
				}
			}
		}
#endif
	}

	// Усреднение значений
  #if T_NUMSAMLES == 1         // При 1 - без усреднения
	Temp = lastTemp + cfg.errTemp;
  #else                        // буфер может быть не полным
	sum = sum-t[last];           // Убрать самое старое значение из суммы
	t[last] = lastTemp;          // Запомить новое значение
	sum = sum + lastTemp;          // Добавить новое значение

	if (last<(T_NUMSAMLES-1)) last++; else { last=0; SETBIT1(flags,fFull);}  // Установить признак буфер полный при T_NUMSAMLES=1 буфер всегда полный (придупреждение компилятора)
	if(GETBIT(flags,fFull))   Temp=sum/T_NUMSAMLES+errTemp;   // буфер полный
	else                      Temp=sum/last+errTemp;
  #endif

	// Проверка на ошибки именно здесь обрабатывются ошибки и передаются на верх
	if(Temp<minTemp) { set_Error(err = ERR_MINTEMP, name); return err; }
	if(Temp>maxTemp) { set_Error(err = ERR_MAXTEMP, name); return err; }
	// дошли до сюда значит ошибок нет
	return (err = OK);                                        // Новый цикл новые ошибки!! СБРОС ОШИБКИ
}

// установить значение систематической ошибки датчика диапазон +-MAX_TEMP_ERR не более
int8_t sensorTemp::set_errTemp(int16_t t)     
{
  if (abs(t)<MAX_TEMP_ERR) { cfg.errTemp=t; return OK;} else return WARNING_VALUE;
}

// Получить значение температуры датчика (n.nn) с учетом удаленных датчиков!!! - это то что используется в работе ТН
int16_t sensorTemp::get_Temp()            
{
 #ifdef SENSOR_IP                                       // используется удаленные датчики
 int16_t t;
 if (!(GETBIT(flags,fPresent))) return 0;               // проводной датчик запрещен в конфигурации ничего не делаем
 if( devIP && (devIP->get_fUse())&&(devIP->get_link()>-1))       // Удаленный датчик привязан к данному проводному датчику надо использовать
   {
    if (devIP->get_update()>UPDATE_IP)  return Temp;    // Время просрочено, удаленный датчик не используем
    t=devIP->get_Temp();                                // Получить значение температуры удаленного датчика
    if ((devIP->get_fRule())) return (t+Temp)/2;          // Усреднение датчиков
    else return t;                                      // Использование только удаленного датчика
   }
  else return Temp;                                     // датчик не привязан
 #else                                                  // При отсутствии удалённых датчиков сразу выводим проводной датчик
   return Temp;
 #endif
}

// Получить значение температуры ПРОВОДНОГО датчика
int16_t sensorTemp::get_rawTemp()
{
	return Temp;
}

// Установить значение температуры датчика в режиме теста
int8_t sensorTemp::set_testTemp(int16_t t)            
{
 if((t>=minTemp)&&(t<=maxTemp)) { cfg.testTemp=t; return OK;} else return WARNING_VALUE;
}

// Шина
uint8_t sensorTemp::get_bus()
{
	return cfg.setup_flags & fDS2482_bus_mask;
}

void sensorTemp::set_onewire_bus_type()
{
	// Привязка шины к датчику
	if(get_bus() == 0) busOneWire = &OneWireBus;	// 1 шина
}

// Установить адрес на шине датчика, bus
void sensorTemp::set_address(byte *addr, byte bus)
{
	uint8_t i;
	err = OK;
	cfg.setup_flags &= ~fDS2482_bus_mask;
	if(addr == NULL) // сброс адреса
	{
		memset(cfg.address, 0, sizeof(cfg.address));	   // обнуление адрес датчика
		SETBIT0(flags, fAddress);                  // Поставить флаг, что адрес не установлен
		return;
	}
	for(i = 0; i < sizeof(cfg.address); i++) cfg.address[i] = addr[i];   // Скопировать адрес
	cfg.setup_flags |= bus & fDS2482_bus_mask;
	after_load();
}
    
// Считать настройки из eeprom i2c на входе адрес с какого, на выходе конечный адрес, если число меньше 0 это код ошибки
void sensorTemp::after_load()
{
	if(cfg.address[0] || cfg.address[1]) // Если адрес не пустой то Установить адрес датчика
	{
		SETBIT1(flags, fAddress);  // Поставить флаг что адрес установлен
		set_onewire_bus_type();
	}
}

// Возвращает 1, если превышен предел ошибок
int8_t sensorTemp::inc_error(void)
{
	if(++numErrorRead == 0) numErrorRead--;
	return numErrorRead > NUM_READ_TEMP_ERR;
}

int8_t sensorTemp::get_radio_received_idx()
{
	return -1;
}


// ---- Удаленные датчики температуры ----
#ifdef SENSOR_IP
// Инициализация датчика
void sensorIP::initIP(uint8_t sensor)
{
    cfg.number = sensor;
	Temp=-12345;                 // Последние показания датчика
    num=-1;                      // Номер датчика, по нему осуществляется идентификация датачика о 0 до MAX_SENSOR_IP-1
    RSSI=-321;                   // Уровень сигнала датчика (-дБ)
    VCC=0;                       // Уровень напряжениея питания датчика (мВ)
    count=0;                     // Кольцевой счетчик пакетов с момента включения контрола
    stime=0;                     // Время получения последнего пакета
    cfg.flags=0;                     // флаги
    cfg.link=-1;                     // датчик не привязан
}

// Запомнить данные (обновление данных)
boolean sensorIP::set_DataIP(int16_t a,int16_t b,int16_t c,int16_t d,uint32_t e, IPAddress f)
{
num=a;                         // Номер датчика -1  датчик отсутсвует
Temp=b;                        // Последние показания датчика
RSSI=c;                        // Уровень сигнала датчика (-дБ)
VCC=d;                         // Уровень напряжениея питания датчика (мВ)
count=e;                       // Кольцевой счетчик пакетов с момента включения контрола
stime=rtcSAM3X8.unixtime();    // Время получения последнего пакета
ip=f;   
return true;      
}

// Получить параметр в виде строки
char* sensorIP::get_sensorIP(char *var, char *ret)
{
if(strcmp(var,ip_SENSOR_TEMP)==0)     {_ftoa(ret,(float)Temp/100.0,2); return ret;     } else
if(strcmp(var,ip_SENSOR_NUMBER)==0)   {return _itoa(num,ret);                      } else  
if(strcmp(var,ip_RSSI)==0)            {_ftoa(ret,(float)RSSI/10.0,1); return ret;      } else
if(strcmp(var,ip_VCC)==0)             {_ftoa(ret,(float)VCC/1000.0,2); return ret;     } else
if(strcmp(var,ip_SENSOR_USE)==0)      {if (GETBIT(cfg.flags,fUse)) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero);} else  
if(strcmp(var,ip_SENSOR_RULE)==0)     {if (GETBIT(cfg.flags,fRule)) return strcat(ret,(char*)cOne); else return  strcat(ret,(char*)cZero);} else     
if(strcmp(var,ip_SENSOR_IP)==0)       {return strcat(ret,IPAddress2String(ip));    } else  
if(strcmp(var,ip_SENSOR_COUNT)==0)    {return _itoa(count,ret);                    } else     
if(strcmp(var,ip_STIME)==0)           {if(stime==0) return strcat(ret,(char*)"none"); else return _itoa(rtcSAM3X8.unixtime()-stime,ret);} else  
if(strcmp(var,ip_SENSOR)==0)          {return strcat(ret,(char*)"----");           } else   
return strcat(ret,(char*)"E26");    
}

void sensorIP::after_load()
{
	if((cfg.link<-1)||(cfg.link >= TNUMBER)) { cfg.link=-1; num=-1; }    // если бредовое значение привязки отвязываем датчик
}

#endif  


// ---------------------------------------------------------------------------
// Реестр параметров веб-API: температурные датчики, ключи temp.<ИМЯ>.<атрибут>,
// и удалённые датчики IP, ключи ipsens.<N>.<атрибут>. Правила - как у oHP
// (HeatPumpSettings.cpp); значения идут через методы классов, прежние проверки
// (OK или код ошибки класса) сохранены: отказ класса даёт range.
// Имя температурного датчика - как отдаёт get_name() (оно же в топиках MQTT).
// Флаг flag6 («тёплый пол») убран из прошивки вместе с функцией и сюда не входит.
// Приём данных от самих удалённых датчиков (set_sensorIP) - отдельный путь в
// WebApiGet.cpp, формат прежний.
// ---------------------------------------------------------------------------
enum { T_VALUE = 1, T_RAW, T_MIN, T_MAX, T_ERR, T_TEST, T_ECODE, T_ESUM, T_PRESENT, T_NOTE, T_ADDR, T_BUS, T_F1, T_F2, T_F3, T_F4, T_F5 };
#define TA(nm, fl, kind, hk) { nm, 0, 0, 0, 0, 100, PT_CALL, fl, kind, hk, 0 }
static const ApiParam apiTempTab[] = {
	TA("value",   PF_RO, MK_NUM,  T_VALUE),     // итог с учётом удалённого датчика; null - первого измерения ещё не было
	TA("raw",     PF_RO, MK_NUM,  T_RAW),       // показание проводного датчика
	TA("min",     PF_RO, MK_NUM,  T_MIN),       // допустимый диапазон: за ним - ошибка датчика
	TA("max",     PF_RO, MK_NUM,  T_MAX),
	TA("err",     0,     MK_NUM,  T_ERR),       // поправка (систематическая ошибка)
	TA("test",    0,     MK_NUM,  T_TEST),      // значение в режиме теста
	TA("ecode",   PF_RO, MK_INT,  T_ECODE),     // код последней ошибки
	TA("esum",    PF_RO, MK_INT,  T_ESUM),      // число ошибок чтения с момента перезагрузки
	TA("present", PF_RO, MK_BOOL, T_PRESENT),
	TA("note",    PF_RO, MK_TEXT, T_NOTE),
	TA("addr",    0,     MK_STR,  T_ADDR),      // чтение: адрес OneWire или null; запись: номер из последнего сканирования (0 - отвязать)
	TA("bus",     PF_RO, MK_INT,  T_BUS),       // номер шины или null, если датчик не привязан
	TA("flag1",   0,     MK_BOOL, T_F1),        // игнорировать ошибки датчика
	TA("flag2",   0,     MK_BOOL, T_F2),        // не логировать ошибки
	TA("flag3",   0,     MK_BOOL, T_F3),        // игнорировать ошибки CRC
	TA("flag4",   0,     MK_BOOL, T_F4),        // участвует в средней температуре TIN
	TA("flag5",   0,     MK_BOOL, T_F5),        // участвует в минимальной температуре TIN
};
static uint8_t api_temp_n(void) { return TNUMBER; }
static const char *api_temp_name(uint8_t i) { return HP.sTemp[i].get_name(); }
static uint8_t api_temp_ok(uint8_t i) { return HP.sTemp[i].get_present(); }
static int api_get_temp(uint8_t hook, uint8_t dev, ApiOut *o)
{
	sensorTemp &s = HP.sTemp[dev];
	switch(hook) {
	case T_VALUE:   if(s.get_Temp() == STARTTEMP) api_put(o, "null"); else api_put_dec(o, s.get_Temp(), 100); break;
	case T_RAW:     if(s.get_Temp() == STARTTEMP) api_put(o, "null"); else api_put_dec(o, s.get_rawTemp(), 100); break;
	case T_MIN:     api_put_dec(o, s.get_minTemp(), 100); break;
	case T_MAX:     api_put_dec(o, s.get_maxTemp(), 100); break;
	case T_ERR:     api_put_dec(o, s.get_errTemp(), 100); break;
	case T_TEST:    api_put_dec(o, s.get_testTemp(), 100); break;
	case T_ECODE:   api_put_int(o, s.get_lastErr()); break;
	case T_ESUM:    api_put_int(o, s.get_sumErrorRead()); break;
	case T_PRESENT: api_put(o, s.get_present() ? "true" : "false"); break;
	case T_NOTE:    api_put_str(o, s.get_note()); break;
	case T_ADDR:    if(!s.get_fAddress()) api_put(o, "null"); else api_put_str(o, addressToHex(s.get_address())); break;
	case T_BUS:     if(!s.get_fAddress()) api_put(o, "null"); else api_put_int(o, s.get_bus() + 1); break;
	case T_F1: case T_F2: case T_F3: case T_F4: case T_F5:
		api_put(o, s.get_setup_flag(hook - T_F1 + fTEMP_ignory_errors) ? "true" : "false"); break;
	default: return AE_FAIL;
	}
	return AE_OK;
}
static int api_set_temp(uint8_t hook, uint8_t dev, const char *val)
{
	sensorTemp &s = HP.sTemp[dev];
	float x = my_atof(val);
	if(hook >= T_F1 && hook <= T_F5) {
		if(!strcmp(val, "1") || !strcmp(val, "true")) s.set_setup_flag(hook - T_F1 + fTEMP_ignory_errors, 1);
		else if(!strcmp(val, "0") || !strcmp(val, "false")) s.set_setup_flag(hook - T_F1 + fTEMP_ignory_errors, 0);
		else return AE_BAD_VALUE;
		return AE_OK;
	}
	if(x == ATOF_ERROR) return AE_BAD_VALUE;
	switch(hook) {
	case T_ERR:  return s.set_errTemp(rd(x, 100)) == OK ? AE_OK : AE_RANGE;
	case T_TEST: return s.set_testTemp(rd(x, 100)) == OK ? AE_OK : AE_RANGE;
	case T_ADDR: {                                    // номер в таблице последнего сканирования: 0 - сбросить адрес
		if(x < 0 || x > TNUMBER || x != (float)(int)x) return AE_RANGE;
		uint8_t n = (uint8_t) x;
		if(n == 0) s.set_address(NULL, 0);
		else if(OW_scanTable) s.set_address(OW_scanTable[n - 1].address, OW_scanTable[n - 1].bus);
		return AE_OK; }
	}
	return AE_FAIL;
}
static const ApiOps apiOpsTemp = { 0, api_get_temp, api_set_temp, 0, 0, api_temp_n, api_temp_name, api_temp_ok };
extern const ApiGroup ApiGrpTemp;
const ApiGroup ApiGrpTemp = { "temp", apiTempTab, sizeof(apiTempTab) / sizeof(apiTempTab[0]), &apiOpsTemp };

#ifdef SENSOR_IP
// ---- удалённые датчики IP: устройства "1".."IPNUMBER" ----
enum { I_TEMP = 1, I_AGE, I_IP, I_RSSI, I_VCC, I_COUNT, I_USE, I_RULE, I_LINK };
static_assert(IPNUMBER <= 8, "имена удалённых датчиков в таблице api_ip_names - увеличить при росте IPNUMBER");
static const ApiParam apiIpTab[] = {
	{ "temp",  0, 0, 0, 0, 100,  PT_CALL, PF_RO, MK_NUM,  I_TEMP,  0 },       // null - данных нет или они просрочены (UPDATE_IP)
	{ "age",   0, 0, 0, 0, 1,    PT_CALL, PF_RO, MK_INT,  I_AGE,   0 },       // секунд с последнего пакета; null - пакетов не было
	{ "ip",    0, 0, 0, 0, 1,    PT_CALL, PF_RO, MK_IP,   I_IP,    0 },
	{ "rssi",  0, 0, 0, 0, 10,   PT_CALL, PF_RO, MK_NUM,  I_RSSI,  0 },       // дБ
	{ "vcc",   0, 0, 0, 0, 1000, PT_CALL, PF_RO, MK_NUM,  I_VCC,   0 },       // В
	{ "count", 0, 0, 0, 0, 1,    PT_CALL, PF_RO, MK_INT,  I_COUNT, 0 },
	{ "use",   0, 0, 0, 0, 1,    PT_CALL, 0,     MK_BOOL, I_USE,   0 },       // использовать датчик
	{ "rule",  0, 0, 0, 0, 1,    PT_CALL, 0,     MK_BOOL, I_RULE,  0 },       // усреднять с проводным (иначе заменять)
	{ "link",  0, 0, 0, 0, 1,    PT_CALL, 0,     MK_STR,  I_LINK,  0 },       // имя температурного датчика, с которым связан; пусто - не связан
};
static uint8_t api_ip_n(void) { return IPNUMBER; }
static const char *api_ip_name(uint8_t i) { static const char *const n[] = { "1", "2", "3", "4", "5", "6", "7", "8" }; return n[i]; }   // const
static uint8_t api_ip_ok(uint8_t) { return 1; }
static int api_get_ip(uint8_t hook, uint8_t dev, ApiOut *o)
{
	sensorIP &s = HP.sIP[dev];
	switch(hook) {
	case I_TEMP:  if(!s.get_count() || s.get_update() > UPDATE_IP) api_put(o, "null"); else api_put_dec(o, s.get_Temp(), 100); break;
	case I_AGE:   if(!s.get_count()) api_put(o, "null"); else api_put_int(o, s.get_update()); break;
	case I_IP:    if(!s.get_count()) api_put(o, "null"); else { IPAddress a = s.get_ip(); api_put_str(o, IPAddress2String(a)); } break;
	case I_RSSI:  if(!s.get_count()) api_put(o, "null"); else api_put_dec(o, s.get_RSSI(), 10); break;
	case I_VCC:   if(!s.get_count()) api_put(o, "null"); else api_put_dec(o, s.get_VCC(), 1000); break;
	case I_COUNT: api_put_int(o, s.get_count()); break;
	case I_USE:   api_put(o, s.get_fUse() ? "true" : "false"); break;
	case I_RULE:  api_put(o, s.get_fRule() ? "true" : "false"); break;
	case I_LINK:  api_put_str(o, s.get_link() < 0 ? "" : HP.sTemp[s.get_link()].get_name()); break;
	default: return AE_FAIL;
	}
	return AE_OK;
}
static int api_set_ip(uint8_t hook, uint8_t dev, const char *val)
{
	sensorIP &s = HP.sIP[dev];
	if(hook == I_USE || hook == I_RULE) {
		bool b;
		if(!strcmp(val, "1") || !strcmp(val, "true")) b = true; else if(!strcmp(val, "0") || !strcmp(val, "false")) b = false; else return AE_BAD_VALUE;
		if(hook == I_USE) s.set_fUse(b); else s.set_fRule(b);
		return AE_OK;
	}
	if(hook == I_LINK) {
		int idx = -1;
		if(*val) {                                    // имя присутствующего температурного датчика
			for(int i = 0; i < TNUMBER; i++) if(HP.sTemp[i].get_present() && !strcmp(val, HP.sTemp[i].get_name())) { idx = i; break; }
			if(idx < 0) return AE_RANGE;
		}
		s.set_link(idx);
		HP.updateLinkIP();                            // обновить ВСЕ привязки удалённых датчиков
		return AE_OK;
	}
	return AE_FAIL;
}
static const ApiOps apiOpsIp = { 0, api_get_ip, api_set_ip, 0, 0, api_ip_n, api_ip_name, api_ip_ok };
extern const ApiGroup ApiGrpIpsens;
const ApiGroup ApiGrpIpsens = { "ipsens", apiIpTab, sizeof(apiIpTab) / sizeof(apiIpTab[0]), &apiOpsIp };
#endif
