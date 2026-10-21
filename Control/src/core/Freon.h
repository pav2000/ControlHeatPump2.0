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
 * Свойства хладагента: перевод давления в температуру кипения/конденсации
 * и подбор положения ЭРВ по таблице.
 *
 * Это первый настоящий модуль проекта: отдельная единица трансляции
 * с явным интерфейсом. Ничего не знает ни о HeatPump, ни о FreeRTOS,
 * ни о железе - только числа. Поэтому проверяется обычными тестами
 * без заглушек (tests/test_freon.cpp).
 *
 * Переходники к состоянию ТН (кеширование в объекте датчика, выбор режима
 * по четырёхходовому клапану) лежат в HeatPump.h (PressToTemp) и EEV.cpp, а заполнение
 * кеша - в Sensors.cpp рядом с sensorADC::Read, которая его сбрасывает. Они по сути
 * часть HeatPump, а не свойств хладагента.
 *
 * ВНИМАНИЕ: этот заголовок включается из Constant.h, то есть виден всему
 * проекту. Он не должен включать ничего, кроме <stdint.h>: любой #include
 * заголовка проекта отсюда даст цикл.
 */
#ifndef Freon_h
#define Freon_h

#include <stdint.h>

// ---------------------------------------------------------------------------
// Выбор хладагента. Это настройка установки, а не программы: правится при
// заправке другим фреоном. Держится здесь, а не в Config.h, потому что нужна
// обеим единицам трансляции - и прошивке, и модулю Freon.cpp, - а Config.h
// с Constant.h определяют объекты и во вторую единицу трансляции не включаются.
// ---------------------------------------------------------------------------
#define     R22     0
#define     R410A   1
#define     R600A   2
#define     R134A   3
#define     R407C   4
#define     R12     5
#define     R290    6
#define     R404A   7
#define     R717    8       // Обязательно последний, новые добавлять ПЕРЕД

// ТИП ХЛАДАГЕНТА
#define FREON  R410A

// Перевод давления в температуру насыщения.
//   press - манометрическое давление в СОТЫХ бара
//   возврат - температура в СОТЫХ градуса
//
// Внимание на единицы: внутри таблица хранит давление в ТЫСЯЧНЫХ бара,
// а START_TEMP задан в ДЕСЯТЫХ градуса. Четыре разные шкалы в одной
// функции - источник ошибок, поэтому они названы здесь явно.
int16_t Freon_PressToTemp(int16_t press);

// Положение ЭРВ по средним температурам испарителя и конденсатора.
//   teva, tcon - СОТЫЕ градуса
//   heating    - true нагрев, false охлаждение
//   возврат    - шаги ЭРВ
int16_t Freon_TempToEEV(int16_t teva, int16_t tcon, bool heating);

// Границы таблицы, СОТЫЕ градуса. Freon_PressToTemp насыщается на них при
// выходе давления за диапазон таблицы, а не экстраполирует.
int16_t Freon_TempMin(void);
int16_t Freon_TempMax(void);

#endif
