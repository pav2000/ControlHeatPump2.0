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
 * Заливка веб-морды: состояние между запросами, чистая логика без железа (tests/test_webupload.cpp).
 *
 * Заливка - это несколько POST-запросов: «начало» (носитель), файлы, «конец». Состояние живёт между ними
 * и меняется только под замком сети (parserPOST вызывается под ним). Носитель запоминается только при
 * принятом начале: отвергнутый повтор начала из второй вкладки не трогает идущую заливку (С-17).
 * Заливка, молчащая дольше WEB_UPLOAD_STALE_MS, считается брошенной, и новое начало её перехватывает.
 */
#ifndef WebUpload_h
#define WebUpload_h

#include <stdint.h>

#define WEB_UPLOAD_IDLE_MS   10000    // нет ни байта от клиента дольше - файл считается оборванным
#define WEB_UPLOAD_STALE_MS  30000    // заливка молчит дольше - брошена, новое начало её перехватывает

enum : uint8_t { UPL_IDLE, UPL_ERASING, UPL_ACTIVE };   // фаза: нет заливки, стирание флеш перед ней, приём файлов
enum : uint8_t { UPL_FLASH = 1, UPL_SD = 2 };          // носитель

struct WebUpload {
	uint8_t  phase;
	uint8_t  media;
	uint16_t files;        // принято файлов
	uint32_t tick;         // последняя активность, мс
};

static inline bool WebUpload_Busy(const WebUpload *u) { return u->phase != UPL_IDLE; }

// Начать заливку. false - идёт свежая заливка, состояние не меняется.
static inline bool WebUpload_Begin(WebUpload *u, uint8_t media, uint32_t now)
{
	if(u->phase != UPL_IDLE && (uint32_t)(now - u->tick) < WEB_UPLOAD_STALE_MS) return false;
	u->phase = media == UPL_FLASH ? UPL_ERASING : UPL_ACTIVE;
	u->media = media;
	u->files = 0;
	u->tick = now;
	return true;
}

static inline void WebUpload_Alive(WebUpload *u, uint32_t now) { u->tick = now; }        // заливка идёт: стирание, пакет

static inline void WebUpload_Erased(WebUpload *u, uint32_t now) { u->phase = UPL_ACTIVE; u->tick = now; }

// Принимать ли файл: только в идущей заливке
static inline bool WebUpload_File(WebUpload *u, uint32_t now)
{
	if(u->phase != UPL_ACTIVE) return false;
	u->tick = now;
	return true;
}

// Файл принят или нет. Не принятый обрывает заливку: дальше файлы не принимаются до нового начала.
static inline void WebUpload_FileDone(WebUpload *u, bool ok)
{
	if(ok) u->files++; else u->phase = UPL_IDLE;
}

// Конец заливки или обрыв. Возвращает, шла ли она.
static inline bool WebUpload_End(WebUpload *u)
{
	bool was = u->phase == UPL_ACTIVE;
	u->phase = UPL_IDLE;
	return was;
}

#endif
