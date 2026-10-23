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
 * Реализация буферизации журнала. Обоснование и интерфейс - в JournalBuf.h.
 *
 * Отдельная единица трансляции: ни FreeRTOS, ни железа, ни объектов проекта -
 * только числа, строки и память. Поэтому проверяется обычными тестами,
 * которые линкуются с этим файлом, а не включают его копию
 * (tests/test_journalbuf.cpp).
 *
 * Код, которому нужен сам журнал (HP, семафоры, запись в EEPROM), живёт
 * в Journal.cpp.
 */
#include "JournalBuf.h"

// ---- подавление повторов ----

void JournalDedup_Reset(JournalDedup *d)
{
	d->last[0] = '\0';
	d->prev[0] = '\0';
	d->count = 0;
}

JournalAction JournalDedup_Check(JournalDedup *d, const char *body, uint16_t *repeats,
                                 const char **body_out)
{
	if(body == NULL) body = "";
	if(d->last[0] && strncmp(d->last, body, JRN_DEDUP_CMP - 1) == 0) {
		if(d->count < 0xFFFF) d->count++;    // насыщение вместо переполнения
		return JRN_SUPPRESS;
	}
	// Сообщение сменилось. Прежнее уезжает в prev: именно оно повторялось,
	// и именно его имя должно попасть в строку о повторах.
	uint16_t pending = d->count;
	d->count = 0;
	memcpy(d->prev, d->last, JRN_DEDUP_CMP);
	strncpy(d->last, body, JRN_DEDUP_CMP - 1);
	d->last[JRN_DEDUP_CMP - 1] = '\0';
	if(pending) {
		if(repeats) *repeats = pending;
		if(body_out) *body_out = d->prev;
		return JRN_EMIT_REPEATS;
	}
	return JRN_WRITE;
}

bool JournalDedup_Flush(JournalDedup *d, uint16_t *repeats, const char **body_out)
{
	if(!d->count) return false;
	if(repeats) *repeats = d->count;
	// Здесь сообщение НЕ сменилось, повторялось именно last - его и отдаём.
	if(body_out) *body_out = d->last;
	d->count = 0;
	// last не трогаем: если та же строка придёт снова, счёт начнётся заново,
	// но повторы уже выданы и не потеряются.
	return true;
}

// ---- кольцо ----

void JournalRing_Init(JournalRing *r, uint8_t *storage, uint16_t size)
{
	r->buf = storage;
	r->size = size;
	r->head = r->tail = 0;
	r->lost = 0;
}

uint16_t JournalRing_Used(const JournalRing *r)
{
	// Нельзя писать (tail - head) % size: при tail < head беззнаковая разность
	// даёт 65536-x, и остаток от деления совпадает с верным ответом только
	// когда size - степень двойки. Поймано тестом на кольце размером 24.
	return r->tail >= r->head ? (uint16_t)(r->tail - r->head)
	                          : (uint16_t)(r->size - r->head + r->tail);
}

// Одна ячейка всегда свободна: иначе полный буфер неотличим от пустого.
uint16_t JournalRing_Free(const JournalRing *r)
{
	return r->size - 1 - JournalRing_Used(r);
}

void JournalRing_Lose(JournalRing *r)
{
	uint16_t v = __atomic_load_n(&r->lost, __ATOMIC_RELAXED);
	while(v < 0xFFFF && !__atomic_compare_exchange_n(&r->lost, &v, (uint16_t)(v + 1), true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

bool JournalRing_Put(JournalRing *r, const char *s, uint16_t len)
{
	if(len == 0) return true;
	if(len > JournalRing_Free(r)) {
		JournalRing_Lose(r);
		return false;                      // строка теряется целиком, не кусками
	}
	for(uint16_t i = 0; i < len; i++) {
		r->buf[r->tail] = (uint8_t) s[i];
		r->tail = (uint16_t)(r->tail + 1) % r->size;
	}
	return true;
}

uint16_t JournalRing_Get(JournalRing *r, uint8_t *dst, uint16_t max)
{
	uint16_t n = JournalRing_Used(r);
	if(n > max) n = max;
	for(uint16_t i = 0; i < n; i++) {
		dst[i] = r->buf[r->head];
		r->head = (uint16_t)(r->head + 1) % r->size;
	}
	return n;
}

void Journal_PlanWrite(int32_t numBytes, int32_t tail, uint8_t full, int32_t cap, JournalPlan *p)
{
	if(tail + numBytes + 2 > cap) {
		// Не помещается вместе с TAIL и HEAD: два приёма. HEAD нужен всегда - после заворота кольцо полное,
		// а раньше он ставился только если полное было уже ДО этой записи: при первом заворота вторым приёмом
		// уходил случайный байт на месте HEAD.
		p->len1 = cap - tail;
		p->len2 = numBytes + 2 - p->len1;
		int32_t rest = numBytes - p->len1;              // сколько данных ушло в начало области; -1 - на конце области только TAIL
		p->tail = rest >= 0 ? rest : cap - 1;
		p->head = (p->tail + 1) % cap;                  // раньше без деления: при TAIL на последнем байте HEAD получал адрес cap, за областью
		p->full_after = 1;
		p->put_head = true;
	} else {
		p->len1 = numBytes + 1 + full;
		p->len2 = 0;
		p->tail = tail + numBytes;
		p->head = p->tail + 1;
		p->full_after = full;
		p->put_head = full != 0;
	}
}
