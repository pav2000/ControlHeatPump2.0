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
//
#ifndef Statistics_h
#define Statistics_h
#include "../../Constant.h"
#include "SdFat.h"
// SD-карта. Создаётся в Control.ino - статистика и история пишутся прямо
// на неё блоками, минуя FAT.
extern SdFat card;   // SdFile и fname_t: заголовок обязан быть самодостаточным

//#define STATS_DO_NOT_SAVE
STRCONST(format_date,  "%04d%02d%02d");  			// yyyymmdd
#define format_date_size 8
STRCONST(format_datetime,  "%04d%02d%02d%02d%02d%02d");	// yyyymmddHHMMSS
#define format_datetime_size 14

#define SD_BLOCK					512
#define STATS_MAX_RECORD_LEN		(format_date_size + 1 + sizeof(Stats_data) / sizeof(Stats_data[0]) * 8)
#define STATS_MAX_FILE_SIZE(days)	((STATS_MAX_RECORD_LEN * days / SD_BLOCK + 1) * SD_BLOCK)

#define HISTORY_MAX_FIELD_LEN		6
#define HISTORY_MAX_RECORD_LEN		(format_datetime_size + 1 + sizeof(HistorySetup) / sizeof(HistorySetup[0]) * HISTORY_MAX_FIELD_LEN)
// Размер файла истории на год - HistoryFileSize() в RingChart.h (минутные записи и резерв на детализацию)

#define MAX_INT32_VALUE 			2147483647
#define MIN_INT32_VALUE 			-2147483647

#define WEB_SEND_FILE_PAUSE			10	// ms
#define CREATE_STATFILE_PAUSE		10	// ms

// what:
#define ID_STATS 	0
#define ID_HISTORY	1
#define ID_HISTORY_OLD	2	// прежний формат hist_ГГГГ.dat (минутные записи, метка до минуты): только чтение и список файлов

enum {
	STATS_TYPE_MIN = 0,
	STATS_TYPE_AVG,
	STATS_TYPE_MAX,
	STATS_TYPE_SUM,
	STATS_TYPE_TIME // Time, ms
};

enum { // когда
	STATS_WHEN_ALWAYS = 0,
	STATS_WHEN_WORKD			// Во время работы компрессора, прошло STATS_WORKD_TIME
	//STATS_WORK				// Во время работы компрессора
};
#define STATS_WORKD_TIME 100000 // ms


struct Stats_Data {
	int32_t		value;			// Для среднего, макс единица: +-1491308
	uint8_t		object;			// STATS_OBJ_*
	uint8_t		type;			// STATS_TYPE_*
	uint8_t		when;			// STATS_WHEN_*
	uint8_t 	number;			// номер датчика
};

// Состав суточной статистики. Размер нужен в объявлении: STATS_MAX_RECORD_LEN
// ниже считает им длину записи, и ещё семь мест берут sizeof(Stats_data).
// Совпадение числа строк с STATS_DATA_COUNT закреплено static_assert
// в Statistics.cpp.
#define STATS_DATA_COUNT 15
extern Stats_Data Stats_data[STATS_DATA_COUNT];

STRCONST(stats_file_start,  "stats_");
STRCONST(stats_file_header,  "head");
STRCONST(stats_file_ext,  ".dat");
STRCONST(stats_csv_file_ext,  ".csv");
STRCONST(history_file_start,  "hist_");		// прежний формат: метка до минуты, 16 столбцов; файлы остаются на карте
// Шапки прежнего формата hist_ГГГГ.dat: столбцы там фиксированы, состав сегодняшней таблицы HistorySetup к ним не относится
STRCONST(history_old_header, "Время;TOUT;TIN;TBOILER;TCOMP;TEVAING;TEVAOUTG;TCONING;TCONOUTG;TEVAOUT;FLOWCON;EEV;OverHeat;Freq;power220;powerCO;fullCOP\n");
STRCONST(history_old_header_axes, "Время;TTOUT;TTIN;TTBOILER;TTCOMP;TTEVAING;TTEVAOUTG;TTCONING;TTCONOUTG;TTEVAOUT;FFLOWCON;SEEV;TOverHeat;HFreq;Wpower220;WpowerCO;CfullCOP\n");
STRCONST(history2_file_start, "hist2_");		// текущий формат: метка с секундами, строка-описание раскладки в потоке
extern uint8_t stats_buffer[SD_BLOCK];
extern uint8_t history_buffer[SD_BLOCK];

class Statistics
{
public:
	Statistics() { NewYearFlag = 0; yearFiles[0] = yearFiles[1] = yearFiles[2] = 0; historyNeedMarker = true; historySaveFail = 0; }
	void	ScanYears();									// Узнать, за какие годы есть файлы stats_/hist_ (при старте); дальше карта дополняется созданием файлов
	bool	HasYear(uint8_t what, uint16_t y) { return y > 2000 && y < 2065 && ((yearFiles[what] >> (y - 2001)) & 1); }   // what: 0 - статистика, 1 - история
	void	Init(uint8_t newyear = 0);
	void	Update();										// Обновить статистику, раз в период
	void	UpdateEnergy();									// Обновить энергию и COP, вызывается часто
	void	Reset();										// Сбросить накопленные промежуточные значения
	int8_t	SaveStats(uint8_t newday);						// Записать статистику на SD
	int8_t 	SaveHistory(uint8_t from_web);					// Записать буфер истории на SD
	void	StatsFileHeader(char *buffer, uint8_t flag);	// Возвращает файл с заголовками полей
	void	StatsFieldHeader(char *ret, uint8_t i, uint8_t flag);
	inline void	StatsFileString(char *ret);					// Строка со значениями за день
	void	StatsFieldString(char **ret, uint8_t i);
	void	StatsWebTable(char *ret);
	void 	HistoryFileHeader(char *ret, uint8_t flag);		// Возвращает файл с заголовками полей
	void	SendFileData(uint8_t thread, SdFile *File, char *filename, bool noMarkers = false); // Отправить в сокет данные файла (noMarkers - без строк-описаний раскладки истории)
	bool	SendFileChunk(uint8_t thread, uint32_t *len, bool noMarkers, bool last);
	void	SendFileDataByPeriod(uint8_t thread, SdFile *File, char *Prefix, char *TimeStart, char *TimeEnd, uint32_t step = 0); // Отправить в сокет данные файла, обрезанные по датам
	boolean	FindEndPosition(uint8_t what);
	void	CheckCreateNewFile();
	int8_t	CreateOpenFile(uint8_t what);
	void	History();										// Логирование параметров работы ТН: раз в минуту, в окне детализации чаще
	bool	HistoryDetailRoom();							// Хватит ли места в файле на минутные записи до конца года, если писать детализацию
	bool	HistoryAppend(const char *data, uint16_t len);
	bool	HistoryNextBlock();								// Заполненный блок истории - на SD и переход к следующему
	void	HistoryFlushPending();							// Раз в секунду: догнать запись заполненного блока	// Дописать в буфер истории, заполненный блок уходит на SD
	void	HistoryMarker();								// Строка-описание раскладки столбцов в потоке данных
private:
	uint16_t historySaveFail;								// подряд неудавшихся записей блока истории (для журнала)
	bool	 historyNeedMarker;								// перед первой записью после запуска и после (пере)открытия файла писать строку-описание
	uint64_t yearFiles[3];									// бит (год - 2001): файл за год есть; список для веба берётся отсюда, без обращения к SD в веб-задаче (стек)
	void	Error(const char *text, uint8_t what);
	uint16_t counts;										// Кол-во уже совершенных обновлений
	uint16_t counts_work;									// Кол-во уже совершенных обновлений во время работы компрессора
	uint32_t compressor_on_timer;
	uint32_t previous;
	uint8_t	 day;
	uint8_t	 month;
	uint16_t year;
	uint8_t  NewYearFlag;
	uint32_t BlockStart;
	uint32_t BlockEnd;
	uint32_t CurrentBlock;
	uint16_t CurrentPos;
	uint32_t HistoryBlockStart;
	uint32_t HistoryBlockEnd;
	uint32_t HistoryCurrentBlock;
	uint16_t HistoryCurrentPos;
	uint32_t HistoryBlockCreating;
	SdFile   StatsFile;
};

// Объект статистики. Создаётся в Statistics.cpp.
extern Statistics Stats;

#endif
