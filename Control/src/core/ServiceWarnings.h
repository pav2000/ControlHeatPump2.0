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

#ifndef SERVICE_WARNINGS_H
#define SERVICE_WARNINGS_H

#include <stdint.h>

// Итог одного топика или полного цикла MQTT.
enum class MqttResult : uint8_t {
	Ok,          // Все запрошенные публикации прошли.
	Connect,     // Сеть недоступна или соединение отклонено.
	Publish,     // Брокеру не удалось отправить топик.
	Skipped      // Отправка не началась из-за локальной занятости или подготовки данных.
};

struct MqttFailureSeries {
	uint16_t failures = 0; // Последовательные неудачные циклы до отключения.
	bool warned = false;   // Предупреждение серии уже зарегистрировано.

	// Принимает итог цикла и лимит, возвращает true при необходимости отключить отправку.
	bool Update(MqttResult result, uint16_t limit)
	{
		if(result == MqttResult::Ok) {
			failures = 0;
			warned = false;
			return false;
		}
		if(result == MqttResult::Skipped) return false;
		if(++failures < limit) return false;
		failures = 0;
		return true;
	}
};

struct NotifyFailureSeries {
	uint8_t warned = 0; // Каналы, по которым уже зарегистрирован окончательный отказ.

	// Обновляет серию по окончательному исходу канала ch, возвращает true при новом отказе.
	bool Update(uint8_t ch, bool ok)
	{
		if(ok) {
			warned &= ~ch;
			return false;
		}
		if(warned & ch) return false;
		warned |= ch;
		return true;
	}
};

struct TimedWarning {
	uint32_t since = 0;   // Начало непрерывного нарушения или восстановления, с.
	bool pending = false; // Выдержка началась.
	bool active = false;  // Предупреждение зарегистрировано.

	// Проверяет нарушение bad и восстановление good в момент now.
	// Выдержки в секундах. true означает новое предупреждение.
	bool Update(uint32_t now, bool bad, bool good, uint32_t tripS, uint32_t recoverS)
	{
		if(!(active ? good : bad)) {
			pending = false;
			return false;
		}
		if(!pending) {
			since = now;
			pending = true;
		}
		if(now - since < (active ? recoverS : tripS)) return false;
		pending = false;
		active = !active;
		return active;
	}
};

struct SpiWarningSample {
	uint32_t counts[3];   // Таймауты шины, таймауты DMA, переполнения DMA.
	uint32_t generation;  // Поколение счётчиков после ручного сброса.
};

class SpiWarning {
public:
	// Закрывает окно по снимку sample в момент now. windowS задаёт длину окна.
	// limits задают пределы трёх счётчиков, windows — выдержку в окнах.
	// Возвращает маску причин нового предупреждения, иначе ноль.
	uint8_t Update(uint32_t now, const SpiWarningSample &sample, uint32_t windowS,
	               const unsigned (&limits)[3], unsigned windows)
	{
		if(!ready || sample.generation != base.generation || now - since >= 2 * windowS) {
			ready = true;
			base = sample;
			since = now;
			for(auto &n : streak) n = 0;
			return 0;
		}
		if(now - since < windowS) return 0;
		uint8_t reasons = 0;
		bool bad = false;
		for(unsigned i = 0; i < 3; ++i) {
			const bool exceeded = sample.counts[i] - base.counts[i] >= limits[i];
			bad |= exceeded;
			if(exceeded) {
				if(streak[i] < windows) ++streak[i];
				if(streak[i] >= windows) reasons |= 1 << i;
			} else streak[i] = 0;
		}
		base = sample;
		since = now;
		if(!bad) active = false;
		if(active || !reasons) return 0;
		active = true;
		return reasons;
	}

private:
	SpiWarningSample base{}; // Снимок на начало текущего окна.
	unsigned streak[3]{};    // Последовательные плохие окна по каждому типу.
	uint32_t since = 0;      // Начало текущего окна, с.
	bool ready = false;      // Начальный снимок получен.
	bool active = false;     // Предупреждение серии уже зарегистрировано.
};

// Проверяет календарную дату в диапазоне внешнего RTC. Возвращает true для допустимой даты.
inline bool ServiceDateValid(int day, int month, int year)
{
	static constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; // Длины месяцев.
	return year >= 2000 && year <= 2099 && month >= 1 && month <= 12 && day >= 1 &&
	       day <= days[month - 1] + (month == 2 && year % 4 == 0);
}

class ServiceClockTrust {
public:
	// Подтверждает установку часов в момент now по проверенному источнику.
	void Confirm(uint32_t now)
	{
		confirmed = true;
		confirmedAt = progressAt = now;
		manualFields = 0;
	}

	// Учитывает ручную установку даты или времени. Возвращает итог достоверности часов.
	bool Manual(bool date, uint32_t now)
	{
		manualFields |= date ? 1 : 2;
		if(manualFields == 3) Confirm(now);
		return confirmed;
	}

	// Проверяет календарь valid и ход часов wall в момент now, возвращает достоверность.
	bool Trusted(uint32_t now, uint32_t wall, bool valid)
	{
		if(wall != lastWall || now == confirmedAt) progressAt = now;
		lastWall = wall;
		if(!valid || now - progressAt >= maxStallS) {
			if(confirmed) manualFields = 0;
			confirmed = false;
		}
		return confirmed;
	}

private:
	static constexpr uint32_t maxStallS = 5; // Остановка секундного счётчика при опросе раз в секунду.
	uint32_t confirmedAt = 0; // Монотонное время подтверждения источника.
	uint32_t progressAt = 0;  // Момент последнего изменения часов.
	uint32_t lastWall = 0;    // Последнее прочитанное настенное время.
	uint8_t manualFields = 0; // Вручную установленные дата и время.
	bool confirmed = false;  // Время подтверждено и часы продолжают идти.
};

// Выполняет один опрос предупреждений времени, памяти и SPI/DMA из сервисной задачи.
void ServiceWarningsPoll();
// Сбрасывает отображаемые счётчики SPI и начинает новое окно наблюдения.
void ServiceWarningsResetSpi();

#endif
