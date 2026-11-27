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

/*
 * Графики работы ТН в оперативной памяти: список, очистка, накопление точек,
 * отдача в web-морду. Отдельная единица трансляции.
 *
 * HeatPump.h тянет Constant.h, а с ним Config.h, поэтому шесть условий
 * (FLOWCON, FLOWEVA, EEV_DEF, EEV_PREFER_PERCENT, USE_ELECTROMETER_SDM,
 * TCONOUT) разрешаются так же, как в главной единице трансляции. Проверено
 * сверкой размеров методов и присутствием в бинарнике кода, существующего
 * только в одной из ветвей.
 */
#include "Rtos.h"
#include "HeatPump.h"     // HeatPump, HP, датчики, PressToTemp
#include "../log/RingChart.h"    // chart_find, STATS_OBJ_names, CHART_NOT_FOUND
#include "../util/Util.h"         // _itoa
#include "../net/ParamRegistry.h"   // таблицы параметров веб-API и ответ /api/chart (внизу файла)

void HeatPump::get_listChart(char* ret, const char *delimiter)
{
	for(uint8_t index = 0; index < sizeof(ChartsModSetup) / sizeof(ChartsModSetup[0]); index++) {
		if(ChartsModSetup[index].object == STATS_OBJ_Temp) strcat(ret, sTemp[ChartsModSetup[index].number].get_note());
		else if(ChartsModSetup[index].object == STATS_OBJ_Press) strcat(ret, sADC[ChartsModSetup[index].number].get_note());
		else if(ChartsModSetup[index].object == STATS_OBJ_PressTemp) {
			strcat(ret, sADC[ChartsModSetup[index].number].get_note());
			strcat(ret, ", °C");
		} else if(ChartsModSetup[index].object == STATS_OBJ_Flow) strcat(ret, sFrequency[ChartsModSetup[index].number].get_note());
		else strcat(ret, STATS_OBJ_names[ChartsModSetup[index].object]);
		strcat(ret, delimiter);
	}
	for(uint8_t index = 0; index < sizeof(ChartsConstSetup) / sizeof(ChartsConstSetup[0]); index++) {
		strcat(ret, ChartsConstSetup[index].name);
		strcat(ret, delimiter);
	}
	for(uint8_t index = 0; index < sizeof(ChartsOnFlySetup) / sizeof(ChartsOnFlySetup[0]); index++) {
		strcat(ret, ChartsOnFlySetup[index].name);
		strcat(ret, delimiter);
	}
}

// сбросить графики в ОЗУ
void HeatPump::clearChart()
{
	const uint8_t suspended = TaskSuspendAll();
	for(auto &chart : Charts) chart.clear();
	chartRows = 0;
	refreshChartSources();
	++chartRevision;
	if(suspended) xTaskResumeAll();
}

void HeatPump::refreshChartSources()
{
	// Позиции величин, из которых считаются производные графики - разности
	// и мощности: ищет одна функция, проверяемая на хосте.
	const uint8_t n = sizeof(ChartsModSetup) / sizeof(ChartsModSetup[0]);
	Chart_PressTemp_PCON = chart_find(ChartsModSetup, n, STATS_OBJ_PressTemp, PCON);
	Chart_Temp_TCONOUT   = chart_find(ChartsModSetup, n, STATS_OBJ_Temp, TCONOUT);
	Chart_Temp_TCOMP     = chart_find(ChartsModSetup, n, STATS_OBJ_Temp, TCOMP);
	Chart_Temp_TCONOUTG  = chart_find(ChartsModSetup, n, STATS_OBJ_Temp, TCONOUTG);
	Chart_Temp_TCONING   = chart_find(ChartsModSetup, n, STATS_OBJ_Temp, TCONING);
	Chart_Temp_TEVAING   = chart_find(ChartsModSetup, n, STATS_OBJ_Temp, TEVAING);
	Chart_Temp_TEVAOUTG  = chart_find(ChartsModSetup, n, STATS_OBJ_Temp, TEVAOUTG);
#ifdef FLOWCON
	Chart_Flow_FLOWCON   = chart_find(ChartsModSetup, n, STATS_OBJ_Flow, FLOWCON);
#else
	Chart_Flow_FLOWCON   = CHART_NOT_FOUND;
#endif
#ifdef FLOWEVA
	Chart_Flow_FLOWEVA   = chart_find(ChartsModSetup, n, STATS_OBJ_Flow, FLOWEVA);
#else
	Chart_Flow_FLOWEVA   = CHART_NOT_FOUND;
#endif
}

void HeatPump::setChartSource(uint8_t index, const Charts_Mod_setup &setup)
{
	if(index >= CHARTS_MOD_COUNT) return;
	const uint8_t suspended = TaskSuspendAll();
	auto &current = ChartsModSetup[index];
	if(current.object != setup.object || current.number != setup.number) {
		current = setup;
		Charts[index].clear();
		refreshChartSources();
		++chartRevision;
	}
	if(suspended) xTaskResumeAll();
}

// обновить статистику, добавить одну точку и если надо записать ее на карту.
// Все значения в графиках целочислены (сотые), выводятся в формате 0.01
void  HeatPump::updateChart()
{
	// Сырые точки одной строки и её положение обновляются вместе.
	// Долгая отправка CSV не держит планировщик и проверяет ревизию.
	const uint8_t suspended = TaskSuspendAll();
	for(uint8_t i = 0; i < sizeof(ChartsModSetup) / sizeof(ChartsModSetup[0]); i++) {
		if(ChartsModSetup[i].object == STATS_OBJ_Temp) Charts[i].add_Point(sTemp[ChartsModSetup[i].number].get_Temp());
		else if(ChartsModSetup[i].object == STATS_OBJ_Press) Charts[i].add_Point(sADC[ChartsModSetup[i].number].get_Value());
		else if(ChartsModSetup[i].object == STATS_OBJ_PressTemp) Charts[i].add_Point(PressToTemp(ChartsModSetup[i].number));
		else if(ChartsModSetup[i].object == STATS_OBJ_Flow) Charts[i].add_Point(sFrequency[ChartsModSetup[i].number].get_Value() / 10);
	}
	for(uint8_t i = 0; i < sizeof(ChartsConstSetup) / sizeof(ChartsConstSetup[0]); i++) {
		uint8_t j = sizeof(ChartsModSetup) / sizeof(ChartsModSetup[0]) + i;
		if(false) {}                                                   // начало цепочки else if: ветки ЭРВ ниже существуют только с EEV_DEF
#ifdef EEV_DEF
		else if(ChartsConstSetup[i].object == STATS_OBJ_Overheat) Charts[j].add_Point(dEEV.get_Overheat());
#ifdef EEV_PREFER_PERCENT
		else if(ChartsConstSetup[i].object == STATS_OBJ_EEV) Charts[j].add_Point(dEEV.calc_percent(dEEV.get_EEV()));
#else
		else if(ChartsConstSetup[i].object == STATS_OBJ_EEV) Charts[j].add_Point(dEEV.get_EEV());
#endif
		else if(ChartsConstSetup[i].object == STATS_OBJ_Overheat2) Charts[j].add_Point(dEEV.get_tOverheat());
#endif
		else if(ChartsConstSetup[i].object == STATS_OBJ_Compressor) Charts[j].add_Point(dFC.get_frequency());
		else if(ChartsConstSetup[i].object == STATS_OBJ_Power_FC) Charts[j].add_Point(dFC.get_power() / 10);
		else if(ChartsConstSetup[i].object == STATS_OBJ_Current_FC) Charts[j].add_Point(dFC.get_current());
#ifdef USE_ELECTROMETER_SDM
		else if(ChartsConstSetup[i].object == STATS_OBJ_Voltage) Charts[j].add_Point(dSDM.get_Voltage() * 100);
		else if(ChartsConstSetup[i].object == STATS_OBJ_Power) Charts[j].add_Point((int32_t)power220 / 10);
		else if(ChartsConstSetup[i].object == STATS_OBJ_COP_Full) Charts[j].add_Point(fullCOP);
#endif
	}
	if(chartRows < CHART_POINT) ++chartRows;
	else ++chartRevision;
	if(suspended) xTaskResumeAll();
}

CondensingChartSource HeatPump::get_condensingChartSource()
{
#ifdef EEV_DEF
	const bool pressure_present = sADC[PCON].get_present();
#else
	const bool pressure_present = false;
#endif
	return chart_condensing_source(pressure_present, is_heating(), Chart_PressTemp_PCON,
		Chart_Temp_TCONOUTG, Chart_Temp_TEVAOUTG);
}

// Единая нумерация рядов и вычисление точки для JSON и CSV.
constexpr uint8_t CHARTS_STORED = CHARTS_MOD_COUNT + CHARTS_CONST_COUNT;
constexpr uint8_t CHARTS_ALL = CHARTS_STORED + CHARTS_ONFLY_COUNT;

uint8_t HeatPump::chartObject(uint8_t index)
{
	if(index < CHARTS_MOD_COUNT) return ChartsModSetup[index].object;
	if(index < CHARTS_STORED) return ChartsConstSetup[index - CHARTS_MOD_COUNT].object;
	return ChartsOnFlySetup[index - CHARTS_STORED].object;
}

uint8_t HeatPump::chartDecimals(uint8_t object)
{
#ifndef EEV_PREFER_PERCENT
	if(object == STATS_OBJ_EEV) return 0;
#endif
	return object == STATS_OBJ_Power_GEO || object == STATS_OBJ_Power_OUT ? 3 : 2;
}

bool HeatPump::readChartPoint(uint8_t index, uint16_t point, const CondensingChartSource &source, int32_t &value)
{
	if(index >= CHARTS_ALL) return false;
	const uint16_t rows = chartRows;
	int16_t va, vb, vf;
	if(index < CHARTS_STORED) {
		if(!Charts[index].readAligned(rows, point, va)) return false;
		value = va;
		return true;
	}

	uint8_t a, b, flow = CHART_NOT_FOUND;
	int16_t offset = 0;
	uint16_t capacity = 0;
	const uint8_t object = chartObject(index);
	switch(object) {
	case STATS_OBJ_Overcool: a = Chart_PressTemp_PCON; b = Chart_Temp_TCONOUT; break;
	case STATS_OBJ_TCOMP_TCON: a = Chart_Temp_TCOMP; b = source.index; offset = source.offset; break;
	case STATS_OBJ_Delta_GEO: a = Chart_Temp_TEVAING; b = Chart_Temp_TEVAOUTG; break;
	case STATS_OBJ_Delta_OUT: a = Chart_Temp_TCONOUTG; b = Chart_Temp_TCONING; break;
#ifdef FLOWEVA
	case STATS_OBJ_Power_GEO:
		// Знак соответствует графику JSON и расчёту мощности при нагреве.
		a = Chart_Temp_TEVAING; b = Chart_Temp_TEVAOUTG;
		flow = Chart_Flow_FLOWEVA; capacity = sFrequency[FLOWEVA].get_Capacity();
		break;
#endif
#ifdef FLOWCON
	case STATS_OBJ_Power_OUT:
		a = Chart_Temp_TCONOUTG; b = Chart_Temp_TCONING;
		flow = Chart_Flow_FLOWCON; capacity = sFrequency[FLOWCON].get_Capacity();
		break;
#endif
	default: return false;
	}
	if(a == CHART_NOT_FOUND || b == CHART_NOT_FOUND ||
	   !Charts[a].readAligned(rows, point, va) || !Charts[b].readAligned(rows, point, vb)) return false;
	value = int32_t(va) - vb - offset;
	if(chartDecimals(object) == 3) {
		return flow != CHART_NOT_FOUND && Charts[flow].readAligned(rows, point, vf) &&
		       chart_power_milli(value, vf, capacity, value);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Связка графиков с состоянием ТН.
//
// Кольцевой буфер лежит в модуле RingChart.cpp, который о HeatPump ничего не знает.
// А эти две функции без него не имеют смысла: они читают имена и подписи прямо из
// HP.sTemp/sADC/sFrequency и перебирают датчики по TNUMBER/ANUMBER/FNUMBER из Config.h.
// Объявлены в RingChart.h, зовёт их WebApiGet.
// ---------------------------------------------------------------------------

void Charts_get_param(uint8_t index, char param, char *ret)
{
	if(param == 'O') strcat(ret, STATS_OBJ_names[ChartsModSetup[index].object]);	// .object
	else if(param == 'N') {
		if(ChartsModSetup[index].object == STATS_OBJ_Temp) strcat(ret, HP.sTemp[ChartsModSetup[index].number].get_note());
		else if(ChartsModSetup[index].object == STATS_OBJ_Press) strcat(ret, HP.sADC[ChartsModSetup[index].number].get_note());
		else if(ChartsModSetup[index].object == STATS_OBJ_PressTemp) {
			strcat(ret, HP.sADC[ChartsModSetup[index].number].get_note());
			strcat(ret, ", °C");
		} else if(ChartsModSetup[index].object == STATS_OBJ_Flow) strcat(ret, HP.sFrequency[ChartsModSetup[index].number].get_note());
	} else if(param == 'X') {														// .number
		uint8_t num = ChartsModSetup[index].number;
		if(ChartsModSetup[index].object == STATS_OBJ_Temp) strcat(ret, HP.sTemp[num].get_name());
		else if(ChartsModSetup[index].object == STATS_OBJ_Press || ChartsModSetup[index].object == STATS_OBJ_PressTemp) strcat(ret, HP.sADC[num].get_name());
		else if(ChartsModSetup[index].object == STATS_OBJ_Flow) strcat(ret, HP.sFrequency[num].get_name());
	}
}

void Charts_set_param(uint8_t index, char param, char *value)
{
	uint8_t i = 0;
	auto setup = ChartsModSetup[index];
	if(param == 'O') { // .object
		for(; STATS_OBJ_names[i]; i++) if(strcmp(value, STATS_OBJ_names[i]) == 0) break;
		if(STATS_OBJ_names[i] && setup.object != i) {
			setup.number = 0;
			setup.object = i;
		}
	} else if(param == 'X') { // .number
		if(setup.object == STATS_OBJ_Temp) {
			for(i = 0; i < TNUMBER; i++) if(strcmp(value, HP.sTemp[i].get_name()) == 0) break;
			if(i < TNUMBER) setup.number = i;
		} else if(setup.object == STATS_OBJ_Press || setup.object == STATS_OBJ_PressTemp) {
			for(i = 0; i < ANUMBER; i++) if(strcmp(value, HP.sADC[i].get_name()) == 0) break;
			if(i < ANUMBER) setup.number = i;
		} else if(setup.object == STATS_OBJ_Flow) {
			for(i = 0; i < FNUMBER; i++) if(strcmp(value, HP.sFrequency[i].get_name()) == 0) break;
			if(i < FNUMBER) setup.number = i;
		}
	}
	HP.setChartSource(index, setup);
}

// ---------------------------------------------------------------------------
// Веб-API: список рядов, состав изменяемых рядов, ответ /api/chart.
// Номер ряда id считается с 0 по тому же списку, что и chart.list: сначала ChartsModSetup (их состав правится),
// затем ChartsConstSetup и ChartsOnFlySetup.
// ---------------------------------------------------------------------------

// Название ряда для списка: у рядов датчиков - описание датчика
static void api_chart_title(ApiOut *o, uint8_t i)
{
	if(i < CHARTS_MOD_COUNT) {
		uint8_t obj = ChartsModSetup[i].object, n = ChartsModSetup[i].number;
		if(obj == STATS_OBJ_Temp) api_put_str(o, HP.sTemp[n].get_note());
		else if(obj == STATS_OBJ_Press) api_put_str(o, HP.sADC[n].get_note());
		else if(obj == STATS_OBJ_PressTemp) api_put_str2(o, HP.sADC[n].get_note(), ", °C");
		else if(obj == STATS_OBJ_Flow) api_put_str(o, HP.sFrequency[n].get_note());
		else api_put_str(o, STATS_OBJ_names[obj]);
	} else if(i < CHARTS_MOD_COUNT + CHARTS_CONST_COUNT) api_put_str(o, ChartsConstSetup[i - CHARTS_MOD_COUNT].name);
	else api_put_str(o, ChartsOnFlySetup[i - CHARTS_MOD_COUNT - CHARTS_CONST_COUNT].name);
}
void api_chart_list(ApiOut *o)
{
	api_putc(o, '[');
	for(uint8_t i = 0; i < CHARTS_ALL; i++) { if(i) api_putc(o, ','); api_chart_title(o, i); }
	api_putc(o, ']');
}

// Индекс ряда по id: число с 0 или имя (EEV, OH, OH2, TT - как раньше, а также любое имя STATS_OBJ_*). -1 - нет такого.
static int chart_index(const char *x)
{
	if(*x >= '0' && *x <= '9') {
		char *end;
		const unsigned long n = strtoul(x, &end, 10);
		return !*end && n < CHARTS_ALL ? n : -1;
	}
	uint8_t obj = 255, i;
	if(!strcmp(x, "EEV")) obj = STATS_OBJ_EEV;
	else if(!strcmp(x, "OH")) obj = STATS_OBJ_Overheat;
	else if(!strcmp(x, "OH2")) obj = STATS_OBJ_Overheat2;
	else if(!strcmp(x, "TT")) obj = STATS_OBJ_TCOMP_TCON;
	else for(i = 0; STATS_OBJ_names[i]; i++) if(!strcmp(x, STATS_OBJ_names[i])) { obj = i; break; }
	if(obj == 255) return -1;
	for(i = 0; i < CHARTS_CONST_COUNT; i++) if(ChartsConstSetup[i].object == obj) return CHARTS_MOD_COUNT + i;
	for(i = 0; i < CHARTS_ONFLY_COUNT; i++) if(ChartsOnFlySetup[i].object == obj) return CHARTS_STORED + i;
	return -1;
}

// /api/chart?id=<N|имя> формирует JSON напрямую в ограниченном выходном буфере.
int api_chart(const char *q, ApiOut *o)
{
	const char *v = q;
	for(; *v; v++) if((v == q || v[-1] == '&') && v[0] == 'i' && v[1] == 'd' && v[2] == '=') break;
	if(!*v) return -1;
	v += 3;
	char id[16]; uint8_t n = 0;
	while(v[n] && v[n] != '&' && n < sizeof(id) - 1) { id[n] = v[n]; n++; }
	if(v[n] && v[n] != '&') return -1;                      // слишком длинное имя
	id[n] = 0;
	int idx = chart_index(id);
	if(idx < 0) { api_put(o, "{\"e\":\"unknown\"}"); return o->over ? -2 : 0; }
	const uint16_t mark = o->len;
	const uint32_t revision = HP.get_chartRevision();
	const uint16_t rows = HP.get_chartRows();
	const auto source = HP.get_condensingChartSource();
	const uint8_t object = HP.chartObject(idx), decimals = HP.chartDecimals(object);
	api_put(o, "{\"obj\":"); api_put_int(o, object); api_put(o, ",\"d\":[");
	bool comma = false;
	for(uint16_t row = 0; row < rows && !o->over; ++row) {
		int32_t value;
		if(!HP.readChartPoint(idx, row, source, value)) continue;
		char number[16];
		dptoa(number, value, decimals);
		if(comma) api_putc(o, ',');
		api_put(o, number);
		comma = true;
	}
	// Пока формировали ответ, ряд мог сменить источник или прокрутиться.
	if(revision != HP.get_chartRevision()) {
		o->len = mark; o->p[mark] = 0; o->over = 0;
		api_put(o, "{\"e\":\"busy\"}");
	} else api_put(o, "]}");
	return o->over ? -2 : 0;
}

enum { CC_N = 1, CC_O, CC_X };
static const ApiParam apiChartCfgTab[] = {
	{ "n", 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, CC_N, 0 },     // описание ряда (по датчику)
	{ "o", 0, 0, 0, 0, 1, PT_CALL, 0,     MK_TEXT, CC_O, 0 },     // тип объекта: Temp, Press, Flow ...
	{ "x", 0, 0, 0, 0, 1, PT_CALL, 0,     MK_TEXT, CC_X, 0 },     // имя датчика
};
static uint8_t api_cc_n(void) { return CHARTS_MOD_COUNT; }
static_assert(CHARTS_MOD_COUNT <= 20, "имена рядов в таблице api_cc_name - увеличить при росте CHARTS_MOD_COUNT");
static const char *api_cc_name(uint8_t i)
{
	static const char *const n[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19" };   // const
	return n[i];
}
static uint8_t api_cc_ok(uint8_t) { return 1; }
static int api_get_cc(uint8_t hook, uint8_t dev, ApiOut *o)
{
	if(hook == CC_N) { uint8_t obj = ChartsModSetup[dev].object;
		if(obj == STATS_OBJ_Temp || obj == STATS_OBJ_Press || obj == STATS_OBJ_PressTemp || obj == STATS_OBJ_Flow) api_chart_title(o, dev); else api_put(o, "\"\"");
	} else if(hook == CC_O) api_put_str(o, STATS_OBJ_names[ChartsModSetup[dev].object]);
	else if(hook == CC_X) {
		uint8_t obj = ChartsModSetup[dev].object, num = ChartsModSetup[dev].number;
		if(obj == STATS_OBJ_Temp) api_put_str(o, HP.sTemp[num].get_name());
		else if(obj == STATS_OBJ_Press || obj == STATS_OBJ_PressTemp) api_put_str(o, HP.sADC[num].get_name());
		else if(obj == STATS_OBJ_Flow) api_put_str(o, HP.sFrequency[num].get_name());
		else api_put_str(o, "");
	} else return AE_FAIL;
	return AE_OK;
}
static int api_set_cc(uint8_t hook, uint8_t dev, const char *val)
{
	if(hook == CC_O) {                                       // имя должно быть в списке типов
		uint8_t i = 0;
		while(STATS_OBJ_names[i] && strcmp(val, STATS_OBJ_names[i])) i++;
		if(!STATS_OBJ_names[i]) return AE_BAD_VALUE;
		Charts_set_param(dev, 'O', (char *) val);
		return AE_OK;
	}
	if(hook == CC_X) {                                       // имя датчика проверяется по типу объекта
		uint8_t before = ChartsModSetup[dev].number;
		Charts_set_param(dev, 'X', (char *) val);
		if(before == ChartsModSetup[dev].number) {          // число не изменилось: то же имя или имени нет
			char t[24]; t[0] = 0;
			Charts_get_param(dev, 'X', t);
			if(strcmp(t, val)) return AE_BAD_VALUE;
		}
		return AE_OK;
	}
	return AE_FAIL;
}
static const ApiOps apiOpsChartCfg = { 0, api_get_cc, api_set_cc, 0, 0, api_cc_n, api_cc_name, api_cc_ok };
extern const ApiGroup ApiGrpChartCfg;
const ApiGroup ApiGrpChartCfg = { "chartcfg", apiChartCfgTab, sizeof(apiChartCfgTab) / sizeof(apiChartCfgTab[0]), &apiOpsChartCfg };

enum { CH_LIST = 1 };
static const ApiParam apiChartTab[] = {
	{ "list", 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TABLE, CH_LIST, 0 },      // названия всех рядов: номер ряда = позиция в массиве
};
static int api_get_chart(uint8_t hook, uint8_t, ApiOut *o) { if(hook != CH_LIST) return AE_FAIL; api_chart_list(o); return AE_OK; }
static const ApiOps apiOpsChart = { 0, api_get_chart, 0, 0, 0, 0, 0, 0 };
extern const ApiGroup ApiGrpChart;
const ApiGroup ApiGrpChart = { "chart", apiChartTab, sizeof(apiChartTab) / sizeof(apiChartTab[0]), &apiOpsChart };
