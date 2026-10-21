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
 * Чтение старых форматов сохранения. Назначение файла и правило правки -
 * в SettingsLegacy.h. Коротко: здесь ничего не переписывается, только
 * перенесено дословно, и файл целиком предназначен к удалению.
 */
#include "../../Constant.h"
#include "SettingsLegacy.h"
#include "HeatPump.h"

// Выбор размеров слотов профиля по версии настроек: сравнения идут от старшей версии
// к младшей.
//
// ver <= 135: поля DailySwitch в той версии не существовало, поэтому его длина задана
// нулём (вызывающий копирует DailySwitch безусловно): ноль означает "поля не было", и
// значения по умолчанию остаются. Неинициализированная длина читала бы с шины
// произвольный размер. Ветка не покрыта тестами, файлов настроек версии 135 не
// существует; нужны ли вообще версии ниже 147, решать надо отдельно.
bool legacy_profile_sizes(uint8_t ver, LegacyProfileSizes *s, uint16_t hdr_size)
{
	if(ver > 146) return false;                  // конверсия не нужна
	if(ver <= 135) {
		s->dataProfile  = 120;
		s->SaveON       =  12;
		s->HeatCool     =  50;
		s->Boiler       =  80;
		s->DailySwitch  =   0;   // в этой версии поля не было (см. выше)
		s->total = hdr_size + s->dataProfile + s->SaveON + s->HeatCool + s->HeatCool + s->Boiler;
	} else if(ver <= 143) {
		s->dataProfile  = 120;
		s->SaveON       =  12;
		s->HeatCool     =  36;
		s->Boiler       =  64;
		s->DailySwitch  =  30;
		s->total = hdr_size + s->dataProfile + s->SaveON + s->HeatCool + s->HeatCool + s->Boiler + s->DailySwitch;
	} else {                                     // ver <= 146
		s->dataProfile  = 120;
		s->SaveON       =  12;
		s->HeatCool     =  38;
		s->Boiler       =  64;
		s->DailySwitch  =  30;
		s->total = hdr_size + s->dataProfile + s->SaveON + s->HeatCool + s->HeatCool + s->Boiler + s->DailySwitch;
	}
	return true;
}

// Расписание имеет контрольную сумму с версии 145: HP.Option.ver <= 144 ? OK : check_crc16_eeprom()
bool legacy_scheduler_has_crc(uint8_t ver)
{
	return ver > 144;
}

// Для devEEV::after_load: в версии 128 флаги ЭРВ лежали в поле верхней границы перегрева.
void legacy_eev_flags(uint8_t ver, uint16_t *flags, uint8_t *OHCor_TDIS_TCON_MAX)
{
	if(ver == 128) {                             // Конвертация флагов
		*flags = *OHCor_TDIS_TCON_MAX;
		*OHCor_TDIS_TCON_MAX = 50;
	}
}

// До версии 139 счётчики лежали по другой схеме.
bool legacy_motohour_needed(uint8_t ver)
{
	return ver <= 138;
}

// До версии 134 настройки пересохранялись сразу после чтения: if(HP.Option.ver <= 133) HP.save();
bool legacy_resave_after_load(uint8_t ver)
{
	return ver <= 133;
}

// ---------------------------------------------------------------------------
// ЧТЕНИЕ ВСЕХ НАСТРОЕК СТАРОГО ФОРМАТА - одной функцией: три области лежат в разных регионах
// EEPROM, их последовательность и поправки по версиям собраны здесь.
//
// ЭТА ФУНКЦИЯ НИЧЕГО НЕ ПИШЕТ. Старые эпохи читаются в кеш (cache_load_legacy) и попадают на
// носитель только при сохранении, по кнопке: чтение настроек не должно молча писать в EEPROM
// при каждой загрузке.
// ---------------------------------------------------------------------------
// Выбор читателя. Версия лежит на смещении 3 от начала блока и в старом
// формате, и в новом - это единственное, что у них общего, и ради этого
// новый формат держит там свой байт версии. Проверено на трёх эталонных
// файлах: у всех троих там 147.
//
// Эта функция исчезнет вместе с файлом: когда старый формат перестанет
// поддерживаться, выбирать будет не из чего.
int32_t HeatPump::load_all(uint8_t *buffer)
{
	uint8_t probe[NEW_OFS_VER + 1];
	if(readEEPROM_I2C(I2C_SETTING_EEPROM, probe, sizeof(probe))) {
		journal.jprintf(" Settings: read error\n");
		return error = ERR_LOAD_EEPROM;
	}
	if(probe[NEW_OFS_VER] >= SETTINGS_VER_NEW) {    // новый формат
		int32_t ret = load_new(buffer, 0);
		load_parts();
		return ret;
	}
	return load_all_old(buffer);
}

int32_t HeatPump::load_all_old(uint8_t *buffer)
{
	int32_t ret = load(buffer, 0);                  // настройки: голова по порядку, хвост по тегам
	load_parts();                                   // расписание, все профили в ОЗУ, текущий профиль
	if(legacy_resave_after_load(Option.ver)) save_all(); // до версии 134 пересохраняли сразу
	return ret;
}
