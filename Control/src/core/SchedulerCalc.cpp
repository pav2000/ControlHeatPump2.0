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
#include <stdlib.h>
#include <string.h>
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
	if(ptr == max || max >= size) return SCHDLR_Profile_off;   // max - индекс ПОСЛЕДНЕГО байта календаря, он обязан быть < size

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
		if(ptr == max || max >= size) continue;                // пустой календарь или вылез за буфер (max - индекс последнего байта)
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

int8_t Scheduler_SetCalendar(uint8_t *tt, uint16_t cap, uint8_t calendars, uint8_t cnum, char *val)
{
	if(cnum >= calendars) return 1;
	char *p;
	uint16_t cur_ptr = 0, cur_end = 0;
	bool started = false;
	while((p = strchr(val, ';'))) {
		*p = 0;
		uint8_t dec = atoi(val);
		val = p + 1;
		if(!started) {
			started = true;
			uint16_t last = Scheduler_TimetablePtr(tt, cap, calendars - 1);
			uint16_t size = last + 1 + tt[last];                 // занято всеми календарями
			cur_ptr = Scheduler_TimetablePtr(tt, cap, cnum);
			uint16_t old_len = tt[cur_ptr];
			uint16_t end = cur_ptr + 1 + old_len;                // за последним байтом редактируемого
			if(size > cap || end > size || size - old_len + dec > cap) return 2;   // не лезет, либо таблица повреждена
			if(cnum < calendars - 1) memmove(tt + cur_ptr + 1 + dec, tt + end, size - end);   // сдвинуть хвост ВЕСЬ, включая последний байт
			if(dec < old_len) memset(tt + size - (old_len - dec), 0, old_len - dec);        // после сжатия хвост буфера обнулить
			memset(tt + cur_ptr + 1, 0, dec);                    // недоданные записи - нули, а не остатки прежних данных
			cur_end = cur_ptr + 1 + dec;
			tt[cur_ptr++] = dec;
			if(dec == 0) return 0;
		} else if(cur_ptr < cur_end) {
			tt[cur_ptr++] = dec;                                 // лишние сверх объявленной длины отбрасываем
		}
	}
	return 0;
}

// Цикл автовыбора из vUpdate (Control.ino). Цикл кончается на первом выбранном календаре.
// Флаг 0x80 ставится и тогда, когда календарь уже активен: иначе ручной перевыбор в его
// декаду на следующий день отменялся бы автовыбором (прежний код так и делал).
bool Scheduler_AutoSelect(uint8_t *asmw, uint8_t calendars, uint8_t *active, uint8_t month, uint8_t mday)
{
	// Декады 1-10, 11-20, 21-31 число. Прежний код делил mday / 10, и границы были 1-9, 10-19, 20-31.
	uint8_t d = mday ? (mday - 1) / 10 : 0;
	if(d > 2) d = 2;
	bool need_save = false;
	for(uint8_t i = 0; i < calendars; i++) {
		if(asmw[i]) {
			if((asmw[i] & ~0x80) == ((month << 3) | d)) {
				if(!(asmw[i] & 0x80)) {
					*active = i;
					asmw[i] |= 0x80;
					need_save = true;
					break;
				}
			} else if(asmw[i] & 0x80) {
				asmw[i] &= ~0x80;
				need_save = true;
			}
		}
	}
	return need_save;
}

uint8_t Scheduler_ProfileStep(int8_t target, int8_t id_profile, bool working, bool waiting, bool cmd_idle, bool no_power)
{
	if(target == SCHDLR_NotActive) return SCHDLR_DoNothing;
	int8_t curr = working ? id_profile : SCHDLR_Profile_off;
	if(target == curr || !cmd_idle) return SCHDLR_DoNothing;      // новый режим и ничего не выполняется?
	if(target == SCHDLR_Profile_off) return SCHDLR_DoWait;
	if(id_profile != target) return SCHDLR_DoSwitch;
	if(waiting && !no_power) return SCHDLR_DoResume;
	return SCHDLR_DoNothing;
}

bool Scheduler_NeedRestart(int32_t new_mode, uint8_t cur_mode)
{
	return new_mode != SCHDLR_MODE_OFF && cur_mode != SCHDLR_MODE_OFF && (cur_mode & (SCHDLR_MODE_HEAT | SCHDLR_MODE_COOL))
			&& (cur_mode & (SCHDLR_MODE_HEAT | SCHDLR_MODE_COOL)) != (new_mode & (SCHDLR_MODE_HEAT | SCHDLR_MODE_COOL));
}
