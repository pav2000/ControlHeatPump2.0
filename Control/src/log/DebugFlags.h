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
 * Флаги отладки: что пишется в журнал и консоль. Таблица флагов и их хранение; чистая логика
 * без обращений к железу, проверяется на хосте (tests/test_dbgflags.cpp).
 *
 * Один набор флагов вместо дефайнов (перепрошивка) и разрозненных флагов в настройках:
 * переключается со страницы "Отладка", сохраняется общей кнопкой "Записать в EEPROM".
 *
 * ПРАВИЛО ВЫВОДА. Что включено флагом, пишется и в консоль, и в журнал (jprintf); эхо в порт
 * зависит от USB (Journal.cpp).
 *
 * ХРАНЕНИЕ. В Option два резервных 16-битных поля: dbg - маска флагов 0..15, dbg2 - старший байт
 * сигнатура DBG_SIG, младший - маска флагов 16..23. Размер и раскладка Option не меняются. При другой
 * или отсутствующей сигнатуре применяются значения по умолчанию. Хранятся ВСЕ флаги: что записано кнопкой, то и стоит после перезагрузки; забытый
 * включённый флаг сбрасывается кнопкой "Сбросить по умолчанию".
 */
#ifndef DebugFlags_h
#define DebugFlags_h

#include <stdint.h>

// Номера действующих битов сохраняются. Пропуски принадлежали удалённым флагам.
#define DBG_MODW   (1UL << 0)    // решения алгоритма и плановые переключения насосов
#define DBG_FRED   (1UL << 1)    // причины снижения частоты
#define DBG_DUMP   (1UL << 2)    // снимок ТН на каждом проходе
#define DBG_PID    (1UL << 4)    // расчёт ПИД и целевая частота
#define DBG_EEV    (1UL << 5)    // шаги ЭРВ и коррекция перегрева
#define DBG_FCW    (1UL << 6)    // обмен с инвертором
#define DBG_SDM    (1UL << 7)    // повторные чтения счётчика SDM
#define DBG_NXT    (1UL << 8)    // обмен с дисплеем Nextion
#define DBG_WFULL  (1UL << 10)   // внешние сетевые сервисы и телеметрия
#define DBG_HTTP   (1UL << 11)   // запросы и ошибки локального веб-сервера
#define DBG_PHY    (1UL << 12)   // Ethernet, SPI/DMA и пинг
#define DBG_CFG    (1UL << 16)   // чтение и запись разделов настроек и файлов

#define DBG_SIG    0xD7          // Новая сигнатура сбрасывает старые маски без переноса битов.

// Зеркало маски. Читают все файлы через DBG_ON; пишут только загрузка
// настроек и команда set_Dbg. Определено в DebugFlags.cpp.
extern volatile uint32_t dbgFlags;
#define DBG_ON(f)  ((dbgFlags & (uint32_t)(f)) != 0)

struct DbgFlagInfo {
	const char *name;     // имя для веба, до 5 знаков
	const char *group;    // группа на странице
	const char *note;     // описание на странице (без ';' и '|')
	uint32_t    bit;
	uint8_t     dflt;     // значение по умолчанию
};
extern const DbgFlagInfo DbgTable[];
extern const uint8_t     DbgCount;

uint32_t Dbg_Defaults(void);                       // маска значений по умолчанию
uint32_t Dbg_KnownMask(void);                      // все биты таблицы
// Читает маску из Option. При несовпадении сигнатуры возвращает false и маску по умолчанию.
bool     Dbg_Unpack(uint16_t dbg, uint16_t dbg2, uint32_t *mask);
void     Dbg_Pack(uint32_t mask, uint16_t *dbg, uint16_t *dbg2);   // флаги + сигнатура
int      Dbg_Find(const char *name);               // номер в таблице или -1
bool     Dbg_Set(uint32_t *mask, const char *name, int val);       // false - имени нет

#endif
