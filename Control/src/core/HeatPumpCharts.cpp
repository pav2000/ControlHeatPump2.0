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
	for(uint8_t i = 0; i < sizeof(Charts) / sizeof(Charts[0]); i++) Charts[i].clear();

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

// обновить статистику, добавить одну точку и если надо записать ее на карту.
// Все значения в графиках целочислены (сотые), выводятся в формате 0.01
void  HeatPump::updateChart()
{
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
		else if(ChartsConstSetup[i].object == STATS_OBJ_Overheat2) Charts[j].add_Point(GETBIT(dEEV.get_flags(), fEEV_DirectAlgorithm) ? dEEV.OverheatTCOMP : dEEV.get_tOverheat());
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
}

// получить данные графика  в виде строки, данные ДОБАВЛЯЮТСЯ к str
void HeatPump::get_Chart(int index, char *str)
{
	if(--index < 0 || index > int(sizeof(Charts) / sizeof(Charts[0]) + sizeof(ChartsOnFlySetup) / sizeof(ChartsOnFlySetup[0]))) return;
	uint8_t obj, idx = index;
	if(idx < int(sizeof(ChartsModSetup) / sizeof(ChartsModSetup[0]))) {
		obj = ChartsModSetup[idx].object;
	} else {
		idx -= sizeof(ChartsModSetup) / sizeof(ChartsModSetup[0]);
		if(idx < int(sizeof(ChartsConstSetup) / sizeof(ChartsConstSetup[0]))) {
			obj = ChartsConstSetup[idx].object;
		} else obj = ChartsOnFlySetup[idx - sizeof(ChartsConstSetup) / sizeof(ChartsConstSetup[0])].object;
	}
	_itoa(obj, str);
	strcat(str, ";");
	switch (obj) {
#ifndef EEV_PREFER_PERCENT
	case STATS_OBJ_EEV:
		Charts[index].get_PointsStr(str);
		break;
#endif
#ifdef TCONOUT
	case STATS_OBJ_Overcool:
		if(Chart_PressTemp_PCON && Chart_Temp_TCONOUT) Charts[Chart_PressTemp_PCON].get_PointsStrSubDiv100(str, &Charts[Chart_Temp_TCONOUT]);
		break;
#endif
	case STATS_OBJ_TCOMP_TCON:
		if(Chart_Temp_TCOMP && Chart_PressTemp_PCON) Charts[Chart_Temp_TCOMP].get_PointsStrSubDiv100(str, &Charts[Chart_PressTemp_PCON]);
		break;
	case STATS_OBJ_Delta_GEO:
		if(Chart_Temp_TEVAING && Chart_Temp_TEVAOUTG) Charts[Chart_Temp_TEVAING].get_PointsStrSubDiv100(str, &Charts[Chart_Temp_TEVAOUTG]);
		break;
	case STATS_OBJ_Delta_OUT:
		if(Chart_Temp_TCONOUTG && Chart_Temp_TCONING) Charts[Chart_Temp_TCONOUTG].get_PointsStrSubDiv100(str, &Charts[Chart_Temp_TCONING]);
		break;
#ifdef FLOWEVA
	case STATS_OBJ_Power_GEO:
		if(Chart_Flow_FLOWEVA && Chart_Temp_TEVAOUTG && Chart_Temp_TEVAING) Charts[Chart_Flow_FLOWEVA].get_PointsStrPower(str, &Charts[Chart_Temp_TEVAOUTG], &Charts[Chart_Temp_TEVAING], sFrequency[FLOWEVA].get_Capacity());
		break;
#endif
#ifdef FLOWCON
	case STATS_OBJ_Power_OUT:
		if(Chart_Flow_FLOWCON && Chart_Temp_TCONING && Chart_Temp_TCONOUTG) Charts[Chart_Flow_FLOWCON].get_PointsStrPower(str, &Charts[Chart_Temp_TCONING], &Charts[Chart_Temp_TCONOUTG], sFrequency[FLOWCON].get_Capacity());
		break;
#endif
	default:
		Charts[index].get_PointsStrDiv100(str);
	}
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
	if(param == 'O') { // .object
		for(; STATS_OBJ_names[i]; i++) if(strcmp(value, STATS_OBJ_names[i]) == 0) break;
		if(STATS_OBJ_names[i] && ChartsModSetup[index].object != i) {
			ChartsModSetup[index].number = 0;
			ChartsModSetup[index].object = i;
		}
	} else if(param == 'X') { // .number
		if(ChartsModSetup[index].object == STATS_OBJ_Temp) {
			for(i = 0; i < TNUMBER; i++) if(strcmp(value, HP.sTemp[i].get_name()) == 0) break;
			if(i < TNUMBER) ChartsModSetup[index].number = i;
		} else if(ChartsModSetup[index].object == STATS_OBJ_Press || ChartsModSetup[index].object == STATS_OBJ_PressTemp) {
			for(i = 0; i < ANUMBER; i++) if(strcmp(value, HP.sADC[i].get_name()) == 0) break;
			if(i < ANUMBER) ChartsModSetup[index].number = i;
		} else if(ChartsModSetup[index].object == STATS_OBJ_Flow) {
			for(i = 0; i < FNUMBER; i++) if(strcmp(value, HP.sFrequency[i].get_name()) == 0) break;
			if(i < FNUMBER) ChartsModSetup[index].number = i;
		}
	}
}

// ---------------------------------------------------------------------------
// Веб-API: список рядов, состав изменяемых рядов, ответ /api/chart.
// Номер ряда id считается с 0 по тому же списку, что и chart.list: сначала ChartsModSetup (их состав правится),
// затем ChartsConstSetup и ChartsOnFlySetup. Прежний get_Chart(N) считал с 1 (нулевой пункт «---» был заглушкой).
// ---------------------------------------------------------------------------
#define CHARTS_ALL (CHARTS_MOD_COUNT + CHARTS_CONST_COUNT + CHARTS_ONFLY_COUNT)

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

// Номер для get_Chart (с 1) по id: число с 0 или имя (EEV, OH, OH2, TT - как раньше, а также любое имя STATS_OBJ_*). -1 - нет такого.
static int chart_index(const char *x)
{
	if(*x >= '0' && *x <= '9') { int n = atoi(x); return n < CHARTS_ALL ? n + 1 : -1; }
	uint8_t obj = 255, i;
	if(!strcmp(x, "EEV")) obj = STATS_OBJ_EEV;
	else if(!strcmp(x, "OH")) obj = STATS_OBJ_Overheat;
	else if(!strcmp(x, "OH2")) obj = STATS_OBJ_Overheat2;
	else if(!strcmp(x, "TT")) obj = STATS_OBJ_TCOMP_TCON;
	else for(i = 0; STATS_OBJ_names[i]; i++) if(!strcmp(x, STATS_OBJ_names[i])) { obj = i; break; }
	if(obj == 255) return -1;
	for(i = 0; i < CHARTS_CONST_COUNT; i++) if(ChartsConstSetup[i].object == obj) return CHARTS_MOD_COUNT + i + 1;
	for(i = 0; i < CHARTS_ONFLY_COUNT; i++) if(ChartsOnFlySetup[i].object == obj) return CHARTS_MOD_COUNT + CHARTS_CONST_COUNT + i + 1;
	return -1;
}

// /api/chart?id=<N|имя>  ->  {"obj":3,"d":[12.1,12.2,...]}. Прежний get_Chart пишет строку «тип;точка;точка;...;» без учёта длины,
// поэтому она формируется в свободной части буфера (запас на заголовок - 32 байта) и переписывается на месте: ';' -> ','.
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
	if(o->cap - o->len < 3700) return -2;                   // до 400 точек по 7 знаков + запас
	char *s = o->p + o->len + 32;
	s[0] = 0;
	HP.get_Chart(idx, s);
	char *data = strchr(s, ';');
	if(!data) { api_put(o, "{\"e\":\"fail\"}"); return o->over ? -2 : 0; }
	*data++ = 0;                                            // s - тип объекта, data - точки
	char head[28];
	char *h = head; strcpy(h, "{\"obj\":"); _itoa(atoi(s), h); strcat(h, ",\"d\":[");
	uint16_t hl = (uint16_t) strlen(h), dl = (uint16_t) strlen(data);
	char *w = o->p + o->len;
	memmove(w + hl, data, dl + 1);
	memcpy(w, h, hl);
	char *e = w + hl;
	for(uint16_t i = 0; i < dl; i++) if(e[i] == ';') e[i] = ',';
	if(dl && e[dl - 1] == ',') dl--;                         // последняя точка без хвостовой запятой
	e[dl] = ']'; e[dl + 1] = '}'; e[dl + 2] = 0;
	o->len += hl + dl + 2;
	return 0;
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
