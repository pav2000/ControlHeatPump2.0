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
 * Буферизация системного журнала.
 *
 * Задачи по очереди форматируют записи и кладут их в кольцо в ОЗУ.
 * Одна задача выгружает данные пачками в I2C EEPROM, чтобы запись
 * каждой строки не задерживала задачу, которая её добавила.
 * Кольцо и расчёт записи в EEPROM проверяются без FreeRTOS и оборудования.
 */
#ifndef JournalBuf_h
#define JournalBuf_h

#include <stdint.h>

enum class JournalStatus : uint8_t {
	Ok,          // Журнал не отмечен как неисправный.
	OpenFailed,  // Не удалось открыть носитель журнала.
	ReadFailed,  // Ошибка чтения или не найдены границы журнала.
	WriteFailed  // Ошибка записи в EEPROM, буфер остаётся доступен для повтора.
};

// ---------------------------------------------------------------------------
// Кольцо отложенной записи
// ---------------------------------------------------------------------------
// Байтовое кольцо: задачи дописывают строки, писатель забирает их пачками.
// При переполнении новые строки отбрасываются, а счётчик потерь выводится
// в журнал при первой возможности - молча терять записи нельзя.
struct JournalRing {
	uint8_t *buf;
	uint16_t size;
	uint16_t head;      // откуда читает писатель
	uint16_t tail;      // куда пишут задачи
	uint16_t lost;      // сколько строк отброшено при переполнении
};

void     JournalRing_Init(JournalRing *r, uint8_t *storage, uint16_t size);
uint16_t JournalRing_Free(const JournalRing *r);
uint16_t JournalRing_Used(const JournalRing *r);
// Положить строку целиком. Возвращает false, если не влезла (строка теряется).
bool     JournalRing_Put(JournalRing *r, const char *s, uint16_t len);
// Засчитать потерянную строку. Атомарно: зовётся и там, где замок журнала взять не удалось.
void     JournalRing_Lose(JournalRing *r);
// Забрать до max байт подряд лежащих данных. Возвращает сколько забрано.
uint16_t JournalRing_Get(JournalRing *r, uint8_t *dst, uint16_t max);

// ---------------------------------------------------------------------------
// Размещение записи в журнале EEPROM
// ---------------------------------------------------------------------------
// Журнал в EEPROM - кольцо: за последней строкой стоит маркер TAIL, а когда кольцо уже
// заворачивалось (full), за ним ещё HEAD - начало самых старых данных. Запись, которая
// не помещается до конца области, уходит в два приёма: хвост области, затем начало.
// Расчёт вынесен из Journal::_write, чтобы проверяться на хосте (tests/test_journalbuf.cpp):
// крайние случаи заворота (маркер ровно на последнем байте, HEAD на границе) в живой
// записи случаются раз за круг, и поймать их на железе почти невозможно.
//
// Записываемые байты: данные (numBytes), TAIL, и HEAD, если put_head. Из них первые len1
// кладутся по адресу tail, остальные len2 (если не ноль) - в начало области.
struct JournalPlan {
	int32_t len1;       // байт в первом приёме
	int32_t len2;       // байт во втором приёме с начала области, 0 - запись целиком в один приём
	int32_t tail;       // новая позиция TAIL
	int32_t head;       // новая позиция HEAD (значима, если full_after)
	uint8_t full_after; // кольцо заворачивалось: после записи HEAD обязан стоять
	bool    put_head;   // ставить HEAD сразу за TAIL
};
void Journal_PlanWrite(int32_t numBytes, int32_t tail, uint8_t full, int32_t cap, JournalPlan *p);

#endif
