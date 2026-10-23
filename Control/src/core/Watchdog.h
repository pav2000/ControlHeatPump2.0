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
 * Сторожевой таймер по отметкам задач (heartbeat).
 *
 * ЗАЧЕМ. Сброс WDT из vApplicationIdleHook не годится: пока хоть одна задача спит, вачдог
 * сброшен, и зависание vUpdate или vReadSensor не видно (ТН с работающим компрессором без надзора).
 *
 * КАК. Каждая задача отмечается (Watchdog_Beat); наблюдатель в vServiceHP сбрасывает WDT, только
 * если все отметились в срок. Приостановленные через vTaskSuspend задачи (штатно StepperEEV,
 * UpdateHP, UpdateEEV) из проверки исключаются. CommandHP не приостанавливается: она ждёт
 * уведомление не дольше секунды и отмечается на каждом витке.
 *
 * ГДЕ ОТМЕТКА. В точке блокировки (Watchdog_Sleep, он же нарезает длинный сон), а не на витке
 * цикла: отметка в начале for верна лишь до первого continue, goto или внутреннего цикла, не
 * доходящего до конца витка, - так UpdateEEV давал перезагрузки на каждом пуске компрессора
 * (tests/test_watchdog_eev.cpp). Логика проверки - чистая функция Watchdog_Check без FreeRTOS и
 * железа (tests/test_watchdog.cpp).
 */
#ifndef Watchdog_h
#define Watchdog_h

#include <stdint.h>

// Идентификаторы задач. Порядок важен только для читаемости журнала.
// Метка в GPBR[6] после перезагрузки по молчанию задачи: 'WD' + индекс задачи.
// GPBR 0..5 заняты диагностикой (код отказа, имя задачи, хлебные крошки).
#define WDT_STUCK_MARK  0x57440000UL

enum {
	WDT_ReadSensor = 0,
	WDT_ServiceHP,
	WDT_UpdateHP,
	WDT_UpdateEEV,
	WDT_CommandHP,
	WDT_StepperEEV,
	WDT_Web0,
	WDT_Web1,
	WDT_Web2,
	WDT_Web3,
	WDT_TASK_COUNT
};

struct WatchdogTask {
	uint32_t beat;          // счётчик отметок, увеличивает сама задача
	uint32_t last_beat;     // снимок, сделанный наблюдателем
	uint32_t last_seen_ms;  // время последнего продвижения beat
	uint32_t limit_ms;      // предел молчания для этой задачи
	uint32_t max_silence_ms;// наибольшее наблюдавшееся молчание, см. "ЗАМЕР ЗАПАСА"
};

/*
 * ЗАМЕР ЗАПАСА (max_silence_ms). Предел молчания задан "на глазок", и запас относительно периода
 * задачи раньше узнавали только постфактум, по перезагрузке. max_silence_ms делает его
 * наблюдаемым: это наибольшее время, которое задача хоть раз молчала. Измеряется не интервал
 * между отметками задачи, а интервал между двумя наблюдениями продвижения beat: наблюдатель
 * вызывается раз в секунду, поэтому у частой задачи величина около секунды, снизу она ограничена
 * периодом наблюдателя, сверху ничем - ровно с ней вачдог и сравнивает limit_ms. Приостановленные
 * задачи в замер не попадают: их молчание законно.
 */

// Результат проверки
enum WatchdogVerdict {
	WDT_ALL_ALIVE = 0,      // все живы, вачдог можно сбрасывать
	WDT_TASK_STUCK          // кто-то молчит дольше своего предела
};

/*
 * Чистая проверка состояния. Ничего не знает про FreeRTOS и железо.
 *
 *   tasks         - массив состояний, n элементов
 *   now_ms        - текущее время в миллисекундах (переполнение учтено)
 *   suspended     - битовая маска приостановленных задач, они пропускаются
 *   stuck_out     - сюда пишется индекс первой зависшей задачи (или -1)
 *   record_mask   - сюда пишется маска задач, у которых в этом вызове обновился
 *                   max_silence_ms. Нужна вызывающему, чтобы написать в журнал
 *                   только про новый рекорд, а не про каждую секунду молчания.
 *                   Необязательный: можно не передавать.
 *
 * Возвращает WDT_ALL_ALIVE или WDT_TASK_STUCK.
 *
 * Побочный эффект: для задач, чей beat продвинулся, обновляет last_beat
 * и last_seen_ms. Это и есть "наблюдение". Плюс ведёт max_silence_ms.
 */
// Реализация в Watchdog.cpp. В заголовке её держать нельзя: arduino-builder
// определяет точку вставки автогенерируемых прототипов по первой функции
// в препроцессированном потоке, и функция из заголовка сбивает этот расчёт -
// прототипы перестают генерироваться для большей части проекта.
WatchdogVerdict Watchdog_Check(WatchdogTask *tasks, uint8_t n,
                               uint32_t now_ms, uint32_t suspended,
                               int8_t *stuck_out, uint32_t *record_mask = 0);

void Watchdog_Init(WatchdogTask *t, uint32_t limit_ms, uint32_t now_ms);

#endif
