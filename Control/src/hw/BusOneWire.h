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

#ifndef devOneWire_h
#define devOneWire_h
 
#include "../../Constant.h"                     // Должен быть первым !!!! кое что берется в стандартных либах для настройки либ, Config.h входит в него
#include <DS2482.h>                       // мост OneWire аппаратная реализация доработана
#include "OneWireStatus.h"
#include <OneWire.h>                      // OneWire библиотека OneWire шина програмная

#define DS2482_CONFIG	(DS2482_CONFIG_APU)	// 0 - не конфигурить

// Типы датчиков температуры
#define tDS18S20 0x10
#define tDS18B20 0x28
#define tDS1822  0x22
// разрешение датчика температур
#define DS18B20_p12BIT 0x7F
#define DS18B20_p11BIT 0x5F
#define DS18B20_p10BIT 0x3F
#define DS18B20_p09BIT 0x1F

#define ERROR_TEMPERATURE	-32768

// Структура для хранения информации при сканировании температурных датчиков на шине OneWire
struct type_scanOneWire
{
  byte num;            // номер по списку
  byte bus;       	   // номер шины: 1-Wire = 0..3, t*_Bus = 4..
  byte address[8];     // адрес
};

#define OW_scanTable_max (TNUMBER)
extern type_scanOneWire *OW_scanTable;   // массив структур для хранения найденных адресов
extern uint8_t OW_scanTableIdx;
extern uint8_t OW_scan_flags;            // 1 - идёт сканирование
int8_t 				 OW_prepare_buffers(void);

class deviceOneWire 								    // Класс шина   OneWire
{
  public:
#ifdef ONEWIRE_DS2482
	deviceOneWire(uint8_t addr, uint8_t _bus): OneWireDrv(addr) { err = OneWireStatus::Ok; bus = _bus; }
#else
	deviceOneWire(uint8_t pin_num): OneWireDrv(pin_num) { err = OneWireStatus::Ok; bus = 0; }
#endif
	OneWireStatus	Init(void);
    int16_t	CalcTemp(uint8_t addr_0, uint8_t *data, uint8_t only_temp_readed);	// Вычислить температуру в сотых градуса по считанному scratchpad
	OneWireStatus	Scan(char *result_str);                    // Заполняет строку результата и возвращает статус сканирования.
	OneWireStatus  PrepareTemp();                              // запуск преобразования всех датчиков на шине возвращает код ошибки
	OneWireReadResult Read(uint8_t *addr, int16_t &val);          // чтение DS18B20 с исходным статусом обмена
	OneWireStatus  SetResolution(uint8_t *addr, uint8_t rs, uint8_t dont_lock_bus = 0);	// установить разрешение конкретного датчика
	OneWireStatus  GetLastErr() { return err; }       			// Получить последнюю ошибку
	OneWireStatus  lock_I2C_bus_reset(uint8_t checkpresence);	// блокирование семафора, проверка наличия ds2482, reset 1-wire.
	void	release_I2C_bus();
  private:
	bool    wr(uint8_t b);                              // записать байт в шину; false - не записался (DS2482: сбой I2C)
	OneWireStatus err;                                         // ошибка шины (работа) при ошибке останов ТН
	uint8_t	bus;										// 0 - первый DS2482, 1 - второй, 2 - третий, 3 - четвертый
#ifdef ONEWIRE_DS2482
	DS2482	OneWireDrv;                                 // мастер OneWire аппаратная
};
#else // ONEWIRE_DS2482
	OneWire	OneWireDrv;                                 // OneWire шина програмная
};
#endif // ONEWIRE_DS2482

// Шина определена в BusOneWire.cpp: тип (аппаратная DS2482 или программная) выбирает
// #ifdef ONEWIRE_DS2482 из Config.h.
extern deviceOneWire OneWireBus;

void Recover_I2C_bus(void);

#endif
