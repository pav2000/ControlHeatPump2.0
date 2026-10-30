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
 * Реализация наблюдателя сторожевого таймера. Описание и обоснование - Watchdog.h.
 *
 * Отдельная единица трансляции: не обращается ни к FreeRTOS, ни к железу,
 * ни к объектам проекта - только числа. Поэтому тест линкуется с этим файлом,
 * а не включает его копию (tests/test_watchdog.cpp).
 *
 * Всё, что общается с системой - опрос состояния задач, сброс WDT, чтение
 * и запись GPBR - живёт в Control.ino.
 */
#include "Watchdog.h"

WatchdogVerdict Watchdog_Check(WatchdogTask *tasks, uint8_t n,
                                             uint32_t now_ms, uint32_t suspended,
                                             int8_t *stuck_out, uint32_t *record_mask)
{
	int8_t stuck = -1;
	uint32_t records = 0;
	for(uint8_t i = 0; i < n; i++) {
		WatchdogTask *t = &tasks[i];

		if(suspended & (1UL << i)) {
			// Приостановленная задача не обязана отмечаться. Сдвигаем отсчёт,
			// иначе сразу после возобновления она окажется "просроченной".
			t->last_beat = t->beat;
			t->last_seen_ms = now_ms;
			continue;
		}
		// Замер запаса, см. "ЗАМЕР ЗАПАСА" в Watchdog.h. Считается до разбора
		// того, отметилась задача или нет, потому что число одно и то же:
		// у отметившейся это длина только что закрытого молчания, у молчащей -
		// длина ещё не закрытого. Вычитание беззнаковых переживает переполнение.
		uint32_t silence = (uint32_t)(now_ms - t->last_seen_ms);
		if(silence > t->max_silence_ms) {
			t->max_silence_ms = silence;
			records |= 1UL << i;
		}
		if(t->beat != t->last_beat) {        // отметилась
			t->last_beat = t->beat;
			t->last_seen_ms = now_ms;
			continue;
		}
		if(t->limit_ms && silence > t->limit_ms) {
			if(stuck < 0) stuck = (int8_t) i;
		}
	}
	if(stuck_out) *stuck_out = stuck;
	if(record_mask) *record_mask = records;
	return stuck < 0 ? WDT_ALL_ALIVE : WDT_TASK_STUCK;
}

// Инициализация состояния одной задачи
void Watchdog_Init(WatchdogTask *t, uint32_t limit_ms, uint32_t now_ms)
{
	t->beat = 0;
	t->last_beat = 0;
	t->last_seen_ms = now_ms;
	t->limit_ms = limit_ms;
	t->max_silence_ms = 0;
}


BootInfo Boot_Decide(BootRegs *r, uint8_t rsttyp)
{
	BootInfo b;
	const bool switched = !(r->rtos & GPBR_RTOS_READ);      // после прошлого старта планировщик переключал задачи
	b.fault = (uint8_t) r->rtos;
	b.rtos  = switched ? (uint16_t) r->rtos : b.fault;
	b.task  = -1;
	if(rsttyp == RST_GENERAL || rsttyp == RST_BACKUP) b.cause = BOOT_POWER;
	else if(b.fault) {
		b.cause = BOOT_KERNEL;
		if(switched) b.task = (uint8_t)(r->rtos >> 8);
	} else if((r->reason & 0xFFFF0000UL) == RESET_STUCK_MARK) {
		b.cause = BOOT_STUCK;
		b.task = (uint8_t) r->reason;
	} else if(r->reason == RESET_ASSERT_MARK) b.cause = BOOT_ASSERT;
	else if(rsttyp == RST_WATCHDOG) b.cause = BOOT_HW_WDT;
	else if(rsttyp == RST_SOFTWARE) b.cause = r->reason == RESET_PLANNED_MARK ? BOOT_PLANNED : BOOT_SOFT_UNKNOWN;
	else b.cause = BOOT_USER;
	if(b.cause == BOOT_POWER) { b.fault = 0; b.rtos = 0; }

	uint32_t n = 0;
	if(b.cause != BOOT_POWER && (r->boot & 0xFFFF0000UL) == BOOT_MARK) n = r->boot & 0xFFFF;
	if(n > 255) n = 255;
	if(Boot_Abnormal(b.cause) && n < 255) n++;
	b.resets = (uint8_t) n;

	r->rtos   = GPBR_RTOS_READ;                              // код прочитан: авария до планировщика при следующем старте не смешается с этой
	r->reason = 0;
	r->boot   = n ? BOOT_MARK | n : 0;
	return b;
}

bool Boot_Abnormal(BOOT_CAUSE c)
{
	return c >= BOOT_KERNEL;
}

bool Boot_NoAutoStart(const BootInfo &b)
{
	return b.resets >= BOOT_MAX_RESETS;
}

static volatile uint32_t uptime_sec = 1;
static uint16_t uptime_ms;

void Uptime_Tick(void)
{
	if(++uptime_ms >= 1000) { uptime_ms = 0; uptime_sec++; }
}

uint32_t Uptime_s(void) { return uptime_sec; }

uint32_t Uptime_ToWall(uint32_t t, uint32_t now_uptime, uint32_t now_wall)
{
	return t ? now_wall - (now_uptime - t) : 0;
}

const char *Boot_CauseText(BOOT_CAUSE c)
{
	switch(c) {
	case BOOT_POWER:        return "подача питания";
	case BOOT_USER:         return "кнопка RESET";
	case BOOT_PLANNED:      return "сброс по команде";
	case BOOT_KERNEL:       return "авария ядра";
	case BOOT_STUCK:        return "молчала задача";
	case BOOT_HW_WDT:       return "аппаратный сторож";
	case BOOT_ASSERT:       return "провал assert";
	case BOOT_SOFT_UNKNOWN: return "программный сброс без причины";
	}
	return "?";
}
