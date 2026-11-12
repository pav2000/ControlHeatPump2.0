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

#ifndef Control_ErrorCatalog_h
#define Control_ErrorCatalog_h

#include <stdint.h>

namespace ErrorCatalog {

using ErrCode = int8_t;

// Каждый код описан в строке ErrorCatalogRows.h вместе с источником и способом регистрации.
enum Code : ErrCode {
#define ERROR_DEF(name, code, group, source, registration, description) k##name = code,
#include "ErrorCatalogRows.h"
#undef ERROR_DEF
};

enum class Group : uint8_t {
	SENSORS,        // Датчики и защиты
	CONTROL,        // ЭРВ, компрессор и управление ТН
	INVERTER,       // Инвертор, подтверждение пуска и останова
	MODBUS_POWER,   // Шина Modbus и питание
	SETTINGS,      // Настройки, конфигурация и профили
	STORAGE_SYSTEM,// Носители и системные ошибки
	INFRASTRUCTURE,// Сеть и шины контроллера
	STAGE_TWO      // Защиты и предупреждения второго этапа
};

enum class Registration : uint8_t {
	Alarm,          // Обычно вызывает защитное действие
	Warning,        // Сообщает о проблеме без аварийного останова
	RequiredAtBoot, // Авария при отказе обязательных данных на старте
	ByDevice,       // Уровень зависит от роли устройства
	ByOperation,    // Уровень зависит от обязательности операции
	LogOnly         // Запись об уже произошедшем событии, без установки активной аварии
};

struct Entry {
	ErrCode code; // Отрицательный номер события
	Group group; // Функциональная группа
	Registration registration; // Обычный способ регистрации
	const char *source; // Короткая метка источника
	const char *description; // Описание причины для оператора
};

// Возвращает запись для отрицательного кода из справочника, иначе nullptr.
const Entry *Find(ErrCode code);
// Возвращает запись по индексу index в порядке каталога, иначе nullptr.
const Entry *At(uint8_t index);
// Возвращает номер события для прежнего статуса или 0, если соответствия нет.
ErrCode FromLegacy(ErrCode legacy);
// Возвращает число строк справочника.
uint8_t Count();

}
#endif
