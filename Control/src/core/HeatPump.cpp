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
 * Ядро теплового насоса: инициализация, состояния и ошибки, пуск и останов,
 * компрессор, разморозка, насосы и бойлер, расчёт целевых температур и ПИД,
 * главный цикл обновления vUpdate. Рядом - HeatPumpCharts, HeatPumpControl и
 * HeatPumpSettings.
 *
 * HeatPump.h тянет Constant.h, а с ним Config.h, поэтому все условия
 * конфигурации разрешаются так же, как в главной единице трансляции.
 * Проверено сверкой размеров методов.
 */
#include "HeatPump.h"     // HeatPump, HP, устройства, датчики, Charts[]
#include "../hw/Nextion.h"      // myNextion - обновление дисплея из vUpdate

// ---------------------------------------------------------------------------
// Описание базового класса для работы Теплового Насоса  ==false
// ---------------------------------------------------------------------------
#include "HeatPump.h"
#include "ClimateControl.h"  // решение об управлении компрессором
#include "OverheatCalc.h"    // OHN_*: какие датчики нужны правилу перегрева

// Объявлены в HeatPump.h - создаются здесь, в единственном экземпляре.
int32_t motohour_OUT_work = 0; // рабочий для счетчиков - энергия отопления, Вт
int32_t motohour_IN_work = 0;  // рабочий для счетчиков - энергия потребленная, Вт
uint16_t task_updstat_chars = 0;
#ifdef CHART_ONLY_COMP_ON
boolean  Charts_when_comp_on = true;	// Графики в памяти только во время работы компрессора
#else
boolean  Charts_when_comp_on = false;
#endif


#define PUMPS_ON          Pumps(true, DELAY_AFTER_SWITCH_RELAY)               // Включить насосы
#define PUMPS_OFF         Pumps(false, DELAY_AFTER_SWITCH_RELAY)              // Выключить насосы
// Макросы по работе с компрессором в зависимости от наличия инвертора
#define COMPRESSOR_ON   { if(dFC.get_present()) dFC.start_FC(); else dRelay[RCOMP].set_ON(); startCompressor = Uptime_s(); }  // Включить компрессор в зависимости от наличия инвертора
#define COMPRESSOR_OFF  { if(dFC.get_present()) dFC.stop_FC(); else dRelay[RCOMP].set_OFF(); stopCompressor = Uptime_s(); } // Выключить компрессор в зависимости от наличия инвертора

// Аварийный останов компрессора из set_Error: без журнала и ожиданий, способ выбирается так же, как в COMPRESSOR_OFF.
// Реле RCOMP снимается всегда: это провод ХОД инвертора или пускатель компрессора без инвертора, а при управлении
// инвертором по Modbus оно не используется (реле нет - set_OFF ничего не делает). Без провода ХОД инвертор
// останавливается ещё и командой по шине (Н-24: прежде пускатель на RCOMP в такой сборке не снимался).
// true - останов отдан.
bool HeatPump::compressor_emergency_off()
{
	dRelay[RCOMP].set_OFF();
#ifndef FC_USE_RCOMP
	if(dFC.get_present()) return dFC.emergency_stop() == OK;
#endif
	return true;
}


// Установка критической ошибки для класса ТН вызывает останов ТН
// Возвращает ошибку останова ТН
void set_Error(int8_t _err, char *nam)
{
	if(HP.is_compressor_on())    // СРАЗУ Если компрессор включен, выключить  ГЛАВНАЯ ЗАЩИТА
	{
		journal.jprintf("$Compressor protection ");
		if(HP.compressor_emergency_off()) HP.set_stopCompressor();
	}

	// Занять поле ошибки одной атомарной операцией: задачи с разными приоритетами зовут set_Error одновременно,
	// и проверка с записью по отдельности давали две записи в source_error/note_error и двойной process_error
	if(Error_Claim(&HP.error, _err)) {
		// Сборка строки "<время> <источник>: <описание>" делается в ErrorCodes:
		// там же лежит расшифровка кода, и там она проверяется на хосте -
		// вместе с обрезкой по границам буферов.
		Error_Store(HP.source_error, sizeof(HP.source_error),
		            HP.note_error,   sizeof(HP.note_error),
		            NowTimeToStr(), nam, _err);
		journal.jprintf_time("$ERROR source: %s, code: %d\n", nam, _err);
		if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) HP.save_DumpJournal(true); // вывод отладочной информации для начала  если запущена freeRTOS
		HP.message.setMessage(pMESSAGE_ERROR, HP.note_error, _err); // уведомление об ошибке; повтор узнаётся по коду
		// Сюда ставить надо останов ТН !!!!!!!!!!!!!!!!!!!!!
		HP.process_error();
	}
}

void HeatPump::process_error(void)
{
	if(get_State() == pOFF_HP) return;   // Насос выключен, выключать нечего

	// Решение принимается в ErrorCodes - там же, где лежит список
	// критических ошибок и расшифровка кодов. Режим "не перезапускать после
	// критических ошибок" приходит параметром, а не через препроцессор:
	// иначе его ветка не собирается и не проверяется ничем.
	ERROR_ACTION act = Error_Decide(false, get_nStart(), num_repeat,
#ifdef NOT_RESTART_ON_CRITICAL_ERRORS
	                                true,
#else
	                                false,
#endif
	                                error);
	if(act == ERR_ACT_Repeat) sendCommand(pREPEAT);     // Повторный пуск ТН
	else if(act == ERR_ACT_Stop) sendCommand(pSTOP);    // Останов без попыток пуска

	if(get_State() == pSTARTING_HP) set_HP_error_state();   // Ошибка во время старта
}

void HeatPump::initHeatPump()
{
	uint8_t i;
	NO_Power = 0;
	flags = 0;
	eraseError();

	for(i = 0; i < TNUMBER; i++) sTemp[i].initTemp(i);            // Инициализация датчиков температуры

#ifdef SENSOR_IP
	for(i=0;i<IPNUMBER;i++) sIP[i].initIP(i);               // Инициализация удаленных датчиков
#endif
	sADC[PEVA].initSensorADC(PEVA, ADC_SENSOR_PEVA, FILTER_SIZE);          // Инициализация аналогового датчика PEVA
	sADC[PCON].initSensorADC(PCON, ADC_SENSOR_PCON, FILTER_SIZE);          // Инициализация аналогового датчика TCON

	for(i = 0; i < INUMBER; i++) sInput[i].initInput(i);           // Инициализация контактных датчиков
	for(i = 0; i < FNUMBER; i++) sFrequency[i].initFrequency(i);  // Инициализация частотных датчиков
	for(i = 0; i < RNUMBER; i++) dRelay[i].initRelay(i);           // Инициализация реле

#ifdef EEV_DEF
	dEEV.initEEV();                                           // Инициализация ЭРВ
#endif

	// Инициализация Modbus  перед инвертором и счетчиком
	journal.jprintf("Init Modbus RTU via RS485:");
	if(Modbus.initModbus() == OK) journal.jprintf(" OK\r\n");  //  выводим сообщение об установлении связи
	else {
		journal.jprintf(" not present config\r\n");
	}         //  нет в конфигурации

	dFC.initFC();                                              // Инициализация FC
#ifdef USE_ELECTROMETER_SDM
	dSDM.initSDM();                                            // инициализация счетчика
#endif
	message.initMessage(MAIN_WEB_TASK);                        // Инициализация Уведомлений, параметр - номер потока сервера в котором идет отправка
#ifdef MQTT
	clMQTT.initMQTT(MAIN_WEB_TASK);                            // Инициализация MQTT, параметр - номер потока сервера в котором идет отправка
#endif

	// Графики в памяти
	for(i = 0; i < sizeof(Charts) / sizeof(Charts[0]); i++)
		if(!Charts[i].init(CHART_POINT)) set_Error(ERR_OUT_OF_MEMORY, (char*)"Charts");
	clearChart();

	resetSettingHP();                                          // все переменные
}
// Стереть последнюю ошибку
void HeatPump::eraseError()
{
	strcpy(note_error, "OK");          // Строка c описанием ошибки
	strcpy(source_error, "");          // Источник ошибки
	error = OK;                         // Код ошибки
}

// Получить число ошибок чтения ВСЕХ датчиков температуры
uint32_t HeatPump::get_errorReadDS18B20()
{
	uint32_t sum = 0;
	for(uint8_t i = 0; i < TNUMBER; i++) sum += sTemp[i].get_sumErrorRead();     // Суммирование ошибок по всем датчикам
	return sum;
}

void HeatPump::Reset_TempErrors()
{
	for(uint8_t i=0; i<TNUMBER; i++) sTemp[i].Reset_Errors();
}

// Установить состояние ТН, при необходимости пишем состояние в ЕЕПРОМ
// true - есть изменения false - нет изменений
boolean HeatPump::setState(TYPE_STATE_HP st)
{
  if (st==Status.State) return false;     // Состояние не меняется
  switch (st)
  {
  case pOFF_HP:       Status.State=pOFF_HP; if(GETBIT(motoHour.flags,fMH_ON))   {SETBIT0(motoHour.flags,fMH_ON);save_motoHour();}  break;  // 0 ТН выключен, при необходимости записываем в ЕЕПРОМ
  case pSTARTING_HP:  Status.State=pSTARTING_HP; break;                                                                                    // 1 Стартует
  case pSTOPING_HP:   Status.State=pSTOPING_HP;  break;                                                                                    // 2 Останавливается
  case pWORK_HP:      Status.State=pWORK_HP;if(!(GETBIT(motoHour.flags,fMH_ON))) {SETBIT1(motoHour.flags,fMH_ON);save_motoHour();}  break; // 3 Работает, при необходимости записываем в ЕЕПРОМ
  case pWAIT_HP:      Status.State=pWAIT_HP;if(!(GETBIT(motoHour.flags,fMH_ON))) {SETBIT1(motoHour.flags,fMH_ON);save_motoHour();}  break; // 4 Ожидание, при необходимости записываем в ЕЕПРОМ
  case pERROR_HP:     Status.State=pERROR_HP;    break;                                                                                    // 5 Ошибка ТН
  case pERROR_CODE:                                                                                                                        // 6 - Эта ошибка возникать не должна!
  default:            Status.State=pERROR_HP;    break;                                                                                    // Обязательно должен быть последним, добавляем ПЕРЕД!!!
  }   
 return true; 
}

// возвращает строку с найденными датчиками
void HeatPump::scan_OneWire(char *result_str)
{
	if(get_State() == pWORK_HP) {   // ТН работает
		return;
	}
	if(!OW_scan_flags && OW_prepare_buffers()) {
		OW_scan_flags = 1; // Идет сканирование
		journal.jprintf("Scan 1-Wire:\n");
		OneWireBus.Scan(result_str);
		journal.jprintf("Found: %d\n", OW_scanTableIdx);
		OW_scan_flags = 0;
	}
}

// Получить флаг возможности синхронизации по NTP
boolean HeatPump::get_updateNTP()
{
  return GETBIT(DateTime.flags,fUpdateNTP);
}

// Установить значение текущий режим работы
void HeatPump::set_testMode(TEST_MODE b)
{
  int i=0; 
  for(i=0;i<TNUMBER;i++) sTemp[i].set_testMode(b);         // датчики температуры
  for(i=0;i<ANUMBER;i++) sADC[i].set_testMode(b);          // Датчик давления
  for(i=0;i<INUMBER;i++) sInput[i].set_testMode(b);        // Датчики сухой контакт
  for(i=0;i<FNUMBER;i++) sFrequency[i].set_testMode(b);    // Частотные датчики
  
  for(i=0;i<RNUMBER;i++) dRelay[i].set_testMode(b);        // Реле
  #ifdef EEV_DEF
  dEEV.set_testMode(b);                                    // ЭРВ
  #endif
  dFC.set_testMode(b);                                     // Инвертор
  testMode=b; 
  // новый режим начинаем без ошибок - не надо при старте ошибки трутся
}

// Получить источник загрузки веб морды
TYPE_SOURSE_WEB HeatPump::get_SourceWeb()
{
	// Аварийный режим по кнопке KEY1 при включении: морда берётся из кода,
	// носители не опрашиваются вовсе. Флаг живёт только в ОЗУ и пропадает
	// при следующей загрузке - настройку места хранения здесь трогать нельзя,
	// иначе режим залипнет.
	if(forceMinWeb) return pMIN_WEB;
	// Носитель годен, только если на нём НАЙДЕН индексный файл, то есть
	// get_f* вернул 2. Значение 1 - носитель есть, индекса нет: ровно так
	// выглядит оборвавшаяся заливка морды.
	//
	// Берём предпочитаемый по настройке, если не годится - второй, если и он
	// не годится - встроенную из кода. Откат симметричен: и при значении 0 (носителя
	// нет), и при 1 (есть, но без индекса) пробуется следующий, потому что в обоих
	// случаях морду отсюда не взять.
	if(get_WebStoreOnSPIFlash()) {
		if(get_fSPIFlash() == 2) return pFLASH_WEB;
		if(get_fSD()       == 2) return pSD_WEB;
	} else {
		if(get_fSD()       == 2) return pSD_WEB;
		if(get_fSPIFlash() == 2) return pFLASH_WEB;
	}
	return pMIN_WEB;
}

// ---------------------------------------------------------------------------
// ---- СОХРАНЕНИЕ ВОССТАНОВЛЕНИЕ ----
// ---------------------------------------------------------------------------
// Обновить ВСЕ привязки удаленных датчиков
// Вызывается  при восстановлении настроек и изменении привязки, для упрощения сделана перепривязка Всех датчиков
#ifdef SENSOR_IP
   boolean HeatPump::updateLinkIP()     
    {
    uint8_t i;
    for(i=0;i<TNUMBER;i++) sTemp[i].devIP=NULL;                 // Ссылка на привязаный датчик (класс) если NULL привявязки нет
    for(i=0;i<IPNUMBER;i++)
         {
          if ((sIP[i].get_fUse())&&(sIP[i].get_link()!=-1))              // включено использование
            if ((sIP[i].get_link()>=0)&&(sIP[i].get_link()<TNUMBER))     // находится в диапазоне можно привязывать
                   sTemp[sIP[i].get_link()].devIP= &sIP[i];    
         }
    return true;     
    }
#endif

// ---- РАБОТА с НАСТРОЙКАМИ ТН ----
// Записать настройки в память i2c: <size all><Option><DateTime><Network><message><dFC> <TYPE_sTemp><sTemp[TNUMBER]><TYPE_sADC><sADC[ANUMBER]><TYPE_sInput><sInput[INUMBER]>...<0x0000><CRC16>
// стартовый адрес I2C_SETTING_EEPROM
// Возвращает ошибку или число записанных байт


// Установить рабочий профиль по текущему Prof
void HeatPump::set_profile()
{
	Option.numProf = Prof.get_idProfile();
}

// Сменить рабочий профиль. Одна точка входа для расписания (UpdateHP) и веб-действия profile.load: копирование
// слота идёт одним блоком внутри Prof.load, номер профиля запоминается после него.
int32_t HeatPump::switch_profile(int8_t num)
{
	int32_t r = Prof.load(num);
	if(r >= 0) set_profile();
	return r;
}

// ---------------------------------------------------------------------------
// ---- ФУНКЦИИ РАБОТЫ С ГРАФИКАМИ ТН ----
// ---------------------------------------------------------------------------

// получить список доступных графиков в виде строки
// cat true - список добавляется в конец, false - строка обнуляется и список добавляется

// расчитать хеш для пользователя возвращает длину хеша
uint8_t HeatPump::set_hashUser()
{
	char buf[20];
	strcpy(buf, NAME_USER);
	strcat(buf, ":");
	strcat(buf, Network.passUser);
	base64_encode(Security.hashUser, buf, strlen(buf));
	Security.hashUserLen = strlen(Security.hashUser);
	journal.jprintf(" Hash user: len=%d\n", (int)Security.hashUserLen);       // хеш - это base64 логина и пароля, в журнал его не пишем
	return Security.hashUserLen;
}
// расчитать хеш для администратора возвращает длину хеша
uint8_t HeatPump::set_hashAdmin()
{
	char buf[20];
	strcpy(buf, NAME_ADMIN);
	strcat(buf, ":");
	strcat(buf, Network.passAdmin);
	base64_encode(Security.hashAdmin, buf, strlen(buf));
	Security.hashAdminLen = strlen(Security.hashAdmin);
	journal.jprintf(" Hash admin: len=%d\n", (int)Security.hashAdminLen);       // хеш - это base64 логина и пароля, в журнал его не пишем
	return Security.hashAdminLen;
}

// Обновить настройки дисплея Nextion
void HeatPump::updateNextion(bool need_init)
{
#ifdef NEXTION
	if(GETBIT(Option.flags, fNextion))  // Дисплей подключен
	{
		if(need_init) {
			journal.jprintf("Nextion init:");
			myNextion.init();
		}
		myNextion.init_display();
		myNextion.set_need_refresh();
	} else                        // Дисплей выключен
	{
	}
#endif
}

// Переключение на следующий режим работы отопления (последовательный перебор режимов)
void HeatPump::set_nextMode()
{
   Prof.SaveON.mode = HouseModeNext(get_modeHouse(), HouseOnlyHeat);
}

// Изменить целевую температуру с проверкой допустимости значений
// Параметр само ИЗМЕНЕНИЕ температуры
int16_t HeatPump::setTargetTemp(int16_t dt)
{
	switch((int)get_modeHouse())   // проверка для режима ДОМА
	{
	case pOFF:
		break;
	case pHEAT:
		if(TargetIsTemp1(get_ruleHeat(), GETBIT(Prof.Heat.flags, fTarget))) {
			if((Prof.Heat.Temp1 + dt >= 0) && (Prof.Heat.Temp1 + dt <= 4000)) Prof.Heat.Temp1 = Prof.Heat.Temp1 + dt;
			return Prof.Heat.Temp1;
		} else {
			if((Prof.Heat.Temp2 + dt >= 1000) && (Prof.Heat.Temp2 + dt <= 5000)) Prof.Heat.Temp2 = Prof.Heat.Temp2 + dt;
			return Prof.Heat.Temp2;
		}
		break;
	case pCOOL:
		if(TargetIsTemp1(get_ruleCool(), GETBIT(Prof.Cool.flags, fTarget))) {
			if((Prof.Cool.Temp1 + dt >= 0) && (Prof.Cool.Temp1 + dt <= 3000)) Prof.Cool.Temp1 = Prof.Cool.Temp1 + dt;
			return Prof.Cool.Temp1;
		} else {
			if((Prof.Cool.Temp2 + dt >= 0) && (Prof.Cool.Temp2 + dt <= 5000)) Prof.Cool.Temp2 = Prof.Cool.Temp2 + dt;
			return Prof.Cool.Temp2;
		}
		break;
	}
	return 0;
}

int16_t HeatPump::get_targetTempCool()
{
	int16_t T;
	T = TargetIsTemp1(get_ruleCool(), GETBIT(Prof.Cool.flags, fTarget)) ? Prof.Cool.Temp1 : Prof.Cool.Temp2;
	T += Schdlr.get_temp_change();
	return T;
}

int16_t HeatPump::get_targetTempHeat()
{
	int16_t T;
	T = TargetIsTemp1(get_ruleHeat(), GETBIT(Prof.Heat.flags, fTarget)) ? Prof.Heat.Temp1 : Prof.Heat.Temp2;
	if(Prof.Heat.add_delta_temp != 0) {
		int8_t h = rtcSAM3X8.get_hours();
		if((Prof.Heat.add_delta_end_hour >= Prof.Heat.add_delta_hour && h >= Prof.Heat.add_delta_hour && h <= Prof.Heat.add_delta_end_hour)
			|| (Prof.Heat.add_delta_end_hour < Prof.Heat.add_delta_hour && (h >= Prof.Heat.add_delta_hour || h <= Prof.Heat.add_delta_end_hour)))
			T += Prof.Heat.add_delta_temp;
	}

	if(Prof.Heat.kWeatherTarget != 0) { // Погодозависимость
		int32_t tmp = Prof.Heat.kWeatherTarget * (Prof.Heat.WeatherBase * 100 - sTemp[TOUT].get_Temp()) / 1000;
		int32_t tmp_r = Prof.Heat.WeatherTargetRange * 10;
		if(tmp > tmp_r) tmp = tmp_r;
		else if(tmp < -tmp_r) tmp = -tmp_r;
		T += tmp;
	}
	T += Schdlr.get_temp_change();
	return T;
}

// ИЗМЕНИТЬ целевую температуру бойлера с проверкой допустимости значений
int16_t HeatPump::setTempTargetBoiler(int16_t dt)
{
  if ((Prof.Boiler.TempTarget+dt>=5.0*100)&&(Prof.Boiler.TempTarget+dt<=90.0*100))   Prof.Boiler.TempTarget=Prof.Boiler.TempTarget+dt;
  return Prof.Boiler.TempTarget;     
}

// Получить целевую температуру бойлера с учетом корректировки
int16_t HeatPump::get_boilerTempTarget()
{
	 if(Prof.Boiler.add_delta_temp != 0) {
		int8_t h = rtcSAM3X8.get_hours();
		if((Prof.Boiler.add_delta_end_hour >= Prof.Boiler.add_delta_hour && h >= Prof.Boiler.add_delta_hour && h <= Prof.Boiler.add_delta_end_hour)
			|| (Prof.Boiler.add_delta_end_hour < Prof.Boiler.add_delta_hour && (h >= Prof.Boiler.add_delta_hour || h <= Prof.Boiler.add_delta_end_hour)))
			return Prof.Boiler.TempTarget + Prof.Boiler.add_delta_temp;
	 }
	 return Prof.Boiler.TempTarget;
}

// Получить целевую температуру отопления
void HeatPump::getTargetTempStr(char *rstr)
{
	switch(get_modeHouse())   // проверка отопления
	{
	case pHEAT:
		rstr = dptoa(rstr, get_targetTempHeat(), 2);
		break;
	case pCOOL:
		rstr = dptoa(rstr, get_targetTempCool(), 2);
		break;
	default:
		strcpy(rstr, "-.-");
		return;
	}
	*--rstr = '\0';
}

// ---------------------------------------------------------------------------
// ---- ОСНОВНЫЕ ФУНКЦИИ РАБОТЫ ТН ----
// ---------------------------------------------------------------------------

// Все реле выключить
void HeatPump::relayAllOFF()
{
  uint8_t i;
  journal.jprintf(" All relay off\n");
  for(i=0;i<RNUMBER;i++)  dRelay[i].set_OFF();         // Выключить все реле;
}                               

// Переключение на бойлер или обратно (true-бойлер false-отопление/охлаждение) возврат onBoiler
// в зависимости от режима, не забываем менять onBoiler по нему определяется включение ГВС
// Функция вызывается на работающем компрессоре
boolean HeatPump::switchBoiler(boolean b)
{
	
	journal.jprintf(" swBoiler(%d): %X, mW:%d\n", b, onBoiler, get_modWork());
#ifdef R3WAY
	dRelay[R3WAY].set_Relay(b);            // Установить в нужное положение 3-х ходового
	Pump_HeatFloor(b);
#else // Нет трехходового - схема с двумя насосами
	// ставим сюда код переключения ГВС/отопление в зависимости от onBoiler=true - ГВС
	if(b) { // переключение на ГВС
		Pump_HeatFloor(false);		 // выключить насос ТП
		dRelay[RPUMPO].set_OFF();    // файнкойлы выключить
		
	} else { // Переключение с ГВС на Отопление/охлаждение идет анализ по режиму работы дома
		if((Status.modWork != pOFF) && (get_modeHouse() != pOFF) && (get_State() != pSTOPING_HP)) { // Если не пауза И отопление/охлаждение дома НЕ выключено И нет процесса выключения ТН то надо включаться
			dRelay[RPUMPO].set_ON();     // файнкойлы
			Pump_HeatFloor(true);
		} else { // пауза ИЛИ работа дома не задействована - все выключить
			Pump_HeatFloor(false);
			dRelay[RPUMPO].set_OFF();     // файнкойлы
		}
	}
#endif // закрытие Нет трехходового - схема с двумя насосами
	if(onBoiler && get_State() == pWORK_HP) { // Если грели бойлер и теперь ТН работает, то обеспечить дополнительное время (delayBoilerSW сек) для прокачивания гликоля - т.к разные уставки по температуре подачи
		journal.jprintf(" Pause %ds, Boiler->House\n", Option.delayBoilerSW);
		Watchdog_SleepHere(Option.delayBoilerSW * 1000); // выравниваем температуру в контуре отопления/ГВС что бы сразу защиты не сработали
	}
	offBoiler = b ? 0 : Uptime_s(); // запомнить время выключения ГВС (нужно для переключения)
	return onBoiler = b;
}
// Проверка и если надо включение EVI если надо то выключение возвращает состояние реле
// Если реле нет то ничего не делает возвращает false
// Проверяет паузу 60 секунд после включения компрессора, и только после этого начинает управлять EVI

void HeatPump::Pump_HeatFloor(boolean) { }

// Включить или выключить насосы контуров и то что требуется для ГВС (трех-ходовой или насос) первый параметр их желаемое состояние
// Второй параметр параметр задержка после включения/выключения мсек. отдельного насоса (борьба с помехами)
// Идет проверка на необходимость изменения состояния насосов
// Генерятся задержки для защиты компрессора, есть задержки между включениями насосов для уменьшения помех
// Не забываем что здесь устанавливается onBoiler, по этому сначала вызывается Pumps потом switchBoiler
void HeatPump::Pumps(boolean b, uint16_t d)
{
	journal.jprintf(" Pumps(%d), modWork: %X\n", b, get_modWork());

#ifdef R3WAY           // Если определен трехходовой то в начале переключаем его (при выключении что бы остыл теплообменник)
if(b && (get_modWork() & pBOILER)){
        dRelay[R3WAY].set_Relay(true);             // скорее всего это пуск ТН (не переключение) по этому надо включить ГВС
		_delay(d); 
		onBoiler = true;
		offBoiler = 0;
	} else {
		dRelay[R3WAY].set_Relay(false);            // скорее всего это выключение ТН (не переключение) по этому надо выключить ГВС     
	    if(onBoiler && get_State() == pWORK_HP) {  // Если грели бойлер и теперь ТН работает, то обеспечить дополнительное время (delayBoilerSW сек) для прокачивания гликоля - т.к разные уставки по температуре подачи
		 journal.jprintf(" Pause %ds, Boiler->Pause\n", Option.delayBoilerSW);
		Watchdog_SleepHere(Option.delayBoilerSW * 1000);    // выравниваем температуру в контуре отопления/ГВС что бы сразу защиты не сработали
	      }
    	onBoiler = false;
		offBoiler = Uptime_s();			// запомнить время выключения ГВС (нужно для переключения)
	}
#endif


	if(!b && dRelay[PUMP_IN].get_flag(fR_StatusMain)) {
		journal.jprintf(" Delay: stop IN pump.\n");
		Watchdog_SleepHere(DELAY_BEFORE_STOP_IN_PUMP * 1000); // задержка перед выключениме гео насоса после выключения компрессора (облегчение останова)
	}
	
	dRelay[PUMP_IN].set_Relay(b);             // Реле включения насоса входного контура  (геоконтур)
	
	if(!b && (dRelay[RPUMPO].get_flag(fR_StatusMain) // пауза перед выключением насосов контуров, если нужно
	)){ // Насосы выключены и будут выключены, нужна пауза идет останов компрессора (новое значение выкл  старое значение вкл)
		journal.jprintf(" Delay: stop OUT pump.\n");
		Watchdog_SleepHere(Option.delayOffPump * 1000); // задержка перед выключениме насосов после выключения компрессора (облегчение останова)
	} else {
		_delay(d);                                // Задержка на d мсек
	}

#ifdef R3WAY  
	dRelay[PUMP_OUT].set_Relay(b);                 // Реле включения насоса выходного контура  (отопление и ГВС)
	_delay(d);                                     // Задержка на d мсек
#else
   	dRelay[RPUMPO].set_Relay(b);                	// насос отопления
   	_delay(d); 										// Задержка на d мсек
#endif // R3WAY
   	if(!b) {
   		SETBIT0(flags, fHP_BoilerTogetherHeat);
   	}
  	Pump_HeatFloor(b); 				  				// насос ТП

}
// Сброс инвертора если он стоит в ошибке, проверяется наличие инвертора и проверка ошибки после сброса
// Проводится различный сброс в зависимости от конфигурации
int8_t HeatPump::ResetFC()
{
	if(!dFC.get_present()) return OK;                                       // Инвертора нет выходим
// 1. Проверка наличия ошибки	
#ifdef SERRFC                                                               // Если есть вход от инвертора об ошибке
    sInput[SERRFC].Read();                                                  // Обновить значение
	if (sInput[SERRFC].get_lastErr()==OK) {                                 // Инвертор сбрасывать не надо
		journal.jprintf(" %s: OK, no inverter reset required\n",sInput[SERRFC].get_name());
	return OK; }
#else
	if(dFC.get_err() == OK && !dFC.get_blockFC()) return OK;
#endif
// 2. Собственно сброс
#ifdef RRESET                                                               // Если есть вход от инвертора об ошибке
	dRelay[RRESET].set_ON(); _delay(100); dRelay[RRESET].set_OFF();         // Подать импульс сброса
	journal.jprintf(" Reset %s by RRESET: ",dFC.get_name());
#else
	dFC.reset_FC();                                                         // подать команду на сброс по Modbus
    journal.jprintf(" Reset %s by Modbus: ",dFC.get_name());
#endif
// 3. Проверка результатов сброса
#ifdef SERRFC                                                               // Если есть вход от инвертора об ошибке
    _delay(100);
	sInput[SERRFC].Read();
	if (sInput[SERRFC].get_lastErr()==OK) {journal.jprintf("%s\r\n",cOk);return OK;}// Инвертор сброшен
	else {journal.jprintf("%s\n",cError); return ERR_RESET_FC;}                   // Сброс не прошел
#else
   if(dFC.get_blockFC()) {journal.jprintf("%s\n",cError); return ERR_RESET_FC;}   // Инвертор блокирован
   else                  {journal.jprintf("%s\n",cOk);return OK;}                 // Инвертор сброшен
#endif
	return OK;
}
           
// проверить, Если есть ли работа для ТН - true.
boolean HeatPump::CheckAvailableWork()
{
	return Prof.SaveON.mode != pOFF || GETBIT(Prof.SaveON.flags, fBoilerON) || Schdlr.IsSchedulerOn();
}

// Датчик годен, если он есть в конфигурации и привязан к адресу или к удалённому
// датчику. Без привязки Read() подставляет тестовую температуру: ТН считал бы
// по выдуманным значениям. Режим теста разрешает работу без адреса осознанно.
bool sensor_usable(sensorTemp &s)
{
	if(!s.get_present()) return false;
#ifndef DEMO                                    // на стенде значения случайные, адресов нет
	if(s.get_testMode() == NORMAL && !s.get_fAddress()) {
#ifdef SENSOR_IP
		if(s.devIP != NULL && s.devIP->get_fUse() && s.devIP->get_link() > -1) return true;
#endif
		return false;
	}
#endif
	return true;
}

sensorTemp *HeatPump::CheckSensors()
{
	type_settingHP *st = get_modeHouseSettings();
	bool house_on = get_modeHouse() != pOFF;
	uint8_t need = SensorsNeeded(house_on,
	                             TargetIsTemp1(st->Rule, GETBIT(st->flags, fTarget)),
	                             GETBIT(st->flags, fWeather),
	                             get_BoilerON());
	static const struct { uint8_t bit, num; } map[] = {
		{ NEED_FEED, TCONOUTG }, { NEED_RET, TCONING }, { NEED_TIN, TIN },
		{ NEED_TOUT, TOUT },     { NEED_TBOILER, TBOILER }
	};
	for(uint8_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if((need & map[i].bit) && !sensor_usable(sTemp[map[i].num])) return &sTemp[map[i].num];
#ifdef EEV_DEF
	// Датчики правила перегрева ЭРВ: непривязанный получил бы тестовую температуру, и ЭРВ регулировался бы по выдуманному перегреву
	uint8_t eev = dEEV.sensors_needed();
	if((eev & OHN_EVAOUT) && !sensor_usable(sTemp[TEVAOUT])) return &sTemp[TEVAOUT];
#ifdef TEVAIN
	if((eev & OHN_EVAIN)  && !sensor_usable(sTemp[TEVAIN]))  return &sTemp[TEVAIN];
#endif
#ifdef TCOMPIN
	if((eev & OHN_COMPIN) && !sensor_usable(sTemp[TCOMPIN])) return &sTemp[TCOMPIN];
#endif
#endif
	return NULL;
}

// ---- START/RESUME ----
// Функция Запуска/Продолжения работы ТН - возвращает ок или код ошибки
// Запускается ВСЕГДА отдельной задачей с приоритетом выше вебсервера
// Параметр задает что делаем true-старт, false-возобновление
void HeatPump::StartResume(boolean start)
{
#ifdef USE_UPS
	if(NO_Power) {
		NO_Power = 2; // Resume after
xGoWait:
		journal.jprintf(" Start task UpdateHP\n");
		journal.jprintf_time("%s WAIT . . .\n", (char*) nameHeatPump);
		startWait = true;                    // Начало работы с ожидания=true;
		setState(pWAIT_HP);
		Wake_UpdateHP();
		return;
	}
#endif
	// Дана команда старт - но возможно надо переходить в ожидание
	// Определяем что делать
	int8_t profile = Schdlr.calc_active_profile();
	if((profile != SCHDLR_NotActive) && (start)) { // расписание активно и дана команда
		if(profile == SCHDLR_Profile_off) {
#ifdef USE_UPS
			goto xGoWait;
#else
		journal.jprintf(" Start task UpdateHP\n");
		journal.jprintf_time("%s WAIT . . .\n", (char*) nameHeatPump);
		startWait = true;                    // Начало работы с ожидания=true;
		setState(pWAIT_HP);
		Wake_UpdateHP();
		return;
#endif			
		} else if(profile != Prof.get_idProfile()) {
			Prof.load(profile);
			set_profile();
			journal.jprintf("Profile changed to #%d\n", profile);
		}
	}
	if(startWait) {
		startWait = false;
		start = true;   // Делаем полный запуск, т.к. в положение wait переходили из стопа (расписания)
	}

	// ---- 1. Переменные  установка, остановка ТН имеет более высокий приоритет чем пуск ! ----
	if(start)  // Команда старт
	{
		if((get_State() == pWORK_HP) || (get_State() == pSTOPING_HP) || (get_State() == pSTARTING_HP)) return; // Если ТН включен или уже стартует или идет процесс остановки то ничего не делаем (исключается многократный заход в функцию)
		journal.jprintf_date( "  Start . . .\n");
	} else {
		if(get_State() != pWAIT_HP) return; // Если состяние НЕ РАВНО ожиданию то ничего не делаем, выходим восстанавливать нечего
		journal.jprintf_date( "  Resume . . .\n");
	}
	setState(pSTARTING_HP);                              // Производится старт -  устанавливаем соответсвующее состояние
	//  Если требуется сбрасываем инвертор  (проверям ошибку и пишем в журнал)
	eraseError();                                      // Обнулить ошибку только после сброса инвертора! иначе она может повторно возникнет при ошибке инвертора

	Status.ret = pNone;                                    // Состояние алгоритма
	lastEEV = -1;                                          // -1 это признак того что слежение eev еще не работает (выключения компрессора  не было)

	if(startPump)                                      // Если задача не остановлена то остановить (0 - останов задачи, 1 - запуск, 2 - в работе (выкл), 3 - в работе (вкл))
	{
		startPump = 0;                                     // Поставить признак останова задачи насос
	    if(get_workPump()) journal.jprintf(" %s: Pumps in pause %s. . .\n",(char*)__FUNCTION__, "OFF");
	}

	offBoiler = 0;                                         // Бойлер никогда не выключался
#ifdef DEFROST
	defrostFails = 0;                                      // счёт неудачных оттаек - заново
#endif
	onSalmonella = false;                                  // Если true то идет Обеззараживание
	onBoiler = false;                                      // Если true то идет нагрев бойлера
	SETBIT0(flags, fHP_BoilerTogetherHeat);

	// ---- 2.1 Проверка конфигурации, которые можно поменять из морды, по этому проверяем всегда ----
	if(!CheckAvailableWork())   // Нет работы для ТН - ничего не включено
	{
		setState(pOFF_HP);  // Еще ничего не сделали по этому сразу ставим состоение выключено
		set_Error(ERR_NO_WORK, (char*) __FUNCTION__);
		return;
	}
    if(get_State() != pSTARTING_HP) return;            // Могли нажать кнопку стоп, выход из процесса запуска
#ifdef EEV_DEF
	if((!sADC[PEVA].get_present()) && (dEEV.get_ruleEEV() == TEVAOUT_PEVA))  //  Отсутвует датчик давления, и выбран алгоритм ЭРВ который его использует",
	{
		setState(pOFF_HP);    // Еще ничего не сделали по этому сразу ставим состоение выключено
		set_Error(ERR_PEVA_EEV, (char*) __FUNCTION__);        // остановить по ошибке;
		return;
	}
#endif
	{
		sensorTemp *bad = CheckSensors();                   // нужный датчик отсутствует или не привязан
		if(bad != NULL) {
			setState(pOFF_HP);
			journal.jprintf(" Sensor %s: not in config or no address!\n", bad->get_name());
			set_Error(ERR_SENSOR, bad->get_name());       // источник ошибки - имя датчика
			return;
		}
	}

	// ---- 2.2 Проверка конфигурации, которые определены конфигом (поменять нельзя), по этому проверяем один раз при страте ТН ----
	if(start)  // Команда старт
	{
		if(!dRelay[PUMP_OUT].get_present())  // отсутсвует насос на конденсаторе, пользователь НЕ может изменить в процессе работы проверка при старте
		{
			setState(pOFF_HP);    // Еще ничего не сделали по этому сразу ставим состоение выключено
			set_Error(ERR_PUMP_CON, (char*) __FUNCTION__);        // остановить по ошибке;
			return;
		}
		if(!dRelay[PUMP_IN].get_present())   // отсутсвует насос на испарителе, пользователь может изменить в процессе работы
		{
			setState(pOFF_HP);    // Еще ничего не сделали по этому сразу ставим состоение выключено
			set_Error(ERR_PUMP_EVA, (char*) __FUNCTION__);        // остановить по ошибке;
			return;
		}
		if((!dRelay[RCOMP].get_present()) && (!dFC.get_present()))   // отсутсвует компрессор, пользователь может изменить в процессе работы
		{
			setState(pOFF_HP);    // Еще ничего не сделали по этому сразу ставим состоение выключено
			set_Error(ERR_NO_COMPRESS, (char*) __FUNCTION__);        // остановить по ошибке;
			return;
		}
	} //  if (start)  // Команда старт

	// ---- 3.  ПОДГОТОВКА ----

	if(start)  // Команда старт - Инициализация ЭРВ и очистка графиков при восстановлени не нужны
	{
#ifdef EEV_DEF
		if(get_State() != pSTARTING_HP) return;            // Могли нажать кнопку стоп, выход из процесса запуска
		else dEEV.Start();                                     // Включить ЭРВ  найти 0 по завершению позиция 0!!!
#endif

#ifdef CLEAR_CHART_HP_ON
		journal.jprintf(" Charts clear and start\n");
		if (get_State()!=pSTARTING_HP) return ;            // Могли нажать кнопку стоп, выход из процесса запуска
		else clearChart();// Запустить графики <- тут не запуск, тут очистка
#endif

	}

	// ---- 4. Определяем что нужно делать ----
	if(get_State() != pSTARTING_HP) return;            // Могли нажать кнопку стоп, выход из процесса запуска
	MODE_HP mod = EvaluateWorkMode();                                   // определяем что делаем с компрессором
	if(mod > pBOILER) mod = pOFF;                              // При первом пуске могут быть только состояния pOFF,pHEAT,pCOOL,pBOILER
	journal.jprintf(" Start modWork:%d[%s]\n", (int) mod, Ret_Text(Status.ret));
	Status.modWork = mod;  // Установка режима!



	// ---- 5. Если не старт ТН то проверка на минимальную паузу между включениями, при включении ТН паузе не будет ----
	if(!start)  // Команда Resume
		while(check_compressor_pause()) {
			Watchdog_SleepHere(100 * 1000);
			if(get_State() != pSTARTING_HP) return;
		}    // Могли нажать кнопку стоп, выход из процесса запуска

	//  6. Конфигурируем 3 и 4-х клапаны и включаем насосы ПАУЗА после включения насосов
	if(!configHP(Status.modWork)) return;

	// ---- 7. Включение компрессора и запуск обновления EEV ----
	if(get_State() != pSTARTING_HP) return;            // Могли нажать кнопку стоп, выход из процесса запуска
	if(is_next_command_stop()) return;			    // следующая команда останов, выходим
#ifdef USE_ELECTROMETER_SDM
	if(!dSDM.get_link()) dSDM.uplinkSDM();
#endif

	bool start_compressor_now = mod && mod <= pBOILER; // pCOOL or pHEAT or pBOILER
	if(start_compressor_now) {
#ifndef DEMO   // проверка блокировки инвертора
		if((dFC.get_present()) && (dFC.get_blockFC()))          // есть инвертор но он блокирован
		{
			if(dFC.get_err() != ERR_485_BUSY) {
				journal.jprintf("%s: is blocked, ignore start\n", dFC.get_name());
				set_Error(ERR_MODBUS_BLOCK, (char*) __FUNCTION__);          // ВОТ ЗДЕСЬ КОМАНДА СТОП И ПРОЙДЕТ
				return;
			}
		}
#endif
	}
	if(get_errcode() != OK)                                 // ОШИБКА перед стартом
	{
		journal.jprintf(" Error %d before start compressor!\n", get_errcode());
		set_Error(ERR_COMP_ERR, (char*) __FUNCTION__);
		return;
	}
	if(start_compressor_now) compressorON(); // Компрессор включить если нет ошибок и надо включаться

	// ---- 10. Сохранение состояния ----
	if(get_State() != pSTARTING_HP) return;                   // Могли нажать кнопку стоп, выход из процесса запуска
	setState(pWORK_HP);

	// ---- 11. Запуск задачи обновления ТН ----
	if(start) {
		journal.jprintf(" Start task UpdateHP\n");
		Wake_UpdateHP();                                                        // Запустить задачу Обновления ТН, дальше она все доделает
	}

	// ---- 12. насос запущен ----
	journal.jprintf_time("%s ON . . .\n", (char*) nameHeatPump);

	return;
}

// ---- STOP/WAIT ----
// Функция Останова/Ожидания ТН  - возвращает код ошибки
// Параметр задает что делаем true-останов, false-ожидание
void HeatPump::StopWait(boolean stop)
{  
  if (stop)
  {
    if ((get_State()==pOFF_HP)||(get_State()==pSTOPING_HP)) return;    // Если ТН выключен или выключается ничего не делаем
    journal.jprintf_date("Stopping...\n");
    setState(pSTOPING_HP);  // Состояние выключения
  } else {
    if ((get_State()==pOFF_HP)||(get_State()==pSTOPING_HP)||(get_State()==pWAIT_HP)) return;    // Если ТН выключен или выключается или ожидание ничего не делаем
    journal.jprintf_date("Switch to waiting...\n");
    setState(pSTOPING_HP);  // Состояние выключения
  }

  journal.jprintf(" modWork: %X[%s]\n", get_modWork(), Ret_Text(Status.ret));
  compressorOFF();		// Останов компрессора, насосов - PUMP_OFF(), ЭРВ

  if (stop) //Обновление ТН отключаем только при останове
  {
	Tasks.Task_vUpdate_run = false;					        // Остановить задачу обновления ТН vUpdate (Tasks.xHandleUpdate)
    journal.jprintf(" Stop task UpdateHP\n");
  }
    
  if(startPump)
  {
     startPump = 0;                                    // Поставить признак что насос выключен
     if(get_workPump()) journal.jprintf(" %s: Pumps in pause %s. . .\n",(char*)__FUNCTION__, "OFF");
  }

 // Принудительное выключение отдельных узлов ТН если они есть в конфиге
  #ifdef RBOILER  // управление дополнительным ТЭНом бойлера
     dRelay[RBOILER].set_OFF();  // выключить ТЭН бойлера
  #endif

  #ifdef RHEAT  // управление  ТЭНом отопления
     dRelay[RHEAT].set_OFF();     // выключить ТЭН отопления
  #endif

  #ifdef RPUMPB  // управление  насосом циркуляции ГВС
     dRelay[RPUMPB].set_OFF();     // выключить насос циркуляции ГВС
  #endif

  SETBIT0(flags, fHP_BoilerTogetherHeat);

  #ifdef RELAY_ALL_OFF_ON_STOP  // случаи бывают разные - должно работать без костылей.- но лучше перебдеть -))
   relayAllOFF(); // Все выключить, все (на всякий случай), * внимание - выключатся реле по расписанию!
  #endif 
 
  if(stop)
  {
     setState(pOFF_HP);
     journal.jprintf_time("%s OFF . . .\n",(char*)nameHeatPump);
  } else {
     setState(pWAIT_HP);
     journal.jprintf_time("%s WAIT . . .\n",(char*)nameHeatPump);
  }
  return;
}

// Инициализировать переменные ПИД регулятора
void HeatPump::resetPID()
{
	pidw.pre_err = 0;
	pidw.sum = 0;
	pidw.max = 0;   // ПИД может менять частоту без ограничений
	updatePidTime = updatePidBoiler = xTaskGetTickCount();                // время обновления ПИДа
	// ГВС Сбросить переменные пид регулятора
}


#ifdef RBOILER  // управление дополнительным ТЭНом бойлера
// Проверка на необходимость греть бойлер дополнительным теном (true - надо греть) ВСЕ РЕЖИМЫ
boolean HeatPump::boilerAddHeat()
{
	if(get_State() != pWORK_HP) return false; // работа ТЭНа бойлера разрешена если только работает ТН, в противном случае выкл
	if (!is_BoilerHeaterAllowed())  { // если переключение на ходу на резервный источник то сбросить догрев бойлера
		flagRBOILER = false;
		return false;
	}
	int16_t T = sTemp[TBOILER].get_Temp();
	if((GETBIT(Prof.SaveON.flags, fBoilerON)) && (GETBIT(Prof.Boiler.flags, fSalmonella))) // Сальмонелла не взирая на расписание если включен бойлер (запрет ТЭНа на генераторе проверен выше)
	{
		if(ClimateSalmonellaStart(rtcSAM3X8.get_day_of_week(), rtcSAM3X8.get_hours(), rtcSAM3X8.get_minutes(), SALMONELLA_DAY, SALMONELLA_HOUR,
				onSalmonella, doneSalmonella, Uptime_s())) { // Надо начитать процесс обеззараживания
			startSalmonella = Uptime_s();
			onSalmonella = true;
			journal.jprintf(" Cycle start salmonella, %.2dC°\n", sTemp[TBOILER].get_Temp());
		}
		if(onSalmonella) {   // Обеззараживание нужно
			if(startSalmonella + SALMONELLA_TIME > Uptime_s()) { // Время цикла еще не исчерпано
				if(T < SALMONELLA_TEMP) return true; // Включить обеззараживание
				if(GETBIT(Prof.Boiler.flags, fSalmonellaHold)) return ClimateDecideSalmonellaHold(T, SALMONELLA_TEMP, dRelay[RBOILER].get_Relay()); // Длительная обработка: стабилизация температуры обеззараживания, гистерезис 0.5 градуса
				else {  // Обычная обработка: только до достижения температуры и сразу выключение
					onSalmonella = false;
					startSalmonella = 0;
					doneSalmonella = Uptime_s();
					journal.jprintf(" Salmonella cycle finished, %.2dC°\n", sTemp[TBOILER].get_Temp());
					return false;
				}
			} else {  // Время вышло, выключаем, и идем дальше по алгоритму
				onSalmonella = false;
				startSalmonella = 0;
				doneSalmonella = Uptime_s();
				journal.jprintf(" Salmonella cycle end, %.2dC°\n", sTemp[TBOILER].get_Temp());
			}
		}
	} else if(onSalmonella) { // если сальмонеллу отключили на ходу выключаем и идем дальше по алгоритму
		onSalmonella = false;
		startSalmonella = 0;
		journal.jprintf(" Off salmonella\n");
	}

	// Выход из режима «бойлер греет только ТЭН» (выключили флаг или включили генератор): обычная логика догрева видит включённое реле как
	// «ТЭН уже греет» и дожимала бы им до цели вместо нагрева ТН, поэтому начинаем с выключенного ТЭНа
	if(heaterOnlyPrev && !is_boilerHeaterOnly()) { dRelay[RBOILER].set_OFF(); flagRBOILER = false; }
	heaterOnlyPrev = is_boilerHeaterOnly();

	// Бойлер греет только ТЭН: ТН бойлер не греет, ТЭН по гистерезису одной цели (без температуры догрева и догрева «по расписанию»)
	if(heaterOnlyPrev)
	{
		flagRBOILER = false;                                                             // признак догрева после ТН здесь не используется
		if(!GETBIT(Prof.SaveON.flags, fBoilerON) || !scheduleBoiler()) return false;     // бойлер выключен или расписание запрещает
		HeaterOnlyOut h = ClimateDecideHeaterOnlyBoiler(T, get_boilerTempTarget(), Prof.Boiler.dTemp, HeatBoilerUrgently, dRelay[RBOILER].get_Relay());
		if(h.clear_urgent && HeatBoilerUrgently) set_HeatBoilerUrgently(false);          // цель достигнута: срочный нагрев закончен
		return h.heat;
	}

	// Догрев бойлера

	if(GETBIT(Prof.SaveON.flags, fBoilerON) && scheduleBoiler()) // Если разрешено греть бойлер согласно расписания И Бойлер включен
	{
		if(GETBIT(Prof.Boiler.flags, fTurboBoiler))  // Если турбо режим, то повторяем за Тепловым насосом (грет или не греть)
		{
			if(T < Prof.Boiler.tempRBOILER) return onBoiler;   // работа параллельно с ТН  если температура МЕНЬШЕ догрева то повторяем работу ТН
		}
		if(GETBIT(Prof.Boiler.flags, fAddHeating))  // Включен догрев
		{
			int16_t b_target = get_boilerTempTarget();
			if(!flagRBOILER && (T < b_target - (HeatBoilerUrgently ? 10 : Prof.Boiler.dTemp))) {  // Бойлер ниже гистерезиса - ставим признак необходимости включения Догрева (но пока не включаем ТЭН)
			    flagRBOILER = true;
				return false;
			}
			if(!flagRBOILER || onBoiler) return false; // флажка нет или работает бойлер, но догрев не включаем
			else {
				if(T < b_target && (T >= Prof.Boiler.tempRBOILER - Prof.Boiler.dAddHeat || dRelay[RBOILER].get_Relay() || GETBIT(Prof.Boiler.flags, fAddHeatingForce))) {  // Греем тэном
					return true;
				} else { // бойлер выше целевой температуры - цель достигнута или греть тэном еще рано
					flagRBOILER = false;
					return false;
				}
			}
		} else { // ТЭН не используется (сняты все флажки)
			flagRBOILER = false;
			return false;
		}
	} else { // Бойлер сейчас запрещен
		flagRBOILER = false;
		return false;
	}

}
#endif

// Проверить расписание бойлера true - нужно греть false - греть не надо, если расписание выключено то возвращает true
boolean HeatPump::scheduleBoiler()
{
	if(HeatBoilerUrgently) return true;
	if(GETBIT(Prof.Boiler.flags, fSchedule))         // Если используется расписание
	{  // Понедельник 0 воскресенье 6 это кодирование в расписании функция get_day_of_week возвращает 1-7
		boolean b = Prof.Boiler.Schedule[rtcSAM3X8.get_day_of_week() - 1] & (0x01 << rtcSAM3X8.get_hours()) ? true : false;
		if(!b) return false;             // запрещено греть бойлер согласно расписания
	}
	return true;
}

// Управление температурой в зависимости от режима
#define STR_REDUCED "Reduced FC"   // Экономим место
#define STR_FREQUENCY " FC> %.2f\n" // Экономим место
// Итерация по управлению Бойлером
// возврат что надо делать компрессору, функция НЕ управляет компрессором а только выдает необходимость включения компрессора
MODE_COMP  HeatPump::UpdateBoiler()
{
#ifdef RBOILER  // управление дополнительным ТЭНом бойлера
	if(boilerAddHeat()) { // Дополнительный нагреватель бойлера - нужно греть
		if(get_State() == pOFF_HP || get_State() == pSTOPING_HP) { // Если ТН выключен или выключается
			dRelay[RBOILER].set_OFF();
			flagRBOILER = false;
			return pCOMP_OFF;
		}
		if(is_BoilerHeaterAllowed()) {   // Включение ТЭНа бойлера если он не запрещен (не на генераторе)
			dRelay[RBOILER].set_ON();
			SETBIT0(flags, fHP_BoilerTogetherHeat);
		}
		if(!GETBIT(Prof.Boiler.flags, fTurboBoiler)) return pCOMP_OFF;
	} else {
		if(dRelay[RBOILER].get_Relay()) set_HeatBoilerUrgently(false);
		dRelay[RBOILER].set_OFF();
		if(get_State() == pOFF_HP || get_State() == pSTOPING_HP) { // Если ТН выключен или выключается
			return pCOMP_OFF;
		}
	}
#else
	if(get_State() == pOFF_HP || get_State() == pSTOPING_HP) { // Если ТН выключен или выключается
		return pCOMP_OFF;
	}
#endif
	if(is_boilerHeaterOnly()) return pCOMP_OFF;     // Бойлер греет только ТЭН: ТН бойлер не греет, дальше (расписание, сброс тепла, компрессор) не идём
	if(!GETBIT(Prof.SaveON.flags,fBoilerON) || (!scheduleBoiler() && !GETBIT(Prof.Boiler.flags,fScheduleAddHeat))) // Если запрещено греть бойлер согласно расписания ИЛИ  Бойлер выключен, выходим и можно смотреть отопление
	{
#ifdef RBOILER  // управление дополнительным ТЭНом бойлера
		if(GETBIT(Prof.Boiler.flags, fAddHeating)) {
			dRelay[RBOILER].set_OFF();
			flagRBOILER=false; // Выключение
		}
#endif
		return pCOMP_OFF;      // запрещено греть бойлер согласно расписания
	}

	// ---------------------------------------------------------------------------
	// Сброс излишней энергии в систему отопления по температуре подачи/конденсации
	// Переключаемся на отопление на ходу и ждем определенное число секунд, дальше возвращаемся на бойлер
	if(GETBIT(Prof.Boiler.flags,fResetHeat))    // Стоит требуемая опция - Сброс тепла в СО
	{
		if(Status.prev == pBdis) {
			if(offBoiler + Prof.Boiler.Reset_Time < Uptime_s()) {
				journal.jprintf(" Boiler: Discharged Ok\n");
				switchBoiler(true);    // Переключиться на ходу на ГВС
			} else {
				Status.ret = pBdis;
				return pCOMP_NONE;
			}
		} else if(
		   FEED
		   > Prof.Boiler.tempInLim - Prof.Boiler.DischargeDelta * 10
		   || (sTemp[TCOMP].get_Temp() > sTemp[TCOMP].get_maxTemp() - BOILER_TEMP_COMP_RESET)) // температура нагнетания компрессора больше максимальной -5 градусов
		{
			journal.jprintf(" Boiler: Discharging %ds...\n", Prof.Boiler.Reset_Time);
			switchBoiler(false);       // Переключиться на ходу на отопление
			Status.ret = pBdis;
			return pCOMP_NONE;
		}
	}

	// Дальше - расчёт, он в ClimateDecideBoiler. Выше осталась
	// оркестровка: ТЭН бойлера, сброс тепла в отопление, расписание. Их
	// нельзя выразить решением - это действия над реле и режимом.
	ClimateIn  in;
	ClimateOut out;

	in.use_pid          = dFC.get_present() && GETBIT(Prof.Boiler.flags, fBoilerPID);
	in.tboiler          = sTemp[TBOILER].get_Temp();
	in.target           = get_boilerTempTarget();
	in.turbo            = GETBIT(Prof.Boiler.flags, fTurboBoiler);
	in.add_heating      = GETBIT(Prof.Boiler.flags, fAddHeating);
	in.heater_allowed   = is_BoilerHeaterAllowed();
	in.urgent           = HeatBoilerUrgently;
	in.tempRBOILER      = Prof.Boiler.tempRBOILER;
	in.dAddHeat         = Prof.Boiler.dAddHeat;
	in.dTemp            = Prof.Boiler.dTemp;
	in.tempInLim        = Prof.Boiler.tempInLim;

	in.hp_stopped       = false;   // проверено выше, в прологе
	in.comp_on          = is_compressor_on();
	in.on_boiler        = onBoiler;
	in.backup_power     = GETBIT(Option.flags, fBackupPower);
	in.r4way_check      = false;
	in.r4way_on         = false;

	in.now              = Uptime_s();
	in.off_boiler       = offBoiler;
	in.start_compressor = startCompressor;
	in.fc_start_time    = dFC.get_startTime();
	in.ticks            = xTaskGetTickCount();
	in.update_pid_time  = updatePidBoiler;              // у бойлера свой счётчик
	in.pid_period_ms    = (uint32_t)get_timeBoiler() * 1000;
	in.delay_boiler_off = Option.delayBoilerOff;
	in.min_compressor_on= Option.MinCompressorOn;

	in.feed             = FEED;
#if defined(SUPERBOILER) && defined(PCON)
	in.feed_limit_temp  = PressToTemp(PCON);   // для предконденсатора - температура конденсации
#else
	in.feed_limit_temp  = FEED;
#endif
	in.ret_t            = RET;
	in.tin              = sTemp[TIN].get_Temp();
	in.tcomp            = sTemp[TCOMP].get_Temp();
	in.tcomp_max        = sTemp[TCOMP].get_maxTemp();
#ifdef PCON
	in.pcon_present     = sADC[PCON].get_present();
	in.pcon             = sADC[PCON].get_Value();
	in.pcon_max         = sADC[PCON].get_maxValue();
#else
	in.pcon_present     = false;
	in.pcon             = 0;
	in.pcon_max         = 0;
#endif
	in.power220         = getPower();

	in.fc_target        = dFC.get_target();
	in.fc_step          = dFC.get_stepFreqBoiler();     // шаг, пределы и мощность
	in.fc_min           = dFC.get_minFreqBoiler();      // у бойлера отдельные
	in.fc_max           = dFC.get_maxFreqBoiler();
	in.fc_max_gen       = dFC.get_maxFreqGen();
	in.fc_pid_freq_step = dFC.get_PidFreqStep();
	in.fc_pid_stop      = dFC.get_PidStop();
	in.fc_power         = dFC.get_power();
	in.fc_current       = dFC.get_current();
	in.fc_dt_temp       = dFC.get_dtTempBoiler();
	in.fc_dt_comp_temp  = dFC.get_dtCompTemp();

	in.rule             = 0;                            // бойлер выбирает по use_pid
	in.target_is_return = false;
	in.dt               = 0;
	in.dTempGen         = 0;
	in.tempOutLim       = 0;
	in.pid_target       = Prof.Boiler.tempPID;          // без погодозависимости
	in.max_backup_power = get_maxBackupPower();
	in.max_power        = FC_MAX_POWER_BOILER;          // у ГВС пределы ниже
	in.max_current      = FC_MAX_CURRENT_BOILER;
	in.dt_con_press     = FC_DT_CON_PRESS;
	in.start_pid_delay  = FC_START_PID_DELAY;

	ClimateDecideBoiler(in, Prof.Boiler.pid, pidw, out);

	if(DBG_ON(DBG_FRED)) {
		switch(out.reduce) {
		case REDUCE_BACKUP_POWER: journal.jprintf("%s %.2f (BACKUP POWER: %.3f kW)\n", STR_REDUCED, in.fc_step/100.0, (float)in.power220/1000.0); break;
		case REDUCE_FEED:         journal.jprintf("%s %.2f (FEED: %.2f)\n",   STR_REDUCED, in.fc_step/100.0, in.feed/100.0); break;
		case REDUCE_POWER:        journal.jprintf("%s %.2f (POWER: %.3f kW)\n", STR_REDUCED, in.fc_step/100.0, (float)in.fc_power/1000.0); break;
		case REDUCE_CURRENT:      journal.jprintf("%s %.2f (CURRENT: %.2f)\n", STR_REDUCED, in.fc_step/100.0, (float)in.fc_current/100.0); break;
		case REDUCE_TCOMP:        journal.jprintf("%s %.2f (TCOMP: %.2f)\n",  STR_REDUCED, in.fc_step/100.0, in.tcomp/100.0); break;
		case REDUCE_PCON:         journal.jprintf("%s %.2f (PCON:  %.2f)\n",  STR_REDUCED, in.fc_step/100.0, in.pcon/100.0); break;
		default: break;
		}
	}
	if(out.touch_pid_time) updatePidBoiler = in.ticks;
	if(out.set_ret) Status.ret = out.ret;
	if(out.clear_urgent) set_HeatBoilerUrgently(false);
	if(out.set_target) {
		if(!out.target_immediate) {
			if(DBG_ON(DBG_FSTEP)) { journal.jprintf((char*)STR_FREQUENCY, out.target/100.0); }
		}
		dFC.set_target(out.target, out.target_immediate, in.fc_min, in.fc_max);
	}
	if(out.reset_pid) resetPID();
	if(out.error != OK) set_Error(out.error, (char*)__FUNCTION__);
	return out.comp;
}

int16_t HeatPump::CalcTargetPID(type_settingHP &settings)
{
    int16_t  targetRealPID;				 // Цель подачи
	if(GETBIT(settings.flags, fWeather)) { // включена погодозависимость
		targetRealPID = settings.tempPID + (settings.kWeatherPID * (TEMP_WEATHER - sTemp[TOUT].get_Temp()) / 1000); // включена погодозависимость, коэффициент в ТЫСЯЧНЫХ результат в сотых градуса, определяем цель
		if(targetRealPID > settings.tempInLim - 50) targetRealPID = settings.tempInLim - 50;  // ограничение целевой подачи = максимальная подача - 0.5 градуса
		if(targetRealPID < MIN_WEATHER) targetRealPID = MIN_WEATHER;
		if(targetRealPID > MAX_WEATHER) targetRealPID = MAX_WEATHER;
	} else targetRealPID = settings.tempPID; // отключена погодозависмость
	return targetRealPID;
}

// ---------------------------------------------------------------------------
// Итерация по управлению НАГРЕВ  пола
// выдает что надо делать компрессору, но ничем не управляет для старт-стопа для инвертора меняет обороты
MODE_COMP HeatPump::UpdateHeat()
{
	// Решение принимает ClimateDecideHeat по структуре входа, здесь только
	// сбор входа и исполнение решения. Так аварийные ветки стали проверяемы
	// на ПК: на живом ТН их либо не воспроизвести, либо опасно.
	ClimateIn  in;
	ClimateOut out;

	in.hp_stopped       = get_State() == pOFF_HP || get_State() == pSTOPING_HP;
	in.comp_on          = is_compressor_on();
	in.on_boiler        = onBoiler;
	in.backup_power     = GETBIT(Option.flags, fBackupPower);
#ifdef R4WAY
	in.r4way_check      = true;
	in.r4way_on         = dRelay[R4WAY].get_Relay();
#else
	in.r4way_check      = false;
	in.r4way_on         = false;
#endif
	in.now              = Uptime_s();
	in.off_boiler       = offBoiler;
	in.start_compressor = startCompressor;
	in.fc_start_time    = dFC.get_startTime();
	in.ticks            = xTaskGetTickCount();
	in.update_pid_time  = updatePidTime;
	in.pid_period_ms    = (uint32_t)get_timeHeat() * 1000;
	in.delay_boiler_off = Option.delayBoilerOff;
	in.min_compressor_on= Option.MinCompressorOn;

	in.feed             = FEED;
	in.ret_t            = RET;
	in.tin              = sTemp[TIN].get_Temp();
	in.tcomp            = sTemp[TCOMP].get_Temp();
	in.tcomp_max        = sTemp[TCOMP].get_maxTemp();
#ifdef PCON
	in.pcon_present     = sADC[PCON].get_present();
	in.pcon             = sADC[PCON].get_Value();
	in.pcon_max         = sADC[PCON].get_maxValue();
#else
	in.pcon_present     = false;
	in.pcon             = 0;
	in.pcon_max         = 0;
#endif
	in.power220         = getPower();

	in.fc_target        = dFC.get_target();
	in.fc_step          = dFC.get_stepFreq();
	in.fc_min           = dFC.get_minFreq();
	in.fc_max           = dFC.get_maxFreq();
	in.fc_max_gen       = dFC.get_maxFreqGen();
	in.fc_pid_freq_step = dFC.get_PidFreqStep();
	in.fc_pid_stop      = dFC.get_PidStop();
	in.fc_power         = dFC.get_power();
	in.fc_current       = dFC.get_current();
	in.fc_dt_temp       = dFC.get_dtTemp();
	in.fc_dt_comp_temp  = dFC.get_dtCompTemp();

	in.rule             = Prof.Heat.Rule;
	in.target_is_return = GETBIT(Prof.Heat.flags, fTarget);
	in.target           = get_targetTempHeat();
	in.dt               = Prof.Heat.dt;
	in.dTemp            = Prof.Heat.dTemp;
	in.dTempGen         = Prof.Heat.dTempGen;
	in.tempInLim        = Prof.Heat.tempInLim;
	in.tempOutLim       = Prof.Heat.tempOutLim;
	in.pid_target       = CalcTargetPID(Prof.Heat);
	in.max_backup_power = get_maxBackupPower();
	in.max_power        = FC_MAX_POWER;
	in.max_current      = FC_MAX_CURRENT;
	in.dt_con_press     = FC_DT_CON_PRESS;
	in.start_pid_delay  = FC_START_PID_DELAY;

	ClimateDecideHeat(in, Prof.Heat.pid, pidw, out);

	if(DBG_ON(DBG_FRED)) {
		switch(out.reduce) {
		case REDUCE_BACKUP_POWER: journal.jprintf("%s %.2f (BACKUP POWER: %.3f kW)\n", STR_REDUCED, in.fc_step/100.0, (float)in.power220/1000.0); break;
		case REDUCE_FEED:         journal.jprintf("%s %.2f (FEED: %.2f)\n",   STR_REDUCED, in.fc_step/100.0, in.feed/100.0); break;
		case REDUCE_POWER:        journal.jprintf("%s %.2f (POWER: %.3f kW)\n", STR_REDUCED, in.fc_step/100.0, in.fc_power/1000.0); break;
		case REDUCE_CURRENT:      journal.jprintf("%s %.2f (CURRENT: %.2f)\n", STR_REDUCED, in.fc_step/100.0, in.fc_current/100.0); break;
		case REDUCE_TCOMP:        journal.jprintf("%s %.2f (TCOMP: %.2f)\n",  STR_REDUCED, in.fc_step/100.0, in.tcomp/100.0); break;
		case REDUCE_PCON:         journal.jprintf("%s %.2f (PCON:  %.2f)\n",  STR_REDUCED, in.fc_step/100.0, in.pcon/100.0); break;
		default: break;
		}
	}
	if(out.touch_pid_time) updatePidTime = in.ticks;
	if(out.set_ret) Status.ret = out.ret;
	if(out.set_target) {
		if(!out.target_immediate) {
			if(DBG_ON(DBG_FSTEP)) { journal.jprintf((char*)STR_FREQUENCY, out.target/100.0); }
		}
		dFC.set_target(out.target, out.target_immediate, in.fc_min, in.fc_max);
	}
	if(out.reset_pid) resetPID();
	if(out.error != OK) set_Error(out.error, (char*)__FUNCTION__);
	return out.comp;
}

// ---------------------------------------------------------------------------
// Итерация по управлению ОХЛАЖДЕНИЕ
// выдает что надо делать компрессору, но ничем не управляет
MODE_COMP HeatPump::UpdateCool()
{
	// Решение принимает ClimateDecideCool, здесь только сбор входа и
	// исполнение. Отличия охлаждения от отопления перечислены там же.
	ClimateIn  in;
	ClimateOut out;

	in.hp_stopped       = get_State() == pOFF_HP || get_State() == pSTOPING_HP;
	in.comp_on          = is_compressor_on();
	in.on_boiler        = onBoiler;
	in.backup_power     = GETBIT(Option.flags, fBackupPower);
	in.r4way_check      = false;   // в исходнике проверка 4-ходового закомментирована
	in.r4way_on         = false;

	in.now              = Uptime_s();
	in.off_boiler       = offBoiler;
	in.start_compressor = startCompressor;
	in.fc_start_time    = dFC.get_startTime();
	in.ticks            = xTaskGetTickCount();
	in.update_pid_time  = updatePidTime;
	// ВНИМАНИЕ: именно get_timeHeat, а не get_timeCool. Так было в исходнике,
	// и поведение сохранено. Похоже на недосмотр: период итераций ПИД в
	// охлаждении берётся из профиля ОТОПЛЕНИЯ, хотя Prof.Cool.pid_time
	// настраивается из веб-морды и на коэффициенты ПИД влияет. get_timeCool
	// при этом не вызывается нигде.
	in.pid_period_ms    = (uint32_t)get_timeHeat() * 1000;
	in.delay_boiler_off = Option.delayBoilerOff;
	in.min_compressor_on= Option.MinCompressorOn;   // охлаждением не используется

	in.feed             = FEED;
	in.ret_t            = RET;
	in.tin              = sTemp[TIN].get_Temp();
	in.tcomp            = sTemp[TCOMP].get_Temp();
	in.tcomp_max        = sTemp[TCOMP].get_maxTemp();
#ifdef PCON
	in.pcon_present     = sADC[PCON].get_present();
	in.pcon             = sADC[PCON].get_Value();
	in.pcon_max         = sADC[PCON].get_maxValue();
#else
	in.pcon_present     = false;
	in.pcon             = 0;
	in.pcon_max         = 0;
#endif
	in.power220         = getPower();

	in.fc_target        = dFC.get_target();
	in.fc_step          = dFC.get_stepFreq();
	in.fc_min           = dFC.get_minFreqCool();
	in.fc_max           = dFC.get_maxFreqCool();
	in.fc_max_gen       = dFC.get_maxFreqGen();
	in.fc_pid_freq_step = dFC.get_PidFreqStep();
	in.fc_pid_stop      = dFC.get_PidStop();
	in.fc_power         = dFC.get_power();
	in.fc_current       = dFC.get_current();
	in.fc_dt_temp       = dFC.get_dtTemp();
	in.fc_dt_comp_temp  = dFC.get_dtCompTemp();

	in.rule             = Prof.Cool.Rule;
	in.target_is_return = GETBIT(Prof.Cool.flags, fTarget);
	in.target           = get_targetTempCool();
	in.dt               = Prof.Cool.dt;
	in.dTemp            = Prof.Cool.dTemp;
	in.dTempGen         = Prof.Cool.dTempGen;       // охлаждением не используется
	in.tempInLim        = Prof.Cool.tempInLim;
	in.tempOutLim       = Prof.Cool.tempOutLim;
	in.pid_target       = CalcTargetPID(Prof.Cool);
	in.max_backup_power = get_maxBackupPower();
	in.max_power        = FC_MAX_POWER;
	in.max_current      = FC_MAX_CURRENT;
	in.dt_con_press     = FC_DT_CON_PRESS;
	in.start_pid_delay  = FC_START_PID_DELAY;

	ClimateDecideCool(in, Prof.Cool.pid, pidw, out);

	if(DBG_ON(DBG_FRED)) {
		switch(out.reduce) {
		case REDUCE_BACKUP_POWER: journal.jprintf("%s %.2f (BACKUP POWER: %.3f kW)\n", STR_REDUCED, in.fc_step/100.0, (float)in.power220/1000.0); break;
		case REDUCE_FEED:         journal.jprintf("%s %.2f (FEED: %.2f)\n",   STR_REDUCED, in.fc_step/100.0, in.feed/100.0); break;
		case REDUCE_POWER:        journal.jprintf("%s %.2f (POWER: %.3f kW)\n", STR_REDUCED, in.fc_step/100.0, in.fc_power/1000.0); break;
		case REDUCE_CURRENT:      journal.jprintf("%s %.2f (CURRENT: %.2f)\n", STR_REDUCED, in.fc_step/100.0, in.fc_current/100.0); break;
		case REDUCE_TCOMP:        journal.jprintf("%s %.2f (TCOMP: %.2f)\n",  STR_REDUCED, in.fc_step/100.0, in.tcomp/100.0); break;
		case REDUCE_PCON:         journal.jprintf("%s %.2f (PCON:  %.2f)\n",  STR_REDUCED, in.fc_step/100.0, in.pcon/100.0); break;
		default: break;
		}
	}
	if(out.touch_pid_time) updatePidTime = in.ticks;
	if(out.set_ret) Status.ret = out.ret;
	if(out.set_target) {
		if(!out.target_immediate) {
			if(DBG_ON(DBG_FSTEP)) { journal.jprintf((char*)STR_FREQUENCY, out.target/100.0); }
		}
		dFC.set_target(out.target, out.target_immediate, in.fc_min, in.fc_max);
	}
	if(out.reset_pid) resetPID();
	if(out.error != OK) set_Error(out.error, (char*)__FUNCTION__);
	return out.comp;
}

// ---- UPDATE ----
// Итерация по управлению всем ТН, для всего, основной цикл управления.
void HeatPump::vUpdate()
{

	if((get_State() == pOFF_HP) || (get_State() == pSTARTING_HP) || (get_State() == pSTOPING_HP)) return; // ТН выключен или включается или выключается выходим  ничего не делаем!!!

	// Различные проверки на ошибки и защиты
	if(!CheckAvailableWork()) {  // Нет работы для ТН - ничего не включено, пользователь может изменить в процессе работы
		set_Error(ERR_NO_WORK, (char*) __FUNCTION__);
		return;
	}
#ifdef EEV_DEF
	if((!sADC[PEVA].get_present()) && (dEEV.get_ruleEEV() == TEVAOUT_PEVA)) {  //  Отсутвует датчик давления, и выбран алгоритм ЭРВ который его использует", пользователь может изменить в процессе работы
		set_Error(ERR_PEVA_EEV, dEEV.get_name());
		return;
	}
#endif


#ifdef DEFROST
	defrost();                                                          // Разморозка только для воздушных ТН
#endif

	Status.modWork = EvaluateWorkMode();                                         // определяем что делаем
	if(DBG_ON(DBG_DUMP)) { save_DumpJournal(true); }                          // Вывод строки состояния
	if(error) return;
	//  реализуем требуемый режим
	if(Status.modWork == pOFF) {
		if(is_compressor_on()) {  // ЕСЛИ компрессор работает, то выключить компрессор,и затем сконфигурировать 3 и 4-х клапаны и включаем насосы
			compressorOFF();
			configHP(Status.modWork);
			if(!startPump && get_modeHouse() != pOFF)  // Когда режим выключен (не отопление и не охлаждение), то насосы отопления крутить не нужно
			{
				startPump = true;                                 // Поставить признак запуска задачи насос
				if(get_workPump()) journal.jprintf(" %s: Pumps in pause %s. . .\n", (char*) __FUNCTION__, "ON");     // Включить задачу насос конденсатора выключение в переключении насосов
			}
			command_completed = Uptime_s(); // поменялся режим
		}
	} else if(!(Status.modWork & pCONTINUE)) { // Начало режимов, Включаем задачу насос, конфигурируем 3 и 4-х клапаны включаем насосы и потом включить компрессор
		if(startPump)                                     // Остановить задачу насос
		{
			startPump = false;                            // Поставить признак останова задачи насос
			if(get_workPump()) journal.jprintf(" %s: Pumps in pause %s. . .\n",(char*)__FUNCTION__, "OFF");
		    command_completed = Uptime_s(); // поменялся режим
		}
		if(!check_compressor_pause()) {
			if(configHP(Status.modWork)) {                   // Конфигурируем насосы
				compressorON();                              // Включаем компрессор
			}
		}
	}
}

// Получить информацию что надо делать сейчас (на данной итерации)
// Управляет дополнительным нагревателем бойлера, на выходе что будет делать ТН
MODE_HP HeatPump::EvaluateWorkMode()
{
	MODE_HP ret = pOFF;
	Status.prev = Status.ret;
	Status.ret = pNone;           // не определено

	// 1. Бойлер (определяем что делать с бойлером)
	switch((int) UpdateBoiler())  // проверка бойлера высший приоритет
	{
	case pCOMP_OFF:
		if(onBoiler) {
			if(Status.ret == pBh22 || Status.ret == pBp22) flagRBOILER = true;
			journal.jprintf(" Stop Boiler [%s]\n", Ret_Text(get_ret()));
		}
		ret = pOFF;
		break;
	case pCOMP_ON:
		ret = pBOILER;
		break;
	case pCOMP_NONE:
		ret = pBOILER + pCONTINUE;
		break;
	}

	if(DBG_ON(DBG_MODW)) { journal.jprintf(" EvaluateWorkMode-Boiler: %s(%s), ret=%X, onBoiler=%d, flagRBOILER=%d\n", Ret_Text(Status.ret), Ret_Text(Status.prev), ret, onBoiler, flagRBOILER); }
	if(((get_modeHouse() == pOFF) && (ret == pOFF)) || error) return ret; // режим ДОМА выключен (т.е. запрещено отопление или охлаждение дома) И бойлер надо выключить, то выходим с сигналом pOFF (переводим ТН в паузу)
	if((ret & pBOILER)) return ret; // работает бойлер больше ничего анализировать не надо выход

	// 3. Отопление/охлаждение
	switch((int) get_modeHouse())   // проверка отопления
	{
	case pOFF:
		ret = pOFF;
		break;
	case pHEAT:
		switch((int) UpdateHeat()) {
		case pCOMP_OFF:
			ret = pOFF;
			break;
		case pCOMP_ON:
			ret = pHEAT;
			break;
		case pCOMP_NONE:
			if(is_compressor_on()) {
				ret = pHEAT;
				if(!onBoiler) ret += pCONTINUE;
			}
			break;
		}
		break;
	case pCOOL:
		switch((int) UpdateCool()) {
		case pCOMP_OFF:
			ret = pOFF;
			break;
		case pCOMP_ON:
			ret = pCOOL;
			break;
		case pCOMP_NONE:
			if(is_compressor_on()) {
				ret = pCOOL;
				if(!onBoiler) ret += pCONTINUE;
			}
			break;
		}
		break;
	}
#ifdef RHEAT  // Дополнительный ТЭН для нагрева отопления
if(!GETBIT(Option.flags,fBackupPower)){ // Нет питания от резервного источника
	if (GETBIT(Option.flags,fAddHeat))
	{
		if(!GETBIT(Option.flags,fTypeRHEAT)) // резерв
		{
			if (((sTemp[TIN].get_Temp()>Option.tempRHEAT)&&(dRelay[RHEAT].get_Relay()))||((ret==pOFF)&&(dRelay[RHEAT].get_Relay()))) {journal.jprintf(" TIN=%.2f, add heatting off . . .\n",sTemp[TIN].get_Temp()/100.0); dRelay[RHEAT].set_OFF();} // Гистерезис 0.2 градуса что бы не щелкало
			if ((sTemp[TIN].get_Temp()<Option.tempRHEAT-HYSTERESIS_RHEAD)&&(!dRelay[RHEAT].get_Relay())&&(ret!=pOFF)) {journal.jprintf(" TIN=%.2f, add heatting on . . .\n",sTemp[TIN].get_Temp()/100.0); dRelay[RHEAT].set_ON();}
		}
		else                                // бивалент
		{
			if ( ((sTemp[TOUT].get_Temp()>Option.tempRHEAT)&&(dRelay[RHEAT].get_Relay()))||((ret==pOFF)&&(dRelay[RHEAT].get_Relay())) ) {journal.jprintf(" TOUT=%.2f, add heatting off . . .\n",sTemp[TOUT].get_Temp()/100.0);dRelay[RHEAT].set_OFF();} // Гистерезис 0.2 градуса что бы не щелкало
			if ((sTemp[TOUT].get_Temp()<Option.tempRHEAT-HYSTERESIS_RHEAD)&&(!dRelay[RHEAT].get_Relay())&&(ret!=pOFF)) {journal.jprintf(" TOUT=%.2f, add heatting on . . .\n",sTemp[TOUT].get_Temp()/100.0); dRelay[RHEAT].set_ON();}
		}
	}
}	
else if(dRelay[RHEAT].get_Relay()) dRelay[RHEAT].set_OFF();// есть питание от резервного источника - запрет использования электрокотла, если надо выключаем
#endif
	if(DBG_ON(DBG_MODW)) { journal.jprintf(" EvaluateWorkMode: %s, ret=%X\n", Ret_Text(Status.ret), ret); }
	return ret;
}

// Конфигурация 4-х, 3-х ходового крана и включение насосов, ТЭН бойлера, ТЭН отопления
// В зависимости от входного параметра конфигурирует краны, выход true - разрешен запуск компрессора
boolean HeatPump::configHP(MODE_HP conf)
{
	if(conf == pOFF) { // ЭТО может быть пауза! Выключить - установить положение как при включении ( перевод 4-х ходового производится после отключения компрессора (см compressorOFF()))
		#ifdef RBOILER
			if((GETBIT(Prof.Boiler.flags,fTurboBoiler))&&(dRelay[RBOILER].get_present())) dRelay[RBOILER].set_OFF();  // Выключить ТЭН бойлера в режиме турбо (догрев не работает)
		#endif
		#ifdef RHEAT
			if(dRelay[RHEAT].get_present()) dRelay[RHEAT].set_OFF();     // Выключить ТЭН отопления
		#endif
		PUMPS_OFF;
	} else if((conf & pHEAT)) {    // Отопление
		if(Switch_R4WAY(false)) return false; 							  // 4-х ходовой на нагрев
		if(is_compressor_on()) {                                          // Компрессор работает, переключаемся на ходу 
			switchBoiler(false);                                          // выключить бойлер 
		} else {
			PUMPS_ON;                                                     // включить насосы
			dFC.set_target(dFC.get_startFreq(),true,dFC.get_minFreq(),dFC.get_maxFreq());  // установить стартовую частоту если компрессор выключен
		}
		#ifdef RBOILER
		 	 if((GETBIT(Prof.Boiler.flags,fTurboBoiler))&&(dRelay[RBOILER].get_present())) dRelay[RBOILER].set_OFF(); // Выключить ТЭН бойлера (режим форсированного нагрева)
		#endif
		#ifdef RHEAT
		#endif
	} else if((conf & pCOOL)) {  // Охлаждение
		if(Switch_R4WAY(true)) return false; 							   // 4-х ходовой на охлаждение
		if(is_compressor_on()) {                                           // Компрессор работает, переключаемся на ходу   
			switchBoiler(false);                                           // выключить бойлер
		} else {
			PUMPS_ON;                                                     // включить насосы
			dFC.set_target(dFC.get_startFreq(),true,dFC.get_minFreqCool(),dFC.get_maxFreqCool());   // установить стартовую частоту
		}
		#ifdef RBOILER
			if((GETBIT(Prof.Boiler.flags,fTurboBoiler)) && (dRelay[RBOILER].get_present())) dRelay[RBOILER].set_OFF(); // Выключить ТЭН бойлера (режим форсированного нагрева)
		#endif
		#ifdef RHEAT
		  	  if (dRelay[RHEAT].get_present()) dRelay[RHEAT].set_OFF();     // Выключить ТЭН отопления
		#endif
	} else if((conf & pBOILER)) {  // Бойлер
		if(Switch_R4WAY(false)) return false; 	 					   // 4-х ходовой на нагрев
		if(is_compressor_on()) {                                       // Компрессор работает, переключаемся на ходу
			switchBoiler(true);                                        // включить бойлер
		} else {
			PUMPS_ON;           // включить насосы
			if(Status.ret < pBp5) dFC.set_target(dFC.get_startFreqBoiler(),true,dFC.get_minFreqBoiler(),dFC.get_maxFreqBoiler()); // установить стартовую частоту
		}
		#ifdef RHEAT
			if (dRelay[RHEAT].get_present()) dRelay[RHEAT].set_OFF();     // Выключить ТЭН отопления
		#endif
	} else if((conf & pDEFROST)) {  // Разморозка
		// to do...
	} else { // ?
		set_Error(ERR_CONFIG,(char*)__FUNCTION__);   // Ошибка!  что то пошло не так
		return false;
	}
	return true;
}

// Переключение реверсивного 4-х ходового клапана (true - охлаждение, false - нагрев), выход - компрессор остановлен
// останов компрессора и обеспечение нужных пауз, компрессор включается далее в vUpdate()
boolean HeatPump::Switch_R4WAY(boolean fCool)
{
#ifdef R4WAY
	if(fCool == dRelay[R4WAY].get_Relay()) return false;
	journal.jprintf("Switch to %s\n", fCool ? "COOL" : "HEAT");
	boolean ret = false;
	if(is_compressor_on()) {
		compressorOFF();
		ret = true;
	}
	if(fCool) dRelay[R4WAY].set_ON(); else dRelay[R4WAY].set_OFF();
	_delay(DELAY_AFTER_SWITCH_RELAY);                        // Задержка
//	пауза не нужна, отстоялись в насосах
#else // R4WAY
    boolean ret = false; 
	if(fCool) journal.jprintf("No 4-way valve avalilable!\n");
#endif
	return ret;
}

// проверка на паузу между включениями, возврат true - в паузе
boolean HeatPump::check_compressor_pause()
{
	if(stopCompressor && Uptime_s() - stopCompressor < Option.pause) {
		if(!compressor_in_pause) journal.jprintf_time("Waiting compressor, pause %d s...\n", (int)(Option.pause - (Uptime_s() - stopCompressor)));
		return compressor_in_pause = true;
	}
	return compressor_in_pause = false;
}

// Попытка включить компрессор  с учетом всех защит КОНФИГУРАЦИЯ уже установлена
// Вход режим работы ТН
// Возможно компрессор уже включен и происходит только смена режима
const char *EEV_go={" EEV go "};  // экономим место
void HeatPump::compressorON()
{
	if(get_State() == pOFF_HP || get_State() == pSTOPING_HP || error) return;  // ТН выключен или выключается выходим ничего не делаем!!!

	if(is_compressor_on()) return;                                  // Компрессор уже работает
	if(is_next_command_stop()) {
		journal.jprintf(" Next command stop in queue, skip start");
		return;
	}

	if(ResetFC() != OK) {                                // Сброс инвертора если нужно
		set_Error(ERR_RESET_FC, (char*) __FUNCTION__);
		return;
	}

#ifdef EEV_DEF
	if(lastEEV != -1) {         // Не первое включение компрессора после старта ТН
		// 1. Обеспечение минимальной паузы компрессора
		if(compressor_in_pause) return;
		journal.jprintf_time("compressorON > modWork:%X[%s], now %s\n",get_modWork(),Ret_Text(Status.ret), is_compressor_on() ? "ON" : "OFF");
	}//get_fEEVStartPosByTemp()
	// 2. Разбираемся с ЭРВ
	journal.jprintf(EEV_go);
	if(dEEV.get_LightStart()) { // Выйти на пусковую позицию
		dEEV.set_EEV(dEEV.get_preStartPos());
#ifdef EEV_PREFER_PERCENT
		journal.jprintf("preStartPos: %.2d\n", dEEV.calc_percent(dEEV.get_preStartPos()));
#else
		journal.jprintf("preStartPos: %d\n", dEEV.get_preStartPos());
#endif
	} else if(dEEV.get_StartFlagPos()) { // Всегда начинать работу ЭРВ со стартовой позиции
		dEEV.set_EEV(dEEV.get_StartPos());
#ifdef EEV_PREFER_PERCENT
		journal.jprintf("StartPos: %.2d\n", dEEV.calc_percent(dEEV.get_EEV()));
#else
		journal.jprintf("StartPos: %d\n", dEEV.get_EEV());
#endif

	} else if(lastEEV != -1) { // установка последнего значения ЭРВ
		dEEV.set_EEV(lastEEV);
#ifdef EEV_PREFER_PERCENT
		journal.jprintf("lastEEV: %.2d\n", dEEV.calc_percent(lastEEV));
#else
		journal.jprintf("lastEEV: %d\n", lastEEV);
#endif
	}
	if(lastEEV != -1 && dEEV.get_EevClose()) {        // Если закрывали то пауза для выравнивания давлений
		// delayOn задаётся в секундах: так сказано и в объявлении поля, и у умолчания,
		// и в сеттере (диапазон 0..255 с), и в описании параметра DON для веба;
		// парный delayOff в compressorOFF умножается на 1000. Без множителя пауза
		// выравнивания давлений при умолчании 5 длилась бы 5 мс, то есть её фактически
		// не было бы.
		//
		// Отмечаемся: с множителем это уже длинная пауза (до 255 с) в пути пуска
		// компрессора, а предел молчания UpdateHP и CommandHP - 120 с.
		Watchdog_SleepHere(dEEV.get_delayOn() * 1000);  // Задержка на delayOn сек для выравнивания давлений
	}
	dEEV.CorrectOverheatInit();
	for(uint8_t i = 1; i && dEEV.stepperEEV.isBusy(); i++) _delay(100); // wait EEV stop
#endif
	

	// 3. Управление компрессором
	if (get_errcode()==OK)                                 // Компрессор включить если нет ошибок
	{
		// Дополнительные защиты перед пуском компрессора
		if (startPump)                                      // Проверка задачи насос - должен быть выключен
		{
			startPump=false;                               // Поставить признак останова задачи насос
			if(get_workPump()) journal.jprintf(" WARNING! %s: Pumps in pause, OFF . . .\n",(char*)__FUNCTION__);
		}
	#ifdef DEFROST
	  if(!(get_modWork() & pDEFROST))  // При разморозке есть лишние проверки
	  {
	#endif		
		// Проверка включения насосов с проверкой и предупреждением (этого не должно быть)
		if(!dRelay[PUMP_IN].get_Relay()) {
			journal.jprintf(" WARNING! %s is off before start compressor!\n", dRelay[PUMP_IN].get_name());
			set_Error(ERR_COMP_NO_PUMP, (char*) dRelay[PUMP_IN].get_name());
			return;
		}
		if(!(dRelay[PUMP_OUT].get_Relay()
		)) {
			journal.jprintf(" WARNING! %s is off before start compressor!\n", dRelay[PUMP_OUT].get_name());
			set_Error(ERR_COMP_NO_PUMP, (char*) dRelay[PUMP_OUT].get_name());
			return;
		}

		journal.jprintf_time("Pause %d s before start compressor\n", Option.delayOnPump);
		uint16_t d = Option.delayOnPump;
#ifdef FLOW_CONTROL
		if(Option.delayOnPump < BASE_TIME_READ + TIME_READ_SENSOR/1000 + 1) d = BASE_TIME_READ + TIME_READ_SENSOR/1000 + 1;
#endif
		for(; d > 0; d--) { // задержка перед включением компрессора
			// Отсчёт идёт посекундно, но суммарно это до 900 с (по умолчанию 60)
			// в одной задаче без единой отметки - вместе с паузой выхода ЭРВ
			// на стартовую позицию ниже это 90 с из предела 120 с уже на
			// заводских настройках. Отмечаемся на каждой секунде.
			Watchdog_SleepHere(1000);
			if(error || is_next_command_stop()) return; // прерваться по ошибке, еще бы проверить команду на останов...
		}

#ifdef FLOW_CONTROL      // если надо проверяем потоки (защита от отказа насосов) ERR_MIN_FLOW
		for(uint8_t i = 0; i < FNUMBER; i++) {   // Проверка потока по каждому датчику
			 if(sFrequency[i].get_checkFlow() && sFrequency[i].get_Value() < sFrequency[i].get_minValue()) {  // Поток меньше минимального
				_delay(TIME_READ_SENSOR);
				if(sFrequency[i].get_Value() < sFrequency[i].get_minValue()) {  // Поток меньше минимального
					journal.jprintf(" Flow %s: %.3d\n", sFrequency[i].get_name(), sFrequency[i].get_Value());
					set_Error(ERR_MIN_FLOW, (char*) sFrequency[i].get_name());
					return;
				}
			}
		}	
#endif
#ifdef SEVA  //Если определен лепестковый датчик протока - это переливная схема ТН - надо контролировать проток при работе
		if(dRelay[RPUMPI].get_Relay())                                                                                             // Только если включен насос геоконтура  (PUMP_IN)
			if (sInput[SEVA].get_Input()==SEVA_OFF) {set_Error(ERR_SEVA_FLOW,(char*)"SEVA"); return;}                              // Выход по ошибке отсутствия протока
#endif

#ifdef DEFROST
   }  // if(!(mod & pDEFROST))
#endif	
		resetPID(); 										// Инициализировать переменные ПИД регулятора
		if(Charts_when_comp_on) task_updstat_chars = 0;
	    command_completed = Uptime_s();
	  	COMPRESSOR_ON;                                      // Включить компрессор
		if(error || dFC.get_err()) return; // Ошибка - выходим
	} else { // if (get_errcode()==OK)
		journal.jprintf(" EEV not set before start compressor!\n");
		set_Error(ERR_COMP_ERR,(char*)__FUNCTION__);return;
	}

	// 4. Если нужно облегченный пуск  в зависимости от флага fEEV_light_start
#ifdef EEV_DEF
	if(dEEV.get_LightStart())                  //  ЭРВ ОБЛЕГЧЕННЫЙ ПУСК
	{
		journal.jprintf(" Pause %d second before go starting position EEV . . .\n", dEEV.get_DelayStartPos());
		Watchdog_SleepHere(dEEV.get_DelayStartPos() * 1000);  // Задержка после включения компрессора до ухода на рабочую позицию
		journal.jprintf(EEV_go);
		if((dEEV.get_StartFlagPos()) || ((lastEEV == -1))) {
			dEEV.set_EEV(dEEV.get_StartPos());
#ifdef EEV_PREFER_PERCENT
			journal.jprintf("StartPos: %.2d\n", dEEV.calc_percent(dEEV.get_EEV()));
#else
			journal.jprintf("StartPos: %d\n", dEEV.get_EEV());
#endif
		}    // если первая итерация или установлен соответсвующий флаг то на стартовую позицию
		else { // установка последнего значения ЭРВ в противном случае
			dEEV.set_EEV(lastEEV);
#ifdef EEV_PREFER_PERCENT
			journal.jprintf("lastEEV: %.2d\n", dEEV.calc_percent(lastEEV));
#else
			journal.jprintf("lastEEV: %d\n", lastEEV);
#endif
		}
	}
#endif
	// 5. Обеспечение задержки отслеживания ЭРВ
#ifdef EEV_DEF
	if (lastEEV>0)                                            // НЕ первое включение компрессора после старта ТН
	{
		dEEV.Resume();
		Wake_UpdateEEV();                                                  // Запустить задачу Обновления ЭРВ
		journal.jprintf(" Resume task UpdateEEV\n");
		#ifdef DEFROST
		 if(!(get_modWork() & pDEFROST)) journal.jprintf_time("%s WORK . . .\n",(char*)nameHeatPump);     // Сообщение о работе
		 else journal.jprintf_time("%s DEFROST . . .\n",(char*)nameHeatPump);               // Сообщение о разморозке
		#else
		journal.jprintf_time("%s WORK . . .\n",(char*)nameHeatPump);     // Сообщение о работе
		#endif
	}
	else  // признак первой итерации
	{
		set_startTime(rtcSAM3X8.unixtime());                         // Запомнить время старта ТН
		lastEEV = dEEV.get_EEV();                                 // ЭРВ работает запомнить
		dEEV.Resume();
		Wake_UpdateEEV();                                                  // Запустить задачу Обновления ЭРВ
		journal.jprintf(" Start task UpdateEEV\n");
	}
#else
	lastEEV = 1;                                                   // Признак первой итерации
	set_startTime(rtcSAM3X8.unixtime());                         // Запомнить время старта ТН
#endif
}

// попытка выключить компрессор  с учетом всех защит
void HeatPump::compressorOFF()
{
#ifdef EEV_DEF
	lastEEV = dEEV.get_EEV();                                             // Запомнить последнюю позицию ЭРВ
	dEEV.Pause();                                                       // Поставить на паузу задачу Обновления ЭРВ
	journal.jprintf(" Stop control EEV\n");
#endif

	command_completed = Uptime_s();
	if(is_compressor_on()) {
		COMPRESSOR_OFF;                                             // Компрессор выключить
	}


	PUMPS_OFF;                                                          // выключить насосы + задержка

	onBoiler = false;
	offBoiler = Uptime_s();

#ifdef EEV_DEF
	if(dEEV.get_EevClose())                                 // Разбираемся с ЭРВ
	{
		journal.jprintf(" Pause before closing EEV %d sec . . .\n", dEEV.get_delayOff());
		Watchdog_SleepHere(dEEV.get_delayOff() * 1000);                                // пауза перед закрытием ЭРВ  на инверторе компрессор останавливается до 2 минут
		dEEV.set_EEV(EEV_CLOSE_STEP);                                    // Если нужно, то закрыть ЭРВ
		journal.jprintf(" EEV closed\n");
	}
#endif
}

// ---- РАЗМОРОЗКА ВОЗДУШНИКА ----
// Все что касается разморозки воздушника. Функция работает следующим образом: пока не закончена разморозка из функции нет выхода, т.е. она автономна
#ifdef DEFROST
#define TEMP_NO_DEFROST    600   // температура выше которой разморозка не включается
#define TEMP_STEAM_DEFROST 200   // температура ниже которой оттаиваем паром
#define TEMP_END_DEFROST   1500  // температура окончания оттайки
void HeatPump::defrost()
{
      if (get_State()==pOFF_HP) return;                                    // если ТН не работает то выходим
      bool defrostTimeout = false;                                         // оттайка не закончилась за DEFROST_WAIT_MAX
      
      #ifdef R4WAY            // Нет четырехходового - нет режима охлаждения
  if (dRelay[R4WAY].get_Relay() == false) return;                          // режим охлаждения - размораживать не надо, у меня false это охлаждение.
#else
 set_Error(ERR_DEFROST_R4WAY, (char*)__FUNCTION__);return;                 // Четырех ходового нет разморозка не возможна - это косяк конфигурации
      #endif
         
// Реализовано два алгоритма выбор по наличию SFROZEN
#ifdef SFROZEN    // Алгоритм разморозки по датчику  SFROZEN             
      if (sInput[SFROZEN].get_Input()==SFROZEN_OFF) {startDefrost=0;return;  }    // размораживать не надо - датчик говорит что все ок
      
      // организация задержки перед включением
      if (startDefrost==0) startDefrost=xTaskGetTickCount();               // первое срабатывание датчика - запоминаем время (тики)
      if (!Defrost_DelayPassed(xTaskGetTickCount(), startDefrost, Option.delayDefrostOn))  return; //  Еще рано размораживать
      // придется размораживать
       journal.jprintf("Start defrost\n");
       Switch_R4WAY(true);  // охлаждение
       
       compressorON();                                                 // включить компрессор на холод
      
      // ПОТОЛОК ОЖИДАНИЯ. До нарезки сна залипший датчик приводил к перезагрузке
      // по молчанию задачи через 120 с - грубо, но заметно. Теперь пауза
      // отмечается, и без потолка это было бы молчаливое вечное ожидание
      // с компрессором в режиме оттайки. DEFROST_WAIT_MAX задаёт предел.
      for(uint16_t w = 0; sInput[SFROZEN].get_Input()!=SFROZEN_OFF; w++)   // ждем оттаивания
      {
        if(w >= DEFROST_WAIT_MAX / 10) { journal.jprintf("$ERROR: defrost sensor timeout, abort\n"); defrostTimeout = true; break; }
      Watchdog_SleepHere(10*1000);                              // Задержка на 10 сек
        journal.jprintf(" Wait process defrost . . .\n"); 
        if((get_State()==pOFF_HP)||(get_State()==pSTARTING_HP)||(get_State()==pSTOPING_HP)) break;     // ТН выключен или включается или выключается выходим из разморозки
      }
#else          // Алгоритм разморозки по температуре
  if (sTemp[TOUT].get_Temp()>TEMP_NO_DEFROST) return;                      // Если температура на улице выше TEMP_NO_DEFROST ТН не обмерзает
  if ((is_compressor_on()) && (Uptime_s()-startCompressor<15*60)) return;   // компрессор работает, но прошло менее 15 минут - размораживать не надо
  if (sTemp[TEVAIN].get_Temp()-sTemp[TOUT].get_Temp()>-1200) {startDefrost=0;return;  }   // размораживать не надо - условие не наступило
  journal.jprintf("Next step, defrost . . .\n");
  // организация задержки перед включением
  if (startDefrost==0) startDefrost=xTaskGetTickCount();                    // первое срабатывание датчика - запоминаем время (тики)// Наступило условие, средняя температура испарителя ниже воздуха, более чем 15С - запоминаем время (тики)
  if (!Defrost_DelayPassed(xTaskGetTickCount(), startDefrost, Option.delayDefrostOn))  return;  // Еще рано размораживать
  journal.jprintf("Start defrost\n");                                       // Пошла оттайка
	 // Дальше в зависимости от работает компрессор или нет - это главное условие
	  if (is_compressor_on())
	  {
	    if (dRelay[R4WAY].get_Relay() == true) // Если четырехходовой стоит на тепло - Главный вариант
	    {
	    	// Компрессор работает и 4-х ходовой стоит на тепле то хитро переключаем 4-х ходовой в положение холод
	    	compressorOFF();
		    if (sTemp[TOUT].get_Temp()<=TEMP_STEAM_DEFROST)                        // Если температура на улице ниже или равна TEMP_STEAM_DEFROST то оттаиваем паром
			     {
			      dRelay[PUMP_IN].set_OFF();                                        // выключаем вентилятор
			     }
		    }
	    else  { set_Error(ERR_DEFROST_R4WAY, (char*)__FUNCTION__);return; }           // ХА ХА  Работаем на охлаждение но при этом требуется разморозка - Это косяк Карл, но до сюда не доедит
	  	
	  }
	  else  // Компрессор не работает - тяжелый случай все делаем руками
	  {
	   dRelay[R4WAY].set_OFF();                                                       // переключаем 4ходовик на оттайку
	   _delay(1*1000);   // WDT-OK: одиночная секундная пауза развязки реле, не цикл
	   dRelay[PUMP_OUT].set_OFF();                                                   // Включение насоса выходного контура
		 if (sTemp[TOUT].get_Temp()>TEMP_STEAM_DEFROST)                              // Если температура на улице ниже или равна TEMP_STEAM_DEFROST то оттаиваем паром 	
		 {
		  dRelay[PUMP_IN].set_ON();                                                  // включаем вентилятор
		 }
	      compressorON();                                                    // включить компрессор на разморозку
	  } // Компрессор не работает
	
	 // ТН в нужном состоянии надо только ждать 
	 // ПОТОЛОК ОЖИДАНИЯ, та же причина, что и у цикла по датчику обмерзания выше.
	 for(uint16_t w = 0; sTemp[TEVAOUT].get_Temp()<TEMP_END_DEFROST; w++)   // Ждем оттаивания, поднятия температуры выхода испарителя. Испаритель и конденсатор меняются местами?
		{
		  if(w >= DEFROST_WAIT_MAX / 10) { journal.jprintf("$ERROR: defrost heat-gas timeout, abort\n"); defrostTimeout = true; break; }
		  Watchdog_SleepHere(10*1000);                                                      // Задержка на 10 сек
		  journal.jprintf(" Wait process HEAT GAS defrost . . .\n");
		  if (error ||(get_State() == pOFF_HP) || (get_State() == pSTARTING_HP) || (get_State() == pSTOPING_HP)) break; // ТН выключен или включается или выключается выходим из разморозки
		}
#endif // #ifdef SFROZEN 
    // Завершение
	if(defrostTimeout) {                                                     // залипший датчик или неисправный испаритель: оттайки по кругу не нужны
		if(++defrostFails >= DEFROST_FAILS_MAX) {
			compressorOFF();
			set_Error(ERR_DEFROST, (char*)__FUNCTION__);                         // останов ТН
			return;
		}
	} else defrostFails = 0;                                                 // оттайка прошла - счёт неудач заново
      journal.jprintf(" Finish defrost, wait delayDefrostOff min.\n"); 
	compressorOFF();                                                         // выключить компрессор   Пока пусть будет так, в дальнейшем надо доработать
	Watchdog_SleepHere(Option.delayDefrostOff*1000);                                     // Задержка путь стекут остатки воды    
   journal.jprintf("Finish defrost\n");                                      // выходим ТН сам определит что надо делать
     
}
#endif

// ОБРАБОТЧИК КОМАНД УПРАВЛЕНИЯ ТН
// Послать команду на управление ТН

// Температура конденсации
#define MAGIC_CONST_CONDENS 200   // Магическая поправка для перевода температуры выхода конденсатора по гликолю  в температуру конденсации   
int16_t HeatPump::get_temp_condensing(void)
{
#ifdef EEV_DEF  
	if(sADC[PCON].get_present()) {
		return PressToTemp(PCON);
	} else {
		return sTemp[is_heating() ? TCONOUTG : TEVAOUTG].get_Temp() + MAGIC_CONST_CONDENS; // +2C
	}
#else
  return sTemp[is_heating() ? TCONOUTG : TEVAOUTG].get_Temp() + MAGIC_CONST_CONDENS; // +2C
#endif
}

// Переохлаждение
int16_t HeatPump::get_overcool(void)
{
	if(!is_heating()) {
#ifdef TCONOUT
		return get_temp_condensing() - sTemp[TEVAIN].get_Temp();
#else
	return 0;
#endif
	} else {
#ifdef TCONOUT
		return get_temp_condensing() - sTemp[TCONOUT].get_Temp();
#else
	return 0;
#endif
	}
}

// Кипение
int16_t HeatPump::get_temp_evaporating(void)
{
#ifdef EEV_DEF
	if(sADC[PEVA].get_present()) {
		return PressToTemp(PEVA);
	} else {
		return 0; // Пока не поддерживается
	}
#else
 return 0;  // ЭРВ нет
#endif
}

// Возвращает 0 - Нет ошибок или ни одного активного датчика, 1 - ошибка, 2 - превышен предел ошибок
int8_t	 HeatPump::Prepare_Temp(uint8_t bus)
{
	int8_t i, ret = 0;
	i = OneWireBus.PrepareTemp();
	if(testMode != NORMAL) return 0;
	if(i) {
		for(uint8_t j = 0; j < TNUMBER; j++) {
			if(sTemp[j].get_fAddress() && sTemp[j].get_bus() == bus) {
				if(sTemp[j].inc_error()) {
					ret = 2;
					break;
				}
				ret = 1;
			}
		}
		if(ret) {
			journal.jprintf_time("Error %d PrepareTemp bus %d\n", i, bus+1);
			if(ret == 2) set_Error(i, (char*) __FUNCTION__);
		}
	}
	return ret ? (1<<bus) : 0;
}

// Обновление расчетных величин мощностей и СОР
// Вызывается из задачи чтения датчиков 
void HeatPump::calculatePower()
{
	// Сам расчёт делает PowerCalculate (ClimateControl.cpp): это чистая арифметика, и там
	// она проверяется на ПК. Здесь осталось только собрать измерения.
	PowerIn in;
	in.heating          = is_heating();
	in.compressor_on    = is_compressor_on();
	in.backup_power     = GETBIT(Option.flags, fBackupPower);
	in.feed             = FEED;
	in.ret              = RET;
	in.tevaing          = sTemp[TEVAING].get_Temp();
	in.tevaoutg         = sTemp[TEVAOUTG].get_Temp();
#ifdef FLOWCON
	in.flowcon_value    = sFrequency[FLOWCON].get_Value();
	in.flowcon_capacity = sFrequency[FLOWCON].get_Capacity();
#else
	in.flowcon_value = in.flowcon_capacity = 0;
#endif
#ifdef FLOWEVA
	in.floweva_value    = sFrequency[FLOWEVA].get_Value();
	in.floweva_capacity = sFrequency[FLOWEVA].get_Capacity();
#else
	in.floweva_value = in.floweva_capacity = 0;
#endif
	in.fc_power         = dFC.get_power();
#ifdef USE_ELECTROMETER_SDM
	in.sdm_link         = dSDM.get_link();
	in.sdm_power        = dSDM.get_Power();
#else
	in.sdm_link         = false;
	in.sdm_power        = 0;
#endif

	// powerOUT и powerGEO входные тоже: без расходомера они не меняются,
	// остаётся прежнее значение.
	PowerOut out = { powerOUT, powerGEO, power220, COP, fullCOP };
	PowerCalculate(in, PowerRulesConfig, out);
	powerOUT = out.powerOUT;
	powerGEO = out.powerGEO;
	power220 = out.power220;
	COP      = out.COP;
	fullCOP  = out.fullCOP;
}

// Получить мощность потребления всего ТН (нужно при ограничении мощности при питании от резерва)
// Предполагается что Электросчетчик стоит на входе ТН (ТЭН не включены) это наиболее точный метод определения мощности
// Если электросчетчика нет, то пытаемся получить из частотного преобразователя.
// если не получается определить мощность то функция возвращает 0
#define SUM_POWER_PUMP 200   // Мощность потребления насосов (для добавления к мощности компрессора)
int16_t HeatPump::getPower(void)
{
#ifdef USE_ELECTROMETER_SDM  // Пытаемся получить мощность по электросчетчику
if (dSDM.get_link()) { return  dSDM.get_Power();} // Если счетчик работает (связь не утеряна)
dSDM.uplinkSDM();	// Попытаемся реанимировать счетчик (связь по Modbus)
if (dSDM.get_link()) { return  dSDM.get_Power();} // Если счетчик работает (связь не утеряна)
#endif
// Дошли до сюда - значит не получилось мощность по электросчетчику определить,	работаем с ПЧ
if (dFC.get_present()) return dFC.get_power()+SUM_POWER_PUMP; 
return 0; // Мощность не определилась
}

void HeatPump::set_HeatBoilerUrgently(boolean onoff)
{
	if(HeatBoilerUrgently != onoff) {
		journal.jprintf("Boiler Urgent = %s\n", (HeatBoilerUrgently = onoff) ? "ON" : "OFF");
	}
}


// Уравнение ПИД регулятора в конечных разностях.
// errorPid - Ошибка ПИД = (Цель - Текущее состояние)  в СОТЫХ
// pid - настройки ПИДа в тысячных
// sum, pre_err - сумма для расчета и предыдущая ошибка
// Выход управляющее воздействие (в СОТЫХ)
int32_t updatePID(int32_t errorPid, PID_STRUCT &pid, PID_WORK_STRUCT &pidw)
{
	int32_t newVal;
	// Трасса ПИД одной строкой в конце: фрагменты раздельными jprintf в журнале
	// были бы отдельными записями, да ещё и сжимались бы как повторы.
	int32_t dbgSum0 = pidw.sum, dbgI = 0, dbgP = 0;
	// Cp, Ci, Cd – коэффициенты дискретного ПИД регулятора;
	// u(t) = P (t) + I (t) + D (t);
	// P (t) = Kp * e (t);
	// I (t) = I (t — 1) + Ki * e (t);
	// D (t) = Kd * {e (t) — e (t — 1)};
	// T – период дискретизации(период, с которым вызывается ПИД регулятор).
  
	if(pid.Ki != 0)// Расчет интегральной составляющей, если она не равна 0
	{
		pidw.sum += (int32_t) pid.Ki * errorPid;     // Интегральная составляющая, с накоплением, в СТО ТЫСЯЧНЫХ (градусы 100 и интегральный коэффициент 1000)
		if(pidw.sum > pidw.max) pidw.sum = pidw.max; // Ограничение диапазона изменения ПИД интегральной составляющей, произведение в СТО ТЫСЯЧНЫХ
		else if(pidw.sum < -pidw.max) pidw.sum = -pidw.max;
	} else pidw.sum = 0;              // если Кi равен 0 то интегрирование не используем
	newVal = pidw.sum;
	dbgI = newVal;
	// Пропорциональная составляющая
	if(abs(errorPid) < pidw.Kp_dmin) newVal += (int32_t) abs(errorPid) * pid.Kp * errorPid / pidw.Kp_dmin; // Вблизи уменьшить воздействие
	else newVal += (int32_t) pid.Kp * errorPid;
	dbgP = newVal - pidw.sum;
	// Дифференциальная составляющая
	newVal += (int32_t) pid.Kd * (pidw.pre_err - errorPid);// ДЕСЯТИТЫСЯЧНЫЕ Положительная составляющая - ошибка растет (воздействие надо увеличить)  Отрицательная составляющая - ошибка уменьшается (воздействие надо уменьшить)
	if ((abs(newVal)>=pidw.max)&&(pidw.max>0)) pidw.sum=0; // Сброс интегральной составляющей при движении на один шаг (оптимизация классического ПИДа) 100000 Это один шаг

	if(DBG_ON(DBG_PID)) { journal.jprintf("PID(%x): err:%d,pre_err:%d,sum:%d (%d,%d,%d). I:%d,P:%d,D:%d PID:%d\n", (unsigned)(uintptr_t)&pid, (int)errorPid, (int)pidw.pre_err, (int)dbgSum0, (int)pid.Kp, (int)pid.Ki, (int)pid.Kd, (int)dbgI, (int)dbgP, (int)(pid.Kd * (pidw.pre_err - errorPid)), (int)newVal); }
	pidw.pre_err = errorPid; // запомнить предыдущую ошибку
	return round_div_int32(newVal, 1000); // Учесть разрядность коэффициентов (ТЫСЯЧНЫЕ), выход в СОТЫХ
}

void UpdatePIDbyTime(uint16_t, uint16_t, PID_STRUCT &) {}
