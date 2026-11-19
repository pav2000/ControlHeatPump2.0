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
	linkWarningActive = false;
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

#if defined(USE_ELECTROMETER_SDM) && defined(DEMO)
	SETBIT1(flags, fSDM);
	note = (char*)noteSDM;
	uplinkSDM();
#elif defined(USE_ELECTROMETER_SDM)
	if(!Modbus.get_present())                  // modbus отсутствует
	{
		updateLinkEvent({ModbusKind::Unavailable, 0});
		SETBIT0(flags, fSDM);                           // счетчик не представлен
		SETBIT0(flags, fSDMLink);
		err = ErrorNumber(ErrorCatalog::Code::kMODBUS_UNAVAILABLE);
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

// Обновляет предупреждение о связи по result и признаку неверной скорости wrongSpeed.
// Регистрирует начало отказа или восстановление один раз, значения не возвращает.
void devSDM::updateLinkEvent(ModbusResult result, bool wrongSpeed)
{
	const bool failed = wrongSpeed || !result.Succeeded();
	const uint32_t irq = irqSave();
	const bool changed = linkWarningActive != failed;
	linkWarningActive = failed;
	irqRestore(irq);
	if(!changed) return;

	if(!failed) {
		HP.log_Event("MODBUS", name, "связь со счётчиком восстановлена");
		return;
	}
	const auto code = wrongSpeed ? ErrorCatalog::Code::kCONFIG : ModbusAlarm(result.kind);
	const char *text = wrongSpeed ? "скорость счётчика не соответствует настройке" : nullptr;
	HP.set_Warning(code, name, (static_cast<uint16_t>(result.kind) << 8) | result.raw,
	               text, true, false, ModbusDetail(result));
}

// Читает регистр скорости счётчика и обновляет состояние связи.
// Возвращает true, если счётчик ответил и скорость соответствует настройке.
boolean devSDM::uplinkSDM()
{
#ifdef DEMO
	if(!GETBIT(flags, fSDM)) return false;
	SETBIT1(flags, fSDMLink);
	Voltage = 230.0f; // Номинальное напряжение сети на стенде.
	err = OK;
	return true;
#endif
	ModbusResult exchange{};
	bool wrongSpeed = false;
	for(int8_t i = 0; i < SDM_NUM_READ; ++i) {
		float speed;
		exchange = Modbus.readHoldingRegistersFloat(SDM_MODBUS_ADR, SDM_BAUD_RATE, &speed);
		wrongSpeed = exchange.Succeeded() && speed != SDM_SPEED;
		if(exchange.Succeeded() && !wrongSpeed) {
			SETBIT1(flags, fSDMLink);
			err = OK;
			updateLinkEvent(exchange);
			return true;
		}
		if(DBG_ON(DBG_SDM))
			journal.jprintf_time("%s: проверка скорости, тип %u, ответ 0x%02X, несовпадение %u, попытка %u/%u\n",
			                     name, (unsigned)exchange.kind, (unsigned)exchange.raw, (unsigned)wrongSpeed,
			                     i + 1, SDM_NUM_READ);
		_delay(SDM_DELAY_REPEAD);
		Watchdog_AliveHere();
	}
#ifdef SDM_BLOCK
	SETBIT0(flags, fSDMLink);
#else
	SETBIT1(flags, fSDMLink);
#endif
	err = wrongSpeed ? ErrorNumber(ErrorCatalog::Code::kCONFIG) : exchange.Number();
	updateLinkEvent(exchange, wrongSpeed);
	return false;
}

// Программирует адрес и скорость счётчика. Возвращает true, если настройки уже верны
// или запись прошла. После записи счётчик может потребовать перезапуска.
boolean devSDM::progConnect()
{
#ifdef DEMO
	return false; // Программирование физического счётчика на стенде недоступно.
#endif
	float speed;
	if(Modbus.readHoldingRegistersFloat(SDM_MODBUS_ADR, SDM_BAUD_RATE, &speed).Succeeded() && speed == SDM_SPEED) {
		SETBIT1(flags, fSDMLink);
		err = OK;
		updateLinkEvent({});
		return true;
	}

	MODBUS_PORT_NUM.begin(DEFAULT_SDM_SPEED, MODBUS_PORT_CONFIG);
	const auto address = Modbus.writeHoldingRegistersFloat(DEFAULT_SDM_MODBUS_ADR, SDM_ADR_MODBUS, SDM_MODBUS_ADR);
	if(!address.Succeeded() && DBG_ON(DBG_SDM))
		journal.jprintf_time("%s: установка адреса, тип %u, ответ 0x%02X\n",
		                     name, (unsigned)address.kind, (unsigned)address.raw);
	const auto baud = Modbus.writeHoldingRegistersFloat(SDM_MODBUS_ADR, SDM_BAUD_RATE, SDM_SPEED);
	MODBUS_PORT_NUM.begin(MODBUS_PORT_SPEED, MODBUS_PORT_CONFIG);

	err = baud.Number();
	if(!baud.Succeeded()) {
		HP.log_Event(ModbusAlarm(baud.kind), name, ModbusDetail(baud), "не удалось установить скорость счётчика");
		return false;
	}
	uplinkSDM();
	return true;
}

// Прочитать инфо с счетчика, group: 0 - основная (расчет мощности), 2 - через SDM_READ_PERIOD
int8_t devSDM::get_readState(uint8_t group)
{
#ifdef DEMO
	return err; // Показания DEMO установлены при инициализации.
#endif
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
			if(exchange.Succeeded()) AcPower = tmp; else goto xErr;
		}
		if(group == 2) {
#if defined(SDM_MAX_VOLTAGE) || defined(SDM_MIN_VOLTAGE) || (SDM_READ_PERIOD > 0)
			exchange = Modbus.readInputRegistersFloat(SDM_MODBUS_ADR, SDM_VOLTAGE, &tmp);   // Напряжение
			if(exchange.Succeeded()) { Voltage=tmp; group = 1; } else goto xErr;
#endif
		}
		if(exchange.Succeeded()) break;
xErr:
#ifdef SPOWER
		HP.sInput[SPOWER].Read(true);
        if(HP.sInput[SPOWER].is_alarm()) return err;
#endif
		numErr++;                  // число ошибок чтение по Modbus
		if(DBG_ON(DBG_SDM)) {
			journal.jprintf_time("%s: чтение группы %u, тип %u, ответ 0x%02X, попытка %u/%u\n",
			                     name, group, (unsigned)exchange.kind, (unsigned)exchange.raw, i + 1, SDM_NUM_READ);
		}
		_delay(SDM_DELAY_REPEAD);  // Чтение не удачно, делаем паузу
	}
	if(exchange.Succeeded())
	{
		if(group == 0 || group == 1) updateLinkEvent(exchange);
#ifdef SDM_MAX_VOLTAGE
		if((settingSDM.maxVoltage>1)&&(settingSDM.maxVoltage< Voltage)) {err=ErrorNumber(ErrorCatalog::Code::kPOWER_VOLTAGE_HIGH);HP.raise_Error(ErrorCatalog::Code::kPOWER_VOLTAGE_HIGH,name);return err; }       // Контроль входного напряжения
#endif
		if ((settingSDM.maxPower>1)&&(settingSDM.maxPower< AcPower))     {err=ErrorNumber(ErrorCatalog::Code::kPOWER_HIGH);HP.raise_Error(ErrorCatalog::Code::kPOWER_HIGH,name);return err; }         // Контроль мощности потребления
#ifdef SDM_MIN_VOLTAGE
		if(group == 1) {
			const bool low = settingSDM.minVoltage > 1 && Voltage < settingSDM.minVoltage;
			if(low && !lowVoltage) HP.set_Warning(ErrorCatalog::Code::kVOLTAGE_LOW, name, 0, nullptr, true);
			lowVoltage = low;
		}
#endif
		return err = exchange.Number();                       // все прочиталось, выходим
	}
#ifdef SDM_BLOCK                     // если стоит флаг блокировки связи
	SETBIT0(flags,fSDMLink);             // связь со счетчиком потеряна
#endif
	updateLinkEvent(exchange);
	return err = exchange.Number();
}

// Читает регистр address для веб-интерфейса. Возвращает показание, 0 при ошибке
// или заданное значение demoValue в профиле DEMO.
float devSDM::readDisplayRegister(uint16_t address, float demoValue)
{
#ifdef DEMO
	(void)address;
	return demoValue;
#else
	(void)demoValue;
	float value = 0;
	Modbus.readInputRegistersFloat(SDM_MODBUS_ADR, address, &value);
	return value;
#endif
}

// Получить параметр счетчика в виде строки
char* devSDM::get_paramSDM(char *var, char *ret)           
{

	if(strcmp(var,sdm_NAME)==0){         return strcat(ret,(char*)name);                                         }else      // Имя счетчика
	if(strcmp(var,sdm_NOTE)==0){         return strcat(ret,(char*)note);                                         }else      // Описание счетчика
	if(strcmp(var,sdm_ERRORS)==0){    	return _itoa(numErr,ret);				                                }else      // Ошибок modbus
	if(strcmp(var,sdm_MAX_POWER)==0){    return _itoa(settingSDM.maxPower,ret);                                  }else      // максимальная мощность контроля мощности
	if(strcmp(var,sdm_VOLTAGE)==0){ // Напряжение
#if defined(SDM_MAX_VOLTAGE) || defined(SDM_MIN_VOLTAGE) || (SDM_READ_PERIOD > 0)
		_ftoa(ret, Voltage, 2);
#else
		_ftoa(ret, readDisplayRegister(SDM_VOLTAGE, Voltage), 2);
#endif
		return ret;
	}else
	if(strcmp(var,sdm_CURRENT)==0){ // Ток
		_ftoa(ret, readDisplayRegister(SDM_CURRENT), 3);
		return ret;
	}else
	if(strcmp(var,sdm_ACPOWER)==0){// Активная мощность
		_ftoa(ret,(float)AcPower, 2);
		return ret;
	}else
	if(strcmp(var,sdm_ACENERGY)==0){ // Суммарная активная энергия
		_ftoa(ret, readDisplayRegister(SDM_AC_ENERGY), 3);
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
			   _ftoa(ret, readDisplayRegister(SDM_POW_FACTOR), 2);
		   } else if(strcmp(var,sdm_PHASE)==0){
			   _ftoa(ret, readDisplayRegister(SDM_PHASE), 2);
		   } else if(strcmp(var,sdm_FREQ)==0){
			   _ftoa(ret, readDisplayRegister(SDM_FREQUENCY, 50), 2);
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
	return ErrorNumber(ErrorCatalog::Code::kMODBUS_UNAVAILABLE);
#endif
}

// ---- Обмен по шине Modbus ----

// Выполняет func для ведомого id с адреса cmd, используя буферы rx и tx. who — имя для диагностики.
// num — размер обмена, одиночное записываемое значение либо контрольное слово, в зависимости от func.
// Возвращает тип результата и исходный байт причины.
ModbusResult devModbus::transact(const char *who, uint8_t id, MB_FUNC func, uint16_t cmd,
                           uint16_t num, uint16_t *rx, const uint16_t *tx)
{
	// Замок создаётся в setup() после HP.initHeatPump, а initFC/initSDM ходят по шине раньше. До старта планировщика
	// SemaphoreTake замок не трогает (задач нет), поэтому пустой замок там допустим; после старта пустой замок - assert ядра.
	if(!get_present() || Modbus_LockRefused(xModbusSemaphore != NULL, xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED))
		return {ModbusKind::Unavailable, 0};
	if(SemaphoreTake(xModbusSemaphore, (MODBUS_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) // Захват мютекса потока или ОЖИДАНИЕ MODBUS_TIME_WAIT
	{
		journal.jprintf((char*) cErrorMutex, who, MutexModbusBusy);
		return {ModbusKind::Busy, 0};
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
	ModbusResult result = translateErr(frame);
	if(result.kind == ModbusKind::Success && rx) {
		const uint16_t words = func == MB_LINK_TEST ? 1 : num;
		for(uint16_t i = 0; i < words; i++) rx[i] = RS485.getResponseBuffer(i);
	}
	if(func == MB_LINK_TEST && result.kind == ModbusKind::Success && *rx != num)
		result = {ModbusKind::EchoMismatch, 0};
	if(id == FC_MODBUS_ADR) {
		const uint32_t irq = irqSave();
		link.Observe(result.kind, GetTickCount());
		irqRestore(irq);
	}
	SemaphoreGive(xModbusSemaphore);
	return result;
}

// Возвращает признак потери связи по счётчику сбоев и паузе ответа. running выбирает тайм-аут.
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
	if(result.Succeeded()) *ret = fromInt16ToFloat(b[0], b[1]);
	else *ret = 0;
	return result;
}

// Читает регистр cmd устройства id. При успехе пишет в ret, при отказе сохраняет ret. Возвращает исход обмена.
ModbusResult devModbus::readInputRegisters16(uint8_t id, uint16_t cmd, uint16_t *ret)
{
	uint16_t b;
	const auto result = transact(__FUNCTION__, id, MB_READ_INPUT, cmd, 1, &b);
	if(result.Succeeded()) *ret = b;
	return result;
}

// LITTLE-ENDIAN! 0x04
ModbusResult devModbus::readInputRegisters32(uint8_t id, uint16_t cmd, uint32_t *ret)
{
	uint16_t b[2];
	const auto result = transact(__FUNCTION__, id, MB_READ_INPUT, cmd, 2, b);
	if(result.Succeeded()) *ret = ((uint32_t)b[1] << 16) | b[0];
	return result;
}

// Получить значение регистра (2 байта) в виде целого  числа возвращает код ошибки данные кладутся в ret
ModbusResult devModbus::readHoldingRegisters16(uint8_t id, uint16_t cmd, uint16_t *ret)
{
	uint16_t b;
	const auto result = transact(__FUNCTION__, id, MB_READ_HOLDING, cmd, 1, &b);
	if(result.Succeeded()) *ret = b;
	else *ret = 0;
	return result;
}

// Получить значение 2-x регистров (4 байта) в виде целого  числа возвращает код ошибки данные кладутся в ret
// BIG-ENDIAN, в отличие от readInputRegisters32 - так отвечают приборы на этой функции.
ModbusResult devModbus::readHoldingRegisters32(uint8_t id, uint16_t cmd, uint32_t *ret)
{
	uint16_t b[2];
	const auto result = transact(__FUNCTION__, id, MB_READ_HOLDING, cmd, 2, b);
	if(result.Succeeded()) *ret = ((uint32_t)b[0] << 16) | b[1];
	else *ret = 0;
	return result;
}

// Получить значение 2-x регистров (4 байта) в виде float возвращает код ошибки данные кладутся в ret
ModbusResult devModbus::readHoldingRegistersFloat(uint8_t id, uint16_t cmd, float *ret)
{
	uint16_t b[2];
	const auto result = transact(__FUNCTION__, id, MB_READ_HOLDING, cmd, 2, b);
	if(result.Succeeded()) *ret = fromInt16ToFloat(b[0], b[1]);
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
	if(result.Succeeded()) *ret = b != 0;
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

// Проверяет ответ Omron на контрольный запрос. Возвращает исход обмена.
#ifndef FC_VACON
ModbusResult devModbus::LinkTestOmronMX2()
{
	uint16_t echo;
	return transact(__FUNCTION__, FC_MODBUS_ADR, MB_LINK_TEST, 0, TEST_NUMBER, &echo);
}
#endif

// Возвращает классификацию и исходный байт frame из ModbusMaster.
ModbusResult devModbus::translateErr(ModbusMaster::Result frame)
{
	using Frame = ModbusMaster::ResultKind;
	switch(frame.kind) {
	case Frame::Success: return {};
	case Frame::Transport: return {ModbusKind::Transport, frame.code};
	case Frame::Exception: return {ModbusKind::Exception, frame.code};
	default: return {ModbusKind::Local, frame.code};
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
