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
 * ЧТЕНИЕ СТАРЫХ ФОРМАТОВ СОХРАНЕНИЯ. Файл целиком предназначен к удалению.
 *
 * Вся обработка старых версий настроек собрана здесь, а на местах остались однострочные вызовы
 * с приставкой legacy_ (их находит один grep). Удалять - через год после выхода прошивки с новым
 * форматом и первого сохранения настроек пользователями: файл, его вызовы и тест под него.
 *
 * ПРАВИЛО ПРАВКИ. Код перенесён ДОСЛОВНО (кроме отмеченных отступлений). Ветки старше версии
 * 147 не покрыты тестами - файлов настроек таких версий не существует, - поэтому защищает их
 * только дословность. Найденные странности отмечены комментариями, но НЕ исправлены.
 *
 * ЧТЕНИЕ СТАРОГО ФОРМАТА НИЧЕГО НЕ ПИШЕТ в EEPROM: запись идёт только в новом формате и только
 * по сохранению. Старые эпохи профилей читает Profile::cache_load_legacy - в ОЗУ.
 */
#ifndef SettingsLegacy_h
#define SettingsLegacy_h

#include <stdint.h>

// Версия формата, начиная с которой запись идёт новым каркасом.
// Всё, что меньше, читается кодом из этого файла.
#define SETTINGS_VER_NEW   150

// Старая раскладка профиля: размеры блоков до версии 147.
struct LegacyProfileSizes {
	uint16_t dataProfile;
	uint16_t SaveON;
	uint16_t HeatCool;
	uint16_t Boiler;
	uint16_t DailySwitch;
	uint16_t total;              // весь слот, включая признак и контрольную сумму
};

// Выбор размеров по версии. false - конверсия не нужна.
bool legacy_profile_sizes(uint8_t ver, LegacyProfileSizes *s, uint16_t hdr_size);

// До версии 145 у расписания не было контрольной суммы.
bool legacy_scheduler_has_crc(uint8_t ver);

// Для devEEV::after_load. В версии 128 флаги ЭРВ лежали
// в поле верхней границы перегрева.
void legacy_eev_flags(uint8_t ver, uint16_t *flags, uint8_t *OHCor_TDIS_TCON_MAX);

// До версии 139 счётчики лежали по другой схеме.
bool legacy_motohour_needed(uint8_t ver);

// До версии 134 настройки пересохранялись сразу после чтения.
bool legacy_resave_after_load(uint8_t ver);

#endif
