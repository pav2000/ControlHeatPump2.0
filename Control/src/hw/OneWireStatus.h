/*
 * Copyright (c) 2016-2026 by Pavel Panfilov <firstlast2007@gmail.com>  telegram @pav20000
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

// Различает причины отказов OneWire и хранит исходный статус чтения scratchpad.
#ifndef Control_OneWireStatus_h
#define Control_OneWireStatus_h

#include <stdint.h>

enum class OneWireStatus : uint8_t {
	Ok,            // Обмен завершён.
	Busy,          // Замок I²C занят.
	BridgeMissing, // Мост DS2482 не отвечает.
	BridgeFailed,  // Сброс или настройка моста не удались.
	NoPresence,    // На сброс шины OneWire нет ответа.
	Crc,           // CRC данных датчика не совпала.
	Partial,       // Обмен прерван после чтения двух байтов температуры.
	Transfer       // Ошибка чтения или записи шины.
};

struct OneWireReadResult {
	OneWireStatus status; // Итог чтения scratchpad.
	uint8_t raw;          // Причина отказа драйвера или 0, если байт причины недоступен.
};

#define OW_RAW_BADDATA 0x40                             // Исходный статус: байты прочитаны, но scratchpad не прошёл проверку

#endif
