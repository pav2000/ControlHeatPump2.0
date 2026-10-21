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
 * Прямой доступ к пинам SAM3X мимо digitalWrite/digitalRead ядра Arduino.
 *
 * Те делают проверку диапазона, разбор таймеров ШИМ и отключение периферии
 * на пине - десятки тактов там, где нужна одна запись в регистр. В цепях
 * выбора SPI, тактирования 1-Wire и фазах шагового двигателя ЭРВ это на
 * счету, поэтому здесь запись идёт прямо в PIO_SODR/PIO_CODR.
 *
 * Функции static inline: так их видит любой отдельный .cpp, а встраивание сохраняется -
 * каждая единица трансляции получает своё определение. Это позволяет выносить драйверы
 * (шаговый двигатель, реле, дискретные входы, 1-Wire) в отдельные единицы трансляции.
 *
 * Заголовок самодостаточен и включает только ядро Arduino: проверено, что
 * Arduino.h включается во вторую единицу трансляции без дублей символов.
 */
#ifndef GpioDirect_h
#define GpioDirect_h

#include "Arduino.h"

// Запись в пин одной командой. always_inline оставлен намеренно: тело - одна
// запись в регистр, вызов стоил бы столько же, сколько сама работа.
__attribute__((always_inline)) static inline void digitalWriteDirect(int pin, bool val)
{
	if(val) g_APinDescription[pin].pPort->PIO_SODR = g_APinDescription[pin].ulPin;
	else    g_APinDescription[pin].pPort->PIO_CODR = g_APinDescription[pin].ulPin;
}

__attribute__((always_inline)) static inline int digitalReadDirect(int pin)
{
	return !!(g_APinDescription[pin].pPort->PIO_PDSR & g_APinDescription[pin].ulPin);
}

#endif
