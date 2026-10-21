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
 * Единственное место, где создаются объекты, объявленные в Config.h и Constant.h.
 *
 * ЗАЧЕМ. Почти всё в этих заголовках - константы с внутренним связыванием, и включить
 * заголовок во вторую единицу трансляции им ничто не мешает. Но три объекта изменяемые, то
 * есть имеют внешнее связывание: определи их в заголовке, и Constant.h можно будет включить
 * только один раз. Поэтому заголовки ОБЪЯВЛЯЮТ их, а определения собраны здесь, рядом с
 * остальной конфигурацией, где их правит владелец.
 *
 *   ChartsModSetup  набор графиков в памяти; веб-API меняет его на ходу (HeatPumpCharts.cpp,
 *                   обработчик set_chart)
 *   ChartsConstSetup, ChartsOnFlySetup - два постоянных набора графиков; const в пространстве
 *                   имён даёт внутреннее связывание: в заголовке это по копии на единицу
 *                   трансляции (65 байт flash за копию)
 *   defaultMAC      MAC по умолчанию; отдаётся Ethernet.begin как uint8_t*
 *   HistorySetup    состав поминутной истории; лежит во flash, имена спрашиваются у датчиков
 *   SPI_RATE        делитель SPI, общий для W5500, SD и flash; объявлен и в
 *                   Libraries/Ethernet/utility/w5100.h
 */
#define CONFIG_DEFINE_OBJECTS
#include "Constant.h"
#include "src/core/ClimateControl.h"   // PowerRules - правила расчёта мощностей ниже
#include "src/log/Statistics.h"       // имена файлов статистики объявлены через STRCONST
#include "src/core/Scheduler.h"        // имена веб-параметров расписания - тоже
#include "src/core/HeatPump.h"         // и строки Hardware.h, MQTT.h, OmronFC.h, VaconFC.h
#include "src/log/Journal.h"          // и сообщения журнала

// Размерность массива Charts[] в HeatPump.h задаётся через CHARTS_MOD_COUNT,
// поэтому расхождение списка и числа стоило бы чтения за границей массива.
static_assert(sizeof(ChartsModSetup) / sizeof(ChartsModSetup[0]) == CHARTS_MOD_COUNT,
              "CHARTS_MOD_COUNT не совпадает с числом строк ChartsModSetup[]");
static_assert(sizeof(defaultMAC) == 6, "defaultMAC должен быть длиной 6 байт");
static_assert(sizeof(TempSetup) / sizeof(TempSetup[0]) == TNUMBER,
              "TempSetup должен содержать ровно TNUMBER строк");
static_assert(sizeof(InputSetup) / sizeof(InputSetup[0]) == INUMBER,
              "InputSetup должен содержать ровно INUMBER строк");
static_assert(sizeof(FrequencySetup) / sizeof(FrequencySetup[0]) == FNUMBER,
              "FrequencySetup должен содержать ровно FNUMBER строк");
static_assert(sizeof(RelaySetup) / sizeof(RelaySetup[0]) == RNUMBER,
              "RelaySetup должен содержать ровно RNUMBER строк");
static_assert(sizeof(AnalogSetup) / sizeof(AnalogSetup[0]) == ANUMBER,
              "AnalogSetup должен содержать ровно ANUMBER строк");
static_assert(sizeof(HistorySetup) / sizeof(HistorySetup[0]) == HISTORY_SETUP_COUNT,
              "HISTORY_SETUP_COUNT не совпадает с числом строк HistorySetup[]");
static_assert(sizeof(ChartsConstSetup) / sizeof(ChartsConstSetup[0]) == CHARTS_CONST_COUNT,
              "CHARTS_CONST_COUNT не совпадает с числом строк ChartsConstSetup[]");
static_assert(sizeof(ChartsOnFlySetup) / sizeof(ChartsOnFlySetup[0]) == CHARTS_ONFLY_COUNT,
              "CHARTS_ONFLY_COUNT не совпадает с числом строк ChartsOnFlySetup[]");

// Правила расчёта мощностей и COP: единственное место, где условная
// компиляция этих четырёх опций превращается в данные. Сам расчёт живёт в
// ClimateControl.cpp и о Config.h не знает - потому и проверяется на ПК.
#ifdef ONLY_HEAT
const bool HouseOnlyHeat = true;
#else
const bool HouseOnlyHeat = false;
#endif
static_assert(pOFF == 0 && pHEAT == 1 && pCOOL == 2, "HouseMode* в ClimateControl.cpp считает режимы числами");

const PowerRules PowerRulesConfig = {
#ifdef FLOWCON
	true,
#else
	false,
#endif
#ifdef FLOWEVA
	true,
#else
	false,
#endif
#ifdef USE_ELECTROMETER_SDM
	true,
#else
	false,
#endif
#ifdef ADD_FC_POWER_WHEN_GENERATOR
	true,
#else
	false,
#endif
};

