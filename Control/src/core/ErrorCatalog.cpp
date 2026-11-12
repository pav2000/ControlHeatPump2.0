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

// Проверяет уникальность и убывание отрицательных номеров начиная с index.
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

// Переход от прежних статусов к кодам событий по карте переноса. Ноль обозначает пропуск.
static constexpr ErrCode legacyEvents[] = {
	0, -1, -2, -6, -7, -3, -3, -9, -10, -10,
	-39, -5, 0, -55, -11, -43, -44, -45, -45, -45,
	-45, -46, -43, -50, -50, -15, -16, 0, -42, -42,
	0, -41, -40, -13, -39, -53, 0, -53, -11, -23,
	-34, -31, -31, -31, -31, -32, -32, -32, -32, -31,
	-31, 0, -31, -31, -31, -31, -35, -24, -25, -18,
	-54, -47, -48, -49, -49, -5, -5, -5, -12, 0,
	-52, -52, -52, 0, -8, -36, -38, -33, -26, -8,
	-14, -39, -17, -27, -4, -51, -4, -4, -21, -28,
	-53, -30, -22, -56,
};

// Возвращает номер события для прежнего статуса или 0, если соответствия нет.
ErrCode FromLegacy(ErrCode legacy)
{
	const int index = -static_cast<int>(legacy);
	if(index < 0 || static_cast<size_t>(index) >= sizeof(legacyEvents)) return 0;
	return legacyEvents[index];
}

// Возвращает число строк справочника.
uint8_t Count()
{
	return count;
}

}
