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

// Определяет коды, группы и способы регистрации событий, а также интерфейс доступа к каталогу.

#ifndef Control_ErrorCatalog_h
#define Control_ErrorCatalog_h

#include <stdint.h>

namespace ErrorCatalog {

// Числовое представление кода для сохранения и передачи через интерфейсы.
using ErrCode = int8_t;

// Каждый код описан в строке ErrorCatalogRows.h вместе с источником и способом регистрации.
enum class Code : ErrCode {
	kOK = 0, // Активного события нет.
#define ERROR_DEF(name, code, group, source, registration, description) k##name = code,
#include "ErrorCatalogRows.h"
#undef ERROR_DEF
};

// Число строк каталога, вычисленное из того же списка, что и enum Code.
constexpr uint8_t kEntryCount = 0
#define ERROR_DEF(name, code, group, source, registration, description) + 1
#include "ErrorCatalogRows.h"
#undef ERROR_DEF
;

// Возвращает номер события для числовых интерфейсов и результатов операций.
constexpr ErrCode ErrorNumber(Code code) { return static_cast<ErrCode>(code); }

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
	Alarm,          // Допускает аварийный останов и активную ошибку
	Warning,        // Допускает предупреждение без аварийного останова
	AlarmOrWarning, // Допускает оба пути в зависимости от места проверки
	LogOnly         // Допускает только историческую запись в журнале
};

// Проверяет, допускает ли способ регистрации аварийный путь.
constexpr bool AllowsAlarm(Registration value)
{
	return value == Registration::Alarm || value == Registration::AlarmOrWarning;
}

// Проверяет, допускает ли способ регистрации предупреждение.
constexpr bool AllowsWarning(Registration value)
{
	return value == Registration::Warning || value == Registration::AlarmOrWarning;
}

struct Entry {
	ErrCode code; // Отрицательный номер события
	Group group; // Функциональная группа
	Registration registration; // Разрешённые способы регистрации события
	const char *source; // Короткая метка источника
	const char *description; // Описание причины для оператора
};

// Возвращает запись для отрицательного кода из справочника, иначе nullptr.
const Entry *Find(ErrCode code);
// Возвращает запись по типизированному коду события либо nullptr.
inline const Entry *Find(Code code) { return Find(ErrorNumber(code)); }
// Возвращает описание code либо fallback, если запись отсутствует в каталоге.
inline const char *DescriptionOr(Code code, const char *fallback)
{
	const Entry *entry = Find(code);
	return entry ? entry->description : fallback;
}
// Возвращает запись по индексу index в порядке каталога, иначе nullptr.
const Entry *At(uint8_t index);
// Возвращает число строк справочника.
uint8_t Count();

}
#endif
