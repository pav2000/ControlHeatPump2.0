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
 * Работа со временем. Делится на две части:
 *
 *   DateTime.cpp - форматирование: unix-время в строку и обратно. Отдельная
 *                  единица трансляции, часов не читает, объектов проекта не
 *                  знает. Проверяется тестами (tests/test_datetime.cpp).
 *   Clock.cpp    - общение с железом: NTP, встроенные часы SAM (регистры
 *                  RTC->), часы DS3232 по I2C, установка времени.
 *
 * Заголовок обязан быть самодостаточным: его включает и DateTime.cpp, где
 * никакого Arduino.h нет. Включается из Constant.h, то есть виден всему
 * проекту - определять здесь объекты нельзя, только объявлять.
 */
#ifndef DateTime_h
#define DateTime_h

#include <stdint.h>

// ---- DateTime.cpp: форматирование ----

// Длительность интервала в строку: "5 дн 12:34" или с секундами.
// Результат ДОБАВЛЯЕТСЯ в ret.
//
char* TimeIntervalToStrCompact(uint32_t idt, char *ret, uint8_t fSec);  // "1 1:01m"
char* TimeIntervalToStrRemote(uint32_t idt, char *ret);                 // "1d 01h 01m "

// Выбирает один из двух по SENSOR_IP, реализован в Clock.cpp.
char* TimeIntervalToStr(uint32_t idt, char *ret, uint8_t fSec = 0);

char* UTimeToStr(uint32_t idt);                       // "12:34:56", НЕ реентерабельна
char* DecodeTimeDate(uint32_t idt, char *ret);        // "hh:mm:ss dd/mm/yyyy", ДОБАВЛЯЕТСЯ в ret
char* StatDate(uint32_t idt, bool forma, char *ret);  // ДДММГГ (forma=true) или Д/М/ГГГГ, ДОБАВЛЯЕТСЯ в ret

// ---- Clock.cpp: читают часы ----
char* NowTimeToStr(char *buf = NULL);                 // текущее время "xx:xx:xx"
char* NowDateToStr(char *buf = NULL);                 // текущая дата "xx.xx.xxxx"

#endif
