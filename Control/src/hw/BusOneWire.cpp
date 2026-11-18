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
 * Шина 1-Wire: отдельная единица трансляции.
 *
 * Здесь семь точек #ifdef ONEWIRE_DS2482 - выбор между аппаратным мастером
 * DS2482 и программной шиной. Условие приходит из Config.h, который виден
 * через Constant.h из BusOneWire.h. Проверено сверкой размеров методов:
 * молчаливый уход в не ту ветку означал бы неработающие датчики температуры.
 */
#include "BusOneWire.h"
#include "Hardware.h"     // addressToHex, Recover_I2C_bus
#include "../core/HeatPump.h"     // HP, set_Error
#include "../log/Journal.h"      // journal
#include "../core/Rtos.h"         // _delay, замки шин, Watchdog_AliveHere
#include "../util/Util.h"         // _itoa, _dtoa
#include "GpioDirect.h"   // digitalReadDirect / digitalWriteDirect


// Объявлены в BusOneWire.h - создаются здесь, в единственном экземпляре.
type_scanOneWire *OW_scanTable = NULL;   // массив найденных адресов
uint8_t           OW_scanTableIdx;
uint8_t           OW_scan_flags = 0;     // 1 - идёт сканирование
#ifdef ONEWIRE_DS2482
deviceOneWire OneWireBus(I2C_ADR_DS2482, 0);   // аппаратная шина DS2482
#else
deviceOneWire OneWireBus(PIN_ONE_WIRE_BUS);    // программная шина
#endif
     

const char OW_Error_memory_low[] = { "Scan: Memory low!\n" };

// Return 1 - success
int8_t OW_prepare_buffers(void)
{
	OW_scanTableIdx = 0;
	if(OW_scanTable == NULL) {
		OW_scanTable = (type_scanOneWire *)malloc(sizeof(type_scanOneWire) * OW_scanTable_max);
		if(OW_scanTable == NULL) {
			journal.jprintf(OW_Error_memory_low);
			return 0;
		}
	}
	return 1;
}

// Возврат Temp * 100, Если ошибка возвращает ERROR_TEMPERATURE
int16_t deviceOneWire::CalcTemp(uint8_t addr_0, uint8_t *data, uint8_t only_temp_readed)
{
	// Разбор данных
	int16_t raw = (data[1] << 8) | data[0];
	if(addr_0 == tDS18S20) {
		raw = raw << 3; // 9 bit resolution default
		if(only_temp_readed) goto xReturnTemp;
		if(data[7] == 0x10) { raw = (raw & 0xFFF0) + 12 - data[6]; }  // "count remain" gives full 12 bit resolution
	} else {
		if(only_temp_readed) goto xReturnTemp;
		byte cfg = data[4];
		// at lower res, the low bits are undefined, so let's zero them
		if(cfg == DS18B20_p09BIT) raw = raw & ~7;      // 9 bit resolution, 93.75 ms
		else if(cfg == DS18B20_p10BIT) raw = raw & ~3; // 10 bit res, 187.5 ms
		else if(cfg == DS18B20_p11BIT) raw = raw & ~1; // 11 bit res, 375 ms
		else if(cfg != DS18B20_p12BIT) return ERROR_TEMPERATURE; // Прочитаны "плохие данные"
		// default is 12 bit resolution, 750 ms conversion time
	}
xReturnTemp:
	return raw * 100 / 16;
}

OneWireStatus	deviceOneWire::Init(void)
{
	err = OneWireStatus::Ok;
	if(lock_I2C_bus_reset(1) == OneWireStatus::Ok) {
#ifdef ONEWIRE_DS2482
		if(!OneWireDrv.reset_bridge()) err = OneWireStatus::BridgeFailed;
#if DS2482_CONFIG != 0
		if(err == OneWireStatus::Ok && !OneWireDrv.configure(DS2482_CONFIG)) err = OneWireStatus::BridgeFailed;
#endif
		release_I2C_bus();
#endif
	}
	return err;
}

// SEM-HOLD: xI2CSemaphore

// Захватывает шину и сбрасывает OneWire. При checkpresence проверяет наличие DS2482.
// Возвращает статус. После успеха вызывающий освобождает замок через release_I2C_bus().
OneWireStatus  deviceOneWire::lock_I2C_bus_reset(uint8_t checkpresence)
{
	uint8_t presence = 0;
	err = OneWireStatus::Ok;
#ifdef ONEWIRE_DS2482
	if(SemaphoreTake(xI2CSemaphore,(I2C_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE) {
		journal.printf((char*)cErrorMutex,__FUNCTION__,MutexI2CBusy);
		err = OneWireStatus::Busy;
		return err;
	}
	if(checkpresence) {
		if(!OneWireDrv.check_presence()){    // Проверяем наличие на i2с шине  ds2482
			err = OneWireStatus::BridgeMissing;
			release_I2C_bus();
		}
		return err;
	}
#else
	if(checkpresence) return OneWireStatus::Ok;
#endif
	for(uint8_t i = 0; i < RES_ONEWIRE_ERR; i++)   // Три попытки сбросить датчики, если не проходит то это ошибка
	{
		if((presence = OneWireDrv.reset())) break;                     // Сброс прошел выходим
#ifdef ONEWIRE_DS2482
		if(!OneWireDrv.reset_bridge()) {
			Recover_I2C_bus();
			if(!OneWireDrv.reset_bridge()) {
				err = OneWireStatus::BridgeMissing;
				break;
			}
		}
	#if DS2482_CONFIG != 0
		if(!OneWireDrv.configure(DS2482_CONFIG)) break;
	#endif
#else
        pinMode(PIN_ONE_WIRE_BUS, INPUT_PULLUP);
        _delay(1);
        digitalReadDirect(PIN_ONE_WIRE_BUS);
#endif
		_delay(50);                               // Сброс не прошел, сделаем паузу
	}
	if(!presence){
		if(err == OneWireStatus::Ok) err = OneWireStatus::NoPresence;
		release_I2C_bus();
	}
	return err;
}

inline void	deviceOneWire::release_I2C_bus()
{
	// SEM-BORROW: xI2CSemaphore - это и есть отдача замка, взятого lock_I2C_bus_reset()
#ifdef ONEWIRE_DS2482
		SemaphoreGive(xI2CSemaphore);
#endif
}

// Заполняет result_str списком датчиков и возвращает статус сканирования.
// Формат: <Номер п.п.>:<тип датчика>:<температура>:<серийный номер>:<шина>;
OneWireStatus  deviceOneWire::Scan(char *result_str)
{
#ifdef DEMO // В демо режиме выводим строку констант
	// Строка которая выдается в демо режиме при сканировании onewire
	strcat(result_str,"1:DS18B20:20.1:28FFC414B51605B3:1;2:DS18S20:-3.56:28FFDE93B3160321:1;3:DS1822:34.6:28FF2B18B5160514:2;");
	OW_scanTable[0].num=1;OW_scanTable[1].num=2;OW_scanTable[2].num=3;
	for (int i=0;i<8;i++)
	{
		OW_scanTable[0].address[i]=(i+1)+(i+1)*16;
		OW_scanTable[1].address[i]=(i+1)+(i+1)*16;
		OW_scanTable[2].address[i]=(i+1)+(i+1)*16;
	}
	OW_scanTable[1].address[0]=0x0;
	OW_scanTable[2].address[0]=0xaa;
	return OneWireStatus::Ok;
#else // сканирование датчика
	byte i;
	byte data[12];
	byte addr[8];
	bool i2c_lost = false;    // замок I2C отпущен на время вывода в журнал и не вернулся: отдавать его в конце нельзя
	Watchdog_AliveHere();

	if(lock_I2C_bus_reset(0) != OneWireStatus::Ok) { // reset 1-wire
		if(err == OneWireStatus::NoPresence) journal.jprintf("1-Wire bus %d is empty. . .\n", bus + 1);
		return err;
	}
	OneWireDrv.skip(); // Все датчики
	OneWireDrv.write(0x44); // начинаем преобразование, используя OneWireDrv.write(0x44,1) с "паразитным" питанием
	_delay(cDELAY_DS1820);  // ждем подготовку температуры
	if(
		OneWireDrv.reset())
	{
		OneWireDrv.reset_search();
		while(OneWireDrv.search(addr)) // до тех пор пока есть свободные адреса
		{
			Watchdog_AliveHere();
			err = OneWireStatus::Ok;
			//  Датчик найден!
			// 1. Номер по порядку
			OW_scanTable[OW_scanTableIdx].num = OW_scanTableIdx + 1;
			_itoa(OW_scanTableIdx + 1, result_str); strcat(result_str,":");
			if(OneWireDrv.crc8(addr, 7) != addr[7]) {
				strcat(result_str,"Error CRC:::;");
				continue;
			}
			// 2. первый байт определяет чип выводим этот тип
			strcat(result_str,"DS18");
			switch (addr[0])
			{
				case tDS18S20: strcat(result_str,"S20"); break;
				case tDS18B20: strcat(result_str,"B20"); break;
				case tDS1822:  strcat(result_str,"22");  break;
				default:       strcat(result_str,"?"); _itoa(addr[0], result_str); break;
			}
			strcat(result_str, ":");
			// 5. Получение данных
			if(!OneWireDrv.reset()) err = OneWireStatus::NoPresence;
			if(err == OneWireStatus::Ok) {
				OneWireDrv.select(addr);
				OneWireDrv.write(0xBE);
				for(i=0; i<9; i++) data[i] = OneWireDrv.read(); // Читаем данные, нам необходимо 9 байт
				// конвертируем данные в фактическую температуру
				int16_t t = CalcTemp(addr[0], data, 0);
				if(OneWireDrv.crc8(data,8) != data[8] || t == ERROR_TEMPERATURE)  // Дополнительная проверка для DS18B20
					strcat(result_str, "CRC");
				else _dtoa(result_str, t, 2);
				strcat(result_str, "(");
				_itoa(((data[4] & 0x60) >> 5) + 9, result_str);
				strcat(result_str, "b)");
				if(data[4] != DS18B20_p12BIT) if(SetResolution(addr, DS18B20_p12BIT, 1) != OneWireStatus::Ok) strcat(result_str, "!");
			}
			strcat(result_str, ":");
			// 8. Адрес добавить
			memcpy(OW_scanTable[OW_scanTableIdx].address, addr, 8);
			strcat(result_str, addressToHex(addr));
			strcat(result_str, ":");
#ifdef ONEWIRE_DS2482
			OW_scanTable[OW_scanTableIdx].bus = bus;
			_itoa(bus+1, result_str);
#else
			OW_scanTable[OW_scanTableIdx].bus = 0;
			strcat(result_str, "1");
#endif
			strcat(result_str, ";");
			release_I2C_bus();
			journal.jprintf("%s Pad:%s", result_str, addressToHex(data));   // подробнее о поддельных датчиках https://github.com/cpetrich/counterfeit_DS18B20
			if (addr[5]!=0x00 || addr[6]!=0x00) journal.jprintf(" not Maxim's ROM address!");
			if (data[5]!=0xff || data[6]!=0x0c || data[7]!=0x10 ) journal.jprintf(" bad scratchpad: fake sensor!");
			journal.jprintf("\n");
#ifdef ONEWIRE_DS2482
			if(SemaphoreTake(xI2CSemaphore, (I2C_TIME_WAIT/portTICK_PERIOD_MS)) == pdFALSE) {
				journal.printf((char*)cErrorMutex,__FUNCTION__,MutexI2CBusy);
				err = OneWireStatus::Busy;
				i2c_lost = true;
				break;
			}
#endif
			result_str += strlen(result_str);
			if(++OW_scanTableIdx >= OW_scanTable_max) break;   // Следующий датчик
		} // while по датчикам
	} else {
		journal.jprintf("Error reset 1-Wire bus %d\n", bus + 1);
	}
	if(!i2c_lost) release_I2C_bus();
	return i2c_lost ? OneWireStatus::Busy : OneWireStatus::Ok;
#endif  // DEMO
}

// Запись байта в шину. У DS2482 сбой I2C даёт false, у программной шины записи
// отказать нечему.
bool deviceOneWire::wr(uint8_t b)
{
#ifdef ONEWIRE_DS2482
	return OneWireDrv.write(b) != 0;
#else
	OneWireDrv.write(b);
	return true;
#endif
}

// Читает scratchpad датчика addr после PrepareTemp. При наличии двух байтов
// записывает в val температуру в сотых градуса. Возвращает статус и байт причины.
OneWireReadResult deviceOneWire::Read(byte *addr, int16_t &val)
{
	const OneWireStatus lock = lock_I2C_bus_reset(0);
	if(lock != OneWireStatus::Ok) return {lock, 0};

	OneWireDrv.select(addr);
	if(!wr(0xBE)) { // READ SCRATCHPAD
		release_I2C_bus();
		return {OneWireStatus::Transfer, 0};
	}
	byte data[9];
	OneWireReadResult result{OneWireStatus::Ok, 0};
	for(uint8_t i = 0; i < sizeof(data); i++) {
		const int16_t r = OneWireDrv.read();
		if(r < 0) {
			result = {i >= 2 ? OneWireStatus::Partial : OneWireStatus::Transfer, static_cast<uint8_t>(-r)};
			break;
		}
		data[i] = r;
	}
	release_I2C_bus();
	if(result.status == OneWireStatus::Transfer) return result;
	if(result.status == OneWireStatus::Ok && OneWireDrv.crc8(data, 8) != data[8]) result.status = OneWireStatus::Crc;
	val = CalcTemp(addr[0], data, result.status != OneWireStatus::Ok);
	if(val == ERROR_TEMPERATURE) return {OneWireStatus::Transfer, OW_RAW_BADDATA};
	return result;
}

// установить разрешение, блокировка шины, возврат OneWireStatus::Ok или err
OneWireStatus deviceOneWire::SetResolution(uint8_t *addr, uint8_t rs, uint8_t dont_lock_bus)
{
	err = OneWireStatus::Ok;
	if(addr[0] == tDS18S20) return err; // not supported
	if(!dont_lock_bus && lock_I2C_bus_reset(0) != OneWireStatus::Ok) return err;
	if(!OneWireDrv.reset()) {
		if(!dont_lock_bus) release_I2C_bus();
		return err = OneWireStatus::NoPresence;
	}
    OneWireDrv.select(addr);
    if(!(wr(0x4E)    // WRITE SCRATCHPAD
      && wr(0x00)    // верх и низ для аварийных температур не используется
      && wr(0x00)
      && wr(rs))) {  // это разрядность конвертации 0x7f - 12бит 0x5f- 11бит 0x3f-10бит 0x1f-9 бит
        err = OneWireStatus::Transfer;
    	if(!dont_lock_bus) release_I2C_bus();
    	return err;
    }
    if(rs == DS18B20_p12BIT) {
        if(!OneWireDrv.reset()){
            err = OneWireStatus::NoPresence;
        	if(!dont_lock_bus) release_I2C_bus();
        	return err;
        }
        OneWireDrv.select(addr);
    	if(!wr(0x48)) {  // Записать в чип разрешение на всякий случай
            err = OneWireStatus::Transfer;
    		if(!dont_lock_bus) release_I2C_bus();
    		return err;
    	}
    }
    if(!dont_lock_bus) release_I2C_bus();
    return err;
}

// запуск преобразования всех датчиков на шине возвращает код ошибки
OneWireStatus  deviceOneWire::PrepareTemp()
{
	const OneWireStatus err = lock_I2C_bus_reset(0);
	if(err != OneWireStatus::Ok) return err;
	// начать преобразование температурных ВСЕХ датчиков две команды
	OneWireDrv.skip();    		// skip ROM command
	bool ok = wr(0x44);	   		// convert temp
	release_I2C_bus();
	return ok ? OneWireStatus::Ok : OneWireStatus::Transfer;   // не записалась команда - датчики остались бы со старым значением
}

// Борьба с зависшими устройствами на шине  I2C (в первую очередь часы) неудачный сброс
void Recover_I2C_bus(void)
{
	// https://forum.arduino.cc/index.php?topic=288573.0
	pinMode(PIN_WIRE_SCL, OUTPUT);
	for(uint8_t i = 0; i < 8; i++) {
		digitalWriteDirect(PIN_WIRE_SCL, HIGH); delayMicroseconds(3);
		digitalWriteDirect(PIN_WIRE_SCL, LOW);  delayMicroseconds(3);
	}
	PIO_Configure(
			g_APinDescription[PIN_WIRE_SCL].pPort,
			g_APinDescription[PIN_WIRE_SCL].ulPinType,
			g_APinDescription[PIN_WIRE_SCL].ulPin,
			g_APinDescription[PIN_WIRE_SCL].ulPinConfiguration);
	delayMicroseconds(10);
	Wire.begin();
	delayMicroseconds(10);
	Wire.Stop();
}
