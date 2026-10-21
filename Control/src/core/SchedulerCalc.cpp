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

/*
 * Разбор календаря расписаний. Обоснование и формат таблицы - SchedulerCalc.h.
 *
 * Порядок проверок, арифметика указателей и откат на последнюю запись соответствуют
 * Scheduler::calc_active_profile. Единственное отличие - вместо HP.Prof.get_idProfile()
 * здесь возвращается SCHDLR_KeepProfile, а подставляет вызывающий.
 */
#include "SchedulerCalc.h"

// Один элемент календаря - два байта подряд.
#define ITEM_SIZE  2

uint16_t Scheduler_TimetablePtr(const uint8_t *timetable, uint16_t size, uint8_t num)
{
	uint16_t i = 0;
	while(num--) if((i += timetable[i] + 1) >= size - 1) return size - 1;
	return i;
}

int8_t Scheduler_CalcProfile(const uint8_t *timetable, uint16_t size,
                             uint8_t active, uint8_t day_of_week, uint8_t hour,
                             int16_t *change_out)
{
	uint16_t ptr = Scheduler_TimetablePtr(timetable, size, active);
	uint16_t max = ptr + timetable[ptr];
	uint16_t found = 0;

	*change_out = 0;
	// Пустой календарь: длина нулевая либо запись вылезла за буфер.
	if(ptr == max || max > size) return SCHDLR_Profile_off;

	// Идём по записям, пока не встретим первую, которая ПОЗЖЕ текущего момента.
	// Последняя пройденная (found) и есть действующая сейчас.
	for(ptr++; ptr < max; ptr += ITEM_SIZE) {
		uint8_t cal_dw = timetable[ptr] >> 5;      // день недели
		uint8_t cal_h  = timetable[ptr] & 0x1F;    // час
		if(cal_dw > day_of_week || (cal_dw == day_of_week && cal_h > hour)) break;
		found = ptr;
	}
	// found == 0 означает "ни одна запись ещё не наступила на этой неделе":
	// записи начинаются со смещения ptr+1, то есть нулём быть не могут.
	// В этом случае действует последняя запись предыдущей недели.
	if(!found) found = max - ITEM_SIZE + 1;

	int8_t profile = (int8_t) timetable[found + 1];
	if(profile == 0) return SCHDLR_Profile_off;          // пауза
	if(profile == 127) return SCHDLR_KeepProfile;        // ничего не менять
	if(profile < -100) return 128 + profile - 1;         // сменить профиль
	*change_out = profile * 10;                          // сдвинуть температуру
	return SCHDLR_KeepProfile;
}

bool Scheduler_UsesProfile(const uint8_t *timetable, uint16_t size, uint8_t calendars, uint8_t profile)
{
	for(uint8_t num = 0; num < calendars; num++) {
		uint16_t ptr = Scheduler_TimetablePtr(timetable, size, num);
		uint16_t max = ptr + timetable[ptr];
		if(ptr == max || max > size) continue;                 // пустой календарь или вылез за буфер
		for(ptr++; ptr < max; ptr += ITEM_SIZE) {
			int8_t b = (int8_t) timetable[ptr + 1];
			if(b < -100 && 128 + b - 1 == profile) return true;
		}
	}
	return false;
}

bool Scheduler_TrimTimetable(uint8_t *timetable, uint16_t size, uint8_t keep)
{
	uint16_t ptr = Scheduler_TimetablePtr(timetable, size, keep);
	if(ptr >= size - 1) return false;                      // дальше keep-го календаря места нет
	bool had = false;
	for(uint16_t i = ptr; i < size; i++) {
		if(timetable[i]) { had = true; timetable[i] = 0; }
	}
	return had;
}

bool Scheduler_FixActive(uint8_t *flags, uint8_t *active, uint8_t keep, uint8_t mask)
{
	if(*active < keep) return false;
	*active = 0;
	*flags &= ~mask;
	return true;
}
