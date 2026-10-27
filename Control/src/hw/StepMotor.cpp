/*
 * Copyright (c) 2016-2026 by Pavel Panfilov <firstlast2007@gmail.com>  telegram @pav20000
 * &                       by Vadim Kulakov vad7@yahoo.com, vad711
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
#include "StepMotor.h"

void StepMotor::initStepMotor(int number_of_steps, int motor_pin_1, int motor_pin_2, int motor_pin_3, int motor_pin_4,
                              uint8_t phase_variant, bool invert)
{
	this->number_of_steps = number_of_steps; // total number of steps for this motor
	this->pin_count = 4;
	this->phase_variant = phase_variant;       // из Config.h через вызывающего
	this->invert = invert;                     // инверсия выходов драйвера L9333
	busy = false;                              // мотор не двигается

	// Arduino pins for the motor control connection:
	this->motor_pin_1 = motor_pin_1;
	this->motor_pin_2 = motor_pin_2;
	this->motor_pin_3 = motor_pin_3;
	this->motor_pin_4 = motor_pin_4;
	// setup the pins on the microcontroller:
	pinMode(this->motor_pin_1, OUTPUT);
	pinMode(this->motor_pin_2, OUTPUT);
	pinMode(this->motor_pin_3, OUTPUT);
	pinMode(this->motor_pin_4, OUTPUT);
	off();                     // Снять напряжение
}


// Установить скорость поступления шагов
void StepMotor::setSpeed(long whatSpeed)
{
	step_delay = 1000 / whatSpeed; // пересчет на время одного шага в мсек
}


// Движение до steps_to_move 
// На входе АБСОЛЮТНАЯ координата, в очередь уходит АБСОЛЮТНАЯ координата
// false - очередь команд полна, движение не начато. Сообщение в журнал
// печатает вызывающий: журнала модуль не знает и знать не должен.
bool StepMotor::step(int steps_to_move)
{
	// busy ставится ДО постановки в очередь: задача шагового двигателя приоритетом выше и может взять команду и закончить её раньше, чем
	// вызывающий вернётся, - флаг, поставленный после, оставался бы поднятым навсегда (compressorON ждёт его до 25,5 с).
	busy = true;                      // флаг начало движения
	if(xQueueSend(xCommandQueue, &steps_to_move, 20 / portTICK_PERIOD_MS) == errQUEUE_FULL) // команду на движение в очередь
	{
		busy = false;                 // не поставлена - движения не будет
		return false;
	}
	if(xHandleStepperEEV != NULL) xTaskNotifyGive(xHandleStepperEEV);   // Разбудить задачу: уведомление не теряется, даже если она ещё не ждёт
	return true;
}

// выставить один пин
// Инверсия - поле объекта, а не #ifdef: DRV_EEV_L9333 живёт в Config.h,
// которого эта единица трансляции не видит.
__attribute__((always_inline)) inline void StepMotor::setPinMotor(int pin, boolean val)
{
	digitalWriteDirect(pin, invert ? !val : val);
}

// Выставить все четыре обмотки по маске: бит 3 - обмотка 1, бит 0 - обмотка 4.
// Единственное место, где номера пинов сопоставляются битам маски, - и фаза,
// и снятие напряжения идут через него.
__attribute__((always_inline)) inline void StepMotor::applyMask(uint8_t mask)
{
	setPinMotor(motor_pin_1, mask & 0x8);
	setPinMotor(motor_pin_2, mask & 0x4);
	setPinMotor(motor_pin_3, mask & 0x2);
	setPinMotor(motor_pin_4, mask & 0x1);
}
/*
 * Движение на один шаг  в зависимости от выбранной последовательности
 */
// Выставить обмотки по номеру фазы.
// Сама последовательность живёт в модуле StepMotorPhase.cpp и проверяется
// тестами.
//
// Вариант последовательности - поле объекта: EEV_PHASE приходит из Config.h,
// которого эта единица трансляции не видит, и задаётся в initStepMotor.
void StepMotor::stepOne(int thisStep)
{
	uint8_t m = StepMotor_PhaseMask(phase_variant, (uint8_t)thisStep);
	// При номере фазы вне диапазона пины не трогаются вовсе: обесточивать обмотки
	// нельзя, потеряется удержание и положение ЭРВ.
	if(m == SM_PHASE_INVALID) return;
	applyMask(m);
}



// Снять напряжения с шаговика
// Снять напряжение со всех обмоток - это просто пустая маска.
void StepMotor::off()
{
	applyMask(0);
}
              
