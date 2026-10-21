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
 * Последовательности фаз шагового двигателя ЭРВ.
 *
 * Таблица данных вместо switch по фазам: отдельная единица трансляции, о железе ничего
 * не знает, возвращает маску обмоток числом. Последовательность фаз проверяется
 * тестами (tests/test_stepmotor.cpp) - для ЭРВ это не
 * косметика, неверная последовательность означает потерянные шаги и уход
 * перегрева.
 *
 * ВЫБОР ВАРИАНТА (EEV_PHASE) сюда не переносится: он приходит из Config.h,
 * которого модуль не видит, и #ifdef здесь молча выбрал бы не ту ветку.
 * Вариант передаётся параметром через StepMotor::initStepMotor. Так уже ломались
 * Freon на R4WAY и DateTime на SENSOR_IP.
 */
#ifndef StepMotorPhase_h
#define StepMotorPhase_h

#include <stdint.h>

// Варианты последовательности. Значения - часть внешнего контракта: их
// использует Config.h (#define EEV_PHASE PHASE_8s).
#define     PHASE_4       0        // 4 фазы, полный шаг, две обмотки всегда под током
#define     PHASE_8       1        // 8 фаз, полушаг
#define     PHASE_8s      2        // 8 фаз, полушаг со сдвигом - основной вариант (pav2000)

#define     SM_PHASE_INVALID  0xFF // возврат при неверном варианте или номере фазы

// Число фаз в последовательности: 4 или 8. Ноль при неверном варианте.
uint8_t StepMotor_PhaseCount(uint8_t variant);

// Маска обмоток для фазы step. Биты СТАРШИЕ->МЛАДШИЕ соответствуют обмоткам
// 1..4: бит 3 - обмотка 1, бит 0 - обмотка 4. Так же, как читались
// комментарии в прежнем switch: 0b1001 = обмотки 1 и 4 под током.
// SM_PHASE_INVALID, если вариант или номер фазы вне диапазона - вызывающий
// в этом случае обязан НЕ трогать пины.
uint8_t StepMotor_PhaseMask(uint8_t variant, uint8_t step);

#endif
