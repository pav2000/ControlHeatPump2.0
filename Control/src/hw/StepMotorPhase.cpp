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
 * Таблицы фаз шагового двигателя ЭРВ. Обоснование и интерфейс - StepMotorPhase.h.
 */
#include "StepMotorPhase.h"

// Биты: 3 - обмотка 1, 2 - обмотка 2, 1 - обмотка 3, 0 - обмотка 4.
// В каждой последовательности соседние фазы отличаются ровно одной обмоткой
// (для PHASE_4 - двумя, там всегда две обмотки под током), а последняя
// смыкается с первой. Это проверяется тестом.

// 8 фаз, полушаг со сдвигом - основной вариант, стоит в Config.h
static const uint8_t phase8s[8] = {
	0x1,  // 0001
	0x9,  // 1001
	0x8,  // 1000
	0xC,  // 1100
	0x4,  // 0100
	0x6,  // 0110
	0x2,  // 0010
	0x3,  // 0011
};

// 8 фаз, полушаг - та же последовательность, начатая с другой фазы
static const uint8_t phase8[8] = {
	0x8,  // 1000
	0xC,  // 1100
	0x4,  // 0100
	0x6,  // 0110
	0x2,  // 0010
	0x3,  // 0011
	0x1,  // 0001
	0x9,  // 1001
};

// 4 фазы, полный шаг - две обмотки всегда под током, момент выше
static const uint8_t phase4[4] = {
	0x9,  // 1001
	0x3,  // 0011
	0x6,  // 0110
	0xC,  // 1100
};

uint8_t StepMotor_PhaseCount(uint8_t variant)
{
	switch(variant) {
	case PHASE_4:  return 4;
	case PHASE_8:  return 8;
	case PHASE_8s: return 8;
	}
	return 0;
}

uint8_t StepMotor_PhaseMask(uint8_t variant, uint8_t step)
{
	switch(variant) {
	case PHASE_4:  if(step < 4) return phase4[step];  break;
	case PHASE_8:  if(step < 8) return phase8[step];  break;
	case PHASE_8s: if(step < 8) return phase8s[step]; break;
	}
	return SM_PHASE_INVALID;
}
