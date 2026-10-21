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

// Реализация кольцевого буфера графиков. Обоснование и интерфейс - в RingChart.h.
//
// Единственная внешняя зависимость - mini-printf, который и так собирается
// отдельным объектным файлом. Ни Constant.h, ни Config.h, ни HeatPump.h здесь
// нет и быть не может: они определяют объекты и во вторую единицу трансляции
// не включаются.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "RingChart.h"
#include "mini-printf.h"

// Имена величин для веб-API. Порядок обязан совпадать с enum STATS_OBJ_* -
// индексом служит само значение перечисления. Завершается NULL: по нему
// Charts_set_param определяет конец списка при поиске имени.
const char * const STATS_OBJ_names[] = {
	"Temp",
	"PressTemp",
	"Overheat",
	"Overheat2",
	"Overcool",
	"TCOMP_TCON",
	"Delta_GEO",
	"Delta_OUT",
	"Press",
	"Flow",
	"EEV",
	"Voltage",
	"Power",
	"Power_FC",
	"Power_GEO",
	"Power_OUT",
	"Current_FC",
	"Compressor",
	"COP_Full",
	"Relay",
	NULL
};

// ---------------------------------------------------------------------------
// ---- Класс ГРАФИКИ ----
// ---------------------------------------------------------------------------
// Инициализация. points - ёмкость кольца: параметр, а не макрос CHART_POINT, потому что
// модуль не должен знать конфигурацию, а тесты гоняют кольцо на разных размерах.
bool statChart::init(uint16_t points)
{
	pos = 0;                                        // текущая позиция для записи
	num = 0;                                        // число накопленных точек
	flagFULL = false;                               // false в буфере менее size точек
	size = points;
	data = (CHART_DATA_TYPE *) malloc(sizeof(CHART_DATA_TYPE) * size);
	if(data == NULL) {                              // память не выделена
		size = 0;                                   // чтобы add_Point/get_Point не трогали NULL
		return false;
	}
	clear();
	return true;
}
// Очистить статистику
void statChart::clear()
{
	pos = 0;                                        // текущая позиция для записи
	num = 0;                                        // число накопленных точек
	flagFULL = false;                               // false в буфере менее size точек
	memset(data, 0, sizeof(CHART_DATA_TYPE) * size);
}

// добавить точку в массиве
void statChart::add_Point(CHART_DATA_TYPE y)
{
	data[pos] = y;
	if(pos < size - 1) {
		pos++;
		if(!flagFULL) num++;          // буфер пока не полный
	} else {
		pos = 0;
		if(!flagFULL) {
			flagFULL = true;
			num = size;        // буфер заполнен целиком
		}
	}
}

// получить точку нумерация 0-самая новая size-1 - самая старая, (работает кольцевой буфер)
// Без inline: определение живёт в этой единице трансляции, а зовут её из
// других файлов. Измерено, что GCC при -Os её и так не встраивал.
CHART_DATA_TYPE statChart::get_Point(uint16_t x)
{
	if(!flagFULL) return data[x];
	else {
		if((pos + x) < size) return data[pos + x];
		else return data[pos + x - size];
	}
}

// получить строку в которой перечислены все точки в строковом виде через; при этом значения делятся на 100
// строка не обнуляется перед записью
void statChart::get_PointsStrDiv100(char *&b)
{
	if(num == 0) return;
	b += m_strlen(b);
	for(uint16_t i = 0; i < num; i++) {
		b = dptoa(b, get_Point(i), 2);
		*b++ = ';';
		*b = '\0';
	}
}

// получить строку в которой перечислены все точки в строковом виде через;
// строка не обнуляется перед записью
void statChart::get_PointsStr(char *&b)
{
	if(num == 0) return;
	b += m_strlen(b);
	for(uint16_t i = 0; i < num; i++) {
		b += m_itoa(get_Point(i), b, 10, 0);
		*b++ = ';';
		*b = '\0';
	}
}

void statChart::get_PointsStrSubDiv100(char *&b, statChart *sChart)
{
	if(num == 0 || sChart->get_num() == 0) {
		return;
	}
	b += m_strlen(b);
	for(uint16_t i = 0; i < num; i++) {
		b = dptoa(b, get_Point(i) - sChart->get_Point(i), 2);
		*b++ = ';';
		*b = '\0';
	}
}
// Расчитать мощность на лету используется для графика потока, передаются указатели на графики температуры + теплоемкость
// График температур - сотые градуса, график потока это ДЕСЯТКИ литров в час
// Теплоемкость kfCapacity =  (3600*100/Capacity)
void statChart::get_PointsStrPower(char *&b, statChart *inChart, statChart *outChart, uint16_t Capacity)
{
	if(num == 0 || inChart->get_num() == 0 || outChart->get_num() == 0) {
		return;
	}
	b += m_strlen(b);
	for(uint16_t i = 0; i < num; i++) {
		b = dptoa(b, (int32_t)(outChart->get_Point(i) - inChart->get_Point(i)) * get_Point(i) / 10 * Capacity / 3600, 3); // выходная мощность в кВт
		*(b - 1) = ';';
	}
}

// Позиция величины в таблице графиков, CHART_NOT_FOUND если её там нет.
// Смысл нуля как признака отсутствия разобран в RingChart.h.
uint8_t chart_find(const Charts_Mod_setup *setup, uint8_t count, uint8_t object, uint8_t number)
{
	for(uint8_t i = 0; i < count; i++)
		if(setup[i].object == object && setup[i].number == number) return i;
	return CHART_NOT_FOUND;
}

// ---------------------------------------------------------------------------
//  История на SD: чистые расчёты. Описание - в RingChart.h.
// ---------------------------------------------------------------------------
bool HistoryDetailUpdate(HistoryDetail &d, bool starting, bool compOn, uint32_t now, uint16_t duration)
{
	if(!d.inited) {
		d.inited = true;
		d.prevStarting = starting;
		d.prevComp = compOn;
		d.hold = starting && !compOn;                        // подхватили контроллер посреди пуска
	} else {
		if(starting && !d.prevStarting && !compOn) d.hold = true;   // начало пуска
		if(compOn != d.prevComp) {                           // компрессор включился или выключился
			d.hold = false;
			d.until = now + duration;
		}
		d.prevStarting = starting;
		d.prevComp = compOn;
	}
	if(d.hold && !starting && !compOn) {                     // пуск сорвался, компрессор так и не включился
		d.hold = false;
		d.until = now + duration;
	}
	return d.hold || now < d.until;
}

bool HistoryDue(HistoryTick &t, uint32_t now, bool detailOpen, uint16_t detailPeriod)
{
	uint32_t m = now / 60;
	uint32_t s = detailPeriod ? now / detailPeriod : 0;
	if(!t.inited) {
		t.inited = true;
		t.minSlot = m;
		t.detSlot = s;
		return false;
	}
	bool due = false;
	if(m != t.minSlot) {
		t.minSlot = m;
		due = true;
	}
	if(detailOpen && detailPeriod && s != t.detSlot) due = true;
	t.detSlot = s;                                           // пока окно закрыто, слот следует за временем: при открытии записи сразу не будет
	return due;
}

uint32_t HistoryFileSize(uint16_t recLen, uint16_t days)
{
	uint32_t bytes = (uint32_t) recLen * (HISTORY_MINUTE_REC_PER_DAY + HISTORY_DETAIL_RESERVE_PER_DAY) * days;
	return (bytes / HISTORY_SD_BLOCK + 1) * HISTORY_SD_BLOCK;
}

static bool history_leap(uint16_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static uint16_t history_day_of_year(uint16_t year, uint8_t month, uint8_t day)   // 1..366
{
	static const uint8_t md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	uint16_t n = day;
	for(uint8_t m = 1; m < month && m <= 12; m++) n += md[m - 1] + (m == 2 && history_leap(year) ? 1 : 0);
	return n;
}

uint32_t HistoryMinutesToYearEnd(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute)
{
	uint32_t total = (history_leap(year) ? 366UL : 365UL) * 1440UL;
	uint32_t elapsed = (history_day_of_year(year, month, day) - 1UL) * 1440UL + hour * 60UL + minute;
	return elapsed >= total ? 0 : total - elapsed;
}

bool HistoryDetailAllowed(uint32_t blocksLeft, uint32_t minutesLeft, uint16_t recLen)
{
	uint32_t need = minutesLeft * (uint32_t) recLen / HISTORY_SD_BLOCK + HISTORY_DETAIL_MARGIN_BLOCKS;
	return blocksLeft > need;
}

static bool history_digits(const char *s, uint8_t n, uint16_t &v)
{
	v = 0;
	for(uint8_t i = 0; i < n; i++) {
		if(s[i] < '0' || s[i] > '9') return false;
		v = v * 10 + (s[i] - '0');
	}
	return true;
}

uint32_t HistoryStampSecond(const char *s, uint8_t len)
{
	uint16_t y, mo, d, h, mi, se = 0;
	if(len != 12 && len != 14) return 0xFFFFFFFF;
	if(!history_digits(s, 4, y) || !history_digits(s + 4, 2, mo) || !history_digits(s + 6, 2, d) ||
	   !history_digits(s + 8, 2, h) || !history_digits(s + 10, 2, mi)) return 0xFFFFFFFF;
	if(len == 14 && !history_digits(s + 12, 2, se)) return 0xFFFFFFFF;
	if(mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 59) return 0xFFFFFFFF;
	return (history_day_of_year(y, (uint8_t) mo, (uint8_t) d) - 1UL) * 86400UL + h * 3600UL + mi * 60UL + se;
}

bool HistoryDecimate(uint32_t &lastStep, uint32_t second, uint32_t step)
{
	if(step == 0) return true;
	uint32_t n = second / step;
	if(n == lastStep) return false;
	lastStep = n;
	return true;
}

// Прореживание выдачи истории: из len байт оставляет строки, у которых «номер шага» (секунда года / step) новый;
// из каждого шага проходит первая запись. Строки без разбираемой метки (шапка csv) и строки-описания раскладки
// (после метки идёт ";#") проходят всегда. Сжатие на месте, возвращает длину результата. last перед первым вызовом = 0xFFFFFFFF.
uint32_t HistoryThin(char *b, uint32_t len, uint32_t step, uint32_t &last)
{
	uint32_t r = 0, w = 0;
	while(r < len) {
		char *nl = (char*) memchr(b + r, '\n', len - r);
		uint32_t ll = nl ? (uint32_t)(nl - (b + r)) + 1 : len - r;		// длина строки вместе с '\n'
		bool pass = true;
		char *sc = (char*) memchr(b + r, ';', ll);
		if(sc && !(sc + 1 < b + r + ll && sc[1] == '#')) {
			uint32_t sec = HistoryStampSecond(b + r, (uint8_t)(sc - (b + r)));
			if(sec != 0xFFFFFFFF) pass = HistoryDecimate(last, sec, step);
		}
		if(pass) {
			if(w != r) memmove(b + w, b + r, ll);
			w += ll;
		}
		r += ll;
	}
	return w;
}

uint32_t HistoryDropMarkers(char *b, uint32_t len)
{
	uint32_t r = 0, w = 0;
	while(r < len) {
		char *nl = (char*) memchr(b + r, '\n', len - r);
		uint32_t ll = nl ? (uint32_t)(nl - (b + r)) + 1 : len - r;		// длина строки вместе с '\n'
		char *sc = (char*) memchr(b + r, ';', ll);
		if(!(sc && sc + 1 < b + r + ll && sc[1] == '#')) {
			if(w != r) memmove(b + w, b + r, ll);
			w += ll;
		}
		r += ll;
	}
	return w;
}
