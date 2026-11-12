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

#ifndef SUPERHEAT_WARNING_H
#define SUPERHEAT_WARNING_H

#include <stdint.h>

enum class SuperheatEvent : uint8_t {
	None, // Нового предупреждения нет.
	Low,  // Подтверждён низкий перегрев.
	High  // Подтверждён высокий перегрев.
};

struct SuperheatLimits {
	int low;                  // Нижний порог, сотые кельвина.
	int high;                 // Верхний порог, сотые кельвина.
	int hysteresis;           // Отступ от порога для восстановления, сотые кельвина.
	uint32_t lowConfirmMs;    // Выдержка нижнего порога.
	uint32_t highConfirmMs;   // Выдержка верхнего порога.
	uint32_t stabilizeMs;     // Задержка после пуска и смены режима.
	uint32_t maxSampleGapMs;  // Наибольший допустимый интервал между измерениями.
};

class SuperheatWarning {
public:
	// Проверяет перегрев value по limits в момент now. start и mode задают участок работы.
	// mode == 0 означает останов или неподдерживаемый режим, valid подтверждает свежий замер.
	// Возвращает предупреждение только при подтверждении нового эпизода.
	SuperheatEvent Update(const SuperheatLimits &limits, uint32_t now, uint32_t start,
	                      uint8_t mode, bool valid, int value)
	{
		if(mode != workMode || start != compressorStart) {
			workMode = mode;
			compressorStart = start;
			modeSince = now;
			stabilized = false;
			pending = active = SuperheatEvent::None;
		}
		if(!mode) return SuperheatEvent::None;
		if(!stabilized) {
			if(now - modeSince < limits.stabilizeMs) return SuperheatEvent::None;
			stabilized = true;
		}
		if(!valid || now - lastSample > limits.maxSampleGapMs) pending = SuperheatEvent::None;
		if(!valid) return SuperheatEvent::None;
		lastSample = now;

		if((active == SuperheatEvent::Low && value >= limits.low + limits.hysteresis) ||
		   (active == SuperheatEvent::High && value <= limits.high - limits.hysteresis))
			active = SuperheatEvent::None;
		const auto side = value <= limits.low ? SuperheatEvent::Low :
		                  value >= limits.high ? SuperheatEvent::High : SuperheatEvent::None;
		if(side == SuperheatEvent::None || side == active) {
			pending = SuperheatEvent::None;
			return SuperheatEvent::None;
		}
		if(pending != side) {
			pending = side;
			violationSince = now;
		}
		const uint32_t confirm = side == SuperheatEvent::Low ? limits.lowConfirmMs : limits.highConfirmMs;
		if(now - violationSince < confirm) return SuperheatEvent::None;
		active = side;
		pending = SuperheatEvent::None;
		return active;
	}

private:
	uint32_t compressorStart = 0; // Метка текущего пуска компрессора.
	uint32_t modeSince = 0;       // Начало работы в текущем режиме.
	uint32_t lastSample = 0;      // Время последнего достоверного измерения.
	uint32_t violationSince = 0;  // Начало подтверждения нарушения.
	uint8_t workMode = 0;         // Режим участка работы, ноль вне контроля.
	bool stabilized = false;     // Выдержка стабилизации завершена.
	SuperheatEvent pending = SuperheatEvent::None; // Граница на подтверждении.
	SuperheatEvent active = SuperheatEvent::None;  // Уже зарегистрированная граница.
};

#endif
