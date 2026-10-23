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
 * Решение об управлении компрессором. Перенесено из HeatPump::UpdateHeat и
 * HeatPump::UpdateCool без изменения логики: порядок ветвей, типы величин и
 * коды точек выхода сохранены дословно. Эквивалентность проверяется тестом
 * tests/test_climate.cpp, который держит копию исходного алгоритма и
 * сравнивает с ней решения на случайных входах.
 *
 * Обоснование и единицы измерения - в ClimateControl.h.
 */
#include "ClimateControl.h"
#include <stdlib.h>   // abs

// updatePID объявлена в HeatPump.h, но тащить сюда весь god-object незачем:
// нужна ровно одна функция, и она свободная.
int32_t updatePID(int32_t errorPid, PID_STRUCT &pid, PID_WORK_STRUCT &pidw);

static void clear(ClimateOut &out)
{
	out.comp = pCOMP_NONE;
	out.set_ret = false;
	out.ret = pNone;
	out.error = OK;
	out.touch_pid_time = false;
	out.set_target = false;
	out.target = 0;
	out.target_immediate = false;
	out.reset_pid = false;
	out.reduce = REDUCE_NONE;
	out.clear_urgent = false;
}

// Снижение частоты на шаг с проверкой нижней границы. Один и тот же кусок
// повторялся в исходнике пять раз подряд на отопление и пять на охлаждение,
// отличаясь только кодами точек выхода и причиной для журнала.
static void reduce_freq(const ClimateIn &in, ClimateOut &out,
                        TYPE_RET_HP ret_cant, TYPE_RET_HP ret_done, CLIMATE_REDUCE why)
{
	out.reduce = why;
	if(in.fc_target - in.fc_step < in.fc_min) {   // Уменьшать дальше некуда, выключаем компрессор
		out.set_ret = true; out.ret = ret_cant;
		out.comp = pCOMP_OFF;
		return;
	}
	out.set_ret = true; out.ret = ret_done;
	out.set_target = true;
	out.target = in.fc_target - in.fc_step;
	out.target_immediate = true;
	out.reset_pid = true;
	out.comp = pCOMP_NONE;
}

// ---------------------------------------------------------------------------
//  ОТОПЛЕНИЕ И ОХЛАЖДЕНИЕ - один алгоритм, различия таблицей
//
//  Что параметризовано направлением: все сравнения температур. В отоплении
//  "слишком много тепла" это больше, в охлаждении меньше, поэтому вместо
//  t1 > target пишется beyond(t1, target), а вместо t1 < target - target
//  плюс дельта - beyond_by(target, t1, delta). Так знак живёт в одном месте,
//  а не в пятнадцати.
//
//  Что параметризовано флагами - четыре настоящих различия между режимами.
//  Они НЕ устранены: поведение сохранено дословно, а решать, какое из двух
//  правильное, будет отдельный разбор охлаждения.
// ---------------------------------------------------------------------------

// "a зашло за b" в сторону, которую режим считает избыточной.
static inline bool beyond(const ClimateRules &r, int16_t a, int16_t b)
{
	return r.cooling ? a < b : a > b;
}

// "a не дошло до b на delta и больше" - порог пуска компрессора.
static inline bool short_by(const ClimateRules &r, int16_t a, int16_t b, int16_t delta)
{
	return r.cooling ? a > b + delta : a < b - delta;
}

const ClimateRules ClimateRulesHeat = {
	false,            // отопление
	true,             // положение 4-ходового проверяется
	true,             // выключение по цели ждёт минимального времени работы
	true,             // при генераторе свой гистерезис
	true,             // пуск разрешён и во время нагрева бойлера
	true,             // onBoiler проверяется до предела подачи
	false,            // нижняя граница только если не сработала верхняя
	true,             // блокировка роста требует PidStop < 100
	pHh3, pHh2, pHh1, pHh13, pHh4,
	pHp3, pHp2, pHp1,
	pHp5, pHp10, pHp11, pHp12,
	{ pHp29, pHp23, pHp24, pHp27, pHp25, pHp26 },
	{ pHp28, pHp6,  pHp7,  pHp16, pHp8,  pHp9  },
	pHp18, pHp19, pHp20, pHp21
};

const ClimateRules ClimateRulesCool = {
	true,             // охлаждение
	false,            // 4-ходовой НЕ проверяется: в исходнике проверка была
	                  // закомментирована, и поведение сохранено. Без этого флага
	                  // слияние молча добавило бы охлаждению чужую проверку -
	                  // ровно это и поймал тест равносильности.
	false,            // минимальное время работы не учитывается
	false,            // отдельного гистерезиса для генератора нет
	false,            // во время нагрева бойлера пуск не разрешён
	false,            // onBoiler проверяется ПОСЛЕ предела подачи
	true,             // нижняя граница проверяется всегда
	false,            // блокировка роста не смотрит на PidStop
	pCh3, pCh2, pCh1, pCh13, pCh4,
	pCp3, pCp2, pCp1,
	pCp5, pCp10, pCp11, pCp12,
	{ pCp29, pCp23, pCp24, pCp27, pCp25, pCp26 },
	{ pCp28, pCp6,  pCp7,  pCp16, pCp8,  pCp9  },
	pCp18, pCp19, pCp20, pCp21
};

void ClimateDecide(const ClimateIn &in, const ClimateRules &r,
                   PID_STRUCT &pid, PID_WORK_STRUCT &pidw, ClimateOut &out)
{
	int16_t target, t1;
	// int16_t, а не шире: в исходнике результат updatePID (int32_t)
	// присваивался в int16_t, то есть усекался. Ширина здесь - часть поведения.
	int16_t newFC;

	clear(out);

	if(in.hp_stopped) { out.comp = pCOMP_OFF; return; }   // ТН выключен или выключается

	// Превышение разности температур конденсатора при работающем компрессоре
	// (есть задержка при переключении с ГВС)
	if(in.comp_on && in.now - in.off_boiler > in.delay_boiler_off && abs(in.feed - in.ret_t) > in.dt) {
		out.error = ERR_DTEMP_CON;
		out.comp = pCOMP_NONE;
		return;
	}

	t1 = in.target_is_return ? in.ret_t : in.tin;   // целевая функция: обратка или дом
	target = in.target;

	if(r.check_r4way && in.comp_on && !in.on_boiler && in.r4way_check && in.r4way_on) {
		out.error = ERR_WRONG_HARD_STATE;          // 4-ходовой в другом положении
		out.comp = pCOMP_NONE;
		return;
	}

	// Достигнута цель - выключаемся. В отоплении дополнительно выдерживается
	// минимальное время работы компрессора, в охлаждении нет.
	bool reached_target = beyond(r, t1, target)
		&& (!r.hyst_min_comp_on
		    || in.now - in.start_compressor > (uint32_t)(in.on_boiler || in.backup_power ? 0 : in.min_compressor_on));
	// Провалились за гистерезис - включаемся.
	int16_t hyst = (r.hyst_gen_delta && in.backup_power) ? in.dTempGen : in.dTemp;
	bool below_hyst = short_by(r, t1, target, hyst)
		&& (!in.comp_on || (r.hyst_on_while_boiler && in.on_boiler));
	// Предел подачи.
	bool feed_limit = in.now - in.off_boiler > in.delay_boiler_off && beyond(r, in.feed, in.tempInLim);

	switch(in.rule)
	{
	case pHYSTERESIS:
		if(reached_target)                        { out.set_ret = true; out.ret = r.h_off_target; out.comp = pCOMP_OFF; }
		else if(below_hyst)                       { out.set_ret = true; out.ret = r.h_on_delta;   out.comp = pCOMP_ON;  }
		else if(r.boiler_before_feed && in.on_boiler) { out.comp = pCOMP_OFF; }
		else if(feed_limit)                       { out.set_ret = true; out.ret = r.h_off_feed;   out.comp = pCOMP_OFF; }
		else if(!r.boiler_before_feed && in.on_boiler) { out.comp = pCOMP_OFF; }
		else if(beyond(r, in.tempOutLim, in.ret_t)) { out.set_ret = true; out.ret = r.h_on_ret;   out.comp = pCOMP_ON;  }
		else                                      { out.set_ret = true; out.ret = r.h_hold;       out.comp = pCOMP_NONE; }
		return;

	case pPID:
		if(reached_target) { out.set_ret = true; out.ret = r.p_off_target; out.comp = pCOMP_OFF; return; }
		if(below_hyst)     { out.set_ret = true; out.ret = r.p_on_delta;   out.comp = pCOMP_ON;  return; }
		if(in.on_boiler)   { out.comp = pCOMP_OFF; return; }
		if(feed_limit) {
			// Достижение предела подачи под ПИД - это ошибка, регулятор не работает
			out.set_ret = true; out.ret = r.p_off_feed; out.error = ERR_PID_FEED; out.comp = pCOMP_OFF;
			return;
		}
		// Ограничение мощности от генератора - жёсткое, поэтому первое
		if(in.backup_power && in.power220 > in.max_backup_power) {
			reduce_freq(in, out, r.p_cant[0], r.p_done[0], REDUCE_BACKUP_POWER); return;
		}
		if(in.comp_on && beyond(r, in.feed, r.cooling ? (int16_t)(in.tempInLim + in.fc_dt_temp)
		                                              : (int16_t)(in.tempInLim - in.fc_dt_temp))) {
			reduce_freq(in, out, r.p_cant[1], r.p_done[1], REDUCE_FEED); return;
		}
		if(in.comp_on && in.fc_power > in.max_power) {
			reduce_freq(in, out, r.p_cant[2], r.p_done[2], REDUCE_POWER); return;
		}
		if(in.comp_on && in.fc_current > in.max_current) {
			reduce_freq(in, out, r.p_cant[3], r.p_done[3], REDUCE_CURRENT); return;
		}
		if(in.comp_on && (in.tcomp + in.fc_dt_comp_temp) > in.tcomp_max) {
			reduce_freq(in, out, r.p_cant[4], r.p_done[4], REDUCE_TCOMP); return;
		}
		if(in.comp_on && in.pcon_present && in.pcon > in.pcon_max - in.dt_con_press) {
			reduce_freq(in, out, r.p_cant[5], r.p_done[5], REDUCE_PCON); return;
		}
		if(!in.comp_on) { out.set_ret = true; out.ret = r.p_no_comp; out.comp = pCOMP_NONE; return; }
		if(in.now - in.fc_start_time < in.start_pid_delay / 100) {
			out.set_ret = true; out.ret = r.p_spinup; out.comp = pCOMP_NONE; return;   // разгон, частоту не трогаем
		}
		if(in.ticks - in.update_pid_time < in.pid_period_ms) {
			out.set_ret = true; out.ret = r.p_wait_pid; out.comp = pCOMP_NONE; return; // время ПИД ещё не пришло
		}

		out.set_ret = true; out.ret = r.p_run;    // дошли сюда - ПИД на подачу, компрессор работает
		out.touch_pid_time = true;

		// Знак ошибки задаёт направление: в отоплении подачу поднимают, в охлаждении опускают
		newFC = updatePID(r.cooling ? in.feed - in.pid_target : in.pid_target - in.feed, pid, pidw);
		if(newFC > in.fc_pid_freq_step) newFC = in.fc_target + in.fc_pid_freq_step;
		else newFC += in.fc_target;

		if(newFC > in.fc_max) newFC = in.fc_max;
		else if(!r.clamp_min_always && newFC < in.fc_min) newFC = in.fc_min;
		if(r.clamp_min_always && newFC < in.fc_min) newFC = in.fc_min;
		if(in.backup_power && newFC > in.fc_max_gen) newFC = in.fc_max_gen;

		// Подход к границам защит при УВЕЛИЧЕНИИ частоты: рост запрещаем
		if(in.fc_target < newFC && (!r.guard_needs_pidstop || in.fc_pid_stop < 100) && in.comp_on) {
			if(in.fc_power > in.max_power * in.fc_pid_stop / 100) {
				out.set_ret = true; out.ret = r.p_guard_power; out.reset_pid = true; out.comp = pCOMP_NONE; return;
			}
			if(in.fc_current > in.max_current * in.fc_pid_stop / 100) {
				out.set_ret = true; out.ret = r.p_guard_current; out.reset_pid = true; out.comp = pCOMP_NONE; return;
			}
			if(in.tcomp + in.fc_dt_comp_temp > in.tcomp_max * in.fc_pid_stop / 100) {
				out.set_ret = true; out.ret = r.p_guard_tcomp; out.reset_pid = true; out.comp = pCOMP_NONE; return;
			}
			if(in.pcon_present && in.pcon > (in.pcon_max - in.dt_con_press) * in.fc_pid_stop / 100) {
				out.set_ret = true; out.ret = r.p_guard_pcon; out.reset_pid = true; out.comp = pCOMP_NONE; return;
			}
		}
		if(in.fc_target != newFC) {
			out.set_target = true; out.target = newFC; out.target_immediate = false;
		}
		out.comp = pCOMP_NONE;
		return;

	case pHYBRID:
	default:
		out.comp = pCOMP_NONE;
		return;
	}
}

void ClimateDecideHeat(const ClimateIn &in, PID_STRUCT &pid, PID_WORK_STRUCT &pidw, ClimateOut &out)
{
	ClimateDecide(in, ClimateRulesHeat, pid, pidw, out);
}

void ClimateDecideCool(const ClimateIn &in, PID_STRUCT &pid, PID_WORK_STRUCT &pidw, ClimateOut &out)
{
	ClimateDecide(in, ClimateRulesCool, pid, pidw, out);
}

// ---------------------------------------------------------------------------
//  НАГРЕВ БОЙЛЕРА (расчётная часть)
//
//  Что осталось в HeatPump::UpdateBoiler и сюда НЕ попало: управление реле
//  ТЭНа бойлера, сброс лишнего тепла в систему отопления и проверка
//  расписания. Это действия, а не расчёт.
//
//  Отличия от отопления, сохранённые как есть:
//   * пределы частоты, шаг снижения, пределы мощности и тока - отдельные,
//     бойлерные; период ПИД тоже свой (в охлаждении, в отличие от бойлера,
//     он ошибочно берётся от отопления);
//   * цель ПИД берётся прямо из Prof.Boiler.tempPID, без погодозависимости;
//   * ограничение частоты снизу идёт отдельным if, как в охлаждении;
//   * когда компрессор не работает, возвращается pCOMP_OFF, а не pCOMP_NONE;
//   * выключение по достижении цели снимает признак срочного нагрева.
// ---------------------------------------------------------------------------
void ClimateDecideBoiler(const ClimateIn &in, PID_STRUCT &pid, PID_WORK_STRUCT &pidw, ClimateOut &out)
{
	int16_t newFC;

	clear(out);

	if(!in.use_pid) {          // гистерезис: старт-стоп
		if(in.feed > in.tempInLim) {
			out.set_ret = true; out.ret = pBh1; out.comp = pCOMP_OFF; return;
		}
		// Догрев ТЭНом, если он разрешён и режим не турбо
		if(!in.turbo && in.add_heating && in.heater_allowed) {
			if(in.tboiler > in.tempRBOILER - (in.on_boiler || in.urgent ? 0 : in.dAddHeat)) {
				out.set_ret = true; out.ret = pBh22; out.comp = pCOMP_OFF; return;
			}
		}
		if(in.tboiler > in.target) {
			out.set_ret = true; out.ret = pBh3;
			out.clear_urgent = true;
			out.comp = pCOMP_OFF; return;
		}
		if(!in.on_boiler && in.tboiler < in.target - (in.urgent ? 10 : in.dTemp)) {
			out.set_ret = true; out.ret = pBh2; out.comp = pCOMP_ON; return;
		}
		// Температура внутри гистерезиса: сохраняем предыдущее состояние
		if(in.on_boiler) { out.set_ret = true; out.ret = pBh4; out.comp = pCOMP_NONE; return; }
		out.set_ret = true; out.ret = pBh5; out.comp = pCOMP_OFF; return;
	}

	// Инвертор, ПИД
	if(in.feed_limit_temp > in.tempInLim) {
		out.set_ret = true; out.ret = pBp1; out.error = ERR_PID_FEED; out.comp = pCOMP_OFF; return;
	}
	if(!in.turbo && in.add_heating && in.heater_allowed) {
		if(in.tboiler > in.tempRBOILER - (in.on_boiler || in.urgent ? 0 : in.dAddHeat)) {
			out.set_ret = true; out.ret = pBp22; out.comp = pCOMP_OFF; return;
		}
	}
	if(in.tboiler > in.target) {
		out.set_ret = true; out.ret = pBp3;
		out.clear_urgent = true;
		out.comp = pCOMP_OFF; return;
	}
	if(!in.on_boiler && in.tboiler < in.target - (in.urgent ? 10 : in.dTemp)) {
		out.set_ret = true; out.ret = pBp2; out.comp = pCOMP_ON; return;
	}
	if(in.comp_on && !in.on_boiler) {
		out.comp = pCOMP_OFF; return;    // компрессор работает, но ГВС греть не надо
	}

	if(in.backup_power && in.power220 > in.max_backup_power) {
		reduce_freq(in, out, pBp29, pBp28, REDUCE_BACKUP_POWER); return;
	}
	if(in.comp_on && in.feed_limit_temp > in.tempInLim - in.fc_dt_temp) {
		reduce_freq(in, out, pBp23, pBp6, REDUCE_FEED); return;
	}
	if(in.comp_on && in.fc_power > in.max_power) {
		reduce_freq(in, out, pBp24, pBp7, REDUCE_POWER); return;
	}
	if(in.comp_on && in.fc_current > in.max_current) {
		reduce_freq(in, out, pBp27, pBp16, REDUCE_CURRENT); return;
	}
	if(in.comp_on && (in.tcomp + in.fc_dt_comp_temp) > in.tcomp_max) {
		reduce_freq(in, out, pBp25, pBp8, REDUCE_TCOMP); return;
	}
	if(in.comp_on && in.pcon_present && in.pcon > in.pcon_max - in.dt_con_press) {
		reduce_freq(in, out, pBp26, pBp9, REDUCE_PCON); return;
	}
	if(!in.comp_on) { out.set_ret = true; out.ret = pBp5; out.comp = pCOMP_OFF; return; }
	if(in.now - in.fc_start_time < in.start_pid_delay / 100) {
		out.set_ret = true; out.ret = pBp10; out.comp = pCOMP_NONE; return;
	}
	if(in.ticks - in.update_pid_time < in.pid_period_ms) {
		out.set_ret = true; out.ret = pBp11; out.comp = pCOMP_NONE; return;
	}

	out.touch_pid_time = true;
	out.set_ret = true; out.ret = pBp12;

	newFC = updatePID(in.pid_target - in.feed, pid, pidw);
	if(newFC > in.fc_pid_freq_step) newFC = in.fc_target + in.fc_pid_freq_step;
	else newFC += in.fc_target;
	if(newFC > in.fc_max) newFC = in.fc_max;
	if(newFC < in.fc_min) newFC = in.fc_min;     // отдельный if, как в охлаждении
	if(in.backup_power && newFC > in.fc_max_gen) newFC = in.fc_max_gen;

	if(in.fc_target < newFC && in.fc_pid_stop < 100 && in.comp_on) {
		if(in.fc_power > in.max_power * in.fc_pid_stop / 100) {
			out.set_ret = true; out.ret = pBp18; out.reset_pid = true; out.comp = pCOMP_NONE; return;
		}
		if(in.fc_current > in.max_current * in.fc_pid_stop / 100) {
			out.set_ret = true; out.ret = pBp19; out.reset_pid = true; out.comp = pCOMP_NONE; return;
		}
		if((in.tcomp + in.fc_dt_comp_temp) > in.tcomp_max * in.fc_pid_stop / 100) {
			out.set_ret = true; out.ret = pBp20; out.reset_pid = true; out.comp = pCOMP_NONE; return;
		}
		if(in.pcon_present && in.pcon > (in.pcon_max - in.dt_con_press) * in.fc_pid_stop / 100) {
			out.set_ret = true; out.ret = pBp21; out.reset_pid = true; out.comp = pCOMP_NONE; return;
		}
	}
	if(in.fc_target != newFC) {
		out.set_target = true; out.target = newFC; out.target_immediate = false;
	}
	out.comp = pCOMP_NONE;
}

// Нагрев бойлера только ТЭНом: гистерезис вокруг цели, см. описание в ClimateControl.h
HeaterOnlyOut ClimateDecideHeaterOnlyBoiler(int16_t tboiler, int16_t target, int16_t dTemp, bool urgent, bool relay_on)
{
	HeaterOnlyOut o;
	o.heat = false;
	o.clear_urgent = false;
	if(tboiler >= target) { o.clear_urgent = true; return o; }          // цель достигнута: выключить, срочный нагрев закончен
	if(relay_on) { o.heat = true; return o; }                           // греем: держим до цели
	o.heat = tboiler < target - (urgent ? 10 : dTemp);                  // включение - ниже гистерезиса
	return o;
}

// Длительная обработка сальмонеллы: поддержание температуры, см. описание в ClimateControl.h
bool ClimateDecideSalmonellaHold(int16_t tboiler, int16_t target, bool relay_on)
{
	if(tboiler < target) return true;
	if(tboiler > target + 50) return false;
	return relay_on;                                                     // гистерезис 0.5 градуса: состояние сохраняется
}

// ---------------------------------------------------------------------------
// Мощности контуров и COP
// ---------------------------------------------------------------------------

// Мощность контура: дельта температур (сотые градуса) на расход и теплоёмкость.
// Порядок действий сохранён из исходного кода дословно, включая деление на 100
// ПОСЕРЕДИНЕ выражения: целочисленное деление не переставить, не изменив
// результат на единицы ватт.
static inline int32_t circuit_power(int32_t delta, uint16_t flow, uint16_t capacity)
{
	return delta * flow / 100 * capacity / 3600;
}

uint8_t HouseModeAllowed(uint8_t mode, bool only_heat)
{
	return (only_heat && mode == 2) ? 1 : mode;
}

uint8_t HouseModeNext(uint8_t mode, bool only_heat)
{
	switch(mode) {
	case 0:  return 1;
	case 1:  return only_heat ? 0 : 2;
	case 2:  return 0;
	default: return mode;
	}
}

bool TargetIsTemp1(uint8_t rule, bool target_flag_return)
{
	return rule == pHYBRID || !target_flag_return;
}

void PowerCalculate(const PowerIn &in, const PowerRules &r, PowerOut &out)
{
	// Мощности контуров. В охлаждении дельта берётся с обратным знаком.
	int32_t d_con = in.heating ? (int32_t)in.feed - in.ret     : (int32_t)in.ret - in.feed;
	int32_t d_eva = in.heating ? (int32_t)in.tevaing - in.tevaoutg : (int32_t)in.tevaoutg - in.tevaing;
	if(r.has_flowcon) out.powerOUT = circuit_power(d_con, in.flowcon_value, in.flowcon_capacity);
	if(r.has_floweva) out.powerGEO = circuit_power(d_eva, in.floweva_value, in.floweva_capacity);

	// Отрицательные мощности - переходные процессы, отбрасываются.
	if(out.powerOUT < 0) out.powerOUT = 0;
	if(out.powerGEO < 0) out.powerGEO = 0;

	// Потребление из сети
	out.power220 = (r.has_sdm && in.sdm_link) ? in.sdm_power : 0;
	if(r.add_fc_when_gen && in.backup_power) out.power220 += in.fc_power;

	// COP
	if(in.compressor_on) {
		out.COP     = in.fc_power   ? out.powerOUT * 100 / in.fc_power  : 0;
		out.fullCOP = out.power220  ? out.powerOUT * 100 / out.power220 : 0;
		// Ограничение переходных процессов, чтобы графики масштабировались.
		// Потолок равен порогу (решение владельца): иначе значение между порогом и потолком
		// проходило бы мимо ограничения, а всё выше превращалось бы в потолок - на графике это
		// разрыв, а не полка. Сравнено по ПОРОГУ, а не по потолку (COP 10, полный COP 8):
		// меняются только значения выше порога.
		if(out.COP     > 10 * 100) out.COP     = 10 * 100;
		if(out.fullCOP >  8 * 100) out.fullCOP =  8 * 100;
	} else {
		out.COP = 0;
		out.fullCOP = 0;
	}
}

uint8_t SensorsNeeded(bool house_on, bool tin_used, bool weather, bool boiler_on)
{
	uint8_t m = NEED_FEED | NEED_RET;          // подача и обратка нужны всегда: защита по разности и мощность
	if(house_on && tin_used) m |= NEED_TIN;
	if(house_on && weather)  m |= NEED_TOUT;
	if(boiler_on)            m |= NEED_TBOILER;
	return m;
}

// Автомат из vServiceHP (Control.ino). Два отличия от прежнего кода:
// - при pause = 0 насосы включаются и стоят включёнными; прежде их выключало на секунду
//   каждые work секунд: "постоянно" проверялось только в состоянии 1;
// - пауза ТН начинается с паузы насосов: таймер взводится на pause. Прежде он оставался от
//   прошлого цикла, и первое включение ждало случайный остаток от 0 до pause секунд.
//   Насосы к этому моменту уже выключены остановом компрессора (после delayOffPump).
uint8_t ClimatePumpPauseStep(uint8_t *start_pump, uint16_t *timer, uint16_t work, uint16_t pause)
{
	if(!*start_pump || !work) return RELAY_KEEP;    // автомат стоит или функция выключена
	if(pause == 0) {                                 // постоянно работают
		if(*start_pump == 3) return RELAY_KEEP;
		*start_pump = 3;
		return RELAY_ON_PLAN;
	}
	if(*start_pump == 1) {                           // запуск - с паузы
		*timer = pause;
		*start_pump = 2;
		return RELAY_OFF_PLAN;
	}
	if(*timer <= 1) {
		if(*start_pump <= 2) {                       // включить
			*timer = work;
			*start_pump = 3;
			return RELAY_ON_PLAN;
		}
		*timer = pause;                              // выключить
		*start_pump = 2;
		return RELAY_OFF_PLAN;
	}
	(*timer)--;
	return RELAY_KEEP;
}

// Перенос из vUpdate (Control.ino). Прежний код в трёх ветках уходил goto delayTask мимо проверки
// расписания: при включённой циркуляции с временем работы или паузы 0 расписание не отрабатывало.
uint8_t ClimateCirculStep(const CirculIn &in, uint32_t *tick)
{
	if(!in.allowed) return RELAY_OFF;                            // по расписанию выключено или бойлер запрещён
	if(in.boiler_mode) return RELAY_ON;                          // нагрев ГВС - насос циркуляции всегда
	if(!in.circulation) return RELAY_OFF;                        // управление запрещено
	if(in.heating_boiler) return RELAY_ON;                       // идёт нагрев ГВС - улучшаем перемешивание
	if(in.work_ticks == 0) return RELAY_OFF;                     // время работы 0 - выключить
	if(in.pause_ticks == 0) return RELAY_ON;                     // время паузы 0 - включить
	if(in.relay_on) {
		if(in.now - *tick > in.work_ticks) {
			*tick = in.now;
			return RELAY_OFF_PLAN;
		}
	} else if(in.now - *tick > in.pause_ticks) {
		*tick = in.now;
		return RELAY_ON_PLAN;
	}
	return RELAY_KEEP;
}
