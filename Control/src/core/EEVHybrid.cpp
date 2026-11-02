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

// Алгоритм «Гибрид» регулирования перегрева ЭРВ. Правило и единицы - в EEVHybrid.h. Модуль ничего не знает ни о HP,
// ни об Arduino: тест и симулятор линкуются с этим файлом без заглушек.
#include "EEVHybrid.h"

// Деление с округлением к ближайшему (половина - от нуля); b > 0
static int64_t div_round(int64_t a, int64_t b)
{
	return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

static int32_t limit(int64_t v, int32_t lim)
{
	return v > lim ? lim : v < -lim ? -lim : (int32_t) v;
}

void eevhyb_reset(EevHybridState *st, const EevHybridParams *p, int16_t overheat100, int16_t freq100)
{
	st->acc1000 = 0;
	st->sPrev100 = (int32_t) overheat100 - p->target100;
	st->fPrev100 = freq100;
}

int16_t eevhyb_step(EevHybridState *st, const EevHybridParams *p, int16_t overheat100, int16_t freq100, int16_t x, uint32_t secSinceStart)
{
	const int32_t stepMax = p->stepMax > 0 ? p->stepMax : EEVHYB_STEP_MAX_DEFAULT;
	const int32_t ffMax = p->ffMax > 0 ? p->ffMax : 0;
	const int32_t Ti = p->Ti < 1 ? 1 : p->Ti;
	const int32_t s100 = (int32_t) overheat100 - p->target100;			// >0: перегрев выше цели, вентиль открывать
	const int32_t ds100 = s100 - st->sPrev100;

	// Скоростной ПИ: Kc * (ds + период/Ti * s) шагов, в тысячных: Kc100/100 * (.../100) * 1000 = Kc100 * (...) / 10
	int32_t dx = limit(div_round((int64_t) p->Kc100 * ((int64_t) ds100 * Ti + (int64_t) p->period * s100), (int64_t) Ti * 10), stepMax * 1000);

	// Прямая подача вслед за частотой: kff * x * (f/f_пред - 1), в тысячных: kff100/100 * x * (f - f_пред)/f_пред * 1000
	if(p->kff100 > 0 && ffMax > 0 && st->fPrev100 > EEVHYB_FREQ_MIN && freq100 > EEVHYB_FREQ_MIN)
		dx += limit(div_round((int64_t) p->kff100 * x * ((int32_t) freq100 - st->fPrev100) * 10, st->fPrev100), ffMax * 1000);

	// Защита от залива: ниже ohMin вентиль прикрывается на 1 шаг и ещё на 1 шаг за каждые 0,5 K недобора
	if(p->ohMin100 > 0 && overheat100 < p->ohMin100 && secSinceStart >= p->protDelay)
		dx -= 1000 + 20 * ((int32_t) p->ohMin100 - overheat100);

	dx = limit(dx, (stepMax + ffMax + 5) * 1000);						// итог за период ограничен, что бы ни пришло в настройках

	st->acc1000 += dx;
	const int32_t step = st->acc1000 / 1000;							// усечение к нулю: дробная часть остаётся в накопителе
	st->acc1000 -= step * 1000;
	st->sPrev100 = s100;
	st->fPrev100 = freq100;

	if(p->maxSteps < p->minSteps) return x;
	int32_t n = (int32_t) x + step;
	return (int16_t) (n < p->minSteps ? p->minSteps : n > p->maxSteps ? p->maxSteps : n);
}

// ---------------------------------------------------------------------------
// Настройки гибрида и стартовая позиция по частоте
// ---------------------------------------------------------------------------
static int32_t scaled(int32_t base, int16_t maxSteps, int32_t lo, int32_t hi)
{
	int32_t v = (base * maxSteps + 240) / 480;					// округление к ближайшему
	return v < lo ? lo : v > hi ? hi : v;
}

void eevhyb_default_cfg(EevHybCfg *c, int16_t maxSteps)
{
	if(maxSteps <= 0) maxSteps = 480;
	c->Kc100 = (int16_t) scaled(300, maxSteps, 10, 1000);
	c->Ti = 75;
	c->ohMin100 = 100;
	c->kff100 = 100;
	c->ffMax = (int16_t) scaled(40, maxSteps, 1, 200);
	c->sphz100 = (int16_t) scaled(220, maxSteps, 1, 3000);
	c->sphzb100 = (int16_t) scaled(180, maxSteps, 1, 3000);
}

int16_t eevhyb_start_pos(int16_t sphz100, int16_t freq100, int16_t minSteps, int16_t maxSteps)
{
	if(maxSteps < minSteps) return minSteps;
	int32_t v = (int32_t) ((div_round((int64_t) sphz100 * freq100, 10000)));
	return (int16_t) (v < minSteps ? minSteps : v > maxSteps ? maxSteps : v);
}

uint16_t eevhyb_migrate_flags(uint8_t savedVer, uint16_t flags)
{
	if(savedVer >= EEV_SETTINGS_VER_HYBRID) return flags;
	return flags & ~((1u << EEV_FLAGBIT_DIRECT_OLD) | (1u << EEV_FLAGBIT_HYBRID) | (1u << EEV_FLAGBIT_STARTFREQ));
}

bool eevhyb_cfg_needs_defaults(uint8_t savedVer)
{
	return savedVer < EEV_SETTINGS_VER_HYBRID;
}
