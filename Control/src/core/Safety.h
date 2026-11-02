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
 * Защиты выходов (раздел 6.2): чистая логика без железа (tests/test_safety.cpp).
 *
 * СОСТОЯНИЕ РЕЛЕ. Реле включает каждый источник своим битом: основной алгоритм и ручное
 * управление из API. Реле включено, если включено хоть одним. Выключение алгоритмом
 * снимает только его бит, ручное включение остаётся. Поэтому аварийный останов и общее
 * выключение снимают все биты (fR_StatusAllOff): иначе включённый вручную компрессор
 * переживал set_Error.
 *
 * МЕЖБЛОКИРОВКИ. Safety_Allow решает, можно ли переключить выход при текущем состоянии ТН.
 * Правила (решение владельца 30.10):
 *   1. компрессор включается только при обоих насосах;
 *   2. насос не выключается под работающим компрессором и раньше выбега после его останова
 *      (входной - DELAY_BEFORE_STOP_IN_PUMP, выходной - Option.delayOffPump: те же величины,
 *      что выдерживает останов, иначе штатный останов попадал бы под запрет);
 *   3. ТЭН отопления включается только при насосе выходного контура;
 *   4. 4-ходовой клапан не переключается при работающем компрессоре.
 * Разморозка снимает правила 1 и 2: при оттайке горячим газом компрессор работает без насоса
 * выходного контура, при оттайке паром - и без вентилятора.
 *
 * ЭТАП НАБЛЮДЕНИЯ. Пока отказ только пишется в журнал, переключение выполняется. Запрет включается,
 * когда журнал покажет, что штатная работа правил не нарушает.
 */
#ifndef Safety_h
#define Safety_h

#include <stdint.h>

#define fR_StatusMain    1      // бит: включено основным алгоритмом
#define fR_StatusManual  3      // бит: включено вручную (API)
#define fR_StatusMask    ((1 << fR_StatusMain) | (1 << fR_StatusManual))
#define fR_StatusAllOff  -127   // выключить по всем источникам

// Флаги реле после команды r: fR_Status* - включить этим источником, -fR_Status* - выключить им,
// 0 - выключить алгоритмом, fR_StatusAllOff - выключить всеми. Остальные биты флагов не трогаются.
uint8_t Relay_ApplyStatus(uint8_t flags, int8_t r);

static_assert(fR_StatusMask < 0x100, "биты состояния реле лежат в байте флагов");

// Выход с точки зрения защит
enum SAFETY_OUT : uint8_t { SAFE_OTHER, SAFE_COMP, SAFE_PUMP_IN, SAFE_PUMP_OUT, SAFE_HEATER, SAFE_4WAY };

// Причина отказа, 0 - разрешено
enum SAFETY_WHY : uint8_t { SAFETY_OK, SAFETY_NO_PUMPS, SAFETY_COMP_ON, SAFETY_OVERRUN, SAFETY_NO_FLOW };

struct SafetyState {
	bool     comp_on;            // компрессор работает (реле или инвертор)
	uint32_t since_comp_off;     // секунд с останова компрессора; останова не было - больше любого выбега
	bool     pump_in, pump_out;  // насосы входного и выходного контура включены
	bool     defrost;            // идёт разморозка
	uint16_t overrun_in;         // выбег насоса входного контура после компрессора, с
	uint16_t overrun_out;        // выбег насоса выходного контура, с
};

// Можно ли переключить выход out в состояние on. Зовётся только при смене состояния.
SAFETY_WHY  Safety_Allow(SAFETY_OUT out, bool on, const SafetyState &s);
const char *Safety_WhyText(SAFETY_WHY w);

#endif
