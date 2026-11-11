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
 * Шина Modbus: транспорт devModbus и единственный прибор, который живёт
 * только на ней, - счётчик электроэнергии devSDM. Отдельная единица
 * трансляции.
 *
 * Транспорт общий: кроме счётчика на нём висят драйверы инвертора
 * OmronFC и VaconFC, каждый в своём файле. Функции обратного вызова
 * библиотеки ModbusMaster (idle, preTransmission, postTransmission) и
 * счётчик входов в критическую секцию - внутренние для этого модуля.
 *
 * Hardware.h тянет Constant.h, а с ним Config.h, поэтому условия
 * конфигурации (USE_ELECTROMETER_SDM, FC_VACON и прочие) разрешаются так
 * же, как в главной единице трансляции. Проверено сверкой размеров методов.
 */
#include "Hardware.h"     // devModbus, devSDM, объекты шины
#include "../core/HeatPump.h"     // HP - счётчик пишет в него мощность
#include "ModbusLock.h"          // Modbus_LockRefused
#include "../core/Rtos.h"         // xModbusSemaphore, _delay, TaskSuspendAll, Watchdog_AliveHere
#include "../net/ParamRegistry.h" // таблица параметров веб-API (внизу файла)

// ---- Счетчик SDM ----
// Инициализация счетчика и проверка и если надо программирование
int8_t devSDM::initSDM()
{
	err = OK;                                        // Ошибок нет
	numErr = 0;                                      // счетчик 0
	Voltage = 0.0f;                                   // Напряжение
	AcPower = 0.0f;                                   // активная мощность
	flags = 0x00;
	// Настройки
#ifdef SDM_MAX_VOLTAGE
	settingSDM.maxVoltage = SDM_MAX_VOLTAGE;         // максимальное напряжение (вольты) иначе ошибка если 0 то не работает
#endif
#ifdef SDM_MIN_VOLTAGE
	settingSDM.minVoltage = SDM_MIN_VOLTAGE;         // минимальное напряжение (вольты) иначе ПРЕДУПРЕЖДЕНИЕ если 0 то не работает
#endif
	settingSDM.maxPower = SDM_MAX_POWER;             // максимальная мощность (ватты) напряжение иначе ошибка если 0 то не работает
	name = (char*) nameSDM;
	note = (char*) noteSDM_NONE;

#ifdef USE_ELECTROMETER_SDM
	if(!Modbus.get_present())                  // modbus отсутствует
	{
		journal.jprintf("%s: modbus not found, block.\n", name);
		SETBIT0(flags, fSDM);                           // счетчик не представлен
		SETBIT0(flags, fSDMLink);
		err = ERR_NO_MODBUS;
	} else {
		SETBIT1(flags, fSDM);                           // счетчик представлен
		note = (char*) noteSDM;
		uplinkSDM();                                  // проверить связь со счетчиком
	}
#else
	SETBIT0(flags,fSDM);                           // счетчик не представлен
	SETBIT0(flags,fSDMLink);
	note=(char*)noteSDM_NONE;
#endif
	return err;
}

// Проверить связь со счетчиком предполагается что Modbus уже неинициализирован читаем из регистра скорость
// Выводит сообщения в журнал и устанавливает флаг связи
boolean devSDM::uplinkSDM()
{
	int8_t i, errModbus=0;
	for(i = 0; i < SDM_NUM_READ; i++)   // делаем SDM_NUM_READ попыток чтения
	{
		float band;
		if(((errModbus=Modbus.readHoldingRegistersFloat(SDM_MODBUS_ADR, SDM_BAUD_RATE, &band)) == OK) && (band == SDM_SPEED)) {
			SETBIT1(flags, fSDMLink);
			journal.jprintf("%s, found, link OK, band rate:%.0f modbus address:%d\n", name, band, SDM_MODBUS_ADR);
			return true;
		} else {
#ifdef SDM_BLOCK                     // если стоит флаг блокировки связи
			SETBIT0(flags, fSDMLink);
#else
			SETBIT1(flags, fSDMLink);
#endif
			journal.jprintf("Error %d, %s, no connect.\n", errModbus, name);
		}
		_delay(SDM_DELAY_REPEAD);
		Watchdog_AliveHere();                                                            // Сбросить вачдог
	}
#ifdef SDM_BLOCK                     // если стоит флаг блокировки связи
	SETBIT0(flags, fSDMLink);
#else
	SETBIT1(flags, fSDMLink);
#endif
	return false;
}

// перепрограммировать счетчик на требуемые параметры связи SDM_SPEED SDM_MODBUS_ADR c DEFAULT_SDM_SPEED DEFAULT_SDM_MODBUS_ADR
// При программировании параметры сразу начинают рабоать
boolean  devSDM::progConnect()
{
  float band; 
  journal.jprintf("%s: Setting band rate and modbus address.\n",name); 
  // 1. Проверка
  if ((Modbus.readHoldingRegistersFloat(SDM_MODBUS_ADR,SDM_BAUD_RATE,&band)==OK)&&(band==SDM_SPEED)) {SETBIT1(flags,fSDMLink); journal.jprintf(" Setting %s are correct, programming is not required\n",name);  return true;} 
  
  // 2. Установка скорости по умолчанию
  
  MODBUS_PORT_NUM.begin(DEFAULT_SDM_SPEED,MODBUS_PORT_CONFIG);            // SERIAL_8N1 - настройки порта для счетчика из коробки
  err=OK;
  
  // 3. На DEFAULT_SDM_SPEED скорости установить правильный адрес SDM_MODBUS_ADR
  if( (err=Modbus.writeHoldingRegistersFloat(DEFAULT_SDM_MODBUS_ADR,SDM_ADR_MODBUS,SDM_MODBUS_ADR))==OK)  journal.jprintf("%s: Set address  %d.\n",name,SDM_MODBUS_ADR);  // установить требуемый адрес
  else  journal.jprintf("%s: Setting address error, code %d.\n",name,err); 
  
  // 4. На правильный адрес SDM_MODBUS_ADR установит желаемую скорость SDM_SPEED
  if( (err=Modbus.writeHoldingRegistersFloat(SDM_MODBUS_ADR, SDM_BAUD_RATE,SDM_SPEED))==OK)  journal.jprintf("%s: Set band rate (0=2400bps 1=4800bps 2=9600bps 5=1200bps) %d.\n",name,SDM_SPEED);  // установить требуемую скорость
  else  journal.jprintf("%s: Setting band rate error, code %d.\n",name,err);
  
  // 5. Порт обратно в требуемые настройки
  MODBUS_PORT_NUM.begin(MODBUS_PORT_SPEED,MODBUS_PORT_CONFIG);                 // SERIAL_8N1 - настройки по умолчанию

  // 6. Вывод результатов
  if (err==OK) { journal.jprintf("%s: Programming is Ok\n",name); uplinkSDM(); return true;}  // Надо сбросить счетчик
  else { journal.jprintf("%s: Programming is wrong, no link\n",name); return false; }
}                           

// Прочитать инфо с счетчика, group: 0 - основная (расчет мощности), 2 - через SDM_READ_PERIOD
int8_t devSDM::get_readState(uint8_t group)
{
	float tmp;
	if(!GETBIT(flags,fSDM) || !GETBIT(flags,fSDMLink)
#ifdef USE_UPS
		|| HP.NO_Power
#endif
		) {   // Если нет счетчика или нет связи выходим
		AcPower = 0.0f;
		Voltage = 0.0f;
		return err;
	}
	// Чтение состояния счетчика
	ModbusResult exchange{};
	for(int8_t i=0; i < SDM_NUM_READ; i++)   // делаем SDM_NUM_READ попыток чтения
	{
		// Читаем значения счетчика
		if(group == 0) {
			exchange = Modbus.readInputRegistersFloat(SDM_MODBUS_ADR, SDM_AC_POWER, &tmp);
			if(exchange == OK) AcPower = tmp; else goto xErr;
		}
		if(group == 2) {
#if defined(SDM_MAX_VOLTAGE) || defined(SDM_MIN_VOLTAGE) || (SDM_READ_PERIOD > 0)
			exchange = Modbus.readInputRegistersFloat(SDM_MODBUS_ADR, SDM_VOLTAGE, &tmp);   // Напряжение
			if(exchange == OK) { Voltage=tmp; group = 1; } else goto xErr;
#endif
		}
		if(exchange == OK) break;
xErr:
#ifdef SPOWER
		HP.sInput[SPOWER].Read(true);
        if(HP.sInput[SPOWER].is_alarm()) return err;
#endif
		numErr++;                  // число ошибок чтение по Modbus
		if(DBG_ON(DBG_SDM)) {
			journal.jprintf_time("%s: Read #%d error %d, repeat...\n", name, group, exchange.status);      // Выводим сообщение о повторном чтении
		}
		_delay(SDM_DELAY_REPEAD);  // Чтение не удачно, делаем паузу
	}
	if(exchange == OK)
	{
#ifdef SDM_MAX_VOLTAGE
		if((settingSDM.maxVoltage>1)&&(settingSDM.maxVoltage< Voltage)) {err=ERR_MAX_VOLTAGE;set_Error(err,name);return err; }       // Контроль входного напряжения
#endif
		if ((settingSDM.maxPower>1)&&(settingSDM.maxPower< AcPower))     {err=ERR_MAX_POWER;set_Error(err,name);return err; }         // Контроль мощности потребления
#ifdef SDM_MIN_VOLTAGE
		if(group == 1) {
			const bool low = settingSDM.minVoltage > 1 && Voltage < settingSDM.minVoltage;
			if(low && !lowVoltage) HP.set_Warning(ErrorCatalog::kVOLTAGE_LOW, name, 0, nullptr, true);
			lowVoltage = low;
		}
#endif
		return err = exchange.status;                       // все прочиталось, выходим
	}
#ifdef SDM_BLOCK                     // если стоит флаг блокировки связи
	SETBIT0(flags,fSDMLink);             // связь со счетчиком потеряна
#endif
	const ErrorDetail detail = ModbusDetail(exchange);
	const char *kind = detail.kind == ErrorDetailKind::Exception ? "EXC" :
	                   detail.kind == ErrorDetailKind::Transport ? "RX" : "LOCAL";
	const auto code = ModbusAlarm(exchange.kind);
	const auto *entry = ErrorCatalog::Find(code);
	char text[128];
	const char *description = nullptr;
	if(detail.kind != ErrorDetailKind::None && entry) {
		m_snprintf(text, sizeof(text), "%s 0x%02X: %s", kind, (unsigned)detail.value, entry->description);
		description = text;
	}
	HP.set_Warning(code, name, (static_cast<uint16_t>(exchange.kind) << 8) | exchange.raw, description);
	return err = exchange.status;
}

// Получить параметр счетчика в виде строки
char* devSDM::get_paramSDM(char *var, char *ret)           
{
	float tmp = 0;                                   // не static: функцию зовут четыре веб-потока, общий tmp перемешивал им ответы

	if(strcmp(var,sdm_NAME)==0){         return strcat(ret,(char*)name);                                         }else      // Имя счетчика
	if(strcmp(var,sdm_NOTE)==0){         return strcat(ret,(char*)note);                                         }else      // Описание счетчика
	if(strcmp(var,sdm_ERRORS)==0){    	return _itoa(numErr,ret);				                                }else      // Ошибок modbus
	if(strcmp(var,sdm_MAX_POWER)==0){    return _itoa(settingSDM.maxPower,ret);                                  }else      // максимальная мощность контроля мощности
	if(strcmp(var,sdm_VOLTAGE)==0){ // Напряжение
#if defined(SDM_MAX_VOLTAGE) || defined(SDM_MIN_VOLTAGE) || (SDM_READ_PERIOD > 0)
		_ftoa(ret, Voltage, 2);
#else
		Modbus.readInputRegistersFloat(SDM_MODBUS_ADR, SDM_VOLTAGE, &tmp);
		_ftoa(ret, tmp, 2);
#endif
		return ret;
	}else
	if(strcmp(var,sdm_CURRENT)==0){ // Ток
		Modbus.readInputRegistersFloat(SDM_MODBUS_ADR, SDM_CURRENT, &tmp);
		_ftoa(ret, tmp, 3);
		return ret;
	}else
	if(strcmp(var,sdm_ACPOWER)==0){// Активная мощность
		_ftoa(ret,(float)AcPower, 2);
		return ret;
	}else
	if(strcmp(var,sdm_ACENERGY)==0){ // Суммарная активная энергия
		Modbus.readInputRegistersFloat(SDM_MODBUS_ADR, SDM_AC_ENERGY, &tmp);
		_ftoa(ret, tmp, 3);
		return ret;
	}else
	if(strcmp(var,sdm_MAX_VOLTAGE)==0){
#ifdef SDM_MAX_VOLTAGE
		return _itoa(settingSDM.maxVoltage,ret);
#else
		return strcat(ret, "-");
#endif
	}else      // мах напряжение контроля напряжения
	if(strcmp(var,sdm_MIN_VOLTAGE)==0){
#ifdef SDM_MIN_VOLTAGE
		return _itoa(settingSDM.minVoltage,ret);
#else
		return strcat(ret, "-");
#endif
	}else      // min напряжение контроля напряжения
	if(strcmp(var,sdm_LINK)==0){         if (GETBIT(flags,fSDMLink)) return strcat(ret,(char*)cYes); else return strcat(ret,(char*)cNo);}       // Состояние связи со счетчиком
	else {
		if(GETBIT(flags,fSDMLink)) {
		   if(strcmp(var,sdm_POW_FACTOR)==0){
			   Modbus.readInputRegistersFloat(SDM_MODBUS_ADR, SDM_POW_FACTOR, &tmp);
			   _ftoa(ret, tmp, 2);
		   } else if(strcmp(var,sdm_PHASE)==0){
			   Modbus.readInputRegistersFloat(SDM_MODBUS_ADR, SDM_PHASE, &tmp);
			   _ftoa(ret, tmp, 2);
		   } else if(strcmp(var,sdm_FREQ)==0){
			   Modbus.readInputRegistersFloat(SDM_MODBUS_ADR, SDM_FREQUENCY, &tmp);
			   _ftoa(ret, tmp, 2);
		   }
	   }
	   return ret;
	}
	return strcat(ret,(char*)cInvalid);
}

// Установить параметр счетчика в виде строки
boolean devSDM::set_paramSDM(char *var, char *c)
{
	int32_t x = atoi(c);
	if(strcmp(var,sdm_MAX_VOLTAGE)==0){   if ((x>=0)&&(x<=400)) {settingSDM.maxVoltage=(uint16_t)x;return true;} else  return false; }else      // мах напряжение контроля напряжения
	if(strcmp(var,sdm_MIN_VOLTAGE)==0){   if ((x>=0)&&(x<=400)) {settingSDM.minVoltage=(uint16_t)x;return true;} else  return false; }else      // min напряжение контроля напряжения
	if(strcmp(var, sdm_MAX_POWER) == 0) {       // максимальная мощность для контроля мощности
		if((x >= 0) && (x <= 65535)) {
			settingSDM.maxPower = (uint16_t) x;
			return true;
		} else return false;
	} else return false;
}

// ---- MODBUS Устройство ----
// функции обратного вызова
static uint8_t Modbus_Entered_Critical = 0;
static inline void idle() // задержка между чтениями отдельных байт по Modbus
    {
		_delay(1);		// Отдает время другим задачам
    }
static inline void preTransmission() // Функция вызываемая ПЕРЕД началом передачи
    {
      #ifdef PIN_MODBUS_RSE
      digitalWriteDirect(PIN_MODBUS_RSE, HIGH);
      _delay(1);       // что бы слейв не терял первый бит повышается надежность передачи
      #endif
      Modbus_Entered_Critical = TaskSuspendAll(); // Запрет других задач во время передачи по Modbus 	
 
     }
// Приёмник включается ДО возврата планировщика. Раньше после xTaskResumeAll шла
// пауза через vTaskDelay, и линия оставалась на передаче, пока задача ждала
// процессор: при загруженных задачах приоритета 3-4 (пуск ТН) начало ответа
// инвертора приходилось на выключенный приёмник - таймаут, код -47. Пауза не
// нужна: flush() в библиотеке возвращается по TXEMPTY, то есть кадр ушёл целиком.
static inline void postTransmission() // Функция вызываемая ПОСЛЕ окончания передачи
    {
    #ifdef PIN_MODBUS_RSE
    digitalWriteDirect(PIN_MODBUS_RSE, LOW);
    #endif
  	if(Modbus_Entered_Critical) {
	xTaskResumeAll();
	Modbus_Entered_Critical = 0;
	}
}

// Инициализация Modbus без проверки связи связи
int8_t devModbus::initModbus()
{
	flags = 0;
#ifdef MODBUS_PORT_NUM
	link = {0, GetTickCount()};
	SETBIT1(flags, fModbus);
#ifdef PIN_MODBUS_RSE
	pinMode(PIN_MODBUS_RSE, OUTPUT);
	digitalWriteDirect(PIN_MODBUS_RSE, LOW);
#endif
	MODBUS_PORT_NUM.begin(MODBUS_PORT_SPEED, MODBUS_PORT_CONFIG);
	RS485.begin(1, MODBUS_PORT_NUM);
	RS485.preTransmission(preTransmission);
	RS485.postTransmission(postTransmission);
	RS485.idle(idle);
	return OK;
#else
	return ERR_NO_MODBUS;
#endif
}

// ---- Обмен по шине Modbus ----
// Ниже одиннадцать методов чтения и записи отличались только вызовом ModbusMaster
// и разбором ответа, а обвязку - захват замка, выбор адреса, освобождение замка -
// каждый повторял своей копией. Обвязка собрана здесь в одном месте, методы
// оставляют себе только то, чем действительно различаются.
// Выполняет func для адреса id и регистра cmd, читает в rx или пишет из tx.
// Возвращает локальный знаковый статус вместе с типом и байтом ответа.
ModbusResult devModbus::transact(const char *who, uint8_t id, MB_FUNC func, uint16_t cmd,
                           uint16_t num, uint16_t *rx, const uint16_t *tx)
{
	// Замок создаётся в setup() после HP.initHeatPump, а initFC/initSDM ходят по шине раньше. До старта планировщика
	// SemaphoreTake замок не трогает (задач нет), поэтому пустой замок там допустим; после старта пустой замок - assert ядра.
	if(!get_present() || Modbus_LockRefused(xModbusSemaphore != NULL, xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED))
		return {ERR_NO_MODBUS, ModbusKind::Unavailable, 0};
	if(SemaphoreTake(xModbusSemaphore, (MODBUS_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) // Захват мютекса потока или ОЖИДАНИЕ MODBUS_TIME_WAIT
	{
		journal.jprintf((char*) cErrorMutex, who, MutexModbusBusy);
		return {ERR_485_BUSY, ModbusKind::Busy, 0};
	}
	RS485.set_slave(id);
	if(tx) for(uint16_t i = 0; i < num; i++) RS485.setTransmitBuffer(i, tx[i]);
	ModbusMaster::Result frame;
	switch(func) {
	case MB_READ_INPUT:   frame = RS485.readInputRegisters(cmd, num);    break;
	case MB_READ_HOLDING: frame = RS485.readHoldingRegisters(cmd, num);  break;
	case MB_READ_COILS:   frame = RS485.readCoils(cmd, num);             break;  // Нумерация регистров с НУЛЯ!
	case MB_WRITE_COIL:   frame = RS485.writeSingleCoil(cmd, num);       break;
	case MB_WRITE_SINGLE: frame = RS485.writeSingleRegister(cmd, num);   break;
	case MB_WRITE_MULTI:  frame = RS485.writeMultipleRegisters(cmd, num);break;
	case MB_LINK_TEST:    frame = RS485.LinkTestOmronMX2Only(num);        break;
	default:              frame = {ModbusMaster::ResultKind::Local, 0xFF}; break;
	}
	ModbusResult result = translateErr(frame, id == FC_MODBUS_ADR);
	if(result.kind == ModbusKind::Success && rx) {
		const uint16_t words = func == MB_LINK_TEST ? 1 : num;
		for(uint16_t i = 0; i < words; i++) rx[i] = RS485.getResponseBuffer(i);
	}
	if(func == MB_LINK_TEST && result.kind == ModbusKind::Success && *rx != num)
		result = {ERR_485_INIT, ModbusKind::EchoMismatch, 0};
	if(id == FC_MODBUS_ADR) {
		const uint32_t irq = irqSave();
		link.Observe(result.kind, GetTickCount());
		irqRestore(irq);
	}
	SemaphoreGive(xModbusSemaphore);
	return result;
}

bool devModbus::link_lost(bool running)
{
	const uint32_t irq = irqSave();
	const uint8_t fails = link.failures;
	const uint32_t since = GetTickCount() - link.lastReply;
	irqRestore(irq);
	return Error_LinkLost(fails, since, running, FC_LINK_FAILS, FC_LINK_TIME_RUN, FC_LINK_TIME_IDLE);
}

// ---- ФУНКЦИИ ЧТЕНИЯ ----
// Значение при ошибке методы ниже трогают по-разному: часть обнуляет *ret, часть
// оставляет прежним. Расхождение сохранено как было - вызывающие обязаны
// проверять возвращённый код ошибки, а не содержимое *ret.

// Получить значение 2-x (Modbus function 0x04 Read Input Registers) регистров (4 байта) в виде float возвращает код ошибки данные кладутся в ret
ModbusResult devModbus::readInputRegistersFloat(uint8_t id, uint16_t cmd, float *ret)
{
	uint16_t b[2];
	const auto result = transact(__FUNCTION__, id, MB_READ_INPUT, cmd, 2, b);
	if(result == OK) *ret = fromInt16ToFloat(b[0], b[1]);
	else *ret = 0;
	return result;
}

ModbusResult devModbus::readInputRegisters16(uint8_t id, uint16_t cmd, uint16_t *ret)
{
	uint16_t b;
	const auto result = transact(__FUNCTION__, id, MB_READ_INPUT, cmd, 1, &b);
	if(result == OK) *ret = b;
	return result;
}

// LITTLE-ENDIAN! 0x04
ModbusResult devModbus::readInputRegisters32(uint8_t id, uint16_t cmd, uint32_t *ret)
{
	uint16_t b[2];
	const auto result = transact(__FUNCTION__, id, MB_READ_INPUT, cmd, 2, b);
	if(result == OK) *ret = ((uint32_t)b[1] << 16) | b[0];
	return result;
}

// Получить значение регистра (2 байта) в виде целого  числа возвращает код ошибки данные кладутся в ret
ModbusResult devModbus::readHoldingRegisters16(uint8_t id, uint16_t cmd, uint16_t *ret)
{
	uint16_t b;
	const auto result = transact(__FUNCTION__, id, MB_READ_HOLDING, cmd, 1, &b);
	if(result == OK) *ret = b;
	else *ret = 0;
	return result;
}

// Получить значение 2-x регистров (4 байта) в виде целого  числа возвращает код ошибки данные кладутся в ret
// BIG-ENDIAN, в отличие от readInputRegisters32 - так отвечают приборы на этой функции.
ModbusResult devModbus::readHoldingRegisters32(uint8_t id, uint16_t cmd, uint32_t *ret)
{
	uint16_t b[2];
	const auto result = transact(__FUNCTION__, id, MB_READ_HOLDING, cmd, 2, b);
	if(result == OK) *ret = ((uint32_t)b[0] << 16) | b[1];
	else *ret = 0;
	return result;
}

// Получить значение 2-x регистров (4 байта) в виде float возвращает код ошибки данные кладутся в ret
ModbusResult devModbus::readHoldingRegistersFloat(uint8_t id, uint16_t cmd, float *ret)
{
	uint16_t b[2];
	const auto result = transact(__FUNCTION__, id, MB_READ_HOLDING, cmd, 2, b);
	if(result == OK) *ret = fromInt16ToFloat(b[0], b[1]);
	else *ret = 0;
	return result;
}

// Получить значение N регистров c cmd (2*N байта) МХ2 в виде целого  числа (uint16_t *buf) при ошибке возвращает err
ModbusResult devModbus::readHoldingRegistersNN(uint8_t id,uint16_t cmd, uint16_t num, uint16_t *buf)
{
	return transact(__FUNCTION__, id, MB_READ_HOLDING, cmd, num, buf);
}

// прочитать отдельный бит возвращает ошибку Modbus function 0x01 Read Coils.
ModbusResult  devModbus::readCoil(uint8_t id,uint16_t cmd, boolean *ret)
{
	uint16_t b;
	const auto result = transact(__FUNCTION__, id, MB_READ_COILS, cmd, 1, &b);
	if(result == OK) *ret = b != 0;
	return result;
}


// ---- ФУНКЦИИ ЗАПИСИ ----
// установить битовый вход функция Modbus function 0x05 Write Single Coil.
ModbusResult devModbus::writeSingleCoil(uint8_t id,uint16_t cmd, uint8_t u8State)
{
	return transact(__FUNCTION__, id, MB_WRITE_COIL, cmd, u8State);
}

// Установить значение регистра (2 байта) МХ2 в виде целого  числа возвращает код ошибки данные data
ModbusResult   devModbus::writeHoldingRegisters16(uint8_t id, uint16_t cmd, uint16_t data)
{
	return transact(__FUNCTION__, id, MB_WRITE_SINGLE, cmd, data);
}

// Записать 2 регистра подряд возвращает код ошибки
ModbusResult devModbus::writeHoldingRegisters32(uint8_t id, uint16_t cmd, uint32_t data)
{
	uint16_t tx[2] = { (uint16_t)(data >> 16), (uint16_t)(data & 0xFFFF) };
	return transact(__FUNCTION__, id, MB_WRITE_MULTI, cmd, 2, NULL, tx);
}

// Записать float как 2 регистра числа возвращает код ошибки данные dat
ModbusResult   devModbus::writeHoldingRegistersFloat(uint8_t id, uint16_t cmd, float dat)
{
	union {
		float f;
		uint16_t i[2];
	} float_map = { .f = dat };
	uint16_t tx[2] = { float_map.i[1], float_map.i[0] };
	return transact(__FUNCTION__, id, MB_WRITE_MULTI, cmd, 2, NULL, tx);
}

// Тестирование связи возвращает код ошибки
#ifndef FC_VACON
ModbusResult devModbus::LinkTestOmronMX2()
{
	uint16_t echo;
	return transact(__FUNCTION__, FC_MODBUS_ADR, MB_LINK_TEST, 0, TEST_NUMBER, &echo);
}
#endif

// Возвращает статус контроллера, тип и байт кадра. inverter включает таблицу исключений привода.
ModbusResult devModbus::translateErr(ModbusMaster::Result frame, bool inverter)
{
	using Frame = ModbusMaster::ResultKind;
	int8_t legacy = ERR_MODBUS_UNKNOW;
	switch(frame.kind) {
	case Frame::Success: return {OK, ModbusKind::Success, 0};
	case Frame::Transport:
		switch(frame.code) {
		case 0xE0: legacy = ERR_MODBUS_0xe0; break;
		case 0xE1: legacy = ERR_MODBUS_0xe1; break;
		case 0xE2: legacy = ERR_MODBUS_0xe2; break;
		case 0xE3: legacy = ERR_MODBUS_0xe3; break;
		}
		return {legacy, ModbusKind::Transport, frame.code};
	case Frame::Exception:
		if(inverter) {
#ifdef FC_VACON
			switch(frame.code) {
			case 5: legacy = ERR_MODBUS_VACON_0x05; break;
			case 6: legacy = ERR_MODBUS_VACON_0x06; break;
			case 7: legacy = ERR_MODBUS_VACON_0x07; break;
			case 8: legacy = ERR_MODBUS_VACON_0x08; break;
			}
#else
			switch(frame.code) {
			case 1: legacy = ERR_MODBUS_MX2_0x01; break;
			case 2: legacy = ERR_MODBUS_MX2_0x02; break;
			case 3: legacy = ERR_MODBUS_MX2_0x03; break;
			case 0x21: legacy = ERR_MODBUS_MX2_0x21; break;
			case 0x22: legacy = ERR_MODBUS_MX2_0x22; break;
			case 0x23: legacy = ERR_MODBUS_MX2_0x23; break;
			}
#endif
		}
		if(legacy == ERR_MODBUS_UNKNOW && frame.code >= 1 && frame.code <= 4)
			legacy = ERR_MODBUS_0x01 - (frame.code - 1);
		return {legacy, ModbusKind::Exception, frame.code};
	default: return {legacy, ModbusKind::Local, frame.code};
	}
}

#ifdef USE_ELECTROMETER_SDM
// ---------------------------------------------------------------------------
// Реестр параметров веб-API: электросчётчик SDM (sdm.*). Значения берутся тем же
// get_paramSDM, что и раньше (часть из них читается по Modbus прямо в запросе),
// число из текста приводится к JSON-числу; пусто или «-» - null (нет связи или
// параметр отключён в сборке). Записываются только три порога.
// ---------------------------------------------------------------------------
enum { SD_NAME = 1, SD_NOTE, SD_ERR, SD_MAXP, SD_VOLT, SD_CURRENT, SD_ACPOWER, SD_ACENERGY, SD_MAXV, SD_MINV, SD_LINK, SD_PF, SD_PHASE, SD_FREQ };
static const char *const sdmVar[] = { 0, sdm_NAME, sdm_NOTE, sdm_ERRORS, sdm_MAX_POWER, sdm_VOLTAGE, sdm_CURRENT, sdm_ACPOWER, sdm_ACENERGY,
                                      sdm_MAX_VOLTAGE, sdm_MIN_VOLTAGE, sdm_LINK, sdm_POW_FACTOR, sdm_PHASE, sdm_FREQ };
#define DA(nm, fl, kind, sc, hk, lo, hi) { nm, 0, (float)(lo), (float)(hi), 0, sc, PT_CALL, fl, kind, hk, 0 }
static const ApiParam apiSdmTab[] = {
	DA(sdm_NAME,        PF_RO, MK_TEXT, 1,   SD_NAME,     0, 0),
	DA(sdm_NOTE,        PF_RO, MK_TEXT, 1,   SD_NOTE,     0, 0),
	DA(sdm_ERRORS,      PF_RO, MK_INT,  1,   SD_ERR,      0, 0),
	DA(sdm_MAX_POWER,   0,     MK_INT,  1,   SD_MAXP,     0, 30000),   // Вт, контроль мощности
	DA(sdm_VOLTAGE,     PF_RO, MK_NUM,  100, SD_VOLT,     0, 0),
	DA(sdm_CURRENT,     PF_RO, MK_NUM,  1000, SD_CURRENT, 0, 0),
	DA(sdm_ACPOWER,     PF_RO, MK_NUM,  100, SD_ACPOWER,  0, 0),
	DA(sdm_ACENERGY,    PF_RO, MK_NUM,  1000, SD_ACENERGY, 0, 0),
	DA(sdm_MAX_VOLTAGE, 0,     MK_INT,  1,   SD_MAXV,     0, 400),     // В; null - контроль выключен в сборке
	DA(sdm_MIN_VOLTAGE, 0,     MK_INT,  1,   SD_MINV,     0, 400),
	DA(sdm_LINK,        PF_RO, MK_BOOL, 1,   SD_LINK,     0, 0),
	DA(sdm_POW_FACTOR,  PF_RO, MK_NUM,  100, SD_PF,       0, 0),
	DA(sdm_PHASE,       PF_RO, MK_NUM,  100, SD_PHASE,    0, 0),
	DA(sdm_FREQ,        PF_RO, MK_NUM,  100, SD_FREQ,     0, 0),
};
static int api_get_sdm(uint8_t hook, uint8_t, ApiOut *o)
{
	char t[80]; t[0] = 0;                            // имя и описание - русский текст до 60 байт (noteSDM = 59 + 0)
	if(hook < SD_NAME || hook > SD_FREQ) return AE_FAIL;
	HP.dSDM.get_paramSDM((char*) sdmVar[hook], t);
	switch(hook) {
	case SD_NAME: case SD_NOTE: api_put_str(o, t); break;
	case SD_LINK: api_put(o, !strcmp(t, cYes) ? "true" : "false"); break;
	default: {                                       // число из текста; всё остальное - null
		bool num = t[0] != 0 && strcmp(t, "-") != 0;
		for(const char *c = t; *c && num; c++) if(!((*c >= '0' && *c <= '9') || *c == '.' || (*c == '-' && c == t))) num = false;
		if(num) api_put(o, t); else api_put(o, "null"); }
	}
	return AE_OK;
}
static int api_set_sdm(uint8_t hook, uint8_t, const char *val)
{
	if(hook != SD_MAXP && hook != SD_MAXV && hook != SD_MINV) return AE_FAIL;
	return HP.dSDM.set_paramSDM((char*) sdmVar[hook], (char*) val) ? AE_OK : AE_RANGE;   // границы уже проверило ядро
}
static const ApiOps apiOpsSdm = { 0, api_get_sdm, api_set_sdm, 0, 0, 0, 0, 0 };
extern const ApiGroup ApiGrpSdm;
const ApiGroup ApiGrpSdm = { "sdm", apiSdmTab, sizeof(apiSdmTab) / sizeof(apiSdmTab[0]), &apiOpsSdm };
#endif
