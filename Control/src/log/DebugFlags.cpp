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
 * Флаги отладки. Обоснование и формат хранения - DebugFlags.h.
 */
#include "DebugFlags.h"
#include <string.h>

// Группы страницы "Отладка".
#define G_ALG  "Алгоритм"
#define G_HW   "Оборудование"
#define G_NET  "Сеть"
#define G_RED  "Сокращение вывода"

const DbgFlagInfo DbgTable[] = {
	{ "MODW",  G_ALG, "Ход алгоритма [EvaluateWorkMode]",                              DBG_MODW,  1 },
	{ "FRED",  G_ALG, "Причины снижения частоты [Reduced FC]",                         DBG_FRED,  1 },
	{ "DUMP",  G_ALG, "Строка состояния ТН [modWork]",                                 DBG_DUMP,  0 },
	{ "FSTEP", G_ALG, "Целевая частота по шагам [FC>]",                                DBG_FSTEP, 0 },
	{ "PID",   G_ALG, "Разбор ПИД частоты [PID(]",                                     DBG_PID,   0 },
	{ "EEV",   G_ALG, "Шаги ЭРВ и коррекция перегрева [EEV:, OHCor]",                  DBG_EEV,   0 },
	{ "FCW",   G_HW,  "Параметры инвертора на каждый опрос [FC:]",                     DBG_FCW,   0 },
	{ "SDM",   G_HW,  "Повторные чтения счётчика SDM [Read #]",                        DBG_SDM,   0 },
	{ "NXT",   G_HW,  "Обмен с дисплеем Nextion [NXTTX:]",                             DBG_NXT,   0 },
	{ "WERR",  G_NET, "Ошибки веб-запросов [WEB:Wrong request, $Error send]",                       DBG_WERR,  0 },
	{ "WFULL", G_NET, "Детальный лог сетевых сервисов [Send request, Response]",       DBG_WFULL, 0 },
	{ "HTTP",  G_NET, "Тела запросов и ответов веб-сервера [$QUERY, $INPUT, $RETURN]", DBG_HTTP,  0 },
	{ "PHY",   G_NET, "Подробности сетевого чипа [Speed Status, DHCP use]",            DBG_PHY,   0 },
	{ "PING",  G_RED, "Успешные пинги [Ping]",                                         DBG_PING,  0 },
	{ "MON",   G_RED, "Успешные отправки на мониторинг и в MQTT [>> сервер OK]",       DBG_MON,   0 },
	{ "RELP",  G_RED, "Плановые переключения насосов [Relay RPUMPB, RPUMPO]",          DBG_RELP,  0 },
};
const uint8_t DbgCount = sizeof(DbgTable) / sizeof(DbgTable[0]);

// Значение до первой загрузки настроек: те же, что по умолчанию.
#define DBG_DEFAULT_MASK  (DBG_MODW | DBG_FRED)
volatile uint32_t dbgFlags = DBG_DEFAULT_MASK;

uint32_t Dbg_Defaults(void)
{
	uint32_t m = 0;
	for(uint8_t i = 0; i < DbgCount; i++) if(DbgTable[i].dflt) m |= DbgTable[i].bit;
	return m;
}

uint32_t Dbg_KnownMask(void)
{
	uint32_t m = 0;
	for(uint8_t i = 0; i < DbgCount; i++) m |= DbgTable[i].bit;
	return m;
}

bool Dbg_Unpack(uint16_t dbg, uint16_t dbg2, uint32_t *mask)
{
	uint32_t def = Dbg_Defaults();
	if((dbg2 >> 8) != DBG_SIG) { *mask = def; return false; }
	// Берутся только известные биты: лишнее в настройках (запись более новой прошивки) игнорируется.
	*mask = (((uint32_t)(dbg2 & 0xFF) << 16) | dbg) & Dbg_KnownMask();
	return true;
}

void Dbg_Pack(uint32_t mask, uint16_t *dbg, uint16_t *dbg2)
{
	mask &= Dbg_KnownMask();
	*dbg  = (uint16_t)(mask & 0xFFFF);
	*dbg2 = (uint16_t)((DBG_SIG << 8) | ((mask >> 16) & 0xFF));
}

int Dbg_Find(const char *name)
{
	for(uint8_t i = 0; i < DbgCount; i++) if(strcmp(DbgTable[i].name, name) == 0) return i;
	return -1;
}

bool Dbg_Set(uint32_t *mask, const char *name, int val)
{
	int i = Dbg_Find(name);
	if(i < 0) return false;
	if(val) *mask |= DbgTable[i].bit; else *mask &= ~DbgTable[i].bit;
	return true;
}
