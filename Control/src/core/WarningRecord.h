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

// Описывает снимок предупреждения и отдельное состояние подавления повторных эпизодов.

#ifndef Control_WarningRecord_h
#define Control_WarningRecord_h

#include "DriveError.h"

struct WarningRecord {
	ErrorCatalog::ErrCode code; // Номер предупреждения или 0 при его отсутствии
	uint16_t cause; // Устойчивая причина для подавления повторов
	uint32_t time; // Время регистрации в секундах Unix
	char device[16]; // Короткое имя устройства, завершённое нулём
	ErrorDetail detail; // Исходные данные события или пустая деталь.

	// Возвращает ключ уведомления по номеру, причине, устройству и типизированной детали.
	uint32_t Subject() const
	{
		uint32_t hash = 2166136261u;
		hash = (hash ^ static_cast<uint8_t>(code)) * 16777619u;
		hash = (hash ^ cause) * 16777619u;
		hash = (hash ^ static_cast<uint8_t>(detail.kind)) * 16777619u;
		hash = (hash ^ detail.value) * 16777619u;
		hash = (hash ^ detail.state) * 16777619u;
		for(const char *p = device; *p; ++p)
			hash = (hash ^ static_cast<uint8_t>(*p)) * 16777619u;
		return hash & 0xFFFFFFu;
	}
};

struct WarningEpisodes {
	static constexpr uint8_t Capacity = 8; // Число одновременно запоминаемых причин предупреждений.
	uint32_t subjects[Capacity]{}; // Ключи текущих или недавно зарегистрированных эпизодов.
	uint8_t count{}; // Число заполненных элементов таблицы.
	uint8_t next{}; // Позиция замены самого старого ключа после заполнения таблицы.

	// Принимает ключ и признак нового эпизода. Возвращает true, если событие надо опубликовать.
	bool Open(uint32_t subject, bool newEpisode)
	{
		for(uint8_t i = 0; i < count; ++i)
			if(subjects[i] == subject) return newEpisode;
		if(count < Capacity) subjects[count++] = subject;
		else {
			subjects[next] = subject;
			next = static_cast<uint8_t>((next + 1) % Capacity);
		}
		return true;
	}
};

#endif
