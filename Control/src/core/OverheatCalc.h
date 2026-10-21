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
 * Расчёт перегрева ЭРВ - чистая арифметика, без HP и без Arduino.
 *
 * ЗАЧЕМ ОТДЕЛЬНО. Пока формула перемешана с чтением датчиков (HP.sTemp[...]) и условной
 * компиляцией, проверить её нечем. Ошибка здесь тихая: ЭРВ едет не на ту позицию, а видно это
 * только на работающем ТН с датчиками давления, которых на стенде нет.
 *
 * Модуль не видит Config.h, поэтому то, что решал препроцессор, приходит полями структуры:
 *   cfg* - есть ли датчик В СБОРКЕ (TEVAIN / TCOMPIN / TCONIN);
 *   has* - есть ли он в текущей конфигурации (get_present()).
 * Исходы разные: при cfg=false ветки правила нет, и управление уходит в default (ручной
 * перебор), при has=false ветка есть и выдаёт ошибку. TEVAOUT обязателен, флага для него нет.
 *
 * НОМЕРА ПРАВИЛ. Номера RULE_EEV из Constant.h зависят от конфигурации (значения появляются
 * только при своих #ifdef), поэтому здесь нумерация постоянная, а перевод делает связка в
 * EEV.cpp явным switch. OverheatTCOMP остался в связке: он считается одной строкой и нужен
 * в DEMO. ЕДИНИЦЫ: температуры и поправка - сотые градуса, как в sensorTemp.
 */
#ifndef OverheatCalc_h
#define OverheatCalc_h

#include <stdint.h>

// Признак "датчика давления испарителя нет"; он же сторож вокруг PressToTemp(PEVA).
#define OVERHEAT_NO_PRESS ((int16_t)-32767)

// Правила расчёта перегрева с ПОСТОЯННОЙ нумерацией - в отличие от RULE_EEV.
// Порядок повторяет RULE_EEV в сборке, где определены все датчики.
enum OverheatRule
{
	OHR_TEVAOUT_PEVA = 0,	// TEVAOUT - T[PEVA]
	OHR_TCOMPIN_PEVA,		// TCOMPIN - T[PEVA]
	OHR_TEVAOUT_TEVAIN,		// TEVAOUT - TEVAIN
	OHR_TCOMPIN_TEVAIN,		// TCOMPIN - TEVAIN
	OHR_TABLE,				// таблица - не реализована, считается как ручной перебор
	OHR_MANUAL				// ручной перебор доступных датчиков
};

struct OverheatIn
{
	uint8_t rule;			// OverheatRule; чужое число уходит в ручной перебор
	bool    heating;		// true - нагрев, false - охлаждение
	int16_t correction;		// поправка, сотые градуса

	// Что есть в сборке (по #ifdef из Config.h)
	bool cfgEvaIn;			// TEVAIN
	bool cfgCompIn;			// TCOMPIN
	bool cfgConIn;			// TCONIN

	// Что есть в текущей конфигурации (get_present())
	bool hasEvaOut;
	bool hasEvaIn;
	bool hasCompIn;

	// Температуры, сотые градуса
	int16_t tEvaOut;
	int16_t tEvaIn;
	int16_t tConIn;
	int16_t tConOut;
	int16_t tCompIn;
	int16_t tPeva;			// OVERHEAT_NO_PRESS, если датчика давления нет
};

// Считает перегрев. prev - предыдущее значение: при ошибке и в случае, когда
// формула выбранного правила в сборке отсутствует, перегрев НЕ меняется.
// error - признак ERR_TYPE_OVERHEAT, ошибку регистрирует вызывающий.
int16_t Overheat_Calc(const OverheatIn &in, int16_t prev, bool *error);

#endif
