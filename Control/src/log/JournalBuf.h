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
 * Буферизация системного журнала: подавление повторов и кольцо отложенной записи.
 *
 * ЗАЧЕМ. На рабочем контроллере журнал пишется в I2C EEPROM 64 КБ (на стенде - в FRAM, там
 * проблема незаметна). Три беды:
 *  1. Каждая строка - отдельная запись (~5 мс внутреннего цикла EEPROM) под xI2CSemaphore, а
 *     пишут десять задач, включая реальное время (vReadSensor, vUpdate).
 *  2. Форматирование идёт в общий буфер pbuf без блокировки: при аварии, когда пишут сразу
 *     несколько задач, строки перемешиваются.
 *  3. Залипшая ошибка в цикле изнашивает EEPROM: при записи раз в 100 мс ресурса хватает на
 *     два года, а у чипа на 100 тыс. циклов - на два месяца.
 * Подавление повторов убирает третью и делает журнал читаемым; кольцо в ОЗУ убирает первую:
 * задачи кладут строку в буфер, пишет одна задача пачками по странице EEPROM.
 * Логика - чистые функции без железа и FreeRTOS (tests/test_journalbuf.cpp).
 */
#ifndef JournalBuf_h
#define JournalBuf_h

#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Подавление повторов
// ---------------------------------------------------------------------------
// Сравнивается тело сообщения БЕЗ отметки времени: строки вида
// "11:39:01 Error -11 PrepareTemp bus 1" отличаются временем, но повторяются
// по сути. Вызывающий передаёт указатель на тело.
#define JRN_DEDUP_CMP   48      // сколько байт тела сравнивать

enum JournalAction {
	JRN_WRITE = 0,      // писать строку как есть
	JRN_SUPPRESS,       // не писать: это повтор предыдущей
	JRN_EMIT_REPEATS    // сначала выдать "повторялось N раз", затем строку
};

struct JournalDedup {
	char     last[JRN_DEDUP_CMP];
	char     prev[JRN_DEDUP_CMP];   // предыдущее сообщение - то, что повторялось
	uint16_t count;     // сколько раз повторилось подряд, не считая первой
};

// Имя повторявшегося сообщения отдаётся ОТДЕЛЬНО, через body_out, и брать его
// из d->last нельзя: к моменту возврата JRN_EMIT_REPEATS там уже лежит новое
// сообщение, то самое, которое ряд повторов прервало. Пока имя брали из last,
// строка "повторялось N раз: X" называла чужое сообщение - поймано
// на журнале стенда, где ряд Error -11 вышел под именем ON/OFF Key pressed!.
// Для этого в структуре и держится prev: копировать на стороне вызывающего
// значило бы занимать кадр стека в _post при каждой записи в журнал.
void          JournalDedup_Reset(JournalDedup *d);
JournalAction JournalDedup_Check(JournalDedup *d, const char *body, uint16_t *repeats,
                                 const char **body_out = NULL);
// Выдать накопленные повторы, если новых строк давно не было.
// Возвращает true, число повторов и имя повторявшегося сообщения.
bool          JournalDedup_Flush(JournalDedup *d, uint16_t *repeats,
                                 const char **body_out = NULL);

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
// Забрать до max байт подряд лежащих данных. Возвращает сколько забрано.
uint16_t JournalRing_Get(JournalRing *r, uint8_t *dst, uint16_t max);

#endif
