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
 * Настройки ТН: сохранение и чтение в i2c EEPROM (формат с тегами и старый),
 * счётчики моточасов, сброс к заводским значениям, а также разбор и выдача
 * веб-параметров сети, времени и опций. Отдельная
 * единица трансляции.
 *
 * HeatPump.h тянет Constant.h, а с ним Config.h, поэтому условия
 * (USE_SCHEDULER, USE_UPS, NEXTION и прочие) разрешаются
 * так же, как в главной единице трансляции. Проверено сверкой размеров
 * методов.
 */
#include "HeatPump.h"     // HeatPump, HP, Status, устройства и датчики
#include "../net/Network.h"      // initW5200, pingW5200, check_address
#include "Rtos.h"         // _delay, SemaphoreTake/Give
#include "../log/Statistics.h"   // Stats
#include "../net/WebShared.h"    // Socket[]
#include "Profile.h"      // Prof
#include "../util/Util.h"         // _itoa, ftoa
#include "../net/Message.h"      // Message
#include "../hw/Nextion.h"      // myNextion
#include "../hw/Clock.h"        // get_NTP_result
#include "../net/ParamRegistry.h" // таблица параметров веб-API (внизу файла)
#include <stddef.h>               // offsetof

// Сохранение и загрузка настроек, счётчиков моточасов и параметров,
// задаваемых из web-морды: сеть, часы, опции, хеши паролей.

// Записи в СТАРОМ формате больше нет. Она была здесь и занимала около
// семидесяти строк; удалена вместе с переходом на новый формат (шаг 6 плана).
// Причина не в чистоте: пока функция существует, её однажды кто-нибудь
// вызовет, и на носителе окажется смесь форматов. Читатель старого формата
// остаётся - он в SettingsLegacy.cpp и удаляется отдельно, через год.

// Считать настройки из памяти i2c или из RAM, если не NULL, на выходе длина или код ошибки (меньше нуля)
int32_t HeatPump::load(uint8_t *buffer, uint8_t from_RAM)
{
	uint16_t size;
	journal.jprintf(" Load settings from ");
	if(from_RAM == 0) {
		journal.jprintf("I2C");
		if(readEEPROM_I2C(I2C_SETTING_EEPROM, (byte*) &size, sizeof(size))) {
x_ReadError:
			error = ERR_CRC16_EEPROM;
x_Error:
			journal.jprintf(" - read error %d!\n", error);
			return error;
		}
		if(size > I2C_PROFILE_EEPROM - I2C_SETTING_EEPROM) { error = ERR_BAD_LEN_EEPROM; goto x_Error; }
		if(readEEPROM_I2C(I2C_SETTING_EEPROM + sizeof(size), buffer, size)) goto x_ReadError;
	} else {
		journal.jprintf("FILE");
		size = *((uint16_t *) buffer);
		buffer += sizeof(size);
	}
	journal.jprintf(", size %d, crc: ", size + 2); // sizeof(crc)
	size -= 2;
#ifdef LOAD_VERIFICATION
	
	uint16_t crc = 0xFFFF;
	crc = calc_crc16(buffer, size, crc);
	if(crc != *((uint16_t *)(buffer + size))) {
		journal.jprintf("Error: %04x != %04x!\n", crc, *((uint16_t *)(buffer + size)));
		return error = ERR_CRC16_EEPROM;
	}
	journal.jprintf("%04x", crc);
#else
	journal.jprintf("*No verification");
#endif
	uint8_t *buffer_max = buffer + size;
	size += 2;
	load_struct(&Option, &buffer, sizeof(Option));
	journal.jprintf(", v.%d ", Option.ver);  // вывести версию сохранения
	load_struct(&DateTime, &buffer, sizeof(DateTime));
	load_struct(&Network, &buffer, sizeof(Network));
	load_struct(message.get_save_addr(), &buffer, message.get_save_size());
	load_struct(dFC.get_save_addr(), &buffer,  dFC.get_save_size());
	int16_t type = SAVE_TYPE_LIMIT;
	while(buffer_max > buffer) { // динамические структуры
		if(*((int16_t *) buffer) <= SAVE_TYPE_END && *((int16_t *) buffer) > SAVE_TYPE_LIMIT) {
			type = *((int16_t *) buffer);
			buffer += 2;
		}
		// массивы, длина структуры должна быть меньше 128 байт, <size[1]><<number>struct>
		uint8_t n = buffer[1]; // номер элемента
		if(type == SAVE_TYPE_sTemp) { // первый в структуре идет номер датчика
			if(n < TNUMBER) { load_struct(sTemp[n].get_save_addr(), &buffer, sTemp[n].get_save_size());	sTemp[n].after_load(); } else goto xSkip;
		} else if(type == SAVE_TYPE_sADC) {
			if(n < ANUMBER) { load_struct(sADC[n].get_save_addr(), &buffer, sADC[n].get_save_size()); sADC[n].after_load(); } else goto xSkip;
		} else if(type == SAVE_TYPE_sInput) {
			if(n < INUMBER) load_struct(sInput[n].get_save_addr(), &buffer, sInput[n].get_save_size()); else goto xSkip;
		} else if(type == SAVE_TYPE_sFrequency) {
			if(n < FNUMBER) load_struct(sFrequency[n].get_save_addr(), &buffer, sFrequency[n].get_save_size()); else goto xSkip;
#ifdef SENSOR_IP
		} else if(type == SAVE_TYPE_sIP) {
			if(n < IPNUMBER) { load_struct(sIP[n].get_save_addr(), &buffer, sIP[n].get_save_size()); sIP[n].after_load(); } else goto xSkip;
#endif
		// не массивы <size[1|2]><struct>
#ifdef EEV_DEF
		} else if(type == SAVE_TYPE_dEEV) {
			load_struct(dEEV.get_save_addr(), &buffer, dEEV.get_save_size()); dEEV.after_load();
#endif
#ifdef USE_ELECTROMETER_SDM
		} else if(type == SAVE_TYPE_dSDM) {
			load_struct(dSDM.get_save_addr(), &buffer, dSDM.get_save_size());
#endif
#ifdef MQTT
		} else if(type == SAVE_TYPE_clMQTT) {
			load_struct(clMQTT.get_save_addr(), &buffer, clMQTT.get_save_size());
#endif
		} else if(type == SAVE_TYPE_END) {
			break;
		} else {
xSkip:		load_struct(NULL, &buffer, 0); // skip unknown type
		}
	}
#ifdef SENSOR_IP
	updateLinkIP();
#endif
	dbg_after_load();
	journal.jprintf("OK\n");
	return size + sizeof(crc);
}

// ---------------------------------------------------------------------------
// НОВЫЙ ФОРМАТ СОХРАНЕНИЯ, версия 150.
//
// Отличий от старого три, и все три устраняют конкретную неприятность.
//
// 1. Голова перестала быть позиционной. Раньше пять первых записей
//    опознавались только порядком: ничто в файле не говорило, что третья -
//    сетевая, и перестановка двух записей портила настройки молча. Теперь у
//    каждой записи есть тег.
// 2. Каркас записи фиксирован: [тег][длина], по два байта. Раньше длина была
//    переменной ширины, а теги отличались от неё только тем, что лежали в
//    отрицательном диапазоне. Подробнее - в Util.h.
// 3. Номер элемента массива читается из данных честно. Раньше он брался как
//    buffer[1], то есть у записей длиннее 128 байт попадал на старший байт
//    длины - отсюда неписаное ограничение "структура меньше 128 байт".
//
// Тег означает РАСКЛАДКУ: при несовместимом изменении структуры заводится
// новый тег, а не поднимается версия формата. Поэтому 150 простоит долго.
// ---------------------------------------------------------------------------

// Копирование с правилом усечения, тем же, что у load_struct: берём
// сколько есть, но не больше, чем ждём. Это и есть совместимость при росте
// структуры с конца - в обе стороны.
static void copy_min(void *to, const uint8_t *from, uint16_t have, uint16_t want)
{
	if(to) memcpy(to, from, have <= want ? have : want);
}

// Версия записи не может быть НИЖЕ порога нового каркаса: иначе прошивка
// писала бы данные, которые сама же потом отправит старому читателю.
static_assert(VER_SAVE >= SETTINGS_VER_NEW,
              "VER_SAVE ниже SETTINGS_VER_NEW: записанные настройки уйдут в старый читатель");

// Флаги отладки: хранятся все (DebugFlags.h). Нет сигнатуры - значения по умолчанию.
void HeatPump::dbg_after_load()
{
	uint32_t m;
	// Настройки записаны до появления флагов (нет сигнатуры): значения берутся
	// из прежних мест хранения. Сами эти биты после загрузки больше не
	// используются и очищаются, при следующем сохранении запишутся нулями.
	if(!Dbg_Unpack(Option.dbg, Option.dbg2, &m)) {
		if(Network.flags & (1 << fWebLogError)) m |= DBG_WERR;
		if(Network.flags & (1 << fWebFullLog))  m |= DBG_WFULL;
		if(GETBIT(Option.flags, fSDMLogErrors)) m |= DBG_SDM;
		if(dFC.get_legacy_logwork())            m |= DBG_FCW;
	}
	Network.flags &= ~((1 << fWebLogError) | (1 << fWebFullLog));
	Option.flags  &= ~(1 << fSDMLogErrors);
	dFC.clear_legacy_logwork();
	dbgFlags = m;
}

void HeatPump::dbg_before_save()
{
	Dbg_Pack(dbgFlags, &Option.dbg, &Option.dbg2);
}

int32_t HeatPump::save_all(void)
{
	// Условие - ПОРОГ: конверсия нужна, только если данные старого каркаса.
	// Сообщаем версию, которой пишем, то есть VER_SAVE.
	if(Option.ver < SETTINGS_VER_NEW) {
		journal.jprintf(" Converting settings to v.%d (was %d)\n", VER_SAVE, Option.ver);
	}
	// Сохранение одно за раз. save_all зовут CommandHP (pSAVE), веб-потоки (save.all) и загрузка прежнего формата,
	// а три области EEPROM (профили, расписание, настройки) пишутся по частям: две записи вперемешку дали бы
	// область из кусков двух сохранений. Флаг занимается атомарно, без мьютекса и без остановки планировщика.
	// Второй вызывающий получает отказ ERR_I2C_BUSY ("занято") и ничего не пишет; set_Error для него не вызывается.
	if(__atomic_exchange_n(&saving, (uint8_t) 1, __ATOMIC_SEQ_CST)) {
		journal.jprintf(" Save settings: busy, another save is in progress\n");
		return ERR_I2C_BUSY;
	}
	int32_t ret;
	Prof.cache_store(Prof.get_idProfile());        // текущий профиль - в кеш, оттуда пишется область
	if(Prof.save_region() < 0) ret = error;
	else if(Schdlr.save() != OK) ret = error;
	else ret = save_new();                         // настройки последними
	__atomic_store_n(&saving, (uint8_t) 0, __ATOMIC_SEQ_CST);
	return ret;
}

int32_t HeatPump::save_new(void)
{
	uint16_t crc = 0xFFFF;
	uint32_t addr = I2C_SETTING_EEPROM + 2;     // впереди общая длина
	Option.numProf = Prof.get_idProfile();      // нужно для восстановления настроек
	// Планировщик НЕ останавливается: он замораживал держателей замка I2C посреди транзакции (замок при остановке
	// фиктивный), а нужен был для согласованности CRC с записанными байтами - её теперь даёт save_rec (Record_WriteStable).
	if(error == ERR_SAVE_EEPROM || error == ERR_LOAD_EEPROM || error == ERR_CRC16_EEPROM || error == ERR_SETTINGS_VERIFY) error = OK;
	journal.jprintf(" Save settings v.%d ", VER_SAVE);
	DateTime.saveTime = rtcSAM3X8.unixtime();
	while(1) {
		// Пролог. Версия ложится на смещение 3 от начала блока - туда же,
		// где её держит старый формат, см. NEW_OFS_VER в Constant.h.
		uint8_t hdr[NEW_HDR_SIZE - 2];
		memset(hdr, 0, sizeof(hdr));
		hdr[NEW_OFS_VER - 2] = VER_SAVE;
		// Смещение NEW_OFS_VERLEGACY держало версию, с которой конвертировали.
		// Больше не нужна: старая обработка нужна ТОЛЬКО до конверсии, а тогда
		// её включает сам Option.ver. Байт остаётся нулём от memset выше и
		// НЕ переиспользуется: прошивки до этой правки читают его как эпоху.
		if(writeEEPROM_I2C(addr, hdr, sizeof(hdr))) { error = ERR_SAVE_EEPROM; break; }
		crc = calc_crc16(hdr, sizeof(hdr), crc);
		addr += sizeof(hdr);
		Option.ver = VER_SAVE;

		dbg_before_save();
		if(save_rec(addr, NTAG_Option,   (uint8_t *)&Option,   sizeof(Option),   crc)) break;
		if(save_rec(addr, NTAG_DateTime, (uint8_t *)&DateTime, sizeof(DateTime), crc)) break;
		if(save_rec(addr, NTAG_Network,  (uint8_t *)&Network,  sizeof(Network),  crc)) break;
		if(save_rec(addr, NTAG_Message,  message.get_save_addr(), message.get_save_size(), crc)) break;
		if(save_rec(addr, NTAG_FC,       dFC.get_save_addr(),     dFC.get_save_size(),     crc)) break;

		for(uint8_t i = 0; i < TNUMBER; i++)
			if(save_rec(addr, NTAG_sTemp, sTemp[i].get_save_addr(), sTemp[i].get_save_size(), crc)) break;
		if(error == ERR_SAVE_EEPROM) break;
		for(uint8_t i = 0; i < ANUMBER; i++)
			if(save_rec(addr, NTAG_sADC, sADC[i].get_save_addr(), sADC[i].get_save_size(), crc)) break;
		if(error == ERR_SAVE_EEPROM) break;
		for(uint8_t i = 0; i < INUMBER; i++)
			if(save_rec(addr, NTAG_sInput, sInput[i].get_save_addr(), sInput[i].get_save_size(), crc)) break;
		if(error == ERR_SAVE_EEPROM) break;
		for(uint8_t i = 0; i < FNUMBER; i++)
			if(save_rec(addr, NTAG_sFrequency, sFrequency[i].get_save_addr(), sFrequency[i].get_save_size(), crc)) break;
		if(error == ERR_SAVE_EEPROM) break;
#ifdef SENSOR_IP
		for(uint8_t i = 0; i < IPNUMBER; i++)
			if(save_rec(addr, NTAG_sIP, sIP[i].get_save_addr(), sIP[i].get_save_size(), crc)) break;
		if(error == ERR_SAVE_EEPROM) break;
#endif
#ifdef EEV_DEF
		if(save_rec(addr, NTAG_dEEV, dEEV.get_save_addr(), dEEV.get_save_size(), crc)) break;
#endif
#ifdef USE_ELECTROMETER_SDM
		if(save_rec(addr, NTAG_dSDM, dSDM.get_save_addr(), dSDM.get_save_size(), crc)) break;
#endif
#ifdef MQTT
		if(save_rec(addr, NTAG_clMQTT, clMQTT.get_save_addr(), clMQTT.get_save_size(), crc)) break;
#endif
		if(save_rec(addr, NTAG_END, NULL, 0, crc)) break;

		if(writeEEPROM_I2C(addr, (uint8_t *)&crc, sizeof(crc))) { error = ERR_SAVE_EEPROM; break; }
		addr = addr + sizeof(crc) - (I2C_SETTING_EEPROM + 2);          // превратить в длину
		if(writeEEPROM_I2C(I2C_SETTING_EEPROM, (uint8_t *)&addr, 2)) { error = ERR_SAVE_EEPROM; break; }
		int8_t _err;
		if((_err = check_crc16_eeprom(I2C_SETTING_EEPROM + 2, addr - 2)) != OK) {
			error = (_err == ERR_CRC16_EEPROM) ? ERR_SETTINGS_VERIFY : _err;   // записали, а прочитали другое
			journal.jprintf("- Verify Error!\n");
			break;
		}
		addr += 2;
		journal.jprintf("OK, wrote: %d bytes, crc: %04x\n", (int)(addr), crc);
		break;
	}
	if(error == ERR_SAVE_EEPROM || error == ERR_LOAD_EEPROM || error == ERR_CRC16_EEPROM || error == ERR_SETTINGS_VERIFY) {
		set_Error(error, (char*)__FUNCTION__);
		return error;
	}
	return addr;
}

// Ошибка загрузки настроек. При чтении из I2C (старт ТН) она идёт в журнал
// ошибок и уведомление: при выключенном ТН set_Error ничего не останавливает,
// а пуск ошибку сбрасывает. При загрузке файла из веба ТН может работать, и
// set_Error его остановил бы - там только код возврата.
int32_t HeatPump::load_fail(int8_t code, uint8_t from_RAM)
{
	if(from_RAM) error = code; else set_Error(code, (char*)nameHeatPump);
	return code;
}

// Замечание о загрузке: настройки применены, но владельцу надо знать.
void HeatPump::load_warn(int8_t code, uint8_t from_RAM)
{
	if(!from_RAM) set_Error(code, (char*)nameHeatPump);
}

// Расписание, кеш профилей и текущий профиль: общий итог для обоих читателей.
int8_t HeatPump::load_parts(void)
{
	int8_t ret = OK;
	if(Schdlr.load() < 0) ret = ERR_SETTINGS_PARTS;
	if(Prof.cache_load_all() < 0) ret = ERR_SETTINGS_PARTS;
	if(Prof.load(Option.numProf) < 0) {
		journal.jprintf(" Error load profile #%d\n", Option.numProf);
		ret = ERR_SETTINGS_PARTS;
	}
	if(ret != OK) {
		journal.jprintf(" Settings: schedule/profiles not loaded!\n");
		load_warn(ret, 0);
	}
	return ret;
}

int32_t HeatPump::load_new(uint8_t *buffer, uint8_t from_RAM)
{
	uint16_t size;
	journal.jprintf(" Load settings from ");
	if(from_RAM == 0) {
		journal.jprintf("I2C");
		if(readEEPROM_I2C(I2C_SETTING_EEPROM, (byte*) &size, sizeof(size))) {
xReadError:
			journal.jprintf(" - read error!\n");
			return load_fail(ERR_LOAD_EEPROM, from_RAM);
		}
		// Пустая память читается как 0xFFFF: без этой проверки её приняли бы за ошибку длины
		if(settings_head_check(size, VER_SAVE, VER_SAVE, NEW_HDR_SIZE) == SETHEAD_EMPTY) {
			journal.jprintf(" - not found (size %d)!\n", size);
			return load_fail(ERR_SETTINGS_HEADER, from_RAM);
		}
		if(size > I2C_PROFILE_EEPROM - I2C_SETTING_EEPROM) {
			journal.jprintf(" - bad size %d!\n", size);
			return load_fail(ERR_BAD_LEN_EEPROM, from_RAM);
		}
		if(readEEPROM_I2C(I2C_SETTING_EEPROM + sizeof(size), buffer, size)) goto xReadError;
	} else {
		journal.jprintf("FILE");
		size = *((uint16_t *) buffer);
		buffer += sizeof(size);
		if(settings_head_check(size, VER_SAVE, VER_SAVE, NEW_HDR_SIZE) == SETHEAD_EMPTY) {
			journal.jprintf(" - bad size %d!\n", size);
			return load_fail(ERR_SETTINGS_HEADER, from_RAM);
		}
	}
	journal.jprintf(", size %d, crc: ", size + 2);
	size -= 2;                                   // без контрольной суммы в конце
#ifdef LOAD_VERIFICATION
	uint16_t crc = calc_crc16(buffer, size, 0xFFFF);
	if(crc != *((uint16_t *)(buffer + size))) {
		journal.jprintf("Error: %04x != %04x!\n", crc, *((uint16_t *)(buffer + size)));
		return load_fail(ERR_CRC16_EEPROM, from_RAM);
	}
	journal.jprintf("%04x", crc);
#else
	journal.jprintf("*No verification");
#endif
	uint8_t ver = buffer[NEW_OFS_VER - 2];
	journal.jprintf(", v.%d ", ver);
	SettingsHead head = settings_head_check(size + 2, ver, VER_SAVE, NEW_HDR_SIZE);
	if(head == SETHEAD_EMPTY) {                  // версия 0 или 0xFF - блок не наш
		journal.jprintf("- unknown version!\n");
		return load_fail(ERR_SETTINGS_HEADER, from_RAM);
	}
	if(head == SETHEAD_NEWER) {                  // записано более новой прошивкой: читаем что знаем
		journal.jprintf("(newer than firmware v.%d!) ", VER_SAVE);
		if(from_RAM) return load_fail(ERR_SETTINGS_HEADER, from_RAM);
		load_warn(ERR_SETTINGS_HEADER, from_RAM);
	}

	uint8_t *p = buffer + (NEW_HDR_SIZE - 2);    // пролог уже разобран
	uint8_t *p_max = buffer + size;
	// Контрольная сумма сошлась, но каркас записей может быть нарушен (обрубленная
	// запись, нет END, лишний тег). Из файла такой блок не принимаем, из I2C
	// читаем что получится: ТН должен стартовать.
	SettingsScan sc;
	settings_scan(p, p_max, NTAG_END, NTAG_clMQTT, &sc);
	bool bad_struct = sc.truncated || !sc.end_seen || (sc.unknown && head == SETHEAD_OK);
	if(bad_struct) {
		journal.jprintf("structure: records %d, truncated %d, unknown %d, end %d! ", sc.records, sc.truncated, sc.unknown, sc.end_seen);
		if(from_RAM) return load_fail(ERR_SETTINGS_STRUCT, from_RAM);
	}
	while(p + REC_HDR_SIZE <= p_max) {
		uint16_t tag = rec_hdr_tag(p);
		uint16_t len = rec_hdr_len(p);
		uint8_t *data = p + REC_HDR_SIZE;
		if(data + len > p_max) break;            // обрубленная запись - дальше читать нечего
		p = data + len;
		uint8_t n = len ? data[0] : 0;           // номер элемента массива лежит первым байтом
		switch(tag) {
		case NTAG_END:      p = p_max; break;
		case NTAG_Option:   copy_min(&Option,   data, len, sizeof(Option));   break;
		case NTAG_DateTime: copy_min(&DateTime, data, len, sizeof(DateTime)); break;
		case NTAG_Network:  copy_min(&Network,  data, len, sizeof(Network));  break;
		case NTAG_Message:  copy_min(message.get_save_addr(), data, len, message.get_save_size()); break;
		case NTAG_FC:       copy_min(dFC.get_save_addr(),     data, len, dFC.get_save_size());     break;
		case NTAG_sTemp:
			if(n < TNUMBER) { copy_min(sTemp[n].get_save_addr(), data, len, sTemp[n].get_save_size()); sTemp[n].after_load(); }
			break;
		case NTAG_sADC:
			if(n < ANUMBER) { copy_min(sADC[n].get_save_addr(), data, len, sADC[n].get_save_size()); sADC[n].after_load(); }
			break;
		case NTAG_sInput:
			if(n < INUMBER) copy_min(sInput[n].get_save_addr(), data, len, sInput[n].get_save_size());
			break;
		case NTAG_sFrequency:
			if(n < FNUMBER) copy_min(sFrequency[n].get_save_addr(), data, len, sFrequency[n].get_save_size());
			break;
#ifdef SENSOR_IP
		case NTAG_sIP:
			if(n < IPNUMBER) { copy_min(sIP[n].get_save_addr(), data, len, sIP[n].get_save_size()); sIP[n].after_load(); }
			break;
#endif
#ifdef EEV_DEF
		case NTAG_dEEV:     copy_min(dEEV.get_save_addr(), data, len, dEEV.get_save_size()); dEEV.after_load(); break;
#endif
#ifdef USE_ELECTROMETER_SDM
		case NTAG_dSDM:     copy_min(dSDM.get_save_addr(), data, len, dSDM.get_save_size()); break;
#endif
#ifdef MQTT
		case NTAG_clMQTT:   copy_min(clMQTT.get_save_addr(), data, len, clMQTT.get_save_size()); break;
#endif
		default: break;                          // незнакомый тег пропускается, сдвиг уже сделан
		}
	}
#ifdef SENSOR_IP
	updateLinkIP();
#endif
	dbg_after_load();
	journal.jprintf(bad_struct ? "WARNING\n" : "OK\n");
	if(bad_struct) load_warn(ERR_SETTINGS_STRUCT, from_RAM);
	return size + 4;                             // длина всего блока: size без CRC, плюс CRC и поле длины. По ней веб ищет профили в файле
}

// Проверить контрольную сумму в EEPROM для данных на выходе ошибка, длина определяется из заголовка
int8_t HeatPump::check_crc16_eeprom(int32_t addr, uint16_t size)
{
	// Читаем пачками, а не по байту. Побайтно это больше тысячи отдельных
	// транзакций I2C, каждая с захватом семафора, и всё под остановленными
	// задачами: там не работает ни idle-хук, ни vServiceHP, то есть watchdog
	// не гладит никто, а его предел 10 секунд.
	uint16_t crc = 0xFFFF;
	// Буфер статический, а не на стеке: проверка вызывается из веб-задачи,
	// где 64 байта - заметная доля стека (STACK_vWebX, 230 слов = 920 байт). Обслуживание
	// семафором, гонки нет.
	static uint8_t blk[64];
	while(size) {
		uint16_t n = size > sizeof(blk) ? sizeof(blk) : size;
		if(readEEPROM_I2C(addr, blk, n)) return ERR_LOAD_EEPROM;
		crc = calc_crc16(blk, n, crc);
		addr += n;
		size -= n;
	}
	uint16_t crc2;
	if(readEEPROM_I2C(addr, (uint8_t *)&crc2, sizeof(crc2))) return ERR_LOAD_EEPROM;   // чтение -2 за вычетом CRC16 из длины
	if(crc == crc2) return OK; else return ERR_CRC16_EEPROM;
}

// ---- СЧЕТЧИКИ ----
// запись счетчиков теплового насоса в I2C память
int8_t HeatPump::save_motoHour()
{
	motoHour.Header = I2C_COUNT_EEPROM_HEADER;
	uint32_t ptr = 0;
	do {
		while(ptr < sizeof(motoHour)) if(((uint8_t*)&motoHour)[ptr] == ((uint8_t*)&motoHour_saved)[ptr]) ptr++; else break;
		if(ptr == sizeof(motoHour)) break;
		uint32_t ptre = ptr + 1;
xNotEqual:
		while(ptre < sizeof(motoHour)) if(((uint8_t*)&motoHour)[ptre] != ((uint8_t*)&motoHour_saved)[ptre]) ptre++; else break;
		if(ptre < sizeof(motoHour)) {
			uint32_t ptrz = ptre + 1;
			while(ptrz < sizeof(motoHour)) if(((uint8_t*)&motoHour)[ptrz] == ((uint8_t*)&motoHour_saved)[ptrz]) ptrz++; else break;
			if(ptrz < sizeof(motoHour)) {
				if(ptrz - ptre <= 4) {
					ptre = ptrz + 1;
					goto xNotEqual;
				}
			}
		}
		uint8_t errcode;
		if((errcode = writeEEPROM_I2C(I2C_COUNT_EEPROM + ptr, (byte*)&motoHour + ptr, ptre - ptr))) {
			journal.jprintf(" ERROR %d save counters!\n", errcode);
			set_Error(ERR_SAVE2_EEPROM, (char*) __FUNCTION__);
			return ERR_SAVE2_EEPROM;
		}
		ptr = ptre;
	} while(ptr < sizeof(motoHour));
	memcpy(&motoHour_saved, &motoHour, sizeof(motoHour_saved));
	return OK;
}

// чтение счетчиков теплового насоса в ЕЕПРОМ
int8_t HeatPump::load_motoHour()          
{
	if(readEEPROM_I2C(I2C_COUNT_EEPROM, &motoHour.Header, sizeof(motoHour.Header))) { // прочитать заголовок
		set_Error(ERR_LOAD2_EEPROM, (char*) __FUNCTION__);
		return ERR_LOAD2_EEPROM;
	}
	if(motoHour.Header != I2C_COUNT_EEPROM_HEADER) { // заголовок плохой
		journal.jprintf("Bad header counters, skip load\n");
		return ERR_HEADER2_EEPROM;
	}
	if(readEEPROM_I2C(I2C_COUNT_EEPROM + sizeof(motoHour.Header), (byte*) &motoHour + sizeof(motoHour.Header), sizeof(motoHour) - sizeof(motoHour.Header))) { // прочитать счетчики
		set_Error(ERR_LOAD2_EEPROM, (char*) __FUNCTION__);
		return ERR_LOAD2_EEPROM;
	}
	if(legacy_motohour_needed(Option.ver)) {
		// Байты из EEPROM разложены по СТАРОЙ схеме, а лежат уже в новой
		// структуре: копируем их и читаем по старым смещениям.
		memcpy(&motoHour_saved, &motoHour, sizeof(motoHour_saved));
		motohour_migrate_from_138(motoHour, *(const type_motoHour_old*)&motoHour_saved);
	}
	memcpy(&motoHour_saved, &motoHour, sizeof(motoHour_saved));
	journal.printf(" Load counters OK, read: %d bytes\n", sizeof(motoHour));
	return OK;
}
// Сборос сезонного счетчика моточасов
// параметр true - сброс всех счетчиков
void HeatPump::resetCount(boolean full)
{
	motohour_reset(motoHour, full, rtcSAM3X8.unixtime(),
	               motohour_IN_work, motohour_OUT_work);
	save_motoHour();  // записать счетчики
}

// Обновление счетчиков моточасов, вызывается раз в минуту
// Электрическая энергия не обновляется, Тепловая энергия обновляется
void HeatPump::updateCount()
{
	motohour_tick_minute(motoHour, is_compressor_on(), get_State() == pWORK_HP,
	                     motohour_IN_work, motohour_OUT_work);
}

// После любого изменения часов необходимо пересчитать все времна которые используются
// параметр изменение времени - корректировка
void HeatPump::updateDateTime(int32_t dTime)
{
	if(dTime != 0 && dTime != int32_t(0x80000000))                                   // было изменено время, надо скорректировать переменные времени
	{
		Prof.SaveON.startTime = Prof.SaveON.startTime + dTime; // время пуска ТН (для организации задержки включения включение ЭРВ)
		if(timeON > 0) timeON = timeON + dTime;                               // время включения контроллера для вычисления UPTIME
		if(startCompressor > 0) startCompressor = startCompressor + dTime;             // время пуска компрессора
		if(stopCompressor > 0) stopCompressor = stopCompressor + dTime;               // время останова компрессора
		if(countNTP > 0) countNTP = countNTP + dTime;                           // число секунд с последнего обновления по NTP
		if(offBoiler > 0) offBoiler = offBoiler + dTime;                         // время выключения нагрева ГВС ТН (необходимо для переключения на другие режимы на ходу)
		if(startDefrost > 0) startDefrost = startDefrost + dTime;                   // время срабатывания датчика разморозки
		if(startSalmonella > 0) startSalmonella = startSalmonella + dTime;             // время начала обеззараживания

	}
}
      

// ---------------------------------------------------------------------------
// ---- НАСТРОЙКИ ТН ----
// ---------------------------------------------------------------------------
// Сброс настроек теплового насоса
void HeatPump::resetSettingHP()
{  
	Prof.initProfile();                           // Инициализировать профиль по умолчанию

	Status.modWork = pOFF;                          // Что сейчас делает ТН (7 стадий)
	Status.State = pOFF_HP;                         // Состояние ТН - выключен
	Status.ret = pNone;                             // точка выхода алгоритма
	motoHour.Header = I2C_COUNT_EEPROM_HEADER;
	motoHour.flags = 0;								// насос выключен
	motoHour.H1 = 0;                                // моточасы ТН ВСЕГО
	motoHour.H2 = 0;                                // моточасы ТН сбрасываемый счетчик (сезон)
	motoHour.C1 = 0;                                // моточасы компрессора ВСЕГО
	motoHour.C2 = 0;                                // моточасы компрессора сбрасываемый счетчик (сезон)
	motoHour.E1 = 0.0;                              // Значение потреленный энергии в момент пуска  актуально при использовании счетчика SDM120 (вычитаем текущее и получам итого)
	motoHour.E2 = 0.0;                              // Значение потреленный энергии в начале сезона актуально при использовании счетчика SDM120 (вычитаем текущее и получам итого)
	motoHour.D1 = motoHour.D2 = rtcSAM3X8.unixtime(); // Дата сброса счетчиков

	startPump = false;                              // Признак работы задачи насос
	flagRBOILER = false;                            // не идет нагрев бойлера
	heaterOnlyPrev = false;                         // режим «бойлер греет только ТЭН» не действовал
	HeatBoilerUrgently = 0;
	fSD = false;                                    // СД карта не работает
	fSPIFlash = false;                              // Признак наличия (физического) spi диска - диска нет по умолчанию

	startWait = false;                              // Начало работы с ожидания
	onBoiler = false;                               // Если true то идет нагрев бойлера
	onSalmonella = false;                           // Если true то идет Обеззараживание
	command = pEMPTY;                               // Команд на выполнение нет
	CommandQueue_Init(&cmdq);
	PauseStart = 0;                                 // начать отсчет задержки пред стартом с начала
	Diag.startRAM = 0;                                   // Свободная память при старте FREE Rtos - пытаемся определить свободную память при работе
	lastEEV = -1;                                   // значение шагов ЭРВ перед выключением  -1 - первое включение
	num_repeat = 0;                                 // текущее число попыток 0 - т.е еще не было работы
	Diag.num_resW5200 = 0;                               // текущее число сбросов сетевого чипа
	Diag.num_resMutexSPI = 0;                            // текущее число сброса митекса SPI
	Diag.num_resMutexI2C = 0;                            // текущее число сброса митекса I2C
	Diag.num_semSuspended = 0;                           // попыток взять замок при остановленном планировщике
	Diag.num_resMQTT = 0;                                // число повторных инициализация MQTT клиента
	Diag.num_resPing = 0;                                // число не прошедших пингов

	fullCOP = -1000;                                // Полный СОР  сотые -1000 признак невозможности расчета
	COP = -1000;                                    // Чистый COP сотые  -1000 признак невозможности расчета

	// Инициализация различных времен
	DateTime.saveTime = 0;                          // дата и время сохранения настроек в eeprom
	timeON = 0;                                     // время включения контроллера для вычисления UPTIME
	countNTP = 0;                                   // число секунд с последнего обновления по NTP
	startCompressor = 0;                            // время пуска компрессора
	stopCompressor = 0;                             // время останова компрессора
	offBoiler = 0;                                  // время выключения нагрева ГВС ТН (необходимо для переключения на другие режимы на ходу)
	startDefrost = 0;                               // время срабатывания датчика разморозки
	timeNTP = 0;                                    // Время обновления по NTP в тиках (0-сразу обновляемся)
	startSalmonella = 0;                            // время начала обеззараживания
	command_completed = 0;
	compressor_in_pause = false;

	safeNetwork = false;                            // режим safeNetwork
	forceMinWeb = false;                            // морда из кода принудительно

	// Установка сетевых параметров по умолчанию
	if(defaultDHCP) SETBIT1(Network.flags, fDHCP);
	else SETBIT0(Network.flags, fDHCP); // использование DHCP
	Network.ip = IPAddress(defaultIP);              // ip адрес
	Network.sdns = IPAddress(defaultSDNS);          // сервер dns
	Network.gateway = IPAddress(defaultGateway);    // шлюз
	Network.subnet = IPAddress(defaultSubnet);      // подсеть
	Network.port = defaultPort;                     // порт веб сервера по умолчанию
	memcpy(Network.mac, defaultMAC, 6);             // mac адрес
	Network.resSocket = 30;                         // Время очистки сокетов
	Network.resW5200 = 0;                           // Время сброса чипа
	Diag.countResSocket = 0;                             // Число сбросов сокетов
	SETBIT1(Network.flags, fInitW5200);           // Ежеминутный контроль SPI для сетевого чипа
	SETBIT0(Network.flags, fPass);                 // !save! Использование паролей
	strcpy(Network.passUser, "user");              // !save! Пароль пользователя
	strcpy(Network.passAdmin, "admin");            // !save! Пароль администратора
	Network.sizePacket = 1465;                      // !save! размер пакета для отправки
	SETBIT0(Network.flags, fNoAck);                // !save! флаг Не ожидать ответа ACK
	Network.delayAck = 10;                          // !save! задержка мсек перед отправкой пакета
	strcpy(Network.pingAdr, PING_SERVER);         // !save! адрес для пинга
	Network.pingTime = 60 * 60;                       // !save! время пинга в секундах
	SETBIT0(Network.flags, fNoPing);               // !save! Запрет пинга контроллера

	// Время
	SETBIT1(DateTime.flags, fUpdateNTP);           // Обновление часов по NTP
	SETBIT1(DateTime.flags, fUpdateI2C);           // Обновление часов I2C

	strcpy(DateTime.serverNTP, (char*) NTP_SERVER);  // NTP сервер по умолчанию

	// Опции теплового насоса
	// Временные задержки
	Option.ver = VER_SAVE;
	Option.delayOnPump = DEF_DELAY_ON_PUMP;
	Option.delayOffPump = DEF_DELAY_OFF_PUMP;
	Option.delayStartRes = DEF_DELAY_START_RES;
	Option.delayRepeadStart = DEF_DELAY_REPEAD_START;
	Option.delayDefrostOn = DEF_DELAY_DEFROST_ON;
	Option.delayDefrostOff = DEF_DELAY_DEFROST_OFF;
	Option.delayR4WAY = DEF_DELAY_R4WAY;
	Option.delayBoilerSW = DEF_DELAY_BOILER_SW;
	Option.delayBoilerOff = DEF_DELAY_BOILER_OFF;
	Option.numProf = Prof.get_idProfile(); //  Профиль не загружен по дефолту 0 профиль
	dbgFlags = Dbg_Defaults();             //  Флаги отладки по умолчанию
	Dbg_Pack(dbgFlags, &Option.dbg, &Option.dbg2);
	Option.nStart = 3;                   //  Число попыток пуска компрессора
	Option.tempRHEAT = 1000;             //  Значение температуры для управления RHEAT (по умолчанию режим резерв - 10 градусов в доме)
	Option.pausePump = 600;              //  Время паузы  насоса при выключенном компрессоре, сек
	Option.workPump = 15;                //  Время работы  насоса при выключенном компрессоре, сек
	Option.tChart = 10;                  //  период накопления статистики по умолчанию 60 секунд
	SETBIT0(Option.flags, fAddHeat);      //  Использование дополнительного тена при нагреве НЕ ИСПОЛЬЗОВАТЬ
	SETBIT0(Option.flags, fTypeRHEAT);    //  Использование дополнительного тена по умолчанию режим резерв
	SETBIT1(Option.flags, fBeep);         //  Звук
	SETBIT1(Option.flags, fNextion);      //  дисплей Nextion
	SETBIT0(Option.flags, fHistory);      //  Сброс статистика на карту
	SETBIT0(Option.flags2, f2HistoryDetail); //  Детализация истории ВКЛ/ВЫКЛ компрессора выключена
	SETBIT0(Option.flags, fSaveON);       //  флаг записи в EEPROM включения ТН
	Option.sleep = 5;                    //  Время засыпания минуты
	Option.dim = 80;                     //  Якрость %
	Option.pause = 5 * 60;               // Минимальное время простоя компрессора, секунды
    SETBIT0(Option.flags, fBackupPower); // Использование резервного питания от генератора (ограничение мощности) 
	Option.maxBackupPower=3000;          // Максимальная мощность при питании от генератора (Вт)

}

// ---- Сетевые настройки ----
//Получить параметр из строки
char* HeatPump::get_network(char *var,char *ret)
{
 if(strcmp(var,net_IP)==0){   return strcat(ret,IPAddress2String(Network.ip));          }else  
 if(strcmp(var,net_DNS)==0){      return strcat(ret,IPAddress2String(Network.sdns));   }else
 if(strcmp(var,net_GATEWAY)==0){   return strcat(ret,IPAddress2String(Network.gateway));}else                
 if(strcmp(var,net_SUBNET)==0){    return strcat(ret,IPAddress2String(Network.subnet)); }else  
 if(strcmp(var,net_DHCP)==0){      if (GETBIT(Network.flags,fDHCP)) return  strcat(ret,(char*)cOne);
                                   else      return  strcat(ret,(char*)cZero);          }else
 if(strcmp(var,net_MAC)==0){       return strcat(ret,MAC2String(Network.mac));          }else  
 if(strcmp(var,net_RES_SOCKET)==0){
 	return web_fill_tag_select(ret, "never:0;10 sec:0;30 sec:0;90 sec:0;",
				Network.resSocket == 0 ? 0 :
				Network.resSocket == 10 ? 1 :
				Network.resSocket == 30 ? 2 :
				Network.resSocket == 90 ? 3 : 4);
 }else if(strcmp(var,net_RES_W5200)==0){
	 	return web_fill_tag_select(ret, "never:0;6 hours:0;24 hours:0;",
					Network.resW5200 == 0 ? 0 :
					Network.resW5200 == 60*60*6 ? 1 :
					Network.resW5200 == 60*60*24 ? 2 : 3);
  }else if(strcmp(var,net_PASS)==0){      if (GETBIT(Network.flags,fPass)) return  strcat(ret,(char*)cOne);
                                    else      return  strcat(ret,(char*)cZero);               }else
  if(strcmp(var,net_PASSUSER)==0){  return strcat(ret,Network.passUser);                      }else                 
  if(strcmp(var,net_PASSADMIN)==0){ return strcat(ret,Network.passAdmin);                     }else   
  if(strcmp(var,net_SIZE_PACKET)==0){return _itoa(Network.sizePacket,ret);          }else   
    if(strcmp(var,net_INIT_W5200)==0){if (GETBIT(Network.flags,fInitW5200)) return  strcat(ret,(char*)cOne);       // флаг Ежеминутный контроль SPI для сетевого чипа
                                      else      return  strcat(ret,(char*)cZero);               }else      
    if(strcmp(var,net_PORT)==0){return _itoa(Network.port,ret);                       }else    // Порт веб сервера
    if(strcmp(var,net_NO_ACK)==0){    if (GETBIT(Network.flags,fNoAck)) return  strcat(ret,(char*)cOne);
                                      else      return  strcat(ret,(char*)cZero);          }else     
    if(strcmp(var,net_DELAY_ACK)==0){return _itoa(Network.delayAck,ret);         }else    
    if(strcmp(var,net_PING_ADR)==0){  return strcat(ret,Network.pingAdr);                  }else
    if(strcmp(var,net_PING_TIME)==0){
    	return web_fill_tag_select(ret, "never:0;1 min:0;5 min:0;20 min:0;60 min:0;120 min:0;",
					Network.pingTime == 0 ? 0 :
					Network.pingTime == 1*60 ? 1 :
					Network.pingTime == 5*60 ? 2 :
					Network.pingTime == 20*60 ? 3 :
					Network.pingTime == 60*60 ? 4 :
					Network.pingTime == 120*60 ? 5 : 6);
    } else if(strcmp(var,net_NO_PING)==0){if (GETBIT(Network.flags,fNoPing)) return  strcat(ret,(char*)cOne);
                                   else      return  strcat(ret,(char*)cZero);                        }else                                                                                          

 return strcat(ret,(char*)cInvalid);
}

// Установить параметр дата и время из строки
boolean HeatPump::set_datetime(char *var, char *c)
{
	int16_t buf[3];
	uint32_t oldTime = rtcSAM3X8.unixtime(); // запомнить время
	int32_t dTime = 0;
	if(strcmp(var, time_TIME) == 0) {
		if(!parseInt16_t(c, ':', buf, 2, 10)) return false;
		rtcSAM3X8.set_time(buf[0], buf[1], 0);  // внутренние
		setTime_RtcI2C(rtcSAM3X8.get_hours(), rtcSAM3X8.get_minutes(), rtcSAM3X8.get_seconds()); // внешние
		dTime = rtcSAM3X8.unixtime() - oldTime; // получить изменение времени
	} else if(strcmp(var, time_DATE) == 0) {
		uint8_t i = 0, f = 0;
		char ch;
		do { // ищем разделитель чисел
			ch = c[i++];
			if(ch >= '0' && ch <= '9') f = 1;
			else if(f == 1) {
				f = 2;
				break;
			}
		} while(ch != '\0');
		if(f != 2 || !parseInt16_t(c, ch, buf, 3, 10)) return false;
		rtcSAM3X8.set_date(buf[0], buf[1], buf[2]); // внутренние
		setDate_RtcI2C(rtcSAM3X8.get_days(), rtcSAM3X8.get_months(), rtcSAM3X8.get_years()); // внешние
		dTime = rtcSAM3X8.unixtime() - oldTime; // получить изменение времени
	} else if(strcmp(var, time_NTP) == 0) {
		if(strlen(c) == 0) return false;                                                 // пустая строка
		if(strlen(c) > NTP_SERVER_LEN) return false;                                     // слишком длиная строка
		else {
			strcpy(DateTime.serverNTP, c);
			return true;
		}                           // ок сохраняем
	} else if(strcmp(var, time_UPDATE) == 0) {
		if(strcmp(c, cZero) == 0) {
			SETBIT0(DateTime.flags, fUpdateNTP);
			return true;
		} else if(strcmp(c, cOne) == 0) {
			SETBIT1(DateTime.flags, fUpdateNTP);
			countNTP = 0;
			return true;
		} else return false;
	} else if(strcmp(var, time_UPDATE_I2C) == 0) {
		if(strcmp(c, cZero) == 0) {
			SETBIT0(DateTime.flags, fUpdateI2C);
			return true;
		} else if(strcmp(c, cOne) == 0) {
			SETBIT1(DateTime.flags, fUpdateI2C);
			countNTP = 0;
			return true;
		} else return false;
	} else return false;

	updateDateTime(dTime);    // было изменено время, надо скорректировать переменные времени
	return true;
}

// Получить параметр дата и время из строки
void HeatPump::get_datetime(char *var, char *ret)
{
	if(strcmp(var, time_TIME) == 0) {
		ret += strlen(ret);
		NowTimeToStr(ret);
		ret[5] = '\0';
	} else if(strcmp(var, time_DATE) == 0) {
		strcat(ret, NowDateToStr());
	} else if(strcmp(var, time_NTP) == 0) {
		strcat(ret, DateTime.serverNTP);
	} else if(strcmp(var, time_UPDATE) == 0) {
		if(GETBIT(DateTime.flags, fUpdateNTP)) strcat(ret, (char*) cOne); else strcat(ret, (char*) cZero);
	} else if(strcmp(var, time_UPDATE_I2C) == 0) {
		if(GETBIT(DateTime.flags, fUpdateI2C)) strcat(ret, (char*) cOne);
		else strcat(ret, (char*) cZero);
	} else strcat(ret, (char*) cInvalid);
}

// Получить опции ТН, результат добавляется в ret, "get_oHP"
char* HeatPump::get_optionHP(char *var, char *ret)
{
	if(strcmp(var,option_ADD_HEAT)==0)         {if(!GETBIT(Option.flags,fAddHeat))          return strcat(ret,(char*)"none:1;reserve:0;bivalent:0;");       // использование ТЭН запрещено
											   else if(!GETBIT(Option.flags,fTypeRHEAT))   return strcat(ret,(char*)"none:0;reserve:1;bivalent:0;");       // резерв
											   else                                        return strcat(ret,(char*)"none:0;reserve:0;bivalent:1;");}else  // бивалент
	if(strcmp(var,option_TEMP_RHEAT)==0)       {_ftoa(ret,(float)Option.tempRHEAT/100,1); return ret; }else                                         // температура управления RHEAT (градусы)
	if(strcmp(var,option_PUMP_WORK)==0)        {return _itoa(Option.workPump,ret);}else                                                           // работа насоса конденсатора при выключенном компрессоре МИНУТЫ
	if(strcmp(var,option_PUMP_PAUSE)==0)       {return _itoa(Option.pausePump,ret);}else                                                          // пауза между работой насоса конденсатора при выключенном компрессоре МИНУТЫ
	if(strcmp(var,option_ATTEMPT)==0)          {return _itoa(Option.nStart,ret);}else                                                             // число попыток пуска
	if(strcmp(var,option_TIME_CHART)==0)       {return _itoa(Option.tChart,ret);} else
	if(strcmp(var,option_Charts_when_comp_on)==0){return _itoa(Charts_when_comp_on, ret);} else
	if(strcmp(var,option_BEEP)==0)             {if(GETBIT(Option.flags,fBeep)) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero); }else            // Подача звукового сигнала
	if(strcmp(var,option_NEXTION)==0)          { return strcat(ret, (char*)(GETBIT(Option.flags,fNextion) ? cOne : cZero)); } else         // использование дисплея nextion
	if(strcmp(var,option_NEXTION_WORK)==0)     { return strcat(ret, (char*)(GETBIT(Option.flags,fNextionOnWhileWork) ? cOne : cZero)); } else         // использование дисплея nextion
	if(strcmp(var,option_History)==0)          {if(GETBIT(Option.flags,fHistory)) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero);   }else            // Сбрасывать статистику на карту
	if(strcmp(var,option_HistoryDetail)==0)    { return strcat(ret, (char*)(GETBIT(Option.flags2,f2HistoryDetail) ? cOne : cZero)); } else
	if(strcmp(var,option_WebOnSPIFlash)==0)    { return strcat(ret, (char*)(GETBIT(Option.flags,fWebStoreOnSPIFlash) ? cOne : cZero)); } else
	if(strcmp(var,option_SAVE_ON)==0)          {if(GETBIT(Option.flags,fSaveON)) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero);    }else           // флаг записи в EEPROM включения ТН (восстановление работы после перезагрузки)
	if(strcmp(var,option_NEXT_SLEEP)==0)       {return _itoa(Option.sleep,ret);                                                     }else            // Время засыпания секунды NEXTION минуты
	if(strcmp(var,option_NEXT_DIM)==0)         {return _itoa(Option.dim,ret);                                                       }else            // Якрость % NEXTION
	if(strcmp(var,option_PAUSE)==0)            {return _itoa(Option.pause/60,ret); } else        // минимальное время простоя компрессора с переводом в минуты но хранится в секундах!!!!!
	if(strcmp(var,option_MinCompressorOn)==0)  {return _itoa(Option.MinCompressorOn, ret); } else
	if(strcmp(var,option_DELAY_ON_PUMP)==0)    {return _itoa(Option.delayOnPump,ret);}else       // Задержка включения компрессора после включения насосов (сек).
	if(strcmp(var,option_DELAY_OFF_PUMP)==0)   {return _itoa(Option.delayOffPump,ret);}else      // Задержка выключения насосов после выключения компрессора (сек).
	if(strcmp(var,option_DELAY_START_RES)==0)  {return _itoa(Option.delayStartRes,ret);}else     // Задержка включения ТН после внезапного сброса контроллера (сек.)
	if(strcmp(var,option_DELAY_REPEAD_START)==0){return _itoa(Option.delayRepeadStart,ret);}else // Задержка перед повторным включениме ТН при ошибке (попытки пуска) секунды
	if(strcmp(var,option_DELAY_DEFROST_ON)==0) {return _itoa(Option.delayDefrostOn,ret);}else    // ДЛЯ ВОЗДУШНОГО ТН Задержка после срабатывания датчика перед включением разморозки (секунды)
	if(strcmp(var,option_DELAY_DEFROST_OFF)==0){return _itoa(Option.delayDefrostOff,ret);}else   // ДЛЯ ВОЗДУШНОГО ТН Задержка перед выключением разморозки (секунды)
	if(strcmp(var,option_DELAY_R4WAY)==0)      {return _itoa(Option.delayR4WAY,ret);}else        // Задержка между переключением 4-х ходового клапана и включением компрессора, для выравнивания давлений (сек). Если включены эти опции (переключение тепло-холод)
	if(strcmp(var,option_DELAY_BOILER_SW)==0)  {return _itoa(Option.delayBoilerSW,ret);}else     // Пауза (сек) после переключение ГВС - выравниваем температуру в контуре отопления/ГВС что бы сразу защиты не сработали
	if(strcmp(var,option_DELAY_BOILER_OFF)==0) {return _itoa(Option.delayBoilerOff,ret);}        // Время (сек) на сколько блокируются защиты при переходе с ГВС на отопление и охлаждение слишком горяче после ГВС
	if(strcmp(var,option_fBackupPower)==0)     {if(GETBIT(Option.flags,fBackupPower)) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero);}else // флаг Использование резервного питания от генератора (ограничение мощности)
	if(strcmp(var,option_fBackupPowerInfo)==0) { // Работа от генератора
	   if(GETBIT(Option.flags,fBackupPower)
	#ifdef USE_UPS
		   && !NO_Power
	#endif
		   ) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero);
	} else
	if(strcmp(var, option_fBackupPowerAuto) == 0) {
	#ifdef SGENERATOR
	   if(GETBIT(Option.flags2, f2BackupPowerAuto)) return strcat(ret, (char*) cOne); else return strcat(ret, (char*) cZero);
#else
	   return strcat(ret, (char*) cZero);
#endif
   } else
	if(strcmp(var,option_maxBackupPower)==0)   {return _itoa(Option.maxBackupPower,ret);}else    // Максимальная мощность при питании от генератора
   return strcat(ret,(char*)cInvalid);
}

// ---------------------------------------------------------------------------
// Реестр параметров веб-API: опции ТН (oHP). Ядро - net/ParamRegistry.cpp,
// протокол - tools/api/spec.md; правила таблицы - как у профиля (Profile.cpp).
//
// Прежние set_optionHP/get_optionHP остаются: ими пользуются settings.txt и
// письмо. Побочные эффекты записи сохранены: дисплей Nextion, очистка графиков
// при смене периода, яркость. Отличия от прежнего поведения (строгие 0/1,
// границы там, где их не было) - tools/api/spec.md, раздел 15.
// ---------------------------------------------------------------------------
enum { O_ADDHEAT = 1, O_TCHART, O_CWCO, O_NXT, O_NXTW, O_NXTS, O_NXTD, O_BP };

#define OO(f) offsetof(type_optionHP, f)
static const char *const apiLabAddHeat[] = { "none", "reserve", "bivalent", 0 };

static const ApiParam apiOhpTab[] = {
	{ option_ADD_HEAT, apiLabAddHeat, 0, 2, 0, 1, PT_CALL, 0, MK_INT, O_ADDHEAT, 0 },   // два бита флагов -> три состояния
	PN_I16(option_TEMP_RHEAT,  -30, 30,    100, OO(tempRHEAT),  0),
	PN_U16(option_PUMP_WORK,    0, 65535,  1,   OO(workPump),   0),
	PN_U16(option_PUMP_PAUSE,   0, 65535,  1,   OO(pausePump),  0),
	PN_U8 (option_ATTEMPT,      0, 255,    1,   OO(nStart),     0),
	{ option_TIME_CHART, 0, 1, 65535, 0, 1, PT_CALL, 0, MK_INT, O_TCHART, 0 },         // смена периода очищает графики при работе ТН
	{ option_Charts_when_comp_on, 0, 0, 1, 0, 1, PT_CALL, 0, MK_BOOL, O_CWCO, 0 },      // отдельная переменная, не поле Option
	PN_BIT(option_BEEP,        OO(flags), fBeep,                PT_B16, 0, 0, 0, 0),
	{ option_NEXTION, 0, 0, 1, 0, 1, PT_CALL, 0, MK_BOOL, O_NXT, 0 },                   // дисплей перенастраивается только при смене
	PN_BIT(option_NEXTION_WORK, OO(flags), fNextionOnWhileWork, PT_B16, 0, O_NXTW, 0, 0),
	PNH_U8(option_NEXT_SLEEP,   0, 255,    1,   OO(sleep),      0, O_NXTS),
#ifdef NEXTION
	PNH_U8(option_NEXT_DIM,     1, 100,    1,   OO(dim),        0, O_NXTD),
#endif
	PN_BIT(option_History,     OO(flags), fHistory,             PT_B16, 0, 0, 0, 0),
	PN_BIT(option_HistoryDetail, OO(flags2), f2HistoryDetail,   PT_B16, 0, 0, 0, 0),
	PN_BIT(option_WebOnSPIFlash, OO(flags), fWebStoreOnSPIFlash, PT_B16, 0, 0, 0, 0),
	PN_BIT(option_SAVE_ON,     OO(flags), fSaveON,              PT_B16, 0, 0, 0, 0),
	PN_U16(option_PAUSE,        0, 200,    60,  OO(pause),      0),                     // в вебе минуты, в памяти секунды
	PN_U16(option_MinCompressorOn, 0, 65535, 1, OO(MinCompressorOn), 0),
	PN_U16(option_DELAY_ON_PUMP,  0, 900,  1,   OO(delayOnPump),  0),
	PN_U16(option_DELAY_OFF_PUMP, 0, 900,  1,   OO(delayOffPump), 0),
	PN_U16(option_DELAY_START_RES, 0, 6000, 1,  OO(delayStartRes), 0),
	PN_U16(option_DELAY_REPEAD_START, 0, 6000, 1, OO(delayRepeadStart), 0),
	PN_U16(option_DELAY_DEFROST_ON,  0, 600, 1, OO(delayDefrostOn), 0),
	PN_U16(option_DELAY_DEFROST_OFF, 0, 600, 1, OO(delayDefrostOff), 0),
	PN_U16(option_DELAY_R4WAY,   0, 600,   1,   OO(delayR4WAY),   0),
	PN_U16(option_DELAY_BOILER_SW,  0, 1200, 1, OO(delayBoilerSW), 0),
	PN_U16(option_DELAY_BOILER_OFF, 0, 1200, 1, OO(delayBoilerOff), 0),
	PN_BIT(option_fBackupPower, OO(flags), fBackupPower,        PT_B16, 0, 0, PF_UW, 0),  // тумблер на главной доступен роли user
	{ option_fBackupPowerInfo, 0, 0, 1, 0, 1, PT_CALL, PF_RO, MK_BOOL, O_BP, 0 },       // работа от генератора сейчас
#ifdef SGENERATOR
	PN_BIT(option_fBackupPowerAuto, OO(flags2), f2BackupPowerAuto, PT_B16, 0, 0, 0, 0),
#endif
	PN_U16(option_maxBackupPower, 0, 10000, 1,  OO(maxBackupPower), 0),
};

static void *api_base_ohp(uint8_t) { return &HP.Option; }

static int api_get_ohp(uint8_t hook, uint8_t, ApiOut *o)
{
	switch(hook) {
	case O_ADDHEAT: api_put_int(o, !GETBIT(HP.Option.flags, fAddHeat) ? 0 : !GETBIT(HP.Option.flags, fTypeRHEAT) ? 1 : 2); break;
	case O_TCHART:  api_put_int(o, HP.Option.tChart); break;
	case O_CWCO:    api_put(o, Charts_when_comp_on ? "true" : "false"); break;
	case O_NXT:     api_put(o, GETBIT(HP.Option.flags, fNextion) ? "true" : "false"); break;
	case O_BP:      api_put(o, (GETBIT(HP.Option.flags, fBackupPower)
#ifdef USE_UPS
	                              && !HP.NO_Power
#endif
	                           ) ? "true" : "false"); break;
	default: return AE_FAIL;
	}
	return AE_OK;
}

// "0"/"1"/"true"/"false" -> 0/1
static bool api_bool(const char *v, int *out)
{
	if(!strcmp(v, "1") || !strcmp(v, "true")) { *out = 1; return true; }
	if(!strcmp(v, "0") || !strcmp(v, "false")) { *out = 0; return true; }
	return false;
}

static int api_set_ohp(uint8_t hook, uint8_t, const char *val)
{
	int b;
	float x;
	switch(hook) {
	case O_ADDHEAT:
		x = my_atof(val);
		if(x == ATOF_ERROR || x != (float)(int)x) return AE_BAD_VALUE;
		if(x < 0 || x > 2) return AE_RANGE;
		if((int)x == 0) SETBIT0(HP.Option.flags, fAddHeat);
		else { SETBIT1(HP.Option.flags, fAddHeat); if((int)x == 1) SETBIT0(HP.Option.flags, fTypeRHEAT); else SETBIT1(HP.Option.flags, fTypeRHEAT); }
		return AE_OK;
	case O_TCHART:
		x = my_atof(val);
		if(x == ATOF_ERROR) return AE_BAD_VALUE;
		if(x < 1 || x > 65535) return AE_RANGE;
		if(HP.get_State() == pWORK_HP) HP.clearChart();      // очистка до смены периода
		HP.Option.tChart = x;
		return AE_OK;
	case O_CWCO:
		if(!api_bool(val, &b)) return AE_BAD_VALUE;
		Charts_when_comp_on = b;
		return AE_OK;
	case O_NXT:
		if(!api_bool(val, &b)) return AE_BAD_VALUE;
		if(b != (GETBIT(HP.Option.flags, fNextion) ? 1 : 0)) {
			if(b) SETBIT1(HP.Option.flags, fNextion); else SETBIT0(HP.Option.flags, fNextion);
			HP.updateNextion(true);
		}
		return AE_OK;
	}
	return AE_FAIL;
}

static void api_after_ohp(uint8_t hook)
{
	switch(hook) {
	case O_NXTW: case O_NXTS: HP.updateNextion(false); break;
#ifdef NEXTION
	case O_NXTD: myNextion.set_dim(HP.Option.dim); break;
#endif
	}
}

static const ApiOps apiOpsOhp = { api_base_ohp, api_get_ohp, api_set_ohp, api_after_ohp, 0, 0, 0, 0 };
extern const ApiGroup ApiGrpOhp;      // без предварительного extern const имела бы внутреннюю привязку
const ApiGroup ApiGrpOhp = { "ohp", apiOhpTab, sizeof(apiOhpTab) / sizeof(apiOhpTab[0]), &apiOpsOhp };


// ---------------------------------------------------------------------------
// Реестр параметров веб-API: сеть (net) и время (time). Правила - как у oHP выше.
// Пароли (net.passuser, net.passadmin): читать может только admin (флаг PF_AR),
// хеш пересчитывается после записи. Применение сетевых настроек -
// действие net.apply (WebApiGet.cpp), сами записи ничего не перенастраивают.
// ---------------------------------------------------------------------------
enum { N_IP = 1, N_DNS, N_GATEWAY, N_SUBNET, N_MAC, N_RESSOCK, N_RESW, N_PINGT, N_HASHU, N_HASHA, N_NOPING };

#define NO(f) offsetof(type_NetworkHP, f)
static const char *const apiLabResSock[] = { "never", "10 sec", "30 sec", "90 sec", 0 };
static const char *const apiLabResW[]    = { "never", "6 hours", "24 hours", 0 };
static const char *const apiLabPing[]    = { "never", "1 min", "5 min", "20 min", "60 min", "120 min", 0 };
static const uint16_t apiResSockSec[] = { 0, 10, 30, 90 };
static const uint32_t apiResWSec[]    = { 0, 60 * 60 * 6, 60 * 60 * 24 };
static const uint16_t apiPingSec[]    = { 0, 1 * 60, 5 * 60, 20 * 60, 60 * 60, 120 * 60 };

static const ApiParam apiNetTab[] = {
	{ net_IP,      0, 0, 0, 0, 1, PT_CALL, 0, MK_IP,  N_IP,      0 },
	{ net_DNS,     0, 0, 0, 0, 1, PT_CALL, 0, MK_IP,  N_DNS,     0 },
	{ net_GATEWAY, 0, 0, 0, 0, 1, PT_CALL, 0, MK_IP,  N_GATEWAY, 0 },
	{ net_SUBNET,  0, 0, 0, 0, 1, PT_CALL, 0, MK_IP,  N_SUBNET,  0 },
	PN_BIT(net_DHCP, NO(flags), fDHCP, PT_B16, 0, 0, 0, 0),
	{ net_MAC,     0, 0, 0, 0, 1, PT_CALL, 0, MK_MAC, N_MAC,     0 },
	{ net_RES_SOCKET, apiLabResSock, 0, 3, 0, 1, PT_CALL, 0, MK_INT, N_RESSOCK, 0 },
	{ net_RES_W5200,  apiLabResW,    0, 2, 0, 1, PT_CALL, 0, MK_INT, N_RESW,    0 },
	PN_BIT(net_PASS, NO(flags), fPass, PT_B16, 0, 0, 0, 0),
	PN_STR(net_PASSUSER,  NO(passUser),  PASS_LEN + 1, PF_AR, N_HASHU),
	PN_STR(net_PASSADMIN, NO(passAdmin), PASS_LEN + 1, PF_AR, N_HASHA),
	PN_U16(net_SIZE_PACKET, 64, 2048, 1, NO(sizePacket), 0),
	PN_BIT(net_INIT_W5200, NO(flags), fInitW5200, PT_B16, 0, 0, 0, 0),
	PN_U16(net_PORT, 1, 65535, 1, NO(port), 0),
	PN_BIT(net_NO_ACK, NO(flags), fNoAck, PT_B16, 0, 0, 0, 0),
	PN_U8 (net_DELAY_ACK, 1, 50, 1, NO(delayAck), 0),
	PN_STR(net_PING_ADR, NO(pingAdr), 40, 0, 0),
	{ net_PING_TIME, apiLabPing, 0, 5, 0, 1, PT_CALL, 0, MK_INT, N_PINGT, 0 },
	PN_BIT(net_NO_PING, NO(flags), fNoPing, PT_B16, 0, N_NOPING, 0, 0),
};

static void *api_base_net(uint8_t) { return HP.api_network(); }

static IPAddress &api_ip_of(uint8_t hook)
{
	type_NetworkHP *n = HP.api_network();
	return hook == N_IP ? n->ip : hook == N_DNS ? n->sdns : hook == N_GATEWAY ? n->gateway : n->subnet;
}
// индекс в таблице значений или -1: незнакомое значение в поле даёт null (в списке такого пункта нет)
static int api_index_of(uint32_t v, const uint32_t *t, int n) { for(int i = 0; i < n; i++) if(t[i] == v) return i; return -1; }

static int api_get_net(uint8_t hook, uint8_t, ApiOut *o)
{
	type_NetworkHP *n = HP.api_network();
	int i;
	switch(hook) {
	case N_IP: case N_DNS: case N_GATEWAY: case N_SUBNET: api_put_str(o, IPAddress2String(api_ip_of(hook))); return AE_OK;
	case N_MAC: api_put_str(o, MAC2String(n->mac)); return AE_OK;
	case N_RESSOCK: i = -1; for(int k = 0; k < 4; k++) if(apiResSockSec[k] == n->resSocket) i = k; break;
	case N_RESW:    i = api_index_of(n->resW5200, apiResWSec, 3); break;
	case N_PINGT:   i = -1; for(int k = 0; k < 6; k++) if(apiPingSec[k] == n->pingTime) i = k; break;
	default: return AE_FAIL;
	}
	if(i < 0) api_put(o, "null"); else api_put_int(o, i);
	return AE_OK;
}

static int api_set_net(uint8_t hook, uint8_t, const char *val)
{
	type_NetworkHP *n = HP.api_network();
	int i = (int)my_atof(val);               // индекс перечислений уже проверило ядро
	switch(hook) {
	case N_IP: case N_DNS: case N_GATEWAY: case N_SUBNET: return parseIPAddress(val, '.', api_ip_of(hook)) ? AE_OK : AE_BAD_VALUE;
	case N_MAC: return parseBytes(val, ':', n->mac, 6, 16) ? AE_OK : AE_BAD_VALUE;
	case N_RESSOCK: n->resSocket = apiResSockSec[i]; return AE_OK;
	case N_RESW:    n->resW5200 = apiResWSec[i]; return AE_OK;
	case N_PINGT:   n->pingTime = apiPingSec[i]; return AE_OK;
	}
	return AE_FAIL;
}

static void api_after_net(uint8_t hook)
{
	switch(hook) {
	case N_HASHU: HP.set_hashUser(); break;
	case N_HASHA: HP.set_hashAdmin(); break;
	case N_NOPING: pingW5200(HP.get_NoPing()); break;    // запрет пинга применяется сразу
	}
}

static const ApiOps apiOpsNet = { api_base_net, api_get_net, api_set_net, api_after_net, 0, 0, 0, 0 };
extern const ApiGroup ApiGrpNet;
const ApiGroup ApiGrpNet = { "net", apiNetTab, sizeof(apiNetTab) / sizeof(apiNetTab[0]), &apiOpsNet };

// ---- время ----
enum { TM_TIME = 1, TM_DATE, TM_NTPR, TM_NTPUPD, TM_I2CUPD };
#define DTO(f) offsetof(type_DateTimeHP, f)
static const ApiParam apiTimeTab[] = {
	{ time_DATE, 0, 0, 0, 0, 1, PT_CALL, 0, MK_STR, TM_DATE, 0 },        // дд.мм.гггг; запись корректирует часы и внешний RTC
	{ time_TIME, 0, 0, 0, 0, 1, PT_CALL, 0, MK_STR, TM_TIME, 0 },        // чч:мм
	PN_STR(time_NTP, DTO(serverNTP), NTP_SERVER_LEN + 1, PF_NE, 0),
	PN_BIT(time_UPDATE,     DTO(flags), fUpdateNTP, PT_B8, 0, TM_NTPUPD, 0, 0),
	PN_BIT(time_UPDATE_I2C, DTO(flags), fUpdateI2C, PT_B8, 0, TM_I2CUPD, 0, 0),
	{ "ntp_result", 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, TM_NTPR, 0 },    // итог последней синхронизации: "попытка;код;чч:мм:сс" или пусто
};
static void *api_base_time(uint8_t) { return HP.api_datetime(); }
static int api_get_time(uint8_t hook, uint8_t, ApiOut *o)
{
	char t[40]; t[0] = 0;
	if(hook == TM_TIME) HP.get_datetime((char*) time_TIME, t);
	else if(hook == TM_DATE) HP.get_datetime((char*) time_DATE, t);
	else if(hook == TM_NTPR) get_NTP_result(t);
	else return AE_FAIL;
	api_put_str(o, t);
	return AE_OK;
}
static int api_set_time(uint8_t hook, uint8_t, const char *val)
{
	if(hook == TM_TIME) return HP.set_datetime((char*) time_TIME, (char*) val) ? AE_OK : AE_BAD_VALUE;     // val - рабочая копия ядра
	if(hook == TM_DATE) return HP.set_datetime((char*) time_DATE, (char*) val) ? AE_OK : AE_BAD_VALUE;
	return AE_FAIL;
}
static void api_after_time(uint8_t hook)
{
	// при ВКЛЮЧЕНИИ синхронизации: считаем с нуля
	if((hook == TM_NTPUPD && GETBIT(HP.api_datetime()->flags, fUpdateNTP)) || (hook == TM_I2CUPD && GETBIT(HP.api_datetime()->flags, fUpdateI2C))) HP.set_countNTP(0);
}
static const ApiOps apiOpsTime = { api_base_time, api_get_time, api_set_time, api_after_time, 0, 0, 0, 0 };
extern const ApiGroup ApiGrpTime;
const ApiGroup ApiGrpTime = { "time", apiTimeTab, sizeof(apiTimeTab) / sizeof(apiTimeTab[0]), &apiOpsTime };
