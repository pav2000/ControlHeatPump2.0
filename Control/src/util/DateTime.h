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

// ---- DateTime.cpp: ответ NTP-сервера ----
// Разбор пакета NTP (48 байт) в unix-время. Прежде принимался любой UDP-пакет: чужой, короткий, «Kiss-o'-Death»
// (stratum 0), с признаком несинхронизированных часов сервера, нулевой отметкой - и часы прыгали куда угодно,
// а за ними сдвигались рабочие времена ТН и перезаписывались часы DS3231.
enum NtpPacket {
	NTP_PKT_OK = 0,
	NTP_PKT_SHORT,        // короче 48 байт
	NTP_PKT_MODE,         // не ответ сервера (mode != 4)
	NTP_PKT_KOD,          // stratum 0: Kiss-o'-Death, сервер просит не опрашивать
	NTP_PKT_UNSYNC,       // LI = 3 или stratum 16 и выше: часы сервера не синхронизированы
	NTP_PKT_NOTIME,       // нулевая отметка Transmit
	NTP_PKT_RANGE         // время вне 2024...2099 годов
};
NtpPacket Ntp_Parse(const uint8_t *p, int len, uint32_t *unix_s);
// Принять ли скачок часов с cur на now. Малый скачок (до суток) - всегда. Большой - только если текущие часы
// явно не выставлены (раньше 2024 года: пустая батарейка, старт до загрузки часов). Иначе скачок на
// сутки и больше от случайного пакета сдвинул бы календарь ТН, статистику и расписание.
#define NTP_MAX_JUMP_S   86400UL
#define NTP_MIN_UNIX     1704067200UL      // 2024-01-01 00:00:00 UTC
#define NTP_MAX_UNIX     4102444800UL      // 2100-01-01 00:00:00 UTC
bool Ntp_JumpOk(uint32_t cur, uint32_t now);

// ---- Clock.cpp: читают часы ----
char* NowTimeToStr(char *buf = NULL);                 // текущее время "xx:xx:xx"
char* NowDateToStr(char *buf = NULL);                 // текущая дата "xx.xx.xxxx"

#endif
