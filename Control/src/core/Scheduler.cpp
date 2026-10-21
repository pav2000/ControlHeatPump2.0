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
 * Расписание: отдельная единица трансляции.
 *
 * Scheduler.h тянет Constant.h, а с ним Config.h, поэтому #ifdef
 * LOAD_VERIFICATION разрешается так же, как в главной единице трансляции.
 * Проверено сверкой размеров методов.
 */
#include "Scheduler.h"
#include "SettingsLegacy.h"   // чтение старых форматов, файл к удалению
#include "SchedulerCalc.h" // разбор календаря, проверяется на хосте
#include "HeatPump.h"     // HP: профиль, состояние, команды
#include "../hw/Hardware.h"     // readEEPROM_I2C / writeEEPROM_I2C
#include "../log/Journal.h"      // journal
#include "../util/Util.h"         // _crc16, urldecode, _itoa
#include "../util/DateTime.h"     // день недели и часы
#include "../net/ParamRegistry.h" // таблица параметров веб-API (внизу файла)

#define ifparam(s) if(strncmp(param, s, sizeof(s)-1) == 0)

Scheduler::Scheduler() {
	memset(&sch_data, 0, sizeof(sch_data));
	for(uint8_t i = 0; i < MAX_CALENDARS; i++) {
		strcpy(sch_data.Names[i], "Расписание 1");
		sch_data.Names[i][strlen(sch_data.Names[i]) - 1] += i;
	}
}

// Получить описание
const char* Scheduler::get_note(void)
{
	return sch_data.Flags & (1<<bScheduler_active) ? " активно" : " выключено";
}

// Возвращает указатель на запись календаря в TimeTable по его номеру
uint16_t Scheduler::Timetable_ptr(uint8_t num)
{
	return Scheduler_TimetablePtr(sch_data.Timetable, TIMETABLES_MAXSIZE, num);
}

// Установка профиля по календарю и текущему времени
// Возврат: -2 - расписание не активно, -1 - пустой календарь
int8_t Scheduler::calc_active_profile(void)
{
	// Связка: часы и текущий профиль - здесь, разбор календаря - в SchedulerCalc.
	current_hour = rtcSAM3X8.get_hours();
	if((sch_data.Flags & (1<<bScheduler_active)) == 0) {
		current_change = 0;
		return SCHDLR_NotActive;
	}
	uint8_t dw = rtcSAM3X8.get_day_of_week(); // 0 - вск
	if(dw == 0) dw = 6; else dw--;            // 0 - пон
	int8_t r = Scheduler_CalcProfile(sch_data.Timetable, TIMETABLES_MAXSIZE,
	                                 sch_data.Active, dw, current_hour, &current_change);
	return r == SCHDLR_KeepProfile ? HP.Prof.get_idProfile() : r;
}

int16_t Scheduler::get_temp_change(void)
{
	if(rtcSAM3X8.get_hours() != current_hour) calc_active_profile();
	return current_change;
}

// Вернуть строку параметра для веба, get_SCHDLR(param)
void Scheduler::web_get_param(char *param, char *result)
{
	uint8_t cnum;
	result += strlen(result);
	ifparam(WEB_SCH_On) {
		strcat(result, sch_data.Flags & (1<<bScheduler_active) ? cOne : cZero);
	} else ifparam(WEB_SCH_Active) {
		itoa(sch_data.Active, result, 10);
	} else ifparam(WEB_SCH_AutoSelectMonth) {
		if((cnum = param[sizeof(WEB_SCH_AutoSelectMonth)-1]) == '\0') cnum = sch_data.Active; else cnum -= '0';
		if(cnum >= MAX_CALENDARS) {
			strcat(result, "E33");
			return;
		}
		if(sch_data.AutoSelectMonthWeek[cnum]) itoa((sch_data.AutoSelectMonthWeek[cnum] >> 3) & 0xF, result, 10);
		else strcat(result, "-");
	} else ifparam(WEB_SCH_AutoSelectWeek) {
		if((cnum = param[sizeof(WEB_SCH_AutoSelectWeek)-1]) == '\0') cnum = sch_data.Active; else cnum -= '0';
		if(cnum >= MAX_CALENDARS) {
			strcat(result, "E33");
			return;
		}
		if(sch_data.AutoSelectMonthWeek[cnum]) itoa((sch_data.AutoSelectMonthWeek[cnum] & 0x7) + 1, result, 10);
		else strcat(result, "-");
	} else ifparam(WEB_SCH_Names) {
		uint8_t i = 0;
		for(; i < MAX_CALENDARS; i++) {
			strcat(result, sch_data.Names[i]);
			if(i == sch_data.Active) strcat(result, ":1;"); else strcat(result, ":0;");
		}
	} else ifparam(WEB_SCH_Name) { // NameX, X - номер
		if((cnum = param[sizeof(WEB_SCH_Name)-1]) == '\0') cnum = sch_data.Active; else cnum -= '0';
		if(cnum >= MAX_CALENDARS) {
			strcat(result, "E33");
			return;
		}
		strcat(result, sch_data.Names[cnum]);
	} else ifparam(WEB_SCH_Calendar) { // get_SCHDLR(CalendarX), X - расписание, если пусто, то активное.
		// Возврат: {wd+h};{profile+1|};... Если профайл -1(255), то вывод ""
		if((cnum = param[sizeof(WEB_SCH_Calendar)-1]) == '\0') cnum = sch_data.Active; else cnum -= '0';
		if(cnum >= MAX_CALENDARS) {
			strcat(result, "E33");
		} else {
			uint16_t ptr = Timetable_ptr(cnum);
			uint16_t max = ptr + sch_data.Timetable[ptr];
			if(max > TIMETABLES_MAXSIZE) return;
			for(ptr++; ptr <= max; ptr++) {
				result += m_strlen(result);
				if(sch_data.Timetable[ptr]) itoa(sch_data.Timetable[ptr], result, 10);
				if(ptr < max) strcat(result, ";");
			}
		}
	} else ifparam(WEB_SCH_ActiveName) { // get_SCHDLR(AN)
		if(sch_data.Flags & (1<<bScheduler_active)) strcat(result, sch_data.Names[sch_data.Active]); else strcat(result, "-");
	}
}

// Установить параметр из веба, set_SCHDLR(param=val)
// Возврат: 0 - ok, 1 - неверный номер расписания, 2 - не хватает места для календаря
uint8_t Scheduler::web_set_param(char *param, char *val)
{
	uint8_t cnum;
	ifparam(WEB_SCH_On) {
		if(atoi(val)) { // Вкл.
			sch_data.Flags |= (1<<bScheduler_active);
		} else { // Выкл.
			sch_data.Flags &= ~(1<<bScheduler_active);
			if (HP.get_State() == pWAIT_HP) HP.sendCommand(pRESUME);   // Если расписание не активно и есть режим ожидания (т.е.изменение флага расписания) надо подать команду старт
		}
	} else ifparam(WEB_SCH_Active) {
		cnum = atoi(val);
		if(cnum >= MAX_CALENDARS) return 1;
		sch_data.Active = cnum;
	} else ifparam(WEB_SCH_AutoSelectMonth) {
		if((cnum = param[sizeof(WEB_SCH_AutoSelectMonth)-1]) == '\0') cnum = sch_data.Active; else cnum -= '0';
		if(cnum >= MAX_CALENDARS) return 1;
		uint8_t n = atoi(val);
		if(n == 0) sch_data.AutoSelectMonthWeek[cnum] = 0;
		else sch_data.AutoSelectMonthWeek[cnum] = (sch_data.AutoSelectMonthWeek[cnum] & 0x7) | (n << 3);
	} else ifparam(WEB_SCH_AutoSelectWeek) {
		if((cnum = param[sizeof(WEB_SCH_AutoSelectWeek)-1]) == '\0') cnum = sch_data.Active; else cnum -= '0';
		if(cnum >= MAX_CALENDARS) return 1;
		if(sch_data.AutoSelectMonthWeek[cnum]) {
			uint8_t n = atoi(val);
			if(n > 0) n--;
			sch_data.AutoSelectMonthWeek[cnum] = (sch_data.AutoSelectMonthWeek[cnum] & ~0x7) | n;
		}
	} else ifparam(WEB_SCH_Name) { // NameX, X - номер
		if((cnum = param[sizeof(WEB_SCH_Name)-1]) == '\0') cnum = sch_data.Active; else cnum -= '0';
		if(cnum >= MAX_CALENDARS) return 1;
		urldecode(sch_data.Names[cnum], val, sizeof(sch_data.Names[0]));
	} else ifparam("Clear") {
		memset(&sch_data.Timetable, 0, sizeof(sch_data.Timetable));
	} else ifparam(WEB_SCH_Calendar) { // CalendarX=str, X - расписание, если пусто, то активное; str = length;{wday+h;profile|-1; wday+h;profile|-1; ...}
		char *p;
		uint16_t size, cur_size, cur_ptr = 0xFFFF;
		if((cnum = param[sizeof(WEB_SCH_Calendar)-1]) == '\0') cnum = sch_data.Active; else cnum -= '0';
		if(cnum >= MAX_CALENDARS) return 1;
		while((p = strpbrk(val, ";"))) {
			p[0] = 0;
			uint8_t dec = atoi(val);
			val = p + 1;
			if(cur_ptr == 0xFFFF) {
				size = Timetable_ptr(MAX_CALENDARS-1);
				size += 1 + sch_data.Timetable[size];
				cur_ptr = Timetable_ptr(cnum);
				cur_size = sch_data.Timetable[cur_ptr];
				if(size - cur_size + dec > TIMETABLES_MAXSIZE) { // не лезет в память
					journal.jprintf("Scheduler full: %d - %d + %d > %d\n", size, cur_size, dec, TIMETABLES_MAXSIZE);
					return 2;
				}
				if(cnum < MAX_CALENDARS - 1) {
					cur_size += cur_ptr + 1;
					memmove(sch_data.Timetable + cur_ptr + 1 + dec, sch_data.Timetable + cur_size, size - cur_size - 1);
				}
				sch_data.Timetable[cur_ptr++] = dec;
				if(dec == 0) return OK;
			} else {
				sch_data.Timetable[cur_ptr++] = dec;
			}
		}
	}
	return OK;
}

// Записать настройки в eeprom i2c, если число меньше 0 это код ошибки
int8_t Scheduler::save(void)
{
    // Пишем всегда НОВЫМ форматом: область опознаёт себя сама, поэтому
    // расписание, сохранённое раньше настроек, ничего не ломает.
    uint8_t hdr[REC_HDR_SIZE];
    rec_hdr_put(hdr, NTAG_Scheduler, sizeof(sch_data));
    if(writeEEPROM_I2C(I2C_SCHEDULER_EEPROM, hdr, sizeof(hdr))) {
        set_Error(ERR_SAVE_EEPROM, (char *) get_name());
        return ERR_SAVE_EEPROM;
    }
    if(writeEEPROM_I2C(I2C_SCHEDULER_EEPROM + REC_HDR_SIZE, (byte*)&sch_data, sizeof(sch_data))) {
        set_Error(ERR_SAVE_EEPROM, (char *) get_name());
        return ERR_SAVE_EEPROM;
    }
    uint16_t crc16 = get_crc16((uint8_t *)&sch_data);
    if(writeEEPROM_I2C(I2C_SCHEDULER_EEPROM + REC_HDR_SIZE + sizeof(sch_data), (byte*)&crc16, sizeof(crc16))) {
    	set_Error(ERR_SAVE_PROFILE, (char *) get_name());
    	return ERR_SAVE_PROFILE;
    }
    int8_t err = check_crc16_eeprom();
    if(err == OK) journal.jprintf("Save scheduler to I2C OK, write: %d bytes, crc: %04x\n", sizeof(sch_data) + sizeof(crc16), crc16);
    return err;
}

// Привести данные, прочитанные из записи любой длины, к нынешним пределам.
// lost - при чтении записи старой длины в отброшенных календарях были данные.
// Остальное (обрезка буфера календарей) сделано при распаковке.
void Scheduler::fix_loaded(Scheduler_Data *d, bool lost)
{
	if(lost) journal.jprintf("\n WARNING: calendars %d-%d dropped (limit is %d), their data lost\n", MAX_CALENDARS + 1, MAX_CALENDARS_OLD, MAX_CALENDARS);
	if(Scheduler_FixActive(&d->Flags, &d->Active, MAX_CALENDARS, 1 << bScheduler_active)) {
		journal.jprintf("\n WARNING: active calendar is out of range, scheduler is turned off, calendar 1 selected\n");
	}
}

// Распаковать запись старой длины (9 календарей) из буфера src в d.
// Возвращает true, если в отброшенных календарях были данные.
static bool unpack_old(Scheduler_Data *d, const uint8_t *src)
{
	const uint8_t *tt  = src + 2 + MAX_CALENDARS_OLD * 32;
	const uint8_t *asw = tt + TIMETABLES_MAXSIZE;
	memcpy(d, src, 2 + MAX_CALENDARS * 32);                 // Flags, Active, имена первых календарей
	memcpy(d->Timetable, tt, TIMETABLES_MAXSIZE);
	memcpy(d->AutoSelectMonthWeek, asw, MAX_CALENDARS);
	bool lost = Scheduler_TrimTimetable(d->Timetable, TIMETABLES_MAXSIZE, MAX_CALENDARS);
	for(uint8_t i = MAX_CALENDARS; i < MAX_CALENDARS_OLD; i++) if(asw[i]) lost = true;
	return lost;
}

// Считать настройки в память по адресу из eeprom i2c, если число меньше 0 это код ошибки
// Запись в EEPROM может быть и прежней длины (9 календарей) - тогда лишнее отбрасывается.
int16_t Scheduler::load(uint8_t *data)
{
	journal.jprintf(" Scheduler ");
	uint16_t len = region_len();
	if(!len) {
		journal.jprintf("unknown region!\n");
		return ERR_CRC16_EEPROM;
	}
	int16_t ret = legacy_scheduler_has_crc(HP.Option.ver) ? check_crc16_eeprom() : OK;
#ifdef LOAD_VERIFICATION
	if(ret != OK) {
		journal.jprintf("CRC mismatch!\n");
		return ret;
	}
#endif
	Scheduler_Data *d = data ? (Scheduler_Data *) data : &sch_data;
	journal.jprintf(data ? "save" : "load");
	uint32_t base = data_addr();
	bool lost = false, fail;
	if(len == SCHEDULER_LEN_NEW) {
		fail = readEEPROM_I2C(base, (uint8_t *) d, sizeof(*d));
	} else {                                              // прежняя длина: читаем кусками, без буфера на 800 байт
		uint8_t asw[MAX_CALENDARS_OLD];
		fail = readEEPROM_I2C(base, (uint8_t *) d, 2 + MAX_CALENDARS * 32)
		    || readEEPROM_I2C(base + 2 + MAX_CALENDARS_OLD * 32, d->Timetable, TIMETABLES_MAXSIZE)
		    || readEEPROM_I2C(base + 2 + MAX_CALENDARS_OLD * 32 + TIMETABLES_MAXSIZE, asw, sizeof(asw));
		if(!fail) {
			memcpy(d->AutoSelectMonthWeek, asw, MAX_CALENDARS);
			lost = Scheduler_TrimTimetable(d->Timetable, TIMETABLES_MAXSIZE, MAX_CALENDARS);
			for(uint8_t i = MAX_CALENDARS; i < MAX_CALENDARS_OLD; i++) if(asw[i]) lost = true;
		}
	}
	if(fail) {
		set_Error(ERR_LOAD_EEPROM, (char *) get_name());
		journal.jprintf(" - Error!\n");
		return ERR_SAVE_EEPROM;
	}
	fix_loaded(d, lost);
	ret = sizeof(*d);
	if(data) {                                            // экспорт: за данными контрольная сумма уже обработанных данных
		uint16_t crc = get_crc16((uint8_t *) d);
		*(uint16_t *)(data + sizeof(*d)) = crc;
		journal.jprintf(" - crc: %04x, ", crc);
		ret += 2;
	}
	if(len != SCHEDULER_LEN_NEW) journal.jprintf(" (old record, %d bytes)", len);
	journal.jprintf(" %d bytes Ok.\n", ret);
	return ret;
}

// Считать настройки из буфера (секция файла настроек).
// avail - сколько байт после начала данных есть в файле; hint - длина данных
// из заголовка записи, либо 0, если заголовка нет (файл прежней выгрузки).
// Длина без подсказки определяется по контрольной сумме: сначала нынешняя, затем прежняя.
int8_t Scheduler::loadFromBuf(byte* buf, uint16_t avail, uint16_t hint)
{
	uint16_t len = 0;
	const uint16_t cand[2] = { SCHEDULER_LEN_NEW, SCHEDULER_LEN_OLD };
	for(uint8_t i = 0; i < 2 && !len; i++) {
		if(hint && hint != cand[i]) continue;
		if(avail < cand[i] + 2) continue;
#ifdef LOAD_VERIFICATION
		uint16_t crc = 0xFFFF;
		for(uint16_t k = 0; k < cand[i]; k++) crc = _crc16(crc, buf[k]);
		if(crc != *(uint16_t *)(buf + cand[i])) continue;
#endif
		len = cand[i];
	}
	if(!len) {
		journal.jprintf(" Load scheduler: unknown length or CRC error (%d bytes available, hint %d)\n", avail, hint);
		return ERR_CRC16_EEPROM;
	}
	journal.jprintf(" Load scheduler %d bytes, ", len + 2);
	bool lost = false;
	if(len == SCHEDULER_LEN_NEW) memcpy((byte *) &sch_data, buf, sizeof(sch_data));
	else lost = unpack_old(&sch_data, buf);
	fix_loaded(&sch_data, lost);
	journal.jprintf("OK\n");
	return OK;
}

// Рассчитать контрольную сумму для данных на входе входная сумма на выходе новая
uint16_t Scheduler::get_crc16(uint8_t *data)
{
	uint16_t crc = 0xFFFF;
	for(uint16_t i = 0; i < sizeof(sch_data); i++) crc = _crc16(crc, data[i]);
	return crc;
}

// Область в EEPROM новая (с заголовком записи)?
bool Scheduler::region_is_new(void)
{
	uint8_t hdr[REC_HDR_SIZE];
	if(readEEPROM_I2C(I2C_SCHEDULER_EEPROM, hdr, sizeof(hdr))) return false;
	return rec_hdr_tag(hdr) == NTAG_Scheduler && region_len() != 0;
}

// Длина данных области: у записи с заголовком её несёт заголовок (принимаются
// нынешняя и прежняя), у самой старой без заголовка - всегда прежняя.
// 0 - заголовок наш, а длина чужая.
uint16_t Scheduler::region_len(void)
{
	uint8_t hdr[REC_HDR_SIZE];
	if(readEEPROM_I2C(I2C_SCHEDULER_EEPROM, hdr, sizeof(hdr))) return 0;
	if(rec_hdr_tag(hdr) != NTAG_Scheduler) return SCHEDULER_LEN_OLD;
	uint16_t l = rec_hdr_len(hdr);
	return (l == SCHEDULER_LEN_NEW || l == SCHEDULER_LEN_OLD) ? l : 0;
}

// Адрес самих данных: в новом формате за заголовком записи.
uint32_t Scheduler::data_addr(void)
{
	return I2C_SCHEDULER_EEPROM + (region_is_new() ? REC_HDR_SIZE : 0);
}

int8_t Scheduler::check_crc16_eeprom(void)
{
	// Читаем пачками, а не по байту. Побайтно это 799 отдельных транзакций
	// I2C, каждая с захватом семафора, и всё это под остановленными задачами,
	// где watchdog не гладит никто.
	uint16_t crc2, crc = 0xFFFF;
	uint16_t len = region_len();
	if(!len) return ERR_CRC16_EEPROM;
	uint32_t base = data_addr();
	// Буфер статический, а не на стеке: проверка вызывается из веб-задачи
	// (STACK_vWebX = 170 слов = 680 байт), где 64 байта - заметная доля.
	// Обслуживание веб-запроса сериализовано семафором, гонки нет.
	static uint8_t  blk[64];
	uint16_t i = 0;
	while(i < len) {
		uint16_t n = len - i;
		if(n > sizeof(blk)) n = sizeof(blk);
		if(readEEPROM_I2C(base + i, blk, n)) {
			set_Error(ERR_LOAD_EEPROM, (char *) get_name());
			return ERR_LOAD_EEPROM;
		}
		crc = calc_crc16(blk, n, crc);
		i += n;
	}
	if(readEEPROM_I2C(base + i, (byte*) &crc2, sizeof(crc2))) {
		set_Error(ERR_LOAD_EEPROM, (char *) get_name());
		return ERR_LOAD_EEPROM;
	}
	return crc == crc2 ? OK : ERR_CRC16_EEPROM;
}

bool Scheduler::UsesProfile(uint8_t profile)
{
	return Scheduler_UsesProfile(sch_data.Timetable, TIMETABLES_MAXSIZE, MAX_CALENDARS, profile);
}

// ---------------------------------------------------------------------------
// Реестр параметров веб-API: расписание. Группа sched (включено, активный календарь, его имя) и группа cal
// (cal.<номер с 0>.name / .asm / .asw / .data). Календарь .data - прежняя строка потока записей
// «{сутки<<5|час};{код};...» (разбор и сборка остались в морде), запись принимает «длина;{...};» как раньше.
// Всё идёт через прежние web_get_param / web_set_param, поэтому поведение то же.
// ---------------------------------------------------------------------------
enum { SC_ON = 1, SC_AN, SC_ACTIVE };
static const ApiParam apiSchedTab[] = {
	{ "on",          0, 0, 0, 0, 1, PT_CALL, 0, MK_BOOL, SC_ON, 0 },
	{ "active",      0, 0, MAX_CALENDARS - 1, 0, 1, PT_CALL, 0, MK_INT, SC_ACTIVE, 0 },   // номер активного календаря с 0
	{ "active_name", 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_STR,  SC_AN, 0 },
};
static int api_get_sched(uint8_t hook, uint8_t, ApiOut *o)
{
	char t[40]; t[0] = 0;
	if(hook == SC_ON) {
		HP.Schdlr.web_get_param((char*) WEB_SCH_On, t);
		api_put(o, t[0] == '1' ? "true" : "false");
	} else if(hook == SC_ACTIVE) {
		HP.Schdlr.web_get_param((char*) WEB_SCH_Active, t);
		api_put(o, t);
	} else if(hook == SC_AN) {
		HP.Schdlr.web_get_param((char*) WEB_SCH_ActiveName, t);
		if(t[0] == '-' && !t[1]) api_put(o, "null"); else api_put_str(o, t);   // расписание выключено - имени нет
	} else return AE_FAIL;
	return AE_OK;
}
static int api_set_sched(uint8_t hook, uint8_t, const char *val)
{
	char t[4];
	if(hook == SC_ON) {
		if(!strcmp(val, "1") || !strcmp(val, "true")) strcpy(t, "1"); else if(!strcmp(val, "0") || !strcmp(val, "false")) strcpy(t, "0"); else return AE_BAD_VALUE;
		return HP.Schdlr.web_set_param((char*) WEB_SCH_On, t) == OK ? AE_OK : AE_FAIL;
	}
	if(hook == SC_ACTIVE) return HP.Schdlr.web_set_param((char*) WEB_SCH_Active, (char*) val) == OK ? AE_OK : AE_RANGE;   // границы проверило ядро
	return AE_FAIL;
}
static const ApiOps apiOpsSched = { 0, api_get_sched, api_set_sched, 0, 0, 0, 0, 0 };
extern const ApiGroup ApiGrpSched;      // без предварительного extern const имела бы внутреннюю привязку
const ApiGroup ApiGrpSched = { "sched", apiSchedTab, sizeof(apiSchedTab) / sizeof(apiSchedTab[0]), &apiOpsSched };

enum { CL_NAME = 1, CL_ASM, CL_ASW, CL_DATA };
static const ApiParam apiCalTab[] = {
	{ "name", 0, 0, 0, 0, 1, PT_CALL, 0, MK_TEXT, CL_NAME, 0 },
	{ "asm",  0, 0, 12, 0, 1, PT_CALL, 0, MK_INT, CL_ASM, 0 },     // автовыбор: месяц, 0 - нет
	{ "asw",  0, 0, 3, 0, 1, PT_CALL, 0, MK_INT, CL_ASW, 0 },      // автовыбор: декада (при asm = 0 запись игнорируется)
	{ "data", 0, 0, 0, 0, 1, PT_CALL, 0, MK_TEXT, CL_DATA, 0 },    // поток записей календаря
};
static uint8_t api_cal_n(void) { return MAX_CALENDARS; }
static const char *api_cal_name(uint8_t i) { static const char *const n[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8" }; return n[i]; }   // const
static uint8_t api_cal_ok(uint8_t) { return 1; }
static_assert(MAX_CALENDARS <= 9, "имена календарей в таблице api_cal_name - увеличить при росте MAX_CALENDARS");
// Имя параметра для web_*_param: «ASM2», «Calendar0» ...
static void api_cal_param(char *dst, const char *base, uint8_t dev) { strcpy(dst, base); size_t l = strlen(dst); dst[l] = (char)('0' + dev); dst[l + 1] = 0; }
static int api_get_cal(uint8_t hook, uint8_t dev, ApiOut *o)
{
	char prm[16], t[40]; t[0] = 0;
	switch(hook) {
	case CL_NAME: api_cal_param(prm, WEB_SCH_Name, dev); HP.Schdlr.web_get_param(prm, t); api_put_str(o, t); break;
	case CL_ASM:  api_cal_param(prm, WEB_SCH_AutoSelectMonth, dev); HP.Schdlr.web_get_param(prm, t); api_put(o, t[0] == '-' ? "0" : t); break;
	case CL_ASW:  api_cal_param(prm, WEB_SCH_AutoSelectWeek, dev); HP.Schdlr.web_get_param(prm, t); api_put(o, t[0] == '-' ? "0" : t); break;
	case CL_DATA:
		if(o->cap - o->len < 2300) return AE_TOO_BIG;       // до 500 записей по 4 знака; пишется в свободную часть буфера и экранируется на месте
		{ char *b = o->p + o->len + 1; b[0] = 0;
		  api_cal_param(prm, WEB_SCH_Calendar, dev); HP.Schdlr.web_get_param(prm, b);
		  api_str_inplace(o); }
		break;
	default: return AE_FAIL;
	}
	return AE_OK;
}
static int api_set_cal(uint8_t hook, uint8_t dev, const char *val)
{
	char prm[16];
	switch(hook) {
	case CL_NAME:
		if(strlen(val) >= sizeof(HP.Schdlr.sch_data.Names[0])) return AE_RANGE;
		strcpy(HP.Schdlr.sch_data.Names[dev], val);        // текст уже раскодирован ядром
		return AE_OK;
	case CL_ASM: api_cal_param(prm, WEB_SCH_AutoSelectMonth, dev); return HP.Schdlr.web_set_param(prm, (char*) val) == OK ? AE_OK : AE_FAIL;
	case CL_ASW: api_cal_param(prm, WEB_SCH_AutoSelectWeek, dev);  return HP.Schdlr.web_set_param(prm, (char*) val) == OK ? AE_OK : AE_FAIL;
	case CL_DATA: {
		api_cal_param(prm, WEB_SCH_Calendar, dev);
		uint8_t r = HP.Schdlr.web_set_param(prm, (char*) val);   // val - рабочая копия ядра: разбор её портит
		return r == OK ? AE_OK : (r == 2 ? AE_TOO_BIG : AE_BAD_VALUE); }
	}
	return AE_FAIL;
}
static const ApiOps apiOpsCal = { 0, api_get_cal, api_set_cal, 0, 0, api_cal_n, api_cal_name, api_cal_ok };
extern const ApiGroup ApiGrpCal;
const ApiGroup ApiGrpCal = { "cal", apiCalTab, sizeof(apiCalTab) / sizeof(apiCalTab[0]), &apiOpsCal };
