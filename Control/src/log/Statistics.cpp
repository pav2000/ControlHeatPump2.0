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
 * Статистика и история: отдельная единица трансляции.
 *
 * Statistics.h тянет Constant.h, а с ним Config.h, поэтому шесть условий
 * (STATS_DO_NOT_SAVE, STATS_USE_BUFFER_FOR_SAVING, STATS_SKIP_COP_WHEN_RELAY_ON,
 * USE_ELECTROMETER_SDM, EEV_DEF, TEST_BOARD) разрешаются так же, как в главной
 * единице трансляции. Проверено сверкой размеров методов.
 */
#include "Statistics.h"
#include "../core/HeatPump.h"     // HP, датчики, PressToTemp
#include "../hw/Hardware.h"     // eepromI2C, I2C-обёртки
#include "Journal.h"      // journal
#include "../net/Network.h"      // sendPacketRTOS, sendConstRTOS, переключатели SPI
#include "../core/Rtos.h"         // _delay, замки шин, Watchdog_AliveHere
#include "../util/Util.h"         // _itoa, my_atof, int_to_dec_str
#include "../util/DateTime.h"     // NowDateToStr
#include "../net/WebShared.h"    // Socket[]
#include "SdFat.h"


// Объявлены в Statistics.h - создаются здесь, в единственном экземпляре. Определение
// объекта в заголовке запрещает включить его во вторую единицу трансляции.
Stats_Data Stats_data[STATS_DATA_COUNT] = {
	{ 0, STATS_OBJ_Temp, STATS_TYPE_MIN, STATS_WHEN_ALWAYS, TOUT },
	{ 0, STATS_OBJ_Temp, STATS_TYPE_AVG, STATS_WHEN_ALWAYS, TOUT },
	{ 0, STATS_OBJ_Temp, STATS_TYPE_MAX, STATS_WHEN_ALWAYS, TOUT },
	{ 0, STATS_OBJ_Temp, STATS_TYPE_AVG, STATS_WHEN_ALWAYS, TIN },
	{ 0, STATS_OBJ_Temp, STATS_TYPE_AVG, STATS_WHEN_WORKD, TEVAING },
	{ 0, STATS_OBJ_Temp, STATS_TYPE_AVG, STATS_WHEN_ALWAYS, TBOILER },
	{ 0, STATS_OBJ_Power_OUT, STATS_TYPE_SUM, STATS_WHEN_WORKD, 0 },	// #6
	{ 0, STATS_OBJ_Power, STATS_TYPE_SUM, STATS_WHEN_ALWAYS, 0 },// #7
	{ 0, STATS_OBJ_Power, STATS_TYPE_MAX, STATS_WHEN_ALWAYS, 0 },
	{ 0, STATS_OBJ_COP_Full, STATS_TYPE_MIN, STATS_WHEN_WORKD, 0 },
	{ 0, STATS_OBJ_COP_Full, STATS_TYPE_AVG, 6, 7 }, // индексы Stats_data[]: STATS_OBJ_Power(SUM, OBJ_powerCO) / STATS_OBJ_Power(SUM, OBJ_power220)
	{ 0, STATS_OBJ_Voltage, STATS_TYPE_MIN, STATS_WHEN_ALWAYS, 0 },
	{ 0, STATS_OBJ_Voltage, STATS_TYPE_AVG, STATS_WHEN_ALWAYS, 0 },
	{ 0, STATS_OBJ_Voltage, STATS_TYPE_MAX, STATS_WHEN_ALWAYS, 0 },
	{ 0, STATS_OBJ_Compressor, STATS_TYPE_TIME, STATS_WHEN_ALWAYS, 0 }
};
uint8_t stats_buffer[SD_BLOCK];
uint8_t history_buffer[SD_BLOCK];
Statistics Stats;

#include "../core/HeatPump.h"
#include "SdFat.h"

#define temp_initbuf Socket[0].outBuf
char filename[sizeof(stats_file_start)-1 + 4 + sizeof(stats_file_ext)];

// what: 0 - Stats, 1 - History, Return: OK or Error
int8_t Statistics::CreateOpenFile(uint8_t what)
{
	if(!HP.get_fSD()) return ERR_SD_INIT;
	uint8_t newfile = 0;
	if(what == ID_HISTORY) {
		if(HistoryBlockCreating) goto xContinue;
		HistoryBlockStart = 0;
		HistoryCurrentPos = 0;
		memset(history_buffer, 0, SD_BLOCK);	// остатки прежнего содержимого не должны попасть в начало файла
		historyNeedMarker = true;
		strcpy(filename, history2_file_start);
	} else {
		BlockStart = 0;
		CurrentPos = 0;
		strcpy(filename, stats_file_start);
	}
	_itoa(year, filename);
	strcat(filename, stats_file_ext);
	journal.jprintf(" File: %s ", filename);
	SPI_switchSD();
	if(!StatsFile.opens(filename, O_READ, &open_fname)) {
		uint16_t days = month == 1 ? 366 : (12 - month + 1) * 31;
		if(!StatsFile.createContiguous(filename, what ? HistoryFileSize((uint16_t) HISTORY_MAX_RECORD_LEN, days) : STATS_MAX_FILE_SIZE(days))) {
			Error("create", what);
			return ERR_SD_WRITE;
		} else {
			StatsFile.timestamp(T_CREATE | T_ACCESS | T_WRITE, rtcSAM3X8.get_years(), rtcSAM3X8.get_months(), rtcSAM3X8.get_days(), rtcSAM3X8.get_hours(), rtcSAM3X8.get_minutes(), rtcSAM3X8.get_seconds());
		}
		newfile = 1;
	}
	if(year > 2000 && year < 2065) yearFiles[what] |= (uint64_t)1 << (year - 2001);   // файл за этот год есть (открыт или только что создан)
	if(!StatsFile.contiguousRange(what ? &HistoryBlockStart : &BlockStart, what ? &HistoryBlockEnd : &BlockEnd)) {
		Error("get blocks", what);
	} else {
		journal.jprintf("[%u..%u] ", (unsigned int)(what ? HistoryBlockStart : BlockStart), (unsigned int)(what ? HistoryBlockEnd : BlockEnd));
		if(newfile) {
			journal.jprintf_time("Create ");
			uint32_t b;
			if(what) {
xContinue:		if(HistoryBlockCreating) b = HistoryBlockCreating; else b = HistoryCurrentBlock = HistoryBlockStart;
			} else {
				b = CurrentBlock = BlockStart;
			}
			uint8_t *temp_buf = (uint8_t*)malloc(SD_BLOCK);
			if(temp_buf == NULL) {
				Error("memory low", what);
				StatsFile.remove();
				return ERR_OUT_OF_MEMORY;
			}
			memset(temp_buf, 0, SD_BLOCK);
			for(; b <= (what ? HistoryBlockEnd : BlockEnd);) {
				Watchdog_AliveHere();
				if(!card.card()->writeBlock(b, temp_buf)) {
					Error("empty", what);
					free(temp_buf);
					goto xError;
				}
				if(((++b) & 0x1FF) == 0) {
					if((b & 0x7FF) == 0) journal.jprintf("."); // каждый 1 Мб
					if(what) { // время другим задачам (~200 bps)
						HistoryBlockCreating = b;
						free(temp_buf);
						_delay(CREATE_STATFILE_PAUSE);
						return OK;
					}
				}
			}
			if(what) HistoryBlockCreating = 0;
			journal.jprintf_time("\n Ok\n");
			free(temp_buf);
			return OK;
		} else if(!FindEndPosition(what)) {
			journal.jprintf("Endpos not found!\n");
		} else {
			return OK;
		}
	}
xError:
	StatsFile.close();
	return ERR_SD_WRITE;
}

boolean Statistics::FindEndPosition(uint8_t what)
{
	uint8_t *buffer, *pos = NULL;
	uint32_t bst, bend, cur;
	if(what) {
		bst = HistoryBlockStart;
		bend = HistoryBlockEnd;
		buffer = history_buffer;
	} else {
		bst = BlockStart;
		bend = BlockEnd;
		buffer = stats_buffer;
	}
	while(bst <= bend) {
		Watchdog_AliveHere();
		cur = bst + (bend - bst) / 2;
		if(!card.card()->readBlock(cur, buffer)) {
			Error("FindPos", what);
			break;
		}
		if(*buffer) {
			if((pos = (uint8_t*)memchr(buffer, 0, SD_BLOCK))) break;
			bst = cur + 1;
			if(bst > bend) {
				if(bend < (what ? HistoryBlockEnd : BlockEnd)) bend++; else break; // file overflow
			}
		} else if(cur == bst) { // empty
			pos = buffer;
			break;
		} else bend = cur - 1;
	}
	if(pos == NULL) return false;
	if(pos == buffer || *(pos-1) != '\n') { // Обрезанные данные - пропускаем
		journal.jprintf("*CUT* ");
		if(pos != buffer) {
xCutSearch:	while(--pos >= buffer) if(*pos == '\n') break;
			pos++;
		}
		if(pos == buffer && cur > (what ? HistoryBlockStart : BlockStart)) {
			if(!card.card()->readBlock(--cur, buffer)) {
				Error("FindPos", what);
				return false;
			}
			pos = buffer + SD_BLOCK;
			goto xCutSearch;
		}
	}
	if(what) {
		HistoryCurrentBlock = cur;
		HistoryCurrentPos = pos - buffer;
	} else {
		CurrentBlock = cur;
		CurrentPos = pos - buffer;
	}
	journal.jprintf("End pos: %u/%u\n", (unsigned int)(cur), pos - buffer);
	return true;
}

void Statistics::Error(const char *text, uint8_t what)
{
	journal.jprintf(" %s Error %s (%d,%d)!\n", what ? "History" : "Stats", text, card.cardErrorCode(), (int)(card.cardErrorData()));
}

// Карта файлов по годам: от текущего года вниз, пока файл существует
void Statistics::ScanYears()
{
	yearFiles[0] = yearFiles[1] = yearFiles[2] = 0;
	if(!HP.get_fSD()) return;
	SPI_switchSD();
	for(uint8_t what = 0; what < 3; what++) {
		for(int32_t y = rtcSAM3X8.get_years(); y > 2000 && y < 2065; y--) {
			strcpy(filename, what == ID_HISTORY_OLD ? history_file_start : what == ID_HISTORY ? history2_file_start : stats_file_start);
			_itoa(y, filename);
			strcat(filename, stats_file_ext);
			if(!StatsFile.opens(filename, O_READ, &open_fname)) break;
			StatsFile.close();
			yearFiles[what] |= (uint64_t)1 << (y - 2001);
		}
	}
}

void Statistics::Init(uint8_t newyear)
{
	if(!newyear) { ScanYears(); Reset(); }
	HistoryCurrentBlock = 0;
	HistoryBlockCreating = 0;
	historyNeedMarker = true;			// после (пере)открытия файла первая запись - строка-описание
	year = rtcSAM3X8.get_years();
#ifdef STATS_DO_NOT_SAVE
	return;
#endif
	if(!HP.get_fSD()) {
		journal.jprintf(" No SD card - statistics will not be saved!\n");
		return;
	}
	if(CreateOpenFile(ID_STATS) == OK) {
		if(!newyear) { // read last stats record
			int32_t pos = (CurrentBlock - BlockStart) * SD_BLOCK + CurrentPos - 1;
			uint8_t b;
			while(--pos >= 0) {
				if(!StatsFile.seekSet(pos)) {
					Error("seek", ID_STATS);
					break;
				}
				if(!StatsFile.read(&b, 1)) {
					Error("readb", ID_STATS);
					break;
				}
				if(b == '\n' || pos == 0) {
					if(pos) pos++;
					if(!StatsFile.read(temp_initbuf, STATS_MAX_RECORD_LEN)) {
						Error("readl", ID_STATS);
						break;
					}
					m_snprintf(temp_initbuf + STATS_MAX_RECORD_LEN, 16, format_date, year, month, day);
					if(memcmp(temp_initbuf, temp_initbuf + STATS_MAX_RECORD_LEN, format_date_size) == 0) { // date the same
						CurrentBlock = BlockStart + pos / SD_BLOCK;
						CurrentPos = pos % SD_BLOCK;
						temp_initbuf[format_date_size] = '\0';
						if(!card.card()->readBlock(CurrentBlock, (uint8_t*)stats_buffer)) {
							Error("readp", ID_STATS);
						} else {
							memcpy(temp_initbuf, stats_buffer + CurrentPos, SD_BLOCK - CurrentPos);
							temp_initbuf[STATS_MAX_RECORD_LEN] = '\0';
							char *p = temp_initbuf + format_date_size;
							if((p = strchr(p, '\n'))) *p = '\0';
							p = temp_initbuf + format_date_size;
							while((p = strchr(p, ';'))) *p++ = '\0';
							p = temp_initbuf + format_date_size;
							for(uint8_t i = 0; i < sizeof(Stats_data) / sizeof(Stats_data[0]); i++) {
								float val = my_atof(++p);
								if(val != ATOF_ERROR) {
									switch(Stats_data[i].object) {
									case STATS_OBJ_Temp:
									case STATS_OBJ_Press:
										Stats_data[i].value = val * 100;
										break;
									case STATS_OBJ_Voltage:
										Stats_data[i].value = val;
										break;
									case STATS_OBJ_Power:
									case STATS_OBJ_Power_OUT:
									case STATS_OBJ_Power_GEO:
										switch(Stats_data[i].type) {
										case STATS_TYPE_SUM:
										case STATS_TYPE_AVG:
											Stats_data[i].value = val * 1000000;
											break;
										default:
											Stats_data[i].value = val * 1000;
										}
										break;
									case STATS_OBJ_COP_Full:
										Stats_data[i].value = val * 100;
										break;
									default:
										if(Stats_data[i].type == STATS_TYPE_TIME) Stats_data[i].value = val * 60000;
										break;
									}
									if(*p == '\0') {
										switch(Stats_data[i].type) {
										case STATS_TYPE_MIN:
											Stats_data[i].value = MAX_INT32_VALUE;
											break;
										case STATS_TYPE_MAX:
											Stats_data[i].value = MIN_INT32_VALUE;
											break;
										}
									} else {
										if(Stats_data[i].when == STATS_WHEN_WORKD) counts_work = 1;
										else if(Stats_data[i].type == STATS_TYPE_AVG) counts = 1;
									}
									if((p = (char*)memchr(p, '\0', STATS_MAX_RECORD_LEN)) == NULL) break;
								}
							}
							StatsFileString(temp_initbuf);
							journal.jprintf(" Loaded: %s", temp_initbuf);
						}
					}
					break;
				}
			}
		}
		StatsFile.close();
	}
}

void Statistics::CheckCreateNewFile()
{
	uint8_t sem = 0;
	if(!HP.get_fSD()) return;
	if(NewYearFlag) {
		if(!(sem = SemaphoreTake(xWebThreadSemaphore, 0))) return;
		SaveHistory(1);
		// Truncate stats
		strcpy(filename, stats_file_start);
		_itoa(year, filename);
		strcat(filename, stats_file_ext);
		SPI_switchSD();
		if(!StatsFile.opens(filename, O_RDWR, &open_fname)) Error("open", ID_STATS);
		else {
			journal.jprintf("Truncate %s to %d\n", filename, (int)((CurrentBlock - BlockStart) * SD_BLOCK + CurrentPos));
			if(!StatsFile.truncate((CurrentBlock - BlockStart) * SD_BLOCK + CurrentPos)) Error("truncate", ID_STATS);
			StatsFile.close();
		}
		// Truncate history
		strcpy(filename, history2_file_start);
		_itoa(year, filename);
		strcat(filename, stats_file_ext);
		if(!StatsFile.opens(filename, O_RDWR, &open_fname)) Error("open", ID_HISTORY);
		else {
			journal.jprintf("Truncate %s to %d\n", filename, (int)((HistoryCurrentBlock - HistoryBlockStart) * SD_BLOCK + HistoryCurrentPos));
			if(!StatsFile.truncate((HistoryCurrentBlock - HistoryBlockStart) * SD_BLOCK + HistoryCurrentPos)) Error("truncate", ID_HISTORY);
			StatsFile.close();
		}
		Init(1);
		NewYearFlag = 0;
	}
	if(GETBIT(HP.Option.flags, fHistory) && (HistoryCurrentBlock == 0 || HistoryBlockCreating != 0)) { // Init History
		if(!sem && !(sem = SemaphoreTake(xWebThreadSemaphore, 0))) return;
		if(CreateOpenFile(ID_HISTORY) == OK) {
			StatsFile.close();
		} else SETBIT0(HP.Option.flags, fHistory); // При ошибке выключаем опцию сохранения истории!
	}
	if(sem) SemaphoreGive(xWebThreadSemaphore);
}

// Сбросить накопленные промежуточные значения
void Statistics::Reset()
{
	for(uint8_t i = 0; i < sizeof(Stats_data) / sizeof(Stats_data[0]); i++) {
		switch(Stats_data[i].type){
		case STATS_TYPE_MIN:
			Stats_data[i].value = MAX_INT32_VALUE;
			break;
		case STATS_TYPE_MAX:
			Stats_data[i].value = MIN_INT32_VALUE;
			break;
		default:
			Stats_data[i].value = 0;
		}
	}
	counts = 0;
	counts_work = 0;
	compressor_on_timer = 0;
	day = rtcSAM3X8.get_days();
	month = rtcSAM3X8.get_months();
	previous = GetTickCount();
}

// Обновить статистику, вызывается часто, раз в TIME_READ_SENSOR
void Statistics::Update()
{
	if(NewYearFlag
#ifndef TEST_BOARD
			|| HP.get_testMode() != NORMAL
#endif
		) return; // waiting to switch a next year
	uint32_t tm = GetTickCount() - previous;
	previous = GetTickCount();
	if(rtcSAM3X8.get_days() != day) {
		if(SaveStats(2) == OK) {
			Reset();
			if(year != rtcSAM3X8.get_years()) NewYearFlag = 1; // waiting to switch a next year
			journal.jprintf("=== %s\n", NowDateToStr()); // Новый день.
		}
	}
	int32_t newval = 0;
	if(HP.is_compressor_on()) {
		compressor_on_timer += tm;
	} else compressor_on_timer = 0;
	for(uint8_t i = 0; i < sizeof(Stats_data) / sizeof(Stats_data[0]); i++) {
		if(Stats_data[i].when == STATS_WHEN_WORKD && compressor_on_timer < STATS_WORKD_TIME) continue;
		uint8_t skip_value = 0;
		switch(Stats_data[i].object) {
		case STATS_OBJ_Temp:
			newval = HP.sTemp[Stats_data[i].number].get_Temp();
			break;
		case STATS_OBJ_Press:
			newval = HP.sADC[Stats_data[i].number].get_Value();
			break;
		case STATS_OBJ_Voltage:
		    #ifdef USE_ELECTROMETER_SDM
			newval = HP.dSDM.get_Voltage();
			#endif
			break;
		case STATS_OBJ_Power: {
			int32_t *ptr = NULL;
			newval = HP.power220; // Вт
			ptr = &motohour_IN_work;
			switch(Stats_data[i].type) {
			case STATS_TYPE_SUM:
				newval = newval * tm / 3600; // в мВтч
				if(ptr) *ptr += newval; // для motoHour
			}
			break;
		}
		case STATS_OBJ_Power_GEO: {
			int32_t *ptr = NULL;
			newval = HP.powerGEO; // Вт
			switch(Stats_data[i].type) {
			case STATS_TYPE_SUM:
				newval = newval * tm / 3600; // в мВтч
				if(ptr) *ptr += newval; // для motoHour
			}
			break;
		}
		case STATS_OBJ_Power_OUT: {
			int32_t *ptr = NULL;
			newval = HP.powerOUT; // Вт
			ptr = &motohour_OUT_work;
			switch(Stats_data[i].type) {
			case STATS_TYPE_SUM:
				newval = newval * tm / 3600; // в мВтч
				if(ptr) *ptr += newval; // для motoHour
			}
			break;
		}
		case STATS_OBJ_COP_Full:
			if(Stats_data[i].type == STATS_TYPE_AVG) continue;
#ifdef STATS_SKIP_COP_WHEN_RELAY_ON
			static uint8_t skip_by_relay = 0;
			if(HP.dRelay[STATS_SKIP_COP_WHEN_RELAY_ON].get_Relay()) {
				skip_by_relay = 30 / (TIME_READ_SENSOR / 1000); // 30 sec pause
				skip_value = 1;
			} else if(skip_by_relay) {
				skip_by_relay--;
				skip_value = 1;
			}
#endif
			newval = HP.fullCOP;
			if(newval == 0) skip_value = 1;
			break;
		case STATS_OBJ_Compressor:
			if(compressor_on_timer == 0) continue;
			break;
		}
		switch(Stats_data[i].type){
		case STATS_TYPE_MIN:
			if(newval < Stats_data[i].value && !skip_value) Stats_data[i].value = newval;
			break;
		case STATS_TYPE_MAX:
			if(newval > Stats_data[i].value && !skip_value) Stats_data[i].value = newval;
			break;
		case STATS_TYPE_SUM:
			Stats_data[i].value += newval;
			break;
		case STATS_TYPE_AVG:
			if(skip_value) newval = Stats_data[i].value / (Stats_data[i].when == STATS_WHEN_WORKD ? counts_work : counts);
			Stats_data[i].value += newval;
			break;
		case STATS_TYPE_TIME:
			Stats_data[i].value += tm;
			break;
		}
	}
	counts++;
	if(compressor_on_timer >= STATS_WORKD_TIME) counts_work++;   // один инкремент за вызов, симметрично counts
}

// Заголовок колонки истории: либо литерал из таблицы, либо имя самого датчика.
//
// В таблице HistorySetup стоит NULL, а не TempSetup[TOUT].name: такая ссылка
// не является константой на этапе компоновки, и вся таблица из-за неё уезжала
// из flash в SRAM (символ в .relocate, 128 байт ОЗУ). Подробности - в Config.h
// над объявлением таблицы. Имя здесь получается то же самое: SensorTemp.cpp
// присваивает датчику name = TempSetup[sensor].name, Sensors.cpp -
// FrequencySetup[sensor].name, то есть указатель буквально тот же.
//
// Вызывается из обработчика веб-запроса (WebServer.cpp) и при записи строки-описания
// раскладки (vServiceHP), то есть когда датчики давно проинициализированы,
// поэтому name не может быть пустым.
static const char *history_name(const History_setup &it)
{
	if(it.name) return it.name;
	switch(it.object) {
	case STATS_OBJ_Temp: return HP.sTemp[it.number].get_name();
	case STATS_OBJ_Flow: return HP.sFrequency[it.number].get_name();
	}
	return "";
}

// Ось колонки для страницы: первая буква шапки, дальше (для сотых долей) цифра 2.
static char history_axis(const History_setup &it)
{
	switch(it.object) {
	case STATS_OBJ_Temp:
	case STATS_OBJ_PressTemp:	return 'T';
	case STATS_OBJ_Press:		return 'P';
	case STATS_OBJ_Voltage:		return 'V';
	case STATS_OBJ_Power:
	case STATS_OBJ_Power_OUT:
	case STATS_OBJ_Power_GEO:
	case STATS_OBJ_Power_FC:	return 'W';
	case STATS_OBJ_COP_Full:	return 'C';
	case STATS_OBJ_Compressor:	return 'H';
	case STATS_OBJ_Flow:		return 'F';
	case STATS_OBJ_HPState:		return 'D';
	case STATS_OBJ_EEV:
		switch(it.number) {
		case STATS_EEV_OverHeat:
		case STATS_EEV_OverCool:
		case STATS_EEV_OHTarget:
		case STATS_EEV_OverHeat2:	return 'T';
		case STATS_EEV_Percent:		return 'R';
		case STATS_EEV_Steps:		return 'S';
		}
		break;
	}
	return '?';
}

// Возвращает файл с заголовками полей, flag: +Axis char (и цифра 2 у столбцов в сотых долях)
void Statistics::HistoryFileHeader(char *ret, uint8_t flag)
{
	strcat(ret, "Время;");
	for(uint8_t i = 0; i < sizeof(HistorySetup) / sizeof(HistorySetup[0]); i++) {
		if(i > 0) strcat(ret, ";");
		if(flag) {
			char ax[3] = { history_axis(HistorySetup[i]), HistorySetup[i].dec == 2 ? '2' : '\0', '\0' };
			strcat(ret, ax);
		}
		strcat(ret, history_name(HistorySetup[i]));
	}
	strcat(ret, "\n");
}

// Возвращает заголовок поля, flag: +Axis char
void Statistics::StatsFieldHeader(char *ret, uint8_t i, uint8_t flag)
{
	if(flag && Stats_data[i].type == STATS_TYPE_TIME) strcat(ret, "M"); // ось часы
	switch(Stats_data[i].object) {
	case STATS_OBJ_Temp:
		if(flag) strcat(ret, "T"); // ось температур
		strcat(ret, HP.sTemp[Stats_data[i].number].get_note());
		break;
	case STATS_OBJ_Press:
		if(flag) strcat(ret, "P"); // ось давление
		strcat(ret, HP.sADC[Stats_data[i].number].get_note());
		break;
	case STATS_OBJ_Voltage:
		if(flag) strcat(ret, "V"); // ось напряжение
		strcat(ret, "Напряжение, V");
		break;
	case STATS_OBJ_Power:
		if(flag) strcat(ret, "W"); // ось мощность
		strcat(ret, "Потребление, кВт"); // хранится в Вт
		if(Stats_data[i].type == STATS_TYPE_SUM) strcat(ret, "ч");
		break;
	case STATS_OBJ_Power_GEO:
		if(flag) strcat(ret, "W"); // ось мощность
		strcat(ret, "Геоконтур, кВтч"); // хранится в Вт
		break;
	case STATS_OBJ_Power_OUT:
		if(flag) strcat(ret, "W"); // ось мощность
		strcat(ret, "Выработано, кВтч"); // хранится в Вт
		break;
	case STATS_OBJ_COP_Full:
		if(flag) strcat(ret, "C"); // ось COP
		strcat(ret, "Полный COP");
		if(Stats_data[i].type == STATS_TYPE_AVG) {
			if(Stats_data[Stats_data[i].when].when == STATS_WHEN_WORKD || Stats_data[Stats_data[i].number].when == STATS_WHEN_WORKD) strcat(ret, "(work)");
			return;
		}
		break;
	case STATS_OBJ_Compressor:
		strcat(ret, "Моточасы, м");
		break;
	default: strcat(ret, "?");
	}
	switch(Stats_data[i].type) {
	case STATS_TYPE_MIN:
		strcat(ret, " (Мин)");
		break;
	case STATS_TYPE_MAX:
		strcat(ret, " (Макс)");
		break;
	case STATS_TYPE_AVG:
		strcat(ret, " (Сред)");
		break;
	}
	if(Stats_data[i].when == STATS_WHEN_WORKD) strcat(ret, "(work)");
}

// Возвращает файл с заголовками полей, flag: +Axis char
void Statistics::StatsFileHeader(char *ret, uint8_t flag)
{
	strcat(ret, "Дата;");
	for(uint8_t i = 0; i < sizeof(Stats_data) / sizeof(Stats_data[0]); i++) {
		if(i > 0) strcat(ret, ";");
		StatsFieldHeader(ret, i, flag);
	}
	strcat(ret, "\n");
}

void Statistics::StatsFieldString(char **ret, uint8_t i)
{
	int32_t val;
	if(Stats_data[i].type == STATS_TYPE_AVG) {
		val = Stats_data[i].when == STATS_WHEN_WORKD ? counts_work : counts;
		if(val == 0) {
xSkipEmpty:
			**ret = '\0';
			return;
		}
		val = Stats_data[i].value / val;
	} else if(Stats_data[i].when == STATS_WHEN_WORKD && counts_work == 0) goto xSkipEmpty; else val = Stats_data[i].value;
	if((val == MIN_INT32_VALUE && Stats_data[i].type == STATS_TYPE_MAX) || (val == MAX_INT32_VALUE && Stats_data[i].type == STATS_TYPE_MIN)) goto xSkipEmpty;
	switch(Stats_data[i].object) {
	case STATS_OBJ_Temp:					// C
	case STATS_OBJ_Press: 					// bar
		int_to_dec_str(val, 100, ret, 1);
		break;
	case STATS_OBJ_Voltage:					// V
		int_to_dec_str(val, 1, ret, 0);
		break;
	case STATS_OBJ_Power:					// кВт*ч
	case STATS_OBJ_Power_OUT:
	case STATS_OBJ_Power_GEO:
		switch(Stats_data[i].type) {
		case STATS_TYPE_SUM:
		case STATS_TYPE_AVG:
			int_to_dec_str(val, 1000000, ret, 3);
			break;
		default:
			int_to_dec_str(val, 1000, ret, 3);
		}
		break;
	case STATS_OBJ_COP_Full:
		if(Stats_data[i].type == STATS_TYPE_AVG) int_to_dec_str(Stats_data[Stats_data[i].when].value / (Stats_data[Stats_data[i].number].value / 100), 100, ret, 2);
		else int_to_dec_str(val, 100, ret, 2);
		break;
	default:
		if(Stats_data[i].type == STATS_TYPE_TIME) int_to_dec_str(val, 60000, ret, 1);  // минуты;
		else goto xSkipEmpty;
		break;
	}
}

// Строка со значениями за день (разделитель ";"), при запуске не из Update() возможны неверные данные!
inline void Statistics::StatsFileString(char *ret)
{
	ret += m_snprintf(ret, 20, format_date, year, month, day);
	for(uint8_t i = 0; i < sizeof(Stats_data) / sizeof(Stats_data[0]); i++) {
		*ret++ = ';';
		StatsFieldString(&ret, i);
	}
	*ret = '\n'; *(ret+1) = '\0';
}

void Statistics::StatsWebTable(char *ret)
{
	for(uint8_t i = 0; i < sizeof(Stats_data) / sizeof(Stats_data[0]); i++) {
		StatsFieldHeader(ret, i, 0);
		ret += m_strlen(ret);
		*ret++ = '|';
		StatsFieldString(&ret, i);
		strcat(ret, ";");
	}
}

#define _buffer_ ((uint8_t*)Socket[thread].outBuf)

// Return: OK, 1 - not found, >2 - error. Network is active
void Statistics::SendFileData(uint8_t thread, SdFile *File, char *filename, bool noMarkers)
{
	SPI_switchSD();
	if(!File->opens(filename, O_READ, &open_fname)) {
		journal.jprintf("Error open %s\n", filename);
		sendConstRTOS(thread, HEADER_FILE_NOT_FOUND);
		return;
	}
	uint32_t bst, bend;
	if(!File->contiguousRange(&bst, &bend)) {
		journal.jprintf(" Error get blocks %s\n", filename);
		File->close();
		return;
	}
	File->close();
	uint32_t readed = m_strlen((char*)_buffer_);
	if(sendPacketRTOS(thread, _buffer_, readed, 0) != readed) {
		journal.jprintf(" Error sendh %s\n", filename);
		return;
	}
	readed = 0;
	bool eod = false;
	for(uint32_t i = bst; i <= bend && !eod; i++) {
		SPI_switchSD();
		if(i == CurrentBlock) {
			memcpy(_buffer_ + readed, stats_buffer, SD_BLOCK);
		} else if(i == HistoryCurrentBlock) {
			memcpy(_buffer_ + readed, history_buffer, SD_BLOCK);
		} else if(!card.card()->readBlock(i, _buffer_ + readed)) {
			Error("read data", ID_STATS);
			break;
		}
		if(_buffer_[readed + SD_BLOCK - 1] == 0) {  // end of data
			if(_buffer_[readed] != 0) readed = (uint8_t*)memchr(_buffer_ + readed, 0, SD_BLOCK) - _buffer_;
			eod = true;
		} else {
			readed += SD_BLOCK;
			if(readed <= W5200_MAX_LEN - SD_BLOCK) continue;
		}
		if(!SendFileChunk(thread, &readed, noMarkers, eod)) {
			journal.jprintf(" Error send %s\n", filename);
			break;
		}
	}
}

// Отправить накопленное в буфере; noMarkers - убрать строки-описания раскладки (неполная последняя строка, если данные
// продолжаются, переносится в начало буфера). *len - сколько байт в буфере, после вызова - сколько осталось. false - ошибка сети.
bool Statistics::SendFileChunk(uint8_t thread, uint32_t *len, bool noMarkers, bool last)
{
	uint32_t n = *len, tail = 0;
	if(noMarkers) {
		uint32_t done = n;
		if(!last) while(done && _buffer_[done - 1] != '\n') done--;
		tail = n - done;
		n = HistoryDropMarkers((char*)_buffer_, done);
		if(tail) memmove(_buffer_ + n, _buffer_ + done, tail);   // хвост сразу за сжатой частью; ниже сдвигается в начало
		if(n && sendPacketRTOS(thread, _buffer_, n, 0) != n) return false;
		if(tail) memmove(_buffer_, _buffer_ + n, tail);
		*len = tail;
		return true;
	}
	if(n && sendPacketRTOS(thread, _buffer_, n, 0) != n) return false;
	*len = 0;
	return true;
}

// Return: OK, 1 - not found, >2 - error. Network is active. Date format: "yyyymmdd...\0"
// step - прореживание, секунды (0 - отдавать все записи), см. history_thin
void Statistics::SendFileDataByPeriod(uint8_t thread, SdFile *File, char *Prefix, char *TimeStart, char *TimeEnd, uint32_t step)
{
	// SEM-BORROW: xWebThreadSemaphore - замок сети принадлежит web_server (вызов из readFileSD); на паузах отдачи файла он отпускается и берётся обратно, при потере выставляется fLOCK_LOST
	uint32_t bendfile = m_strlen((char*)_buffer_);
	strcpy((char*)_buffer_ + bendfile + 1, Prefix);
	strncat((char*)_buffer_+ bendfile + 1, TimeStart, 4); // year
	strcat((char*)_buffer_ + bendfile + 1, stats_file_ext);
	SPI_switchSD();
	if(!File->opens((char*)_buffer_ + bendfile + 1, O_READ, &open_fname)) {
		journal.jprintf("Error open %s\n", _buffer_ + bendfile + 1);
		sendConstRTOS(thread, HEADER_FILE_NOT_FOUND);
		return;
	}
	if(sendPacketRTOS(thread, _buffer_, bendfile, 0) != bendfile) {
		journal.jprintf(" Error sendh %s\n", Prefix);
		return;
	}
	uint32_t bstfile;
	if(!File->contiguousRange(&bstfile, &bendfile)) {
		journal.jprintf(" Error get blocks %s\n", filename);
		File->close();
		return;
	}
	File->close();
	uint32_t bst = bstfile, bend = bendfile;
	uint8_t findst = 0;
	char *pos = NULL;
	uint8_t len_Time = strlen(TimeStart);
	while(bst <= bend) {
		uint32_t cur = bst + (bend - bst) / 2;
xReadBlock:
		if(cur == CurrentBlock) {
			memcpy(_buffer_, stats_buffer, SD_BLOCK);
		} else if(cur == HistoryCurrentBlock) {
			memcpy(_buffer_, history_buffer, SD_BLOCK);
		} else if(!card.card()->readBlock(cur, _buffer_)) {
			Error("read f", ID_HISTORY);
			return;
		}
		if(*_buffer_) {
			pos = (char*)memchr(_buffer_, '\n', SD_BLOCK);
			if(pos == NULL) return;  // garbage
			if(*++pos == '\0') goto xGoDown;
			{
				int8_t cmp = strncmp(pos, TimeStart, len_Time);
				if(cmp >= 0) {
					findst = 1;
					if(cmp > 0) {
						if(cur == bst) {
							if(strncmp(pos, TimeEnd, len_Time) > 0) return; // greater - not found
							goto xNext;
						}
						goto xGoDown;
					}
					if(cur > bst) { // slow down at equal
						cur--;
						goto xReadBlock;
					}
				}
			}
			bst = cur;
xNext:		if(findst) { // found
				memmove(_buffer_, pos, bend = SD_BLOCK - ((uint8_t*)pos - _buffer_));
				break;
			}
			bst++;
			if(bst > bend) {
				if(bend < bendfile) bend++;
				else { // file overflow
					return;
				}
			}
		} else {
xGoDown:	if(cur == bst) { // low limit
				if(bst == bstfile) {
					pos = (char*)_buffer_;
					if(*pos == '\0') return; // empty
				} else {
					pos = (char*)memchr(_buffer_, '\n', SD_BLOCK-1);
					if(pos == NULL) return;
					pos++;
				}
				goto xNext;
			} else {
				bend = cur - 1;
				findst = 0;
			}
		}
	}
	uint32_t readed = 0;
	uint32_t tail = 0;						// при прореживании: недописанная строка, которая переходит в следующий пакет
	uint32_t lastStep = 0xFFFFFFFF;			// при прореживании: номер последнего отправленного шага
	uint16_t packcnt = 0;
	if(pos) {
		pos = (char*)memchr(_buffer_, 0, bend);
		readed = pos ? pos - (char*)_buffer_ : bend;
		pos = (char*)_buffer_;
		goto xFoundStart;
	}
	for(; bst <= bendfile; bst++) {
		SPI_switchSD();
		if(bst == CurrentBlock) {
			memcpy(_buffer_ + readed, stats_buffer, SD_BLOCK);
		} else if(bst == HistoryCurrentBlock) {
			memcpy(_buffer_ + readed, history_buffer, SD_BLOCK);
		} else if(!card.card()->readBlock(bst, _buffer_ + readed)) {
			Error("read data", ID_HISTORY);
			break;
		}
		if(_buffer_[readed + SD_BLOCK - 1] == '\0') {  // end of data
			if(_buffer_[readed] == '\0') break;
			pos = (char*)memchr(_buffer_ + readed, '\0', SD_BLOCK);
			readed = (uint8_t*)pos - _buffer_;
			bendfile = 0;
		} else {
			pos = (char*)memchr(_buffer_ + readed, '\n', SD_BLOCK);
			readed += SD_BLOCK;
		}
		if(pos++) {
xFoundStart:
			if(strncmp(pos, TimeEnd, len_Time) > 0) {
				readed = (uint8_t*)pos - _buffer_;
				bendfile = 0; // stop
			} else if(readed <= W5200_MAX_LEN - SD_BLOCK) continue;
		} else bendfile = 0;
		if(step) {							// отправляются только целые строки, остаток последней остаётся в начале буфера
			uint32_t done = readed;
			if(bendfile) while(done && _buffer_[done - 1] != '\n') done--;
			tail = readed - done;
			uint32_t w = HistoryThin((char*)_buffer_, done, step, lastStep);
			if(w && sendPacketRTOS(thread, _buffer_, w, 0) != w) {
				journal.jprintf(" Error send %s\n", filename);
				return;
			}
			memmove(_buffer_, _buffer_ + done, tail);
			readed = tail;
		} else if(sendPacketRTOS(thread, _buffer_, readed, 0) != readed) {
			journal.jprintf(" Error send %s\n", filename);
			return;
		}
		if(++packcnt == 50) { // 100kb send
			packcnt = 0;
			SemaphoreGive(xWebThreadSemaphore);
			_delay(WEB_SEND_FILE_PAUSE);
			if(SemaphoreTake(xWebThreadSemaphore, W5200_TIME_WAIT) !=pdPASS) {
				SETBIT1(Socket[thread].flags, fLOCK_LOST);   // web_server по этому флагу не отдаёт чужой замок
				journal.jprintf("Cant take SEM while sending file!\n");
				return;
			}
		}
		if(!step) readed = 0;
	}
	if(readed && step) readed = HistoryThin((char*)_buffer_, readed, step, lastStep);
	if(readed) {
		if(sendPacketRTOS(thread, _buffer_, readed, 0) != readed) {
			journal.jprintf(" Error send %s\n", filename);
		}
	}
}

// Записать статистику на SD, 0 - только записать, 1 - только записать c веба, 2 - новый день
// Return: OK или Ошибка
int8_t Statistics::SaveStats(uint8_t newday)
{
#ifdef STATS_DO_NOT_SAVE
	return OK;
#endif
	if(!HP.get_fSD() || CurrentBlock == 0) return OK;
	char *rbuf = (char*) malloc(STATS_MAX_RECORD_LEN);
	if(rbuf == NULL) {
		Error("memory low", ID_STATS);
		return ERR_OUT_OF_MEMORY;
	}
	int8_t retval = OK;
	StatsFileString(rbuf);
	uint16_t lensav, len = m_strlen(rbuf) + 1;
#ifdef STATS_USE_BUFFER_FOR_SAVING
	memcpy(stats_buffer + CurrentPos, rbuf, lensav = SD_BLOCK - CurrentPos < len ? SD_BLOCK - CurrentPos : len);
	if(newday < 2 || lensav != len) { // save when there is no space in buffer
#endif
		if(newday != 1 && SemaphoreTake(xWebThreadSemaphore, newday == 0 ? W5200_TIME_WAIT : 0) == pdFALSE) {
			retval = ERR_CONFIG;
			free(rbuf);
			return retval;
		}
#ifndef STATS_USE_BUFFER_FOR_SAVING
		// Позиция в блоке и запись в stats_buffer - ПОД замком сети. Раньше memcpy стоял до захвата: полночный
		// SaveStats(2) из ReadSensor правил буфер и CurrentPos одновременно с SaveStats(0) из CommandHP или
		// выгрузкой файла статистики из веб-потока (она копирует stats_buffer под тем же замком), и блок уходил
		// на карту, а в браузер - рваным. Если замок не взят, запись не копируется: следующее сохранение
		// соберёт строку заново.
		memcpy(stats_buffer + CurrentPos, rbuf, lensav = SD_BLOCK - CurrentPos < len ? SD_BLOCK - CurrentPos : len);
#endif
		SPI_switchSD();
		if(!card.card()->writeBlock(CurrentBlock, stats_buffer)) {
			Error("save", ID_STATS);
			// to do - reinit card but in other task
			retval = ERR_SD_WRITE;
		} else if(lensav != len){ // next block
			if(CurrentBlock >= BlockEnd) {
				journal.jprintf("Stats file size exceeded!\n"); // to do: increase file
				retval = ERR_SD_WRITE;
			} else {
				memset(stats_buffer, 0, SD_BLOCK);
				memcpy(stats_buffer, rbuf + lensav, len - lensav);
				if(!card.card()->writeBlock(CurrentBlock + 1, (uint8_t*)stats_buffer)) {
					Error("save 2", ID_STATS);
					retval = ERR_SD_WRITE;
				} else if(newday == 2) { // new day
					if(CurrentBlock >= BlockEnd) {
						Error("File Overflow", ID_STATS);
						retval = ERR_CONFIG;
					} else CurrentBlock++;
					CurrentPos = len - lensav - 1;
				} else { // reread current block
					if(!card.card()->readBlock(CurrentBlock, (uint8_t*)stats_buffer)) {
						Error("read", ID_STATS);
						retval = ERR_SD_READ;
					}
				}
			}
		} else if(newday == 2) CurrentPos += lensav - 1; // new day
	    if(newday != 1) SemaphoreGive(xWebThreadSemaphore);
#ifdef STATS_USE_BUFFER_FOR_SAVING
	} else CurrentPos += lensav - 1; // new day
#endif
	free(rbuf);
	return retval;
}

// Return: OK или Ошибка
int8_t Statistics::SaveHistory(uint8_t from_web)
{
#ifdef STATS_DO_NOT_SAVE
	return OK;
#endif
	if(!GETBIT(HP.Option.flags, fHistory) || !HP.get_fSD() || HistoryCurrentBlock == 0) return OK;
	if(!from_web && SemaphoreTake(xWebThreadSemaphore, W5200_TIME_WAIT) == pdFALSE) return ERR_CONFIG;
	int8_t retval = OK;
	SPI_switchSD();
	if(!card.card()->writeBlock(HistoryCurrentBlock, history_buffer)) {
		Error("save", ID_STATS);
		retval = ERR_SD_WRITE;
	}
	if(!from_web) SemaphoreGive(xWebThreadSemaphore);
	return retval;
}

// Поле записи истории: пустое, если значение недостоверно. div - делитель печати (10: десятые, 1: сотые).
#define HISTORY_BAD_TEMP	(-10000)	// так датчик температуры сообщает, что значению верить нельзя (SensorTemp.h)
static inline void history_val(char *&b, int32_t value, int32_t div, bool ok)
{
	if(ok) int_to_dec_str(value, div, &b, 0);
}

// Заполненный блок истории - на карту, дальше следующий блок. Мютекс сети ждём недолго: короткая заминка не должна
// останавливать сервисную задачу, а запись, не получившаяся сейчас, повторяется следующей секундой (HistoryFlushPending).
// false - не вышло: данные остаются в буфере до следующей попытки.
bool Statistics::HistoryNextBlock()
{
	if(SemaphoreTake(xWebThreadSemaphore, 100 / portTICK_PERIOD_MS) == pdFALSE) {
		if(++historySaveFail == 1 || historySaveFail % 60 == 0) journal.jprintf("History: save postponed, SD busy (x%d)\n", (int) historySaveFail);
		return false;
	}
	int8_t r = SaveHistory(1);					// мютекс уже наш
	SemaphoreGive(xWebThreadSemaphore);
	if(r != OK) {
		if(++historySaveFail == 1 || historySaveFail % 60 == 0) journal.jprintf("History: save failed %d (x%d)\n", (int) r, (int) historySaveFail);
		return false;
	}
	historySaveFail = 0;
	if(HistoryCurrentBlock >= HistoryBlockEnd) {
		Error("File Overflow", ID_HISTORY);
		return false;
	}
	HistoryCurrentBlock++;
	memset(history_buffer, 0, SD_BLOCK);
	HistoryCurrentPos = 0;
	return true;
}

// Раз в секунду: догнать запись заполненного блока, если она не удалась сразу
void Statistics::HistoryFlushPending()
{
	if(GETBIT(HP.Option.flags, fHistory) && HistoryCurrentBlock != 0 && !HistoryBlockCreating && HistoryCurrentPos >= SD_BLOCK) HistoryNextBlock();
}

// Дописать len байт в буфер истории; заполненный блок уходит на карту. false - блок записать не удалось
// или файл кончился: продолжать запись сейчас нельзя, остаток этой записи потерян.
bool Statistics::HistoryAppend(const char *data, uint16_t len)
{
	while(len) {
		if(HistoryCurrentPos >= SD_BLOCK && !HistoryNextBlock()) return false;	// блок ждёт записи с прошлого раза
		uint16_t n = SD_BLOCK - HistoryCurrentPos;
		if(n > len) n = len;
		memcpy(history_buffer + HistoryCurrentPos, data, n);
		HistoryCurrentPos += n;
		data += n;
		len -= n;
		if(HistoryCurrentPos >= SD_BLOCK && !HistoryNextBlock()) return len == 0;	// остаток записи не поместился, сам блок дождётся
	}
	return true;
}

// Строка-описание раскладки в потоке данных: "ггггммддччммсс;#;<ось[2]><имя>;...". Метка впереди сохраняет возрастание времени,
// на котором держится двоичный поиск по файлу. Пишется перед первой записью после запуска контроллера и после создания файла.
void Statistics::HistoryMarker()
{
	char st[format_datetime_size + 4];
	char *b = st;
	b += m_snprintf(b, sizeof(st), format_datetime, rtcSAM3X8.get_years(), rtcSAM3X8.get_months(), rtcSAM3X8.get_days(), rtcSAM3X8.get_hours(), rtcSAM3X8.get_minutes(), rtcSAM3X8.get_seconds());
	*b++ = ';';
	*b++ = '#';
	if(!HistoryAppend(st, b - st)) return;
	for(uint8_t i = 0; i < sizeof(HistorySetup) / sizeof(HistorySetup[0]); i++) {
		char ax[3] = { ';', history_axis(HistorySetup[i]), HistorySetup[i].dec == 2 ? '2' : '\0' };
		const char *nm = history_name(HistorySetup[i]);
		if(!HistoryAppend(ax, ax[2] ? 3 : 2) || !HistoryAppend(nm, strlen(nm))) return;
	}
	HistoryAppend("\n", 1);
}

// Хватит ли места в файле на минутные записи до конца года, если писать ещё и детализацию. Минутные записи в приоритете.
bool Statistics::HistoryDetailRoom()
{
	if(HistoryCurrentBlock == 0 || HistoryBlockCreating) return false;
	uint32_t left = HistoryCurrentBlock >= HistoryBlockEnd ? 0 : HistoryBlockEnd - HistoryCurrentBlock;
	return HistoryDetailAllowed(left, HistoryMinutesToYearEnd(year, rtcSAM3X8.get_months(), rtcSAM3X8.get_days(), rtcSAM3X8.get_hours(), rtcSAM3X8.get_minutes()), (uint16_t) HISTORY_MAX_RECORD_LEN);
}

// Логирование параметров работы ТН: раз в минуту, а в окне детализации чаще (Control.ino, vServiceHP)
void Statistics::History()
{
	if(!GETBIT(HP.Option.flags, fHistory)
#ifndef TEST_BOARD
			|| HP.get_testMode() != NORMAL
#endif
		) return;
	uint16_t y = rtcSAM3X8.get_years();
	if(y != year) return;
	if(HistoryCurrentBlock == 0 || HistoryBlockCreating) return;	// файл ещё не открыт или заполняется нулями: записи в это время пропускаются
	if(historyNeedMarker) {
		historyNeedMarker = false;
		HistoryMarker();
	}
	// Буфер статический, а не malloc: History() вызывается только из vServiceHP
	// (Control.ino) и только оттуда, поэтому гонки нет. malloc/free раз в минуту из задачи реального времени
	// фрагментировал бы кучу, в которой FreeRTOS держит стеки задач (heap_3).
	static char mbuf[HISTORY_MAX_RECORD_LEN];
	char *buf = mbuf;
	buf += m_snprintf(buf, format_datetime_size + 1, format_datetime, y, rtcSAM3X8.get_months(), rtcSAM3X8.get_days(), rtcSAM3X8.get_hours(), rtcSAM3X8.get_minutes(), rtcSAM3X8.get_seconds());
	for(uint8_t i = 0; i < sizeof(HistorySetup) / sizeof(HistorySetup[0]); i++) {
		const History_setup &it = HistorySetup[i];
		const int32_t d = it.dec == 2 ? 1 : 10;		// делитель печати температур и перегревов
		*buf++ = ';';
		switch(it.object) {
		case STATS_OBJ_Temp: {		// C
			int16_t t = HP.sTemp[it.number].get_Temp();
			history_val(buf, t, d, HP.sTemp[it.number].get_present() && t > HISTORY_BAD_TEMP); // T
			break; }
		case STATS_OBJ_Press:		// bar
			history_val(buf, HP.sADC[it.number].get_Value(), 10, HP.sADC[it.number].get_present()); // P
			break;
		case STATS_OBJ_Flow:		// m3h
			history_val(buf, HP.sFrequency[it.number].get_Value(), 100, HP.sFrequency[it.number].get_present()); // F
			break;
#ifdef EEV_DEF
		case STATS_OBJ_PressTemp:	// C
			history_val(buf, HP.sADC[it.number].get_present() ? PressToTemp(it.number) : 0, d, HP.sADC[it.number].get_present()); // T
			break;
		case STATS_OBJ_EEV:
			switch(it.number) {
			case STATS_EEV_Percent:
				history_val(buf, HP.dEEV.calc_percent(HP.dEEV.get_EEV()), 10, true); // R
				break;
			case STATS_EEV_Steps:
				history_val(buf, HP.dEEV.get_EEV() * 10, 1, true); // S
				break;
			case STATS_EEV_OverHeat:	// главный перегрев всегда, без подмены на перегрев у компрессора: он пишется отдельным столбцом
				history_val(buf, HP.dEEV.get_Overheat(), d, true); // T
				break;
			case STATS_EEV_OverCool:
				history_val(buf, HP.get_overcool(), d, true); // T
				break;
			case STATS_EEV_OHTarget:
				history_val(buf, HP.dEEV.get_tOverheat(), d, true); // T
				break;
			case STATS_EEV_OverHeat2:
				history_val(buf, HP.dEEV.OverheatTCOMP, d, true); // T
				break;
			}
			break;
#endif
		case STATS_OBJ_Compressor:
			history_val(buf, HP.dFC.get_frequency(), 10, true); // H
			break;
		case STATS_OBJ_Power:
			history_val(buf, HP.power220, 1, true);  // W
			break;
		case STATS_OBJ_Power_OUT:
			history_val(buf, HP.powerOUT, 1, true); // W
			break;
		case STATS_OBJ_Power_GEO:
			history_val(buf, HP.powerGEO, 1, true); // W
			break;
		case STATS_OBJ_COP_Full:
			history_val(buf, HP.fullCOP, 1, true); // C
			break;
		case STATS_OBJ_HPState:		// D
			history_val(buf, (int32_t) HP.get_State() * 100 + (int32_t) HP.get_modWork(), 1, true);
			break;
		}
		if(buf > mbuf + HISTORY_MAX_RECORD_LEN - HISTORY_MAX_FIELD_LEN) {
			journal.jprintf("%s memory overflow(%d): %d, max: %d\n", "History", i, buf - mbuf, HISTORY_MAX_RECORD_LEN);
			break;
		}
	}
	*buf++ = '\n';
	HistoryAppend(mbuf, buf - mbuf);
}

static_assert(sizeof(Stats_data) / sizeof(Stats_data[0]) == STATS_DATA_COUNT,
              "STATS_DATA_COUNT не совпадает с числом строк Stats_data[]");
