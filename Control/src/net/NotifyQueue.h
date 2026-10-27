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
 * Очередь ожидающих уведомлений: чистая логика без железа (tests/test_notifyqueue.cpp).
 *
 * ЗАЧЕМ. Message хранил ОДНО уведомление: пока Web0 отправлял письмо (секунды), следующее
 * setMessage затирало его и теряло - ошибка, за которой сразу пришло предупреждение, не уходила.
 * Теперь отправляемое сейчас уведомление лежит в messageData, а пришедшие за это время - в
 * очереди из NOTIFY_DEPTH мест. Тексты лежат в массиве Message::pend[], очередь только
 * раздаёт номера мест и порядок; атомарность вокруг вызовов обеспечивает вызывающий.
 *
 * ПРАВИЛА. Не помещается - вытесняется самое старое МАЛОВАЖНОЕ (предупреждение, сигнал
 * жизни), в том числе новым маловажным: свежее предупреждение полезнее устаревшего. Новое
 * маловажное при очереди из одних важных отбрасывается; новое важное при очереди из одних
 * важных вытесняет самое старое важное. Извлекается самое старое ВАЖНОЕ, а при их отсутствии - самое
 * старое маловажное: ошибка уходит раньше предупреждения, пришедшего перед ней.
 */
#ifndef NotifyQueue_h
#define NotifyQueue_h

#include <stdint.h>

#ifndef NOTIFY_DEPTH
#define NOTIFY_DEPTH 2          // мест ожидания, кроме отправляемого сейчас
#endif
static_assert(NOTIFY_DEPTH >= 1 && NOTIFY_DEPTH <= 8, "NotifyQueue: used_mask - один байт");

struct NotifyQueue {
	uint8_t slot[NOTIFY_DEPTH];   // номера мест по порядку прихода, [0] - самое старое
	uint8_t high[NOTIFY_DEPTH];   // важное ли уведомление на этой позиции
	uint8_t used;                 // маска занятых мест
	uint8_t count;                // сколько ждёт
};

static inline void NotifyQueue_Init(NotifyQueue *q) { q->used = 0; q->count = 0; }

static inline void NotifyQueue_Remove(NotifyQueue *q, uint8_t k)   // убрать позицию k, сдвинув остальные
{
	for(uint8_t j = k; j + 1 < q->count; j++) { q->slot[j] = q->slot[j + 1]; q->high[j] = q->high[j + 1]; }
	q->count--;
}

// Поставить уведомление. Возвращает номер места, куда вызывающий обязан записать текст, или -1, если
// уведомление отброшено. *evicted = 1, если ради него вытеснено более старое.
static inline int8_t NotifyQueue_Push(NotifyQueue *q, uint8_t high, uint8_t *evicted)
{
	*evicted = 0;
	if(q->count >= NOTIFY_DEPTH) {
		uint8_t k = 0;
		while(k < q->count && q->high[k]) k++;            // самое старое маловажное
		if(k == q->count) {                                // маловажных нет
			if(!high) return -1;                           // маловажное при очереди из важных не вытесняет никого
			k = 0;                                         // важное вытесняет самое старое важное
		}
		q->used &= (uint8_t) ~(1u << q->slot[k]);
		NotifyQueue_Remove(q, k);
		*evicted = 1;
	}
	uint8_t s = 0;
	while(q->used & (1u << s)) s++;                        // первое свободное место (после вытеснения оно есть)
	q->used |= (uint8_t)(1u << s);
	q->slot[q->count] = s;
	q->high[q->count] = high ? 1 : 0;
	q->count++;
	return (int8_t) s;
}

// Взять следующее: номер места с текстом или -1, если очередь пуста. Место освобождается сразу -
// вызывающий копирует текст до того, как кто-то успеет поставить новое уведомление (под тем же запретом прерываний).
static inline int8_t NotifyQueue_Pop(NotifyQueue *q)
{
	if(!q->count) return -1;
	uint8_t k = 0;
	while(k < q->count && !q->high[k]) k++;                // самое старое важное
	if(k == q->count) k = 0;                               // важных нет - самое старое
	uint8_t s = q->slot[k];
	q->used &= (uint8_t) ~(1u << s);
	NotifyQueue_Remove(q, k);
	return (int8_t) s;
}

#endif
