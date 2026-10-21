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
#ifndef __CLIMATE_CONTROL_H__
#define __CLIMATE_CONTROL_H__

#include <stdint.h>
#include "ControlTypes.h"   // MODE_COMP, RULE_HP, структуры ПИД
#include "ErrorCodes.h"     // TYPE_RET_HP и коды ошибок

/*
 * РЕШЕНИЕ об управлении компрессором в режимах отопления и охлаждения.
 *
 * ЗАЧЕМ ОТДЕЛЬНО. UpdateHeat и UpdateCool - ядро управления: включить компрессор, выключить или
 * изменить частоту. Пока они читали поля HeatPump напрямую, аварийные ветки проверялись только
 * на живом ТН, где они невоспроизводимы или опасны. Здесь решение принимается по структуре входа
 * и складывается в структуру выхода, поэтому его можно прогнать на ПК обычным g++.
 *
 * ЧЕГО ЗДЕСЬ НЕТ. Модуль только считает, что следует сделать: не трогает реле, не пишет в
 * инвертор, не заводит ошибку и не пишет в журнал. Исполняет вызывающий - HeatPump::UpdateHeat
 * и UpdateCool (около двух десятков строк). Полей много, потому что решение зависит от сорока
 * величин: здесь они перечислены поимённо.
 *
 * ЕДИНИЦЫ. Температуры - сотые градуса, частоты - сотые герца, мощность - ватты, ток - сотые
 * ампера, давление - сотые бара, время - секунды unix (кроме ticks и update_pid_time: тики
 * FreeRTOS, 1 кГц).
 */

// Причина принудительного снижения частоты. Нужна только для журнала при
// DEBUG_MODWORK: сам расчёт от неё не зависит, а текст сообщения зависит.
enum CLIMATE_REDUCE {
	REDUCE_NONE = 0,
	REDUCE_BACKUP_POWER,   // ограничение мощности от генератора
	REDUCE_FEED,           // подача подошла к пределу
	REDUCE_POWER,          // мощность инвертора
	REDUCE_CURRENT,        // ток инвертора
	REDUCE_TCOMP,          // температура компрессора
	REDUCE_PCON            // давление конденсатора
};

struct ClimateIn {
	// ---- состояние теплового насоса ----
	bool     hp_stopped;         // ТН выключен или выключается
	bool     comp_on;            // компрессор работает
	bool     on_boiler;          // сейчас греется бойлер
	bool     backup_power;       // питание от резервного источника (генератор)
	bool     r4way_check;        // проверять положение 4-ходового (определён R4WAY)
	bool     r4way_on;           // 4-ходовой включён

	// ---- время ----
	uint32_t now;                // unix-время, секунды
	uint32_t off_boiler;         // когда закончили греть бойлер
	uint32_t start_compressor;   // когда пустили компрессор
	uint32_t fc_start_time;      // когда инвертор начал разгон
	uint32_t ticks;              // счётчик тиков FreeRTOS
	uint32_t update_pid_time;    // тик последней итерации ПИД
	uint32_t pid_period_ms;      // период ПИД, миллисекунды
	uint32_t delay_boiler_off;   // пауза после переключения с ГВС, секунды
	uint32_t min_compressor_on;  // минимальное время работы компрессора, секунды

	// ---- измерения ----
	int16_t  feed;               // подача (выход конденсатора по гликолю)
	int16_t  ret_t;              // обратка (вход конденсатора по гликолю)
	int16_t  tin;                // температура в доме
	int16_t  tcomp;              // температура нагнетания
	int16_t  tcomp_max;          // её предел
	bool     pcon_present;       // датчик давления конденсатора есть
	int16_t  pcon;               // давление конденсатора
	int16_t  pcon_max;           // его предел
	int16_t  power220;           // потребление из сети, ватты (getPower отдаёт int16_t)

	// ---- инвертор ----
	int16_t  fc_target;          // текущая целевая частота
	uint16_t fc_step;            // шаг снижения частоты
	uint16_t fc_min;             // минимальная частота для этого режима
	uint16_t fc_max;             // максимальная частота для этого режима
	uint16_t fc_max_gen;         // максимальная частота при работе от генератора
	uint16_t fc_pid_freq_step;   // максимальный шаг роста частоты за итерацию ПИД
	uint16_t fc_pid_stop;        // процент от уровня защит, выше которого рост запрещён
	uint16_t fc_power;           // мощность инвертора, ватты
	uint16_t fc_current;         // ток инвертора
	uint16_t fc_dt_temp;         // запас по подаче до срабатывания защиты
	uint16_t fc_dt_comp_temp;    // запас по температуре компрессора

	// ---- профиль режима ----
	uint8_t  rule;               // pHYSTERESIS, pPID или pHYBRID
	bool     target_is_return;   // целевая функция: обратка (иначе дом)
	int16_t  target;             // целевая температура
	int16_t  dt;                 // предельная разность подача-обратка
	int16_t  dTemp;              // гистерезис
	int16_t  dTempGen;           // гистерезис при работе от генератора
	int16_t  tempInLim;          // предел подачи
	int16_t  tempOutLim;         // предел обратки
	int16_t  pid_target;         // расчётная подача для ПИД (CalcTargetPID)

	// ---- только для бойлера ----
	bool     use_pid;            // инвертор есть И включён ПИД бойлера
	int16_t  tboiler;            // температура бойлера
	int16_t  feed_limit_temp;    // величина, сравниваемая с пределом подачи в ветви
	                             // ПИД: обычно подача, при SUPERBOILER - температура
	                             // конденсации. Выбор делает вызывающий, чтобы
	                             // #ifdef не попал в расчёт
	bool     turbo;              // турбо-режим бойлера
	bool     add_heating;        // включён догрев ТЭНом
	bool     heater_allowed;     // ТЭН бойлера разрешён (не на генераторе)
	bool     urgent;             // срочный нагрев бойлера
	int16_t  tempRBOILER;        // целевая температура догрева
	int16_t  dAddHeat;           // гистерезис догрева
	uint16_t max_backup_power;   // предел мощности от генератора, ватты

	// ---- пределы защит. Приходят входом, а не берутся из Config.h: иначе ----
	// модуль не собрать на ПК. Типы намеренно вещественные - в Config.h это
	// FC_MAX_POWER (1.8*1000) и FC_MAX_CURRENT (10.0*100), то есть double,
	// и сравнение с ними идёт в вещественной арифметике. Подмена на целое
	// изменила бы поведение на границе.
	double   max_power;          // предел мощности инвертора
	double   max_current;        // предел тока инвертора
	int16_t  dt_con_press;       // запас по давлению конденсатора до предела
	uint32_t start_pid_delay;    // задержка ПИД после пуска, сотые секунды
};

struct ClimateOut {
	MODE_COMP      comp;             // что делать компрессору
	bool           set_ret;          // надо ли записать код точки выхода
	TYPE_RET_HP    ret;              // сам код
	int8_t         error;            // код ошибки или OK
	bool           touch_pid_time;   // отметить время итерации ПИД
	bool           set_target;       // надо ли менять целевую частоту
	int16_t        target;           // новая целевая частота
	bool           target_immediate; // менять немедленно (аргумент set_target)
	bool           reset_pid;        // сбросить накопленную ошибку ПИД
	CLIMATE_REDUCE reduce;           // причина снижения, для журнала
	bool           clear_urgent;     // снять признак срочного нагрева (только бойлер)
};

/*
 * Отопление и охлаждение считает ОДИН алгоритм, отличия заданы таблицей.
 *
 * Правка общей части действует на оба режима. Различия перечислены явно и видны рядом:
 * их четыре, и ни одно не устранено - поведение сохранено дословно (например, период
 * итераций ПИД в охлаждении унаследован от отопления). Устранять их (то есть решать,
 * какое из двух поведений правильное) - отдельная задача по разбору режима охлаждения.
 *
 * Пятое различие, которое числилось за этой парой, оказалось мнимым: проверка
 * разности температур конденсатора записана по-разному, но условие одно и то же.
 */
struct ClimateRules {
	bool cooling;              // направление: охлаждение переворачивает все сравнения
	bool check_r4way;          // проверять положение 4-ходового клапана
	bool hyst_min_comp_on;     // выключение по цели учитывает минимальное время работы
	bool hyst_gen_delta;       // при работе от генератора свой гистерезис (dTempGen)
	bool hyst_on_while_boiler; // пуск разрешён и когда идёт нагрев бойлера
	bool boiler_before_feed;   // в гистерезисе onBoiler проверяется ДО предела подачи
	bool clamp_min_always;     // нижняя граница частоты проверяется даже после верхней
	bool guard_needs_pidstop;  // блокировка роста частоты требует PidStop < 100
	// Коды точек выхода: у каждого режима свои, общего смысла в них нет.
	TYPE_RET_HP h_off_target, h_on_delta, h_off_feed, h_on_ret, h_hold;
	TYPE_RET_HP p_off_target, p_on_delta, p_off_feed;
	TYPE_RET_HP p_no_comp, p_spinup, p_wait_pid, p_run;
	TYPE_RET_HP p_cant[6], p_done[6];   // по паре на каждую защиту, порядок как в CLIMATE_REDUCE
	TYPE_RET_HP p_guard_power, p_guard_current, p_guard_tcomp, p_guard_pcon;
};

extern const ClimateRules ClimateRulesHeat;
extern const ClimateRules ClimateRulesCool;

// Общий расчёт. pid и pidw нужны для итерации ПИД и изменяются ею - это
// единственное состояние, которое модуль правит сам.
void ClimateDecide(const ClimateIn &in, const ClimateRules &r,
                   PID_STRUCT &pid, PID_WORK_STRUCT &pidw, ClimateOut &out);

// Обёртки для читаемости на месте вызова.
void ClimateDecideHeat(const ClimateIn &in, PID_STRUCT &pid, PID_WORK_STRUCT &pidw, ClimateOut &out);
void ClimateDecideCool(const ClimateIn &in, PID_STRUCT &pid, PID_WORK_STRUCT &pidw, ClimateOut &out);

/*
 * Решение для НАГРЕВА БОЙЛЕРА - только его расчётная часть.
 *
 * В отличие от отопления и охлаждения, HeatPump::UpdateBoiler начинается не с
 * решения, а с действий: включает и выключает ТЭН бойлера, сбрасывает лишнее
 * тепло в систему отопления, проверяет расписание. Это оркестровка, завязанная
 * на реле и на состояние ТН, и она осталась в методе. Сюда вынесено то, что
 * действительно является расчётом: гистерезис включения и выключения и ветвь
 * ПИД со всеми защитами.
 *
 * Пределы здесь СВОИ: у бойлера отдельные минимальная и максимальная частоты,
 * свой шаг снижения, свои пределы мощности и тока (в Config.h это
 * FC_MAX_POWER_BOILER и FC_MAX_CURRENT_BOILER - они ниже, чем для отопления)
 * и свой период ПИД. Всё это приходит в тех же полях ClimateIn - заполняет их
 * вызывающий.
 */
void ClimateDecideBoiler(const ClimateIn &in, PID_STRUCT &pid, PID_WORK_STRUCT &pidw, ClimateOut &out);

/*
 * Нагрев бойлера ТОЛЬКО ТЭНом (флаг профиля fBoilerHeaterOnly, веб-ключ boiler.honly).
 *
 * ТН бойлер не греет совсем, поэтому компрессорная часть (ClimateDecideBoiler: ПИД,
 * пределы частоты, сброс тепла) не вызывается, а ТЭН включается по гистерезису цели:
 * включить при tboiler < target - dTemp (при срочном нагреве - 0,1 °C), держать до
 * tboiler >= target. Это та же логика, что у догрева ТЭНом, но без температуры
 * догрева: цель одна. Условия «бойлер включён», расписание, ТН в работе, генератор
 * проверяет вызывающий (HeatPump::boilerAddHeat). Температуры - сотые градуса.
 */
struct HeaterOnlyOut {
	bool heat;            // ТЭН должен греть
	bool clear_urgent;    // цель достигнута: снять признак срочного нагрева
};
HeaterOnlyOut ClimateDecideHeaterOnlyBoiler(int16_t tboiler, int16_t target, int16_t dTemp, bool urgent, bool relay_on);

/* Длительная обработка сальмонеллы (флаг fSalmonellaHold): после нагрева до target ТЭН поддерживает
 * температуру с гистерезисом 0.5 °C до конца цикла. Ниже target - греть; выше target + 0.5 - выключить;
 * между ними остаётся прежнее состояние реле. Температуры - сотые градуса. */
bool ClimateDecideSalmonellaHold(int16_t tboiler, int16_t target, bool relay_on);

// ---------------------------------------------------------------------------
// Мощности контуров и коэффициент преобразования (COP).
//
// Лежит здесь, а не отдельным файлом: это такая же чистая арифметика по
// тепловому насосу, проверяемая на ПК, что и решение об управлении выше.
// Оба куска не знают о HeatPump, включаются в одной точке и тестируются
// одинаково - разводить их по разным файлам значило бы плодить файлы на
// полсотни строк без выигрыша для читателя.
// ---------------------------------------------------------------------------

// Измерения на входе. Температуры - сотые градуса, расход - как отдаёт
// датчик (тысячные м3/ч), теплоёмкость - Дж/(кг*град).
struct PowerIn {
	bool     heating;           // режим нагрева; иначе охлаждение - знак дельты меняется
	bool     compressor_on;     // компрессор работает
	bool     backup_power;      // работа от генератора (Option.flags/fBackupPower)
	int16_t  feed;              // подача системы отопления, TCONOUTG
	int16_t  ret;               // обратка системы отопления, TCONING
	int16_t  tevaing;           // вход по гликолю испарителя
	int16_t  tevaoutg;          // выход по гликолю испарителя
	uint16_t flowcon_value;     // расход по конденсатору
	uint16_t flowcon_capacity;  // теплоёмкость теплоносителя конденсатора
	uint16_t floweva_value;     // расход по испарителю
	uint16_t floweva_capacity;  // теплоёмкость теплоносителя испарителя
	uint16_t fc_power;          // мощность компрессора по инвертору, Вт
	int32_t  sdm_power;         // мощность по электросчётчику, Вт
	bool     sdm_link;          // связь со счётчиком есть
};

// Что именно считать (параметрами, а не #ifdef: обе ветки проверяются тестом).
struct PowerRules {
	bool has_flowcon;      // FLOWCON - есть расходомер конденсатора
	bool has_floweva;      // FLOWEVA - есть расходомер испарителя
	bool has_sdm;          // USE_ELECTROMETER_SDM - есть электросчётчик
	bool add_fc_when_gen;  // ADD_FC_POWER_WHEN_GENERATOR - прибавлять мощность инвертора на генераторе
};

// Результат. powerOUT и powerGEO входные тоже: при отсутствующем расходомере
// они не меняются, остаётся прежнее значение.
struct PowerOut {
	int32_t powerOUT;   // мощность выходная, Вт
	int32_t powerGEO;   // мощность геоконтура, Вт
	int32_t power220;   // потребление из сети, Вт
	int32_t COP;        // чистый COP, сотые
	int32_t fullCOP;    // полный COP, сотые
};

// Правила текущей конфигурации. Определены в Config.cpp - там же, где
// остальные таблицы, собранные из #ifdef. Здесь модуль о конфигурации не
// знает вовсе, поэтому и собирается на хосте без Config.h.
extern const PowerRules PowerRulesConfig;

void PowerCalculate(const PowerIn &in, const PowerRules &r, PowerOut &out);

// Режим дома (Prof.SaveON.mode): 0 выключено, 1 отопление, 2 охлаждение
// (pOFF, pHEAT, pCOOL - совпадение проверяет Config.cpp). В прошивке "только
// отопление" охлаждение недостижимо: HouseModeAllowed заменяет его отоплением,
// HouseModeNext перебирает режимы по кругу без охлаждения.
extern const bool HouseOnlyHeat;               // ONLY_HEAT, определён в Config.cpp
uint8_t HouseModeAllowed(uint8_t mode, bool only_heat);
uint8_t HouseModeNext(uint8_t mode, bool only_heat);

// Датчики температуры, без которых алгоритм считает по выдуманным значениям
// (датчика нет в конфигурации или он не привязан: подставляется 0 или тестовая
// температура). Бит = роль датчика, номера датчиков знает HeatPump.
#define NEED_FEED    0x01   // подача (выход конденсатора по гликолю)
#define NEED_RET     0x02   // обратка (вход конденсатора по гликолю)
#define NEED_TIN     0x04   // температура в доме
#define NEED_TOUT    0x08   // температура улицы
#define NEED_TBOILER 0x10   // температура бойлера
// house_on - режим дома не "выключено"; tin_used - цель по дому или гибридный
// алгоритм; weather - включена погодозависимость; boiler_on - ГВС включён.
uint8_t SensorsNeeded(bool house_on, bool tin_used, bool weather, bool boiler_on);

#endif
