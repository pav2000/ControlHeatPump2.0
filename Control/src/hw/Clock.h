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
#ifndef __CLOCK_H__
#define __CLOCK_H__

#include <Arduino.h>
#include <DS3232.h>   // tmElements_t

/*
 * Часы контроллера: встроенный RTC SAM3X, часы DS3232 по I2C и синхронизация
 * по сети. Разделение с DateTime.h намеренное: там чистое форматирование,
 * которое ничего не знает о железе и проверяется на хосте, здесь - всё, что
 * читает регистры и ходит в сеть.
 */

// Начальная установка времени при старте: I2C-часы, затем попытка NTP.
// Возвращает код ошибки (OK при успехе).
int8_t set_time(void);

// Обновление времени по сети. true - время обновлено.
// Реализация зависит от HTTP_TIME_REQUEST: NTP по UDP или HTTP GET.
boolean set_time_NTP(void);

// Итог последней попытки синхронизации - для веб-морды (get_NTPr): без него «Обновить время» ничего не сообщает.
// Строка "номер попытки;код;ЧЧ:ММ:СС" (пустая, пока попыток не было). Номер растёт на каждую завершённую попытку,
// по нему страница отличает свежий итог от прежнего.
enum { NTP_RES_OK = 1,		// время получено
	NTP_RES_DNS,			// имя сервера не разрешилось (DNS)
	NTP_RES_NOANSWER,		// сервер не ответил (нет соединения или ответа)
	NTP_RES_BADANSWER,		// ответ получен, но неверный
	NTP_RES_BUSY,			// сеть занята, попытка не состоялась
	NTP_RES_SOCKET };		// не удалось открыть сокет
char* get_NTP_result(char *ret);

// Перевод структуры времени в unix-время. Год 0 означает "часы не прочитались"
// и даёт 0 на выходе - вызывающий обязан это проверять.
unsigned long TimeToUnixTime(tmElements_t *t);

#endif
