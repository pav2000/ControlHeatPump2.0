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

// Сериализует чтение и запись настроек, профилей и расписания.

#ifndef SettingsLock_h
#define SettingsLock_h

#include <stdint.h>

// Сериализует операции над областями настроек между вебом и задачей ТН.
class SettingsLock {
public:
	// Пытается занять общий флаг flag на время жизни объекта.
	explicit SettingsLock(volatile uint8_t &flag) : flag(flag), owned(!__atomic_exchange_n(&flag, 1, __ATOMIC_SEQ_CST)) {}
	// Освобождает флаг, если объект успел его занять.
	~SettingsLock() { if(owned) __atomic_store_n(&flag, 0, __ATOMIC_SEQ_CST); }
	// Возвращает true, если флаг принадлежит этому объекту.
	explicit operator bool() const { return owned; }

	SettingsLock(const SettingsLock &) = delete;
	SettingsLock &operator=(const SettingsLock &) = delete;

private:
	// Общий флаг операции, которым владеет не более одного объекта.
	volatile uint8_t &flag;
	// Признак успешного захвата флага этим объектом.
	const bool owned;
};

#endif
