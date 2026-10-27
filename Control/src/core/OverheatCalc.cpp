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
 * Расчёт перегрева ЭРВ. Обоснование и смысл полей - OverheatCalc.h.
 *
 * Сохранены три вещи, каждая из которых выглядит недосмотром, но менять её без решения
 * владельца нельзя:
 *   1. При ошибке перегрев НЕ обнуляется - остаётся предыдущим (выставляется ERR_TYPE_OVERHEAT).
 *   2. В охлаждении наличие TCONIN, TCONOUT и TCOMPIN не проверяется: правило TEVAOUT_TEVAIN
 *      смотрит на TEVAOUT и TEVAIN, а считает по TCONIN и TCONOUT - датчикам, которых может не быть.
 *   3. Если в сборке нет ни TCONIN, ни TCOMPIN, то у правил TEVAOUT_TEVAIN и TEVAOUT_PEVA в
 *      охлаждении нет ни одной ветки присваивания, и перегрев молча остаётся прежним (возврат
 *      false из calc_*).
 *
 * Ручной перебор (OHR_MANUAL, OHR_TABLE и любое чужое число) идёт в фиксированном порядке с теми
 * же условиями; чужие ветки вызываются напрямую, минуя их проверки наличия датчиков.
 */
#include "OverheatCalc.h"

// TEVAOUT - TEVAIN в нагреве; в охлаждении вместо них конденсатор.
// Возвращает false, если в сборке нет ни TCONIN, ни TCOMPIN.
static bool calc_EvaOut_EvaIn(const OverheatIn &in, int16_t *v)
{
	if(in.cfgConIn) {
		*v = (in.heating ? in.tEvaOut - in.tEvaIn : in.tConIn - in.tConOut) + in.correction;
		return true;
	}
	if(in.cfgCompIn) {
		*v = (in.heating ? in.tEvaOut - in.tEvaIn : in.tCompIn - in.tConOut) + in.correction;
		return true;
	}
	return false;
}

// TCOMPIN - TEVAIN в нагреве, TCOMPIN - TCONOUT в охлаждении.
static int16_t calc_CompIn_EvaIn(const OverheatIn &in)
{
	return in.tCompIn - (in.heating ? in.tEvaIn : in.tConOut) + in.correction;
}

// TEVAOUT - T[PEVA] в нагреве; в охлаждении вместо TEVAOUT конденсатор.
// Возвращает false по той же причине, что и calc_EvaOut_EvaIn.
static bool calc_EvaOut_Peva(const OverheatIn &in, int16_t *v)
{
	if(in.cfgConIn) {
		*v = (in.heating ? in.tEvaOut : in.tConIn) - in.tPeva + in.correction;
		return true;
	}
	if(in.cfgCompIn) {
		*v = (in.heating ? in.tEvaOut : in.tCompIn) - in.tPeva + in.correction;
		return true;
	}
	return false;
}

// TCOMPIN - T[PEVA], режим не влияет.
static int16_t calc_CompIn_Peva(const OverheatIn &in)
{
	return in.tCompIn - in.tPeva + in.correction;
}

// Ручной перебор: первый набор датчиков, который есть, тот и считает.
// Порядок датчиков фиксирован: первый подходящий набор.
static int16_t calc_manual(const OverheatIn &in, int16_t prev)
{
	int16_t v;
	if(in.hasEvaOut && in.tPeva != OVERHEAT_NO_PRESS) {
		return calc_EvaOut_Peva(in, &v) ? v : prev;
	}
	if(in.cfgCompIn && in.hasCompIn && in.tPeva != OVERHEAT_NO_PRESS) {
		return calc_CompIn_Peva(in);
	}
	if(in.cfgCompIn && in.cfgEvaIn && in.hasCompIn) {
		return calc_CompIn_EvaIn(in);
	}
	if(in.cfgEvaIn && in.hasEvaOut) {
		return calc_EvaOut_EvaIn(in, &v) ? v : prev;
	}
	// Нужных датчиков нет: перегрев 0, но ошибка НЕ выставляется.
	return 0;
}

uint8_t Overheat_SensorsNeeded(uint8_t rule)
{
	switch(rule) {
	case OHR_TEVAOUT_PEVA:	return OHN_EVAOUT;
	case OHR_TCOMPIN_PEVA:	return OHN_COMPIN;
	case OHR_TEVAOUT_TEVAIN:return OHN_EVAOUT | OHN_EVAIN;
	case OHR_TCOMPIN_TEVAIN:return OHN_COMPIN | OHN_EVAIN;
	default:				return 0;			// таблица и ручной перебор: считаются по тому, что есть
	}
}

int16_t Overheat_Calc(const OverheatIn &in, int16_t prev, bool *error)
{
	int16_t v;
	*error = false;

	// Правило, которого в сборке нет, выходит из switch через break и попадает
	// в ручной перебор (так же, как через default).
	switch(in.rule) {
	case OHR_TEVAOUT_TEVAIN:
		if(in.cfgEvaIn) {
			if(in.hasEvaOut && in.hasEvaIn) return calc_EvaOut_EvaIn(in, &v) ? v : prev;
			*error = true;
			return prev;
		}
		break;

	case OHR_TCOMPIN_TEVAIN:
		if(in.cfgEvaIn && in.cfgCompIn) {
			if(in.hasCompIn && in.hasEvaIn) return calc_CompIn_EvaIn(in);
			*error = true;
			return prev;
		}
		break;

	case OHR_TEVAOUT_PEVA:		// это правило есть в любой сборке
		if(in.hasEvaOut && in.tPeva != OVERHEAT_NO_PRESS) return calc_EvaOut_Peva(in, &v) ? v : prev;
		*error = true;
		return prev;

	case OHR_TCOMPIN_PEVA:
		if(in.cfgCompIn) {
			if(in.hasCompIn && in.tPeva != OVERHEAT_NO_PRESS) return calc_CompIn_Peva(in);
			*error = true;
			return prev;
		}
		break;

	case OHR_TABLE:		// таблица не реализована - считается как ручной перебор
	case OHR_MANUAL:
	default:
		break;
	}
	return calc_manual(in, prev);
}
