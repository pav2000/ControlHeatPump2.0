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
 * Форматирование дат и интервалов: перевод unix-времени в строку.
 * Обоснование и интерфейс - в DateTime.h.
 *
 * Отдельная единица трансляции: часов не читает, объектов проекта не знает,
 * получает время числом и возвращает строку. Всё, что общается с железом -
 * NTP, встроенные часы SAM, часы DS3232 по I2C - в Clock.cpp.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mini-printf.h"
#include "Util.h"          // _itoa
#include "DateTime.h"

// Используется только здесь.
static const char FORMAT_DATE_STR[] = "%02d/%02d/%04d";

// (Длительность интервала в строку) Время в формате день day 12:34 используется для расчёта uptime
// Результат ДОБАВЛЯЕТСЯ в ret
// Компактный формат: "1 1:01m", без ведущих нулей.
// ВНИМАНИЕ: выбор между этим форматом и форматом для удалённых датчиков
// делается в Clock.cpp по #ifdef SENSOR_IP. Здесь его делать нельзя -
// модуль не видит Config.h, и такой #ifdef молча выбрал бы не ту ветку
// (ровно так уже ломался Freon на R4WAY).
NtpPacket Ntp_Parse(const uint8_t *p, int len, uint32_t *unix_s)
{
	if(len < 48) return NTP_PKT_SHORT;
	if((p[0] & 7) != 4) return NTP_PKT_MODE;
	if(p[1] == 0) return NTP_PKT_KOD;
	if((p[0] >> 6) == 3 || p[1] >= 16) return NTP_PKT_UNSYNC;
	uint32_t ntp = ((uint32_t) p[40] << 24) | ((uint32_t) p[41] << 16) | ((uint32_t) p[42] << 8) | p[43];
	if(ntp == 0) return NTP_PKT_NOTIME;
	const uint64_t NTP_TO_UNIX = 2208988800ULL;                         // секунд от 1900 до 1970
	if((uint64_t) ntp < NTP_TO_UNIX + NTP_MIN_UNIX || (uint64_t) ntp >= NTP_TO_UNIX + NTP_MAX_UNIX) return NTP_PKT_RANGE;
	*unix_s = (uint32_t) ((uint64_t) ntp - NTP_TO_UNIX);                // вычитание в 64 битах: в 32 битных меньшее значение заворачивалось в 2036 год
	return NTP_PKT_OK;
}

bool Ntp_JumpOk(uint32_t cur, uint32_t now)
{
	if(cur < NTP_MIN_UNIX) return true;                                 // часы не выставлены
	uint32_t d = now > cur ? now - cur : cur - now;
	return d <= NTP_MAX_JUMP_S;
}

char* TimeIntervalToStrCompact(uint32_t idt, char *ret, uint8_t fSec)
{
	uint8_t Hour, Min, Sec = 0;
	/* decode the interval into days, hours, minutes, seconds */
	if(fSec) Sec = idt % 60;
	idt /= 60;
	Min = idt % 60;
	idt /= 60;
	Hour = idt % 24;
	idt /= 24;

	if(idt) {
		_itoa(idt, ret);
		strcat(ret, " ");
		goto xHour;
	}
	if(Hour) {
xHour:	_itoa(Hour, ret);
		strcat(ret, ":");
		goto xMin;
	}
	if(Min) {
xMin:	_itoa(Min, ret);
		if(!fSec) strcat(ret, "m");
		else {
			strcat(ret, ":");
			goto xSec;
		}
	}
	if(fSec) {
xSec:	_itoa(Sec, ret);
		strcat(ret, "s");
	}
	return ret;
}

// Формат для удалённых датчиков: "1d 01h 01m ", с ведущими нулями и всегда
// с минутами. Их парсер разбирает только его, поэтому форматы не
// взаимозаменяемы.
char* TimeIntervalToStrRemote(uint32_t idt, char *ret)
{
	uint8_t Hour, Min;
	idt /= 60;
	Min = idt % 60;
	idt /= 60;
	Hour = idt % 24;
	idt /= 24;

	if(idt > 0) { _itoa(idt, ret); strcat(ret, "d "); }
	if(Hour > 0) { if(Hour < 10) strcat(ret, "0"); _itoa(Hour, ret); strcat(ret, "h "); }
	if(Min > 0) { if(Min < 10) strcat(ret, "0"); _itoa(Min, ret); strcat(ret, "m "); } else strcat(ret, "00m ");
	return ret;
}

// вывод Времени в формате 12:34:34, не реентерабельна
char* UTimeToStr(uint32_t idt)
{
	static char _tmp[10];  // Длина xx:xx:xx - 10+1 символов
	m_snprintf((char *)_tmp, sizeof(_tmp), "%02d:%02d:%02d", (int)((idt % 86400L) / 3600), (int)((idt % 3600) / 60), (int)(idt % 60));
	return _tmp;
}

// вывод Время и даты  в формате hh:mm:ss dd/mm/yyyy
// Результат ДОБАВЛЯЕТСЯ в ret
//http://stackoverflow.com/questions/21593692/convert-unix-timestamp-to-date-without-system-libs
char*  DecodeTimeDate(uint32_t idt,char *ret)
{
  uint32_t seconds, minutes, hours, days, year, month;
  uint32_t dayOfWeek;
  int x;

  if(idt == 0) return ret;
  seconds=idt;
  /* calculate minutes */
  minutes  = seconds / 60;
  seconds -= minutes * 60;
  /* calculate hours */
  hours    = minutes / 60;
  minutes -= hours   * 60;
  /* calculate days */
  days     = hours   / 24;
  hours   -= days    * 24;

  /* Unix time starts in 1970 on a Thursday */
  year      = 1970;
  dayOfWeek = 4;

  while(1)
  {
    bool     leapYear   = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    uint16_t daysInYear = leapYear ? 366 : 365;
    if (days >= daysInYear)
    {
      dayOfWeek += leapYear ? 2 : 1;
      days      -= daysInYear;
      if (dayOfWeek >= 7)
        dayOfWeek -= 7;
      ++year;
    }
    else
    {
      dayOfWeek  += days;
      dayOfWeek  %= 7;
      /* calculate the month and day */
      static const uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      for(month = 0; month < 12; ++month)
      {
        uint8_t dim = daysInMonth[month];
        /* add a day to feburary if this is a leap year */
        if (month == 1 && leapYear)
          ++dim;
        if (days >= dim)
          days -= dim;
        else
          break;
      }
      break;
    }
  }
// формирование строки
  x=hours;
  if (x<10) strcat(ret,"0"); else strcat(ret,"");  _itoa(x,ret); strcat(ret,":");
  x=minutes;
  if (x<10) strcat(ret,"0");   _itoa(x,ret); strcat(ret,":");
   x=seconds;
  if (x<10) strcat(ret,"0");   _itoa(x,ret); 
  strcat(ret," "); 
  m_snprintf(ret + m_strlen(ret), 16, FORMAT_DATE_STR, (int)(days + 1), (int)(month+1), (int)(year));
  return ret;   
}

// вывод даты  в формате 010218 для графиков статистики (всегда 6 байт+1)
// forma тип вывода true - краткий (ДДММГГ) false - полный (Д/М/ГГГГ)
// результат ДОБАВЛЯЕТСЯ в ret
//http://stackoverflow.com/questions/21593692/convert-unix-timestamp-to-date-without-system-libs
char*  StatDate(uint32_t idt,bool forma,char *ret)
{
  uint32_t seconds, minutes, hours, days, year, month;
  uint32_t dayOfWeek;
  int x;
  seconds=idt;

  /* calculate minutes */
  minutes  = seconds / 60;
  seconds -= minutes * 60;
  /* calculate hours */
  hours    = minutes / 60;
  minutes -= hours   * 60;
  /* calculate days */
  days     = hours   / 24;
  hours   -= days    * 24;

  /* Unix time starts in 1970 on a Thursday */
  year      = 1970;
  dayOfWeek = 4;
  while(1)
  {
    bool     leapYear   = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    uint16_t daysInYear = leapYear ? 366 : 365;
    if (days >= daysInYear)
    {
      dayOfWeek += leapYear ? 2 : 1;
      days      -= daysInYear;
      if (dayOfWeek >= 7)
        dayOfWeek -= 7;
      ++year;
    }
    else
    {
      dayOfWeek  += days;
      dayOfWeek  %= 7;
      /* calculate the month and day */
      static const uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      for(month = 0; month < 12; ++month)
      {
        uint8_t dim = daysInMonth[month];
        /* add a day to feburary if this is a leap year */
        if (month == 1 && leapYear)
          ++dim;
        if (days >= dim)
          days -= dim;
        else
          break;
      }
      break;
    }
  }

  // _itoa, а не itoa: itoa из ядра Arduino пишет С НАЧАЛА буфера, а не дописывает,
  // и каждое поле затирало бы предыдущее вместе с добавленным нулём ("18" вместо
  // "010218"), уничтожая содержимое ret.
  if(forma)  // Тип вывода
   {
    x=days+1;  if (x<10) strcat(ret,"0"); _itoa(x,ret);
    x=month+1; if (x<10) strcat(ret,"0"); _itoa(x,ret);
    x=year;    if (x>2000) _itoa(x-2000,ret); else _itoa(x-1900,ret); // формирование строки формата ДДММГГ (год последние две цифры)
    }
 else
    {_itoa(days + 1,ret);strcat(ret,"/");_itoa(month+1,ret);strcat(ret,"/");_itoa(year,ret); }
 return ret;   
}
