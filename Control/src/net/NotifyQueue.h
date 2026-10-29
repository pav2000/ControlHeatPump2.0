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
 * Очередь уведомлений: чистая логика без железа (tests/test_notifyqueue.cpp).
 *
 * МЕСТА. Тексты лежат в массиве Message::pend[NOTIFY_SLOTS], очередь раздаёт номера мест и порядок.
 * Отправляемое сейчас уведомление - тоже место очереди (cur): оно занято, пока отправка не закончена
 * (NotifyQueue_Done), и вытеснению не подлежит. Признака «есть что отправлять» отдельно от очереди нет:
 * это cur >= 0. Атомарность вокруг вызовов обеспечивает вызывающий (С-15).
 *
 * ПРАВИЛА. Не помещается - вытесняется самое старое МАЛОВАЖНОЕ (предупреждение, сигнал
 * жизни), в том числе новым маловажным: свежее предупреждение полезнее устаревшего. Новое
 * маловажное при очереди из одних важных отбрасывается; новое важное при очереди из одних
 * важных вытесняет самое старое важное. Следующим отправляется самое старое ВАЖНОЕ, а при их отсутствии - самое
 * старое маловажное: ошибка уходит раньше предупреждения, пришедшего перед ней.
 *
 * ПОВТОРЫ (С-16). Повтор - то же уведомление, а не тот же тип: ключ - тип и предмет (код ошибки, crc текста).
 * Помнятся NOTIFY_RECENT последних ключей: чередование двух порогов («дом», «ГВС») тоже подавляется.
 *
 * КАНАЛЫ (Н-28). У отправляемого уведомления маска каналов, которые ещё предстоит пройти. Канал снимается
 * при успехе и при постоянной ошибке, при временной остаётся до повтора.
 */
#ifndef NotifyQueue_h
#define NotifyQueue_h

#include <stdint.h>

#ifndef NOTIFY_DEPTH
#define NOTIFY_DEPTH 2          // мест ожидания, кроме отправляемого сейчас
#endif
#define NOTIFY_SLOTS (NOTIFY_DEPTH + 1)
static_assert(NOTIFY_DEPTH >= 1 && NOTIFY_SLOTS <= 8, "NotifyQueue: used - маска в одном байте");

struct NotifyQueue {
	uint8_t slot[NOTIFY_DEPTH];   // номера мест ожидающих по порядку прихода, [0] - самое старое
	uint8_t high[NOTIFY_DEPTH];   // важное ли уведомление на этой позиции
	uint8_t used;                 // маска занятых мест, вместе с cur
	uint8_t count;                // сколько ждёт
	int8_t  cur;                  // место отправляемого сейчас, -1 - отправлять нечего
};

static inline void NotifyQueue_Init(NotifyQueue *q) { q->used = 0; q->count = 0; q->cur = -1; }

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

// Сделать отправляемым следующее ожидающее, если отправлять нечего. Возвращает cur.
static inline int8_t NotifyQueue_Next(NotifyQueue *q)
{
	if(q->cur >= 0 || !q->count) return q->cur;
	uint8_t k = 0;
	while(k < q->count && !q->high[k]) k++;                // самое старое важное
	if(k == q->count) k = 0;                               // важных нет - самое старое
	q->cur = (int8_t) q->slot[k];
	NotifyQueue_Remove(q, k);
	return q->cur;
}

// Отправка закончена: место отправляемого освобождается.
static inline void NotifyQueue_Done(NotifyQueue *q)
{
	if(q->cur < 0) return;
	q->used &= (uint8_t) ~(1u << q->cur);
	q->cur = -1;
}

// ---- Подавление повторов ----
#ifndef NOTIFY_RECENT
#define NOTIFY_RECENT 4
#endif
struct NotifyRecent {
	uint32_t key[NOTIFY_RECENT];  // тип << 16 | предмет
	uint32_t at[NOTIFY_RECENT];   // когда принято, с
	uint8_t  n;                   // сколько записей занято
};

static inline void NotifyRecent_Init(NotifyRecent *r) { r->n = 0; }

static inline uint32_t Notify_Key(uint8_t type, uint16_t subject) { return (uint32_t) type << 16 | subject; }

// Принималось ли такое уведомление за последние window секунд
static inline bool Notify_Seen(const NotifyRecent *r, uint32_t key, uint32_t now, uint32_t window)
{
	for(uint8_t i = 0; i < r->n; i++)
		if(r->key[i] == key && (uint32_t)(now - r->at[i]) < window) return true;
	return false;
}

// Запомнить принятое: свою прежнюю запись обновляет, иначе занимает свободную или самую старую
static inline void Notify_Remember(NotifyRecent *r, uint32_t key, uint32_t now)
{
	uint8_t k = 0;
	while(k < r->n && r->key[k] != key) k++;
	if(k == r->n) {
		if(r->n < NOTIFY_RECENT) r->n++;
		else {                                             // все заняты: самая старая запись
			k = 0;
			for(uint8_t i = 1; i < r->n; i++) if((uint32_t)(now - r->at[i]) > (uint32_t)(now - r->at[k])) k = i;
		}
	}
	r->key[k] = key;
	r->at[k] = now;
}

// ---- Каналы и повторы отправки ----
enum { NCH_MAIL = 1, NCH_SMS = 2 };
enum NOTIFY_TRY : uint8_t { NTRY_OK, NTRY_RETRY, NTRY_FAIL };   // отправлено, временная ошибка, постоянная

// Итог попытки по каналу. Тестовое уведомление не повторяется: ответ на тест нужен сразу.
static inline NOTIFY_TRY Notify_TryResult(bool ok, bool temporary, bool test)
{
	return ok ? NTRY_OK : (temporary && !test) ? NTRY_RETRY : NTRY_FAIL;
}

// Каналы, оставшиеся после попытки по каналу ch
static inline uint8_t Notify_AfterTry(uint8_t pending, uint8_t ch, NOTIFY_TRY r)
{
	return r == NTRY_RETRY ? pending : (uint8_t)(pending & ~ch);
}

// После прохода по каналам: true - уведомление закончено; false - повторить (счётчик *retry увеличен)
static inline bool Notify_Finished(uint8_t pending, uint8_t *retry, uint8_t max)
{
	if(!pending || *retry >= max) return true;
	(*retry)++;
	return false;
}

#endif
