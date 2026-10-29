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

/*
 * Учёт перезагрузок (С-18, Н-26).
 *
 * Карта GPBR. Восемь регистров переживают сброс, но не снятие питания: после подачи питания
 * в них мусор, поэтому значения, которые читаются при старте, несут метку.
 */
enum {
	GPBR_RTOS   = 0,   // FreeRTOS: номер текущей задачи << 8 | код аварии ядра (пишут tasks.c и errorBlink); GPBR_RTOS_READ - прочитан при старте
	GPBR_TASK   = 1,   // 1..3: имя задачи при переполнении стека (vApplicationStackOverflowHook)
	GPBR_DBG    = 4,   // точка отладки задач (STORE_DEBUG_INFO)
	GPBR_WEBDBG = 5,   // точка отладки веба (WEB_STORE_DEBUG_INFO)
	GPBR_REASON = 6,   // причина, записанная перед сбросом: RESET_*_MARK
	GPBR_BOOT   = 7    // BOOT_MARK | аварийных перезагрузок подряд
};
#define GPBR_RTOS_READ      0x80000000UL   // снимает первое же переключение задач: признак стоит - планировщик после старта не работал
#define RESET_STUCK_MARK    0x57440000UL   // 'WD' | индекс молчавшей задачи (Watchdog_Supervise)
#define RESET_PLANNED_MARK  0x504C0000UL   // 'PL': сброс по команде (pRESET)
#define RESET_ASSERT_MARK   0x41530000UL   // 'AS': провал assert
#define BOOT_MARK           0xFA170000UL
#define BOOT_MAX_RESETS     3              // аварийных перезагрузок подряд, после которых ТН сам не запускается
#define BOOT_HEALTHY_S      600            // столько секунд работы без перезагрузки обнуляют счёт

// Тип сброса из RSTC_SR.RSTTYP (SAM3X)
enum : uint8_t { RST_GENERAL = 0, RST_BACKUP, RST_WATCHDOG, RST_SOFTWARE, RST_USER };

// Причины с BOOT_KERNEL и дальше - аварийные: они считаются подряд и после BOOT_MAX_RESETS отменяют автопуск.
enum BOOT_CAUSE : uint8_t {
	BOOT_POWER,        // подача питания (регистры не читаются - в них мусор)
	BOOT_USER,         // кнопка RESET
	BOOT_PLANNED,      // сброс по команде
	BOOT_KERNEL,       // авария ядра (errorBlink -> vApplicationFaultHook)
	BOOT_STUCK,        // задача перестала отмечаться, наблюдатель отдал сброс аппаратному WDT
	BOOT_HW_WDT,       // аппаратный WDT без причины от наблюдателя (встал сам наблюдатель)
	BOOT_ASSERT,       // провал assert
	BOOT_SOFT_UNKNOWN  // программный сброс, перед которым причину не записали
};

struct BootRegs {          // GPBR_RTOS, GPBR_REASON, GPBR_BOOT
	uint32_t rtos, reason, boot;
};

struct BootInfo {
	BOOT_CAUSE cause;
	uint8_t  fault;        // код аварии ядра, 0 - не было
	int16_t  task;         // BOOT_STUCK - индекс задачи наблюдателя, BOOT_KERNEL - номер задачи FreeRTOS; -1 - неизвестна (авария до планировщика)
	uint16_t rtos;         // номер задачи << 8 | код - для диагностики
	uint8_t  resets;       // аварийных перезагрузок подряд, включая эту
};

// Решение при старте: причина и счёт подряд. Регистры переписываются (причина прочитана, счёт обновлён),
// запись их обратно в GPBR - за вызывающим. rsttyp - RST_*.
BootInfo    Boot_Decide(BootRegs *r, uint8_t rsttyp);
bool        Boot_Abnormal(BOOT_CAUSE c);
bool        Boot_NoAutoStart(const BootInfo &b);   // аварии по кругу: не запускать компрессор, ждать человека
const char *Boot_CauseText(BOOT_CAUSE c);

#endif
