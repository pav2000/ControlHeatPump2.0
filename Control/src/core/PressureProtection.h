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

#ifndef PRESSURE_PROTECTION_H
#define PRESSURE_PROTECTION_H

#include <stdint.h>

class PressureProtection {
public:
	// Проверяет низкое давление pressure в сотых бара по свежим отсчётам valid.
	// mode и start задают участок работы, нулевой mode сбрасывает его.
	// Возвращает true один раз при подтверждении нарушения после стабилизации.
	bool ObserveLow(uint32_t now, uint8_t mode, uint32_t start, bool valid, int pressure,
	                int trip, int recover, uint32_t stabilizeMs, uint32_t confirmMs, uint32_t maxGapMs)
	{
		if(mode != lowMode || start != lowStart) {
			lowMode = mode;
			lowStart = start;
			lowSince = now;
			low = {};
			lowReady = false;
		}
		if(!mode) return false;
		if(!lowReady) {
			if(now - lowSince < stabilizeMs) return false;
			lowReady = true;
		}
		return low.Update(now, valid, pressure <= trip, pressure >= recover, confirmMs, maxGapMs);
	}

	// Принимает PCON pressure и valid в момент now, в том числе на остановленном ТН.
	// running разрешает подтверждать аварию. Возвращает true при новом срабатывании.
	bool ObserveHigh(uint32_t now, bool running, bool valid, int pressure,
	                 int trip, int recover, uint32_t confirmMs, uint32_t maxGapMs)
	{
		highValid = valid;
		highPressure = pressure;
		highAt = now;
		if(!running) high.pending = false;
		return high.Update(now, valid, running && pressure >= trip, pressure < recover, confirmMs, maxGapMs);
	}

	// Возвращает допуск пуска на момент now. applicable означает режим контроля PCON.
	// trip — верхний порог, maxAgeMs — срок годности последнего измерения.
	bool CanStart(uint32_t now, bool applicable, int trip, uint32_t maxAgeMs) const
	{
		if(high.reported) return false;
		return !applicable || (highValid && now - highAt <= maxAgeMs && highPressure < trip);
	}

private:
	struct LimitState {
		uint32_t since = 0;  // Начало непрерывного нарушения.
		uint32_t last = 0;   // Время последнего достоверного отсчёта.
		bool pending = false;  // Идёт подтверждение нарушения.
		bool reported = false; // Авария зарегистрирована, восстановления ещё не было.

		// Возвращает true при новом нарушении outside по достоверным отсчётам valid.
		// recovered снимает зарегистрированное состояние. Время и интервалы в миллисекундах.
		bool Update(uint32_t now, bool valid, bool outside, bool recovered,
		            uint32_t confirmMs, uint32_t maxGapMs)
		{
			if(!valid || now - last > maxGapMs) pending = false;
			if(!valid) return false;
			last = now;
			if(recovered) reported = false;
			if(!outside || reported) {
				pending = false;
				return false;
			}
			if(!pending) {
				pending = true;
				since = now;
			}
			if(now - since < confirmMs) return false;
			reported = true;
			pending = false;
			return true;
		}
	};

	LimitState low;  // Подтверждение нижней границы и восстановление.
	LimitState high; // Подтверждение верхней границы и блокировка пуска.
	uint32_t lowStart = 0; // Метка пуска компрессора для нижней защиты.
	uint32_t lowSince = 0; // Начало стабилизации в текущем режиме.
	uint8_t lowMode = 0;   // Режим нижней защиты, ноль вне контроля.
	bool lowReady = false; // Выдержка стабилизации пройдена.
	uint32_t highAt = 0;   // Время последнего опроса PCON.
	int highPressure = 0;  // Последний отсчёт PCON в сотых бара.
	bool highValid = false; // Последний отсчёт свежий и пригоден для допуска пуска.
};

#endif
