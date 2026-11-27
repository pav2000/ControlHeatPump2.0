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

#include "SettingsImport.h"
#include "HeatPump.h"

// Проверяет секцию профилей между ptr и file_end. version задаёт раскладку старых слотов.
// Возвращает длину секции с END и CRC либо -1 при отказе.
static int32_t fileProfilesLen(const uint8_t *ptr, const uint8_t *file_end, uint8_t version, bool trace)
{
	int32_t len;
	const int32_t available = file_end - ptr;
	if(available < REC_HDR_SIZE) return -1;
	const uint16_t tag = rec_hdr_tag(ptr);
	if(tag == NTAG_Profile || tag == NTAG_END) {
		ProfileScan ps;
		profile_scan(ptr, file_end, NTAG_Profile, NTAG_END, 1 + sizeof(ProfileSlot), I2C_PROFIL_NUM, &ps);
		len = ps.bytes + sizeof(uint16_t);               // плюс END (уже в bytes) и контрольная сумма
		if(ps.bad_len || ps.bad_slot || ps.bad_tag || ps.truncated || !ps.end_seen || len > available) {
			if(trace) journal.jprintf_time("CFG: профили файла, неверная длина %d, слот %d, тег %d, обрыв %d, END %d\n",
			                              ps.bad_len, ps.bad_slot, ps.bad_tag, ps.truncated, ps.end_seen);
			return -1;
		}
		// Контрольная сумма области: по всем байтам записей и заголовку END, как при записи в save_region
		uint16_t crc_stored;
		memcpy(&crc_stored, ptr + ps.bytes, sizeof(crc_stored));
		uint16_t crc_file = calc_crc16((unsigned char*) ptr, ps.bytes, 0xFFFF);
		if(crc_file != crc_stored) {
			if(trace) journal.jprintf_time("CFG: CRC профилей рассчитана %04x, ожидалась %04x\n", crc_file, crc_stored);
			return -1;
		}
	} else {
		if(version >= SETTINGS_VER_NEW) return -1; // Новый формат профилей содержит CRC всей области.
		LegacyProfileSizes sz;
		const int32_t stride = legacy_profile_sizes(version, &sz, sizeof(uint8_t) + sizeof(uint16_t))
		                       ? sz.total : HP.Prof.get_sizeProfile();
		len = stride * I2C_PROFIL_NUM_LEGACY;
		if(len > available) return -1;
	}
	if(len > I2C_SCHEDULER_EEPROM - I2C_PROFILE_EEPROM) return -1;
	return len;
}

// Отказ файла сам по себе не меняет активную аварию ТН.
static SettingsImportResult rejectImport(const char *reason, SettingsImportResult result = SettingsImportResult::Invalid)
{
	journal.event(Journal::Stamp::Time, "CFG", "RESTORE", "Файл отклонён: %s, CRC16=н/д", reason);
	return result;
}

// Проверяет формат и границы всех секций перед применением. Основные настройки и расписание
// загружает в ОЗУ, профили записывает в EEPROM. trace передан до загрузки новой маски.
SettingsImportResult restoreSettingsFile(uint8_t *ptr, int32_t buf_len, bool trace)
{
	// Ищем HEADER_BIN в собранных бинарных данных без завершающего нуля.
	const int32_t ml = sizeof(HEADER_BIN) - 1;
	int32_t k = -1;
	for(int32_t n = 0; n + ml <= buf_len; n++)
		if(memcmp(ptr + n, HEADER_BIN, ml) == 0) { k = n + ml; break; }
	if(k < 0) return rejectImport("нет заголовка настроек");
	buf_len -= k;                                                        // дальше идут бинарные данные, без текстового заголовка
	memmove(ptr, ptr + k, buf_len);
	// Версия должна находиться в теле настроек перед CRC, а весь блок — в принятом файле.
	const uint32_t block = settings_file_block_size(ptr, buf_len);
	if(block < NEW_OFS_VER + 1 + sizeof(uint16_t)) return rejectImport("неверная длина блока настроек");
	const uint8_t version = ptr[NEW_OFS_VER];
	// Порог формата остаётся 150 независимо от версии, которой сохраняет прошивка.
	if(version == 0 || version > VER_SAVE) return rejectImport("неподдерживаемая версия настроек");
	const bool newFormat = version >= SETTINGS_VER_NEW;
	const uint8_t *file_end = ptr + buf_len;
	const int32_t profilesLen = fileProfilesLen(ptr + block, file_end, version, trace);
	if(profilesLen < 0) return rejectImport("неверная секция профилей");
	uint8_t *schedule = ptr + block + profilesLen;
	uint16_t hint = 0;
	if(file_end - schedule >= REC_HDR_SIZE && rec_hdr_tag(schedule) == NTAG_Scheduler) {
		hint = rec_hdr_len(schedule);
		if(!hint) return rejectImport("пустая секция расписания");
		schedule += REC_HDR_SIZE;
	}
	const uint16_t scheduleLen = HP.Schdlr.file_data_len(schedule, file_end - schedule, hint, newFormat);
	if(!scheduleLen) return rejectImport("неверная длина или CRC секции расписания");
	if(HP.IsWorkingNow()) return rejectImport("ТН запущен", SettingsImportResult::Busy);

	SettingsLock lock(HP.saving);
	if(!lock) return rejectImport("идёт другая операция с настройками", SettingsImportResult::Busy);
#ifdef EEV_DEF
	// Загрузка вызывает InitStepper; проверка раннего снимка IsWorkingNow недостаточна.
	if(!HP.beginEEVService()) return rejectImport("ТН или ЭРВ заняты", SettingsImportResult::Busy);
	struct SettingsEEVGuard { ~SettingsEEVGuard() { HP.endEEVService(); } } eevGuard;
#endif
	const int32_t len = newFormat ? HP.load_new(ptr, block, 1) : HP.load(ptr, block, 1);
	if(len <= 0) return SettingsImportResult::Invalid;
	const unsigned crc = rec_hdr_tag(ptr + block - sizeof(uint16_t));
	ptr += block;
	if(writeEEPROM_I2C(I2C_PROFILE_EEPROM, ptr, profilesLen)) {
		HP.Prof.invalidate_cache(ErrorNumber(ErrorCatalog::Code::kPROFILE_WRITE));
		HP.Prof.update_list(HP.Prof.get_idProfile());
		HP.set_Warning(ErrorCatalog::Code::kPROFILE_WRITE, "profiles", 0,
		               "Сбой записи профилей. Настройки в ОЗУ, расписание прежнее");
		return SettingsImportResult::Invalid;
	}
	int32_t profiles = HP.Prof.cache_load_all(trace);
	if(profiles == OK) profiles = HP.Prof.load(HP.Option.numProf);
	HP.Prof.update_list(HP.Prof.get_idProfile());
	if(profiles < 0) {
		HP.log_Event(static_cast<ErrorCatalog::Code>(profiles), "profiles", {},
		             "Профиль не загружен. Профили в EEPROM, настройки в ОЗУ");
		return SettingsImportResult::Invalid;
	}
	if(HP.Schdlr.loadFromBuf(schedule, file_end - schedule, scheduleLen, trace) != OK) {
		HP.log_Event(ErrorCatalog::Code::kSETTINGS_DATA, "SCHED", {},
		             "Сбой расписания. Настройки в ОЗУ, профили в EEPROM");
		return SettingsImportResult::Invalid;
	}
#ifdef LOAD_VERIFICATION
	const char *verification = "";
#else
	const char *verification = newFormat ? "" : " (проверка CRC отключена)";
#endif
	journal.event(Journal::Stamp::Time, "CFG", "RESTORE",
	              "Настройки и расписание в ОЗУ, профили в EEPROM%s, CRC16=%04X", verification, crc);
	return SettingsImportResult::Ok;
}
