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

// Решение о замке шины Modbus: чистая функция без железа, проверяется на ПК (tests/test_modbuslock.cpp).
// Замок создаётся в setup() после HP.initHeatPump(), а initFC/initSDM ходят по шине раньше (doc/Modbus-fix.txt).
// До старта планировщика задач нет и SemaphoreTake замок не трогает, поэтому пустой замок там допустим;
// после старта пустой замок - assert ядра FreeRTOS, обмен отвергается (MODBUS_UNAVAILABLE).
#ifndef ModbusLock_h
#define ModbusLock_h

inline bool Modbus_LockRefused(bool haveLock, bool schedulerStarted)
{
	return !haveLock && schedulerStarted;
}

#endif
