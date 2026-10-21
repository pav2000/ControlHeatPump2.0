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
#ifndef Scheduler_h
#define Scheduler_h

#include "../../Constant.h"

#include "SchedulerCalc.h"   // SCHDLR_* и разбор календаря

#define bScheduler_active	1

STRCONST(WEB_SCH_On,  "On");
STRCONST(WEB_SCH_Active,  "Active");
STRCONST(WEB_SCH_Names,  "n_list");
STRCONST(WEB_SCH_Name,  "Name");
STRCONST(WEB_SCH_Calendar,  "Calendar");
STRCONST(WEB_SCH_ActiveName,  "AN");
STRCONST(WEB_SCH_AutoSelectMonth,  "ASM");
STRCONST(WEB_SCH_AutoSelectWeek,  "ASW");

struct Scheduler_Calendar_Item {
	uint8_t		WD_Hour; 	// День недели (3bits, 0 = monday)  + час (5bits)
	int8_t		Profile;	// 0x80+Номер профиля(макс 27)+1, 0 = выключен, 127 - ничего не менять, иначе температура -10,0..+12,6
} __attribute__((packed));

struct Scheduler_Data {
	uint8_t		Flags;							// bScheduler_*
	uint8_t		Active;							// Активное расписание
	char 		Names[MAX_CALENDARS][32];		// Названия (русские - 2 байта)
	uint8_t		Timetable[TIMETABLES_MAXSIZE]; 	// буфер для расписаний: {len},{{WD+H},{Profile+1}},..., {len},{{WD+H},{Profile+1}},...,
	uint8_t		AutoSelectMonthWeek[MAX_CALENDARS]; // Автовыбор расписания в 00:00, f + месяц(1..12) + неделя(0..3), 0 - нет
} __attribute__((packed));

// Размеры записи расписания: нынешняя и прежняя (9 календарей). Область в
// EEPROM и секция файла настроек читаются в обеих длинах; лишние календари
// отбрасываются (Scheduler::convert_old). Версия формата настроек при этом
// не менялась - длину записи несёт её заголовок.
#define SCHEDULER_LEN_NEW	(2 + MAX_CALENDARS * 32 + TIMETABLES_MAXSIZE + MAX_CALENDARS)
#define SCHEDULER_LEN_OLD	(2 + MAX_CALENDARS_OLD * 32 + TIMETABLES_MAXSIZE + MAX_CALENDARS_OLD)
static_assert(sizeof(Scheduler_Data) == SCHEDULER_LEN_NEW, "размер Scheduler_Data разошёлся с SCHEDULER_LEN_NEW");

class Scheduler
{
public:
	Scheduler();
	int8_t   calc_active_profile(void);					// Возвращает профиль (или -1) по календарю и текущему времени
	int16_t  get_temp_change(void);						// Возвращает на сколько корректировать целевую температуру, сотые градуса
	bool     UsesProfile(uint8_t profile);					// Профиль (0-based) есть хоть в одном календаре?
	boolean  IsSchedulerOn() { return GETBIT(sch_data.Flags, bScheduler_active); }; // Расписание включено?
	uint16_t Timetable_ptr(uint8_t num);				// Возвращает указатель на запись календаря в TimeTable по его номеру
	void	 web_get_param(char *param, char* result);	// Вернуть строку параметра для веба
	uint8_t  web_set_param(char *param, char *val);		// Установить параметр из веба
	const char* get_note(void); 						// Получить описание
	const char* get_name(void) { return  "Scheduler"; } // Получить имя
	int8_t   save(void);  			               		// Записать настройки в eeprom i2c
	int16_t  load(uint8_t *data = NULL);  		     	// Считать настройки из eeprom i2c, возвращает длину или ошибку
	int8_t   loadFromBuf(byte *buf, uint16_t avail, uint16_t hint = 0);	// Считать из буфера (секция файла): avail - сколько байт доступно, hint - длина данных из заголовка записи или 0; возврат код ошибки
	uint16_t get_crc16(uint8_t *data);             		// Рассчитать контрольную сумму для данных на входе входная сумма на выходе новая
	int8_t   check_crc16_eeprom(void);
	// Область расписания опознаёт себя сама: в новом формате она начинается
	// с NTAG_Scheduler. Старые данные начинались с Flags и Active, а Active
	// не больше MAX_CALENDARS, поэтому старший байт тега их исключает.
	bool     region_is_new(void);
	uint16_t region_len(void);							// Длина данных области в EEPROM (без заголовка и CRC): LEN_NEW, LEN_OLD или 0, если область не опознана
	uint32_t data_addr(void);
	uint16_t get_data_size(void) { return sizeof(sch_data); }
	Scheduler_Data sch_data;
private:
	void fix_loaded(Scheduler_Data *d, bool lost);	// Привести прочитанные данные к нынешним пределам (обрезка, активный календарь)
	uint8_t current_hour;
	int16_t current_change; // -10,00..+10,00
};

#endif
