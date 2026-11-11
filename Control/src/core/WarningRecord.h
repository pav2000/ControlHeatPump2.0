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

#ifndef Control_WarningRecord_h
#define Control_WarningRecord_h

#include <string.h>
#include "ErrorCatalog.h"

struct WarningRecord {
	ErrorCatalog::ErrCode code; // Номер предупреждения или 0 при его отсутствии
	uint16_t cause; // Устойчивая причина для подавления повторов
	uint32_t time; // Время регистрации в секундах Unix
	char device[16]; // Короткое имя устройства, завершённое нулём

	// Сравнивает код, устройство и причину с другой записью. Возвращает true для повтора.
	bool SameCause(const WarningRecord &other) const
	{
		return code == other.code && cause == other.cause && strcmp(device, other.device) == 0;
	}

	// Возвращает ключ уведомления по номеру, причине и устройству.
	uint32_t Subject() const
	{
		uint32_t hash = 2166136261u;
		hash = (hash ^ static_cast<uint8_t>(code)) * 16777619u;
		hash = (hash ^ cause) * 16777619u;
		for(const char *p = device; *p; ++p)
			hash = (hash ^ static_cast<uint8_t>(*p)) * 16777619u;
		return hash & 0xFFFFFFu;
	}
};

#endif
