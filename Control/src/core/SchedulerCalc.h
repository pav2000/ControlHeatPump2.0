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
 * Разбор календаря расписаний - чистый расчёт, без часов и без HP.
 *
 * ЗАЧЕМ ОТДЕЛЬНО. В Scheduler::calc_active_profile логика перемешана с чтением часов и текущего
 * профиля, и проверить её можно только на живом контроллере. Ошибка тихая (граница дня недели,
 * откат "взять последнюю запись", четыре кодировки байта Profile): ТН едет не по тому профилю.
 * Здесь день недели и час - параметры, размер таблицы - аргумент: модуль не включает Constant.h
 * и Arduino.h и проверяется на хосте (tests/test_schedulercalc.cpp).
 * ФОРМАТ ТАБЛИЦЫ. Расписания лежат подряд в одном буфере:
 *   {len}{item}{item}...{len}{item}...
 * len - длина расписания в байтах, не считая самого len. Каждый item - два
 * байта: день недели (3 бита) + час (5 бит), затем код профиля.
 *
 * КОД ПРОФИЛЯ (байт Profile) означает четыре разные вещи:
 *      0           пауза - ТН выключить
 *    127           не менять ничего
 *   < -100         сменить профиль на 128 + Profile - 1  (то есть 0..26)
 *   иначе          оставить профиль, но сдвинуть целевую температуру
 *                  на Profile десятых градуса (возвращается через change_out)
 */
#ifndef SchedulerCalc_h
#define SchedulerCalc_h

#include <stdint.h>

// Возвраты Scheduler_CalcProfile. Совпадают с кодами Scheduler.h, кроме SCHDLR_KeepProfile:
// он значит "оставить текущий профиль" (подставляет вызывающий), чтобы расчёт не
// обращался к HP.
#define SCHDLR_NotActive     -2     // расписание выключено флагом
#define SCHDLR_Profile_off   -1     // пустой календарь или пауза
#define SCHDLR_KeepProfile  -128    // оставить профиль, который работает сейчас

// Смещение расписания номер num в общем буфере. При выходе за буфер
// возвращает size-1 - так же, как Scheduler::Timetable_ptr.
uint16_t Scheduler_TimetablePtr(const uint8_t *timetable, uint16_t size, uint8_t num);

// Разбор календаря. day_of_week: 0 - понедельник (уже пересчитанный из RTC,
// где 0 - воскресенье). hour: 0..23. change_out - сдвиг целевой температуры
// в сотых градуса, всегда записывается.
int8_t Scheduler_CalcProfile(const uint8_t *timetable, uint16_t size,
                             uint8_t active, uint8_t day_of_week, uint8_t hour,
                             int16_t *change_out);

// Записать календарь cnum из строки val вида "длина;байт;байт;...;" (разбор SetSchdlr CalendarX).
// val портится (в нём ставятся нули вместо ';'). Возврат: 0 - готово, 1 - нет такого календаря,
// 2 - не лезет в cap байт (или таблица повреждена); при отказе буфер не меняется.
// Байты сверх объявленной длины отбрасываются, недоданные - нули. Календари после
// редактируемого сдвигаются целиком.
int8_t Scheduler_SetCalendar(uint8_t *timetable, uint16_t cap, uint8_t calendars, uint8_t cnum, char *val);

// Используется ли профиль profile (0-based) хоть в одном из calendars календарей.
// Нужно, чтобы не стереть профиль, на который ссылается расписание.
// Разбор записей тот же, что в Scheduler_CalcProfile: "сменить профиль" - это
// байт меньше -100, номер профиля = 128 + байт - 1.
bool Scheduler_UsesProfile(const uint8_t *timetable, uint16_t size, uint8_t calendars, uint8_t profile);


// ---------------------------------------------------------------------------
// Сокращение числа календарей с 9 до 5 (формат настроек остаётся 150).
// Данные, записанные прежней прошивкой, содержат 9 календарей; при чтении
// лишние отбрасываются этими функциями.
// ---------------------------------------------------------------------------

// Обрезать общий буфер календарей: оставить первые keep календарей, всё
// после них обнулить. Возвращает true, если после keep-го календаря что-то
// было (непустой календарь или хотя бы ненулевой байт).
bool Scheduler_TrimTimetable(uint8_t *timetable, uint16_t size, uint8_t keep);

// Привести активный календарь к допустимому. Если active >= keep, то
// active = 0 и из flags сбрасываются биты mask (расписание выключается).
// Возвращает true, если пришлось исправлять.
bool Scheduler_FixActive(uint8_t *flags, uint8_t *active, uint8_t keep, uint8_t mask);

#endif
