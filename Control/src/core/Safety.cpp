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
 * Защиты выходов, описание - в Safety.h.
 */
#include "Safety.h"

uint8_t Relay_ApplyStatus(uint8_t flags, int8_t r)
{
	if(r == fR_StatusAllOff) return flags & ~fR_StatusMask;
	if(r == 0) r = -fR_StatusMain;
	uint8_t bit = (uint8_t)(1 << (r > 0 ? r : -r));
	return r > 0 ? (flags | bit) : (flags & ~bit);
}

SAFETY_WHY Safety_Allow(SAFETY_OUT out, bool on, const SafetyState &s)
{
	switch(out) {
	case SAFE_COMP:
		if(on && !s.defrost && !(s.pump_in && s.pump_out)) return SAFETY_NO_PUMPS;
		break;
	case SAFE_PUMP_IN:
	case SAFE_PUMP_OUT:
		if(on || s.defrost) break;
		if(s.comp_on) return SAFETY_COMP_ON;
		if(s.since_comp_off < (out == SAFE_PUMP_IN ? s.overrun_in : s.overrun_out)) return SAFETY_OVERRUN;
		break;
	case SAFE_HEATER:
		if(on && !s.pump_out) return SAFETY_NO_FLOW;
		break;
	case SAFE_4WAY:
		if(s.comp_on) return SAFETY_COMP_ON;
		break;
	case SAFE_OTHER:
		break;
	}
	return SAFETY_OK;
}

const char *Safety_WhyText(SAFETY_WHY w)
{
	switch(w) {
	case SAFETY_OK:       return "ok";
	case SAFETY_NO_PUMPS: return "pumps off";
	case SAFETY_COMP_ON:  return "compressor on";
	case SAFETY_OVERRUN:  return "pump overrun";
	case SAFETY_NO_FLOW:  return "no circulation";
	}
	return "?";
}
