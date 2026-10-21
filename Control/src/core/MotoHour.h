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
 * Счётчики наработки и энергии.
 *
 * ЗАЧЕМ ОТДЕЛЬНЫЙ МОДУЛЬ. Три операции над этими счётчиками - накопление раз
 * в минуту, сброс и переход со старого формата записи - проверяются тестом, а не
 * живут внутри HeatPump. Цена ошибки высокая и отложенная: счётчики копятся
 * годами, показываются пользователю как наработка и потреблённая энергия, и
 * неверный пересчёт заметят через сезон, когда сравнивать уже не с чем.
 *
 * МОДУЛЬ ЗАГОЛОВОЧНЫЙ. Три функции целиком помещаются здесь, отдельная
 * единица трансляции ради них не нужна, а проверки раскладки ниже выполняются
 * в каждом файле, который счётчики использует, - то есть срабатывают раньше и
 * надёжнее.
 *
 * СТРУКТУРЫ БЕЗ ИЗМЕНЕНИЙ. Они пишутся в EEPROM сырыми байтами, поэтому ни порядок
 * полей, ни типы менять нельзя.
 */
#ifndef MotoHour_h
#define MotoHour_h

#include <stdint.h>
#include <stddef.h>

#define I2C_COUNT_EEPROM_HEADER 0xAA

// Формат версий <= 138. Оставлен, чтобы читать записи старых прошивок.
// Порядок полей здесь ДРУГОЙ: после C2 идут E1,E2, и только потом D1,D2.
struct type_motoHour_old
{
  uint8_t  Header;  // признак данных
  uint8_t  flags;   // флаги
  uint32_t H1;      // моточасы ТН ВСЕГО
  uint32_t H2;      // моточасы ТН сбрасываемый счетчик (сезон)
  uint32_t C1;      // моточасы компрессора ВСЕГО
  uint32_t C2;      // моточасы компрессора сбрасываемый счетчик (сезон)
  uint32_t E1;		// Значение потребленной энергии ВСЕГО
  uint32_t E2;		// Значение потребленной энергии в начале сезона
  uint32_t D1;      // Дата сброса общих счетчиков
  uint32_t D2;      // дата сброса сезонных счетчиков
  uint32_t P1;      // выработанное тепло  ВСЕГО
  uint32_t P2;      // выработанное тепло  сбрасываемый счетчик (сезон)
  uint32_t Z1;      // Резервный параметр 1
  uint32_t Z2;      // Резервный параметр 2
};

struct type_motoHour
{
  uint8_t  Header;  // признак данных
  uint8_t  flags;   // флаги
  uint32_t H1;      // моточасы ТН ВСЕГО  [минуты]
  uint32_t H2;      // моточасы ТН сбрасываемый счетчик (сезон) [минуты]
  uint32_t C1;      // моточасы компрессора ВСЕГО [минуты]
  uint32_t C2;      // моточасы компрессора сбрасываемый счетчик (сезон) [минуты]
  uint32_t D1;      // Дата сброса общих счетчиков [юникс формат]
  uint32_t D2;      // дата сброса сезонных счетчиков [юникс формат]
  uint64_t E1;		// Значение потребленной энергии ВСЕГО [ватт*ч * 1000 или кВт*ч * 1000000]
  uint64_t E2;		// Значение потребленной энергии в начале сезона [ватт*ч * 1000 или кВт*ч * 1000000]
  uint64_t P1;      // выработанное тепло  ВСЕГО [ватт*ч * 1000 или кВт*ч * 1000000]
  uint64_t P2;      // выработанное тепло  сбрасываемый счетчик (сезон) [ватт*ч * 1000 или кВт*ч * 1000000]
};

// Обе структуры пишутся в EEPROM сырыми байтами, поэтому их раскладка -
// формат хранения, а не деталь реализации. Вставка поля в середину молча
// испортила бы счётчики у всех пользователей: заголовок проверяется, а
// содержимое - нет. Проверки ниже ловят это на сборке.
static_assert(sizeof(type_motoHour) == 64, "раскладка счётчиков изменилась");
static_assert(offsetof(type_motoHour, H1) ==  4, "сдвинулось H1");
static_assert(offsetof(type_motoHour, C2) == 16, "сдвинулось C2");
static_assert(offsetof(type_motoHour, D1) == 20, "сдвинулось D1");
static_assert(offsetof(type_motoHour, E1) == 32, "сдвинулось E1");
static_assert(offsetof(type_motoHour, P2) == 56, "сдвинулось P2");

// Старый формат: 52 байта, и E1 стоит ДО дат. Именно поэтому переход
// нельзя сделать простым копированием структуры.
static_assert(sizeof(type_motoHour_old) == 52, "раскладка старых счётчиков изменилась");
static_assert(offsetof(type_motoHour_old, E1) == 20, "сдвинулось старое E1");
static_assert(offsetof(type_motoHour_old, D1) == 28, "сдвинулось старое D1");
static_assert(offsetof(type_motoHour_old, P1) == 36, "сдвинулось старое P1");

inline void motohour_tick_minute(type_motoHour &m, bool compressor_on, bool hp_working,
                          int32_t &energy_in_work, int32_t &energy_out_work)
{
	if(compressor_on) {
		m.C1++;      // моточасы компрессора ВСЕГО
		m.C2++;      // моточасы компрессора сбрасываемый счетчик (сезон)
	}
	if(hp_working) {
		m.H1++;      // моточасы ТН ВСЕГО
		m.H2++;      // моточасы ТН сбрасываемый счетчик (сезон)
	}
	// Накопленное задачей чтения датчиков переливается и обнуляется одним
	// движением: между чтением и обнулением задача может добавить ещё, и
	// тогда добавка потерялась бы.
	int32_t p = energy_in_work;
	energy_in_work = 0;
	m.E1 += p;
	m.E2 += p;
	p = energy_out_work;
	energy_out_work = 0;
	m.P1 += p;
	m.P2 += p;
}

inline void motohour_reset(type_motoHour &m, bool full, uint32_t now,
                    int32_t &energy_in_work, int32_t &energy_out_work)
{
	if(full) {
		m.H1 = 0;
		m.C1 = 0;
		m.P1 = 0;
		m.E1 = 0;
		m.D1 = now;    // Дата сброса общих счетчиков
	}
	m.H2 = 0;
	m.C2 = 0;
	m.P2 = 0;
	m.E2 = 0;
	m.D2 = now;        // дата сброса сезонных счетчиков
	energy_in_work = 0;
	energy_out_work = 0;
}

inline void motohour_migrate_from_138(type_motoHour &m, const type_motoHour_old &old)
{
	// H1, H2, C1, C2 в обоих форматах лежат на одних и тех же смещениях,
	// поэтому они уже на месте и не переносятся.
	m.D1 = old.D1;
	m.D2 = old.D2;
	m.E1 = old.E1;
	m.E2 = old.E2;
	m.P1 = old.P1;
	m.P2 = old.P2;
}

#endif
