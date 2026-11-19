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

// Хранит каталог ошибок и реализует поиск записей по коду и индексу.

#include "ErrorCatalog.h"
#include <stddef.h>

namespace ErrorCatalog {

// Метаданные ошибок в порядке от -1 к последнему коду.
static constexpr Entry catalog[] = {
#define ERROR_DEF(name, code, group, source, registration, description) \
	{code, Group::group, Registration::registration, source, description},
#include "ErrorCatalogRows.h"
#undef ERROR_DEF
};

// Число записей, доступных по отрицательному коду.
static constexpr size_t count = sizeof(catalog) / sizeof(catalog[0]);
static_assert(count == kEntryCount, "Error catalog count differs from Code list");
static_assert(count <= UINT8_MAX, "Error catalog index does not fit uint8_t");
static_assert(catalog[count - 1].code >= INT8_MIN, "Error catalog code does not fit ErrCode");

// Проверяет уникальность и строгое убывание отрицательных номеров начиная с index.
// Возвращает true, если оставшиеся записи упорядочены.
constexpr bool IsOrdered(size_t index)
{
	return index == count ||
	       (catalog[index].code < (index == 0 ? 0 : catalog[index - 1].code) &&
	        IsOrdered(index + 1));
}

static_assert(IsOrdered(0), "Invalid error catalog order");

// Возвращает запись по отрицательному коду или nullptr для отсутствующего кода.
const Entry *Find(ErrCode code)
{
	if(code >= 0) return nullptr;
	for(const auto &entry : catalog) {
		if(entry.code == code) return &entry;
		if(entry.code < code) break;
	}
	return nullptr;
}

// Возвращает запись по индексу index в порядке каталога, иначе nullptr.
const Entry *At(uint8_t index)
{
	return index < count ? &catalog[index] : nullptr;
}

// Возвращает число строк справочника.
uint8_t Count()
{
	return kEntryCount;
}

}
