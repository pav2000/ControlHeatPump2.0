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
 * Кольцевой буфер графиков.
 *
 * Модуль: RingChart.cpp - отдельная единица трансляции, не знающая ни о
 * HeatPump, ни о конфигурации. Размер кольца задаётся параметром init(),
 * об ошибке выделения памяти сообщается возвратом, а не вызовом set_Error, -
 * обработка ошибок не дело кольцевого буфера. Благодаря этому модуль
 * проверяется обычными тестами (tests/test_ringchart.cpp).
 *
 * Связка с состоянием ТН (Charts_get_param/Charts_set_param, они читают
 * HP.sTemp/sADC/sFrequency) живёт в HeatPumpCharts.cpp.
 *
 * Типы ниже нужны и Config.h, где описаны таблицы графиков, поэтому
 * заголовок включается из Constant.h и виден всему проекту. Определять
 * здесь объекты нельзя - только объявлять.
 */
#ifndef RingChart_h
#define RingChart_h

// Заголовок обязан быть самодостаточным: его включает и модуль RingChart.cpp,
// где никакого Arduino.h нет.
#include <stdint.h>

// ---- Определения нужные в этом файле ----
//  Перечисляемый тип -ТИПЫ датчиков сухой контакт
enum TYPE_SENSOR
{
	pALARM,                    // 0 Аварийный датчик, его срабатываение приводит к аварии и останове Тн
	pSENSOR,                   // 1 Обычный датчик, его значение используется в алгоритмах ТН, его изменение не приводит к ошибке.
	pPULSE,                    // 2 Импульсный висит на прерывании и считает частоты - выходная величина ЧАСТОТА не используется
	pEND13                     // Обязательно должен быть последним, добавляем ПЕРЕД!!!
};

enum {
	STATS_OBJ_Temp			= 0,	// °C
	STATS_OBJ_PressTemp,			// °C
	STATS_OBJ_Overheat, 	//= 2,
	STATS_OBJ_Overheat2,
	STATS_OBJ_Overcool,		//= 4,
	STATS_OBJ_TCOMP_TCON,	//= 5,
	STATS_OBJ_Delta_GEO,
	STATS_OBJ_Delta_OUT,
	STATS_OBJ_Press,		//= 8,	// bar
	STATS_OBJ_Flow,			//= 9,	// м³ч
	STATS_OBJ_EEV,			//= 10,	// ЭРВ
	STATS_OBJ_Voltage,		//= 11,	// V
	STATS_OBJ_Power,		//= 12,	// кВт*ч, пока только TYPE_SUM для OBJ_*
	STATS_OBJ_Power_FC,
	STATS_OBJ_Power_GEO,
	STATS_OBJ_Power_OUT,
	STATS_OBJ_Current_FC,	//= 16,
	STATS_OBJ_Compressor,	//= 17,
	STATS_OBJ_COP_Full,		//= 18,
	STATS_OBJ_Relay,		//= 19
	STATS_OBJ_HPState		//= 20, состояние и режим ТН одним числом: состояние*100 + режим. Только история: в STATS_OBJ_names его нет, графиками он не выбирается
};

// Имена для веб-API и Nextion. Определены в RingChart.cpp: заголовок не
// имеет права определять объекты, он включается двумя единицами трансляции.
extern const char * const STATS_OBJ_names[];

enum {
	STATS_EEV_Steps = 0,	// Шаги
	STATS_EEV_Percent,		// %
	STATS_EEV_OverHeat,		// Перегрев
	STATS_EEV_OverCool,		// Переохлаждение
	STATS_EEV_OHTarget,		// Цель перегрева (только история)
	STATS_EEV_OverHeat2,	// Перегрев на входе компрессора (только история)
};

struct History_setup {
	uint8_t		object;		// STATS_OBJ_*
	uint8_t		number;		// номер датчика
	// Заголовок колонки. NULL - спросить имя у самого датчика (history_name()
	// в Statistics.cpp). Ссылку вида TempSetup[TOUT].name сюда ставить нельзя:
	// она не константа на этапе компоновки и уводит всю таблицу из flash в
	// SRAM. Проверяется адресом символа HistorySetup, см. Config.h.
	const char	*name;
	// Точность записи температур и перегревов: 0 - десятые доли, 2 - сотые. В шапке файла и
	// hist2_head.csv сотые помечаются цифрой 2 после буквы оси; страница делит на 100 вместо 10.
	uint8_t		dec;
};

struct Charts_Mod_setup {
	uint8_t		object;		// STATS_OBJ_*
	uint8_t		number;		// номер датчика
} __attribute__((packed));

struct Charts_Const_setup {
	uint8_t		object;		// STATS_OBJ_*
	const char	*name;
} __attribute__((packed));

/*
 * Поиск графика в таблице по величине и номеру датчика.
 *
 * ЗАЧЕМ ЭТО ЗДЕСЬ. Несколько графиков считаются как разность двух других -
 * переохлаждение, дельты контуров, мощности. Чтобы их посчитать, нужно знать
 * позиции слагаемых в таблице, а без отдельной функции они искались бы девятью
 * вложенными if внутри HeatPump::clearChart. Ошибка там не падает и не
 * логируется: график просто перестаёт рисоваться или показывает чужую
 * величину.
 *
 * "НЕ НАЙДЕНО" ОБОЗНАЧАЕТСЯ НУЛЁМ, и это сохранённое поведение, а не выбор.
 * Ноль - ещё и вполне законный индекс первого графика, поэтому соглашение
 * работает только пока ни одна из искомых величин не стоит в таблице первой.
 * Сегодня первым стоит TOUT, которого среди искомых нет, - и это закреплено
 * тестом: переставит кто-нибудь таблицу, тест упадёт раньше, чем пользователь
 * увидит пустой график. Заменить ноль на честный признак нельзя в одиночку:
 * в WebFiles.cpp индексы используются БЕЗ проверки, и любое значение вне
 * диапазона стало бы чтением за границей массива. Записано в задачи.
 */
#define CHART_NOT_FOUND 0

uint8_t chart_find(const Charts_Mod_setup *setup, uint8_t count, uint8_t object, uint8_t number);

typedef int16_t CHART_DATA_TYPE;

// ---------------------------------------------------------------------------
// ---- Класс Графики работы ----
// ---------------------------------------------------------------------------
// Бинарные данные могут хранится в упакованном виде до 16 значений в одной точке.
// запихиваем уже упакованное значение
// читаем get_boolPointsStr
class statChart                                         // организован кольцевой буфер
{
public:
	// Выделяет буфер на points точек. false - память не выделена; что делать
	// в этом случае, решает вызывающий (в прошивке - set_Error).
	//
	// Зовётся ОДИН раз за включение (HeatPump::initHeatPump из Control.ino) и
	// буфер не освобождает: в прошивке память графиков живёт до перезагрузки,
	// освобождать её некуда и незачем. Повторный вызов на том же объекте
	// потерял бы прежний буфер, поэтому так делать нельзя. Освобождения нет
	// сознательно: деструктор потребовал бы конструктора для 22 глобальных
	// объектов, то есть цикла инициализации на старте вместо нулей в bss.
	bool init(uint16_t points);
	void clear();                                         // очистить статистику
	void add_Point(CHART_DATA_TYPE y);                    // добавить точку в массиве (для бинарных данных добавляем все точки сразу)
	CHART_DATA_TYPE get_Point(uint16_t x);                // получить точку нумерация 0-самая старая, size-1 - самая новая (работает кольцевой буфер)

	void get_PointsStrDiv100(char *&b);             		// получить строку в которой перечислены все точки в строковом виде через ";" при этом значения делятся на 100
	void get_PointsStr(char *&b);							// получить строку в которой перечислены все точки в строковом виде через ";"
	void get_PointsStrSubDiv100(char *&b, statChart *sChart); // получить строку, вычесть точки sChart
	void get_PointsStrPower(char *&b, statChart *inChart, statChart *outChart, uint16_t Capacity); // Расчитать мощность на лету используется для графика потока, передаются указатели на температуры

	inline uint16_t get_num()  {return num;}              // Получить число накопленных точек
	inline uint16_t get_size() {return size;}             // Ёмкость кольца, задана в init()

private:
	CHART_DATA_TYPE *data;                                // указатель на массив для накопления данных
	uint16_t size;                                        // ёмкость кольца
	int16_t pos;                                          // текущая позиция для записи
	int16_t num;                                          // число накопленных точек
	bool flagFULL;                                        // false в буфере менее size точек
};

// Реализованы в HeatPumpCharts.cpp, а не в модуле: читают HP.sTemp/sADC/sFrequency.
void Charts_get_param(uint8_t index, char param, char *ret);
void Charts_set_param(uint8_t index, char param, char *value);

// ---------------------------------------------------------------------------
//  История на SD: чистые расчёты, без железа и без HeatPump. Тесты - tests/test_ringchart.cpp.
//  Сама запись, файлы и веб - Statistics.cpp; здесь только то, что можно проверить на хосте.
// ---------------------------------------------------------------------------
#define HISTORY_SD_BLOCK                512     // блок SD, как SD_BLOCK в Statistics.h
#define HISTORY_STAMP_LEN               14      // метка записи ггггммддччммсс
#define HISTORY_MINUTE_REC_PER_DAY      1440    // записи раз в минуту, при работе круглые сутки
#define HISTORY_DETAIL_RESERVE_PER_DAY  720     // резерв файла на детализацию: ~24 события по 30 записей
#define HISTORY_DETAIL_MARGIN_BLOCKS    64      // запас блоков, который детализация не трогает

// Окно детализации ВКЛ/ВЫКЛ компрессора. Открывается началом пуска (starting) и остаётся открытым, пока
// компрессор не включится; после включения и после выключения компрессора держится ещё duration секунд.
// Пуск, сорванный до включения компрессора, тоже заканчивается хвостом duration. Повторное событие продлевает окно.
struct HistoryDetail {
	uint32_t until;        // конец окна, unixtime, секунды; 0 - хвоста нет
	bool     hold;         // идёт пуск и компрессор ещё не включился
	bool     prevStarting; // прошлые значения для поиска фронтов
	bool     prevComp;
	bool     inited;
};
// Вызывать раз в секунду. Возвращает true, пока окно открыто. Первый вызов только запоминает состояние.
bool HistoryDetailUpdate(HistoryDetail &d, bool starting, bool compOn, uint32_t now, uint16_t duration);

// Когда писать запись: раз в минуту (смена минуты) и, пока окно открыто, при смене слота detailPeriod секунд.
// Если подошли оба срока - одна запись. Первый вызов только запоминает слоты.
struct HistoryTick {
	uint32_t minSlot;
	uint32_t detSlot;
	bool     inited;
};
bool HistoryDue(HistoryTick &t, uint32_t now, bool detailOpen, uint16_t detailPeriod);

// Размер годового файла истории в байтах (кратен блоку SD) на days дней при длине записи recLen.
uint32_t HistoryFileSize(uint16_t recLen, uint16_t days);
// Сколько минут осталось до конца года (включая текущую).
uint32_t HistoryMinutesToYearEnd(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute);
// Можно ли писать детализацию: свободных блоков должно хватить на минутные записи до конца года и запас.
bool HistoryDetailAllowed(uint32_t blocksLeft, uint32_t minutesLeft, uint16_t recLen);

// Секунда года из метки записи "ггггммддччммсс" (14 знаков) или "ггггммддччмм" (12): для прореживания выдачи.
// 0xFFFFFFFF - метка не разобрана.
uint32_t HistoryStampSecond(const char *s, uint8_t len);
// Прореживание: true - запись отправить. lastStep перед первым вызовом = 0xFFFFFFFF. step = 0 - отправлять всё.
bool HistoryDecimate(uint32_t &lastStep, uint32_t second, uint32_t step);
// Прореживание буфера со строками: оставляет первую запись каждого шага step секунд, строки без разбираемой метки (шапка)
// и строки-описания раскладки (после метки идёт ";#") пропускает всегда. Сжатие на месте, возвращает длину результата.
// last перед первым вызовом = 0xFFFFFFFF; строка без '\n' в конце считается строкой.
uint32_t HistoryThin(char *b, uint32_t len, uint32_t step, uint32_t &last);
// Убирает из буфера со строками строки-описания раскладки (после метки идёт ";#"): в скачиваемом csv достаточно шапки.
// Сжатие на месте, возвращает длину результата.
uint32_t HistoryDropMarkers(char *b, uint32_t len);

#endif
