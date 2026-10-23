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
 * Очередь команд управления ТН: чистая логика без железа и FreeRTOS (tests/test_commandqueue.cpp).
 *
 * ЗАЧЕМ. Раньше команда лежала в двух полях (command, next_command), а задачу CommandHP
 * будили vTaskResume. Две беды:
 *  1. Пробуждение терялось: sendCommand между "command=pEMPTY" и vTaskSuspend(NULL) в CommandHP
 *     оставлял command!=pEMPTY у уснувшей задачи, и все следующие команды (в том числе STOP)
 *     только писали next_command - до сброса контроллера.
 *  2. Один слот "следующая": пара STOP, SAVE теряла STOP; команда, пришедшая в окне между
 *     проверкой next_command и сбросом command, выполнялась позже, после чужой команды.
 * Здесь очередь фиксированной глубины. Вызывающий сам обеспечивает атомарность (запрет
 * прерываний вокруг Push/Pop); будит CommandHP уведомлением задачи, которое не теряется.
 */
#ifndef CommandQueue_h
#define CommandQueue_h

#include <stdint.h>

// Глубина очереди. Меняется здесь (или -DCOMMAND_QUEUE_DEPTH=N), больше ничего править не нужно.
#ifndef COMMAND_QUEUE_DEPTH
#define COMMAND_QUEUE_DEPTH 4
#endif

#define CQ_STOP      1   // команда останова: пуск прерывается, если такая стоит в очереди
#define CQ_CRITICAL  2   // не теряется при полной очереди, вытесняя обычную

enum CqResult {
	CQ_QUEUED = 0,   // поставлена
	CQ_DUPLICATE,    // повтор выполняемой или последней в очереди - проигнорирована
	CQ_EVICTED,      // поставлена, вытеснив более старую
	CQ_FULL          // очередь полна, команда потеряна
};

struct CommandQueue {
	uint8_t cmd[COMMAND_QUEUE_DEPTH];
	uint8_t fl[COMMAND_QUEUE_DEPTH];
	uint8_t head;    // откуда берут
	uint8_t count;   // сколько лежит
};

static inline void CommandQueue_Init(CommandQueue *q) { q->head = 0; q->count = 0; }

static inline uint8_t CommandQueue_Index(const CommandQueue *q, uint8_t k) { return (uint8_t)((q->head + k) % COMMAND_QUEUE_DEPTH); }

// running - выполняемая сейчас команда (0, если нет). Повтор выполняемой или последней в очереди игнорируется.
static inline CqResult CommandQueue_Push(CommandQueue *q, uint8_t running, uint8_t c, uint8_t fl)
{
	if(c == running) return CQ_DUPLICATE;
	if(q->count && q->cmd[CommandQueue_Index(q, (uint8_t)(q->count - 1))] == c) return CQ_DUPLICATE;
	CqResult r = CQ_QUEUED;
	if(q->count >= COMMAND_QUEUE_DEPTH) {
		if(!(fl & CQ_CRITICAL)) return CQ_FULL;
		// вытесняем самую старую обычную, а если все критичные - самую старую
		uint8_t k = 0;
		while(k < q->count && (q->fl[CommandQueue_Index(q, k)] & CQ_CRITICAL)) k++;
		if(k == q->count) k = 0;
		for(uint8_t j = k; j + 1 < q->count; j++) {
			q->cmd[CommandQueue_Index(q, j)] = q->cmd[CommandQueue_Index(q, (uint8_t)(j + 1))];
			q->fl[CommandQueue_Index(q, j)]  = q->fl[CommandQueue_Index(q, (uint8_t)(j + 1))];
		}
		q->count--;
		r = CQ_EVICTED;
	}
	uint8_t i = CommandQueue_Index(q, q->count);
	q->cmd[i] = c; q->fl[i] = fl;
	q->count++;
	return r;
}

// Взять самую старую. false - очередь пуста.
static inline bool CommandQueue_Pop(CommandQueue *q, uint8_t *c)
{
	if(!q->count) return false;
	*c = q->cmd[q->head];
	q->head = (uint8_t)((q->head + 1) % COMMAND_QUEUE_DEPTH);
	q->count--;
	return true;
}

// Есть ли в очереди команда с флагом.
static inline bool CommandQueue_Has(const CommandQueue *q, uint8_t flag)
{
	for(uint8_t k = 0; k < q->count; k++) if(q->fl[CommandQueue_Index(q, k)] & flag) return true;
	return false;
}

#endif
