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
 * Обёртки над FreeRTOS, общие для всего проекта.
 *
 * ЗАЧЕМ ОТДЕЛЬНЫЙ ЗАГОЛОВОК. Задержка и замок шины нужны любому модулю, поэтому эти три
 * функции лежат здесь, а не в Control.ino. Две из них inline: из отдельной единицы
 * трансляции их иначе нельзя было бы позвать - компоновщик не нашёл бы тела.
 *
 * Здесь они static inline: тело видно каждому, кто включил заголовок, поэтому
 * инлайн сохраняется и в отдельных единицах трансляции - ровно как в GpioDirect.h.
 */
#ifndef Rtos_h
#define Rtos_h

#include "Arduino.h"
#include "FreeRTOS_ARM.h"

// Замки шин и очередей. Создаются в Control.ino, в setup().
extern SemaphoreHandle_t xModbusSemaphore;      // Modbus, инвертор и счётчик
extern SemaphoreHandle_t xWebThreadSemaphore;   // веб-потоки
extern SemaphoreHandle_t xI2CSemaphore;         // шина I2C
extern SemaphoreHandle_t xSPISemaphore;         // шина SPI

/*
 * Хендлы задач теплового насоса.
 *
 * Лежали полями HeatPump, и это заставляло каждый модуль, которому нужно
 * разбудить или приостановить задачу, включать весь god-объект. Здесь они
 * рядом с семафорами - вся связка с FreeRTOS в одном месте.
 *
 * Объявлены безусловно, хотя часть задач создаётся по условию (EEV_DEF,
 * W5200_THREAD). Условная компиляция осталась там, где задачи создаются и
 * используются; здесь она стоила бы включения Config.h во все четырнадцать
 * модулей, которые включают этот заголовок, ради экономии до 16 байт - и
 * только в конфигурации без ЭРВ и с одним веб-потоком. Сегодня не
 * экономится ничего: потоков четыре, ЭРВ есть.
 */
struct HeatPumpTasks
{
	TaskHandle_t xHandleUpdate;         // "Обновление ТН"
	TaskHandle_t xHandleUpdateEEV;      // "Обновление ЭРВ"
	TaskHandle_t xHandleReadSensor;     // "Чтение датчиков"
	TaskHandle_t xHandleSericeHP;       // обслуживание ТН
	TaskHandle_t xHandleUpdateCommand;  // разбор очереди команд
	TaskHandle_t xHandleUpdateWeb0;     // веб-сервер, поток 0
	TaskHandle_t xHandleUpdateWeb1;     // веб-сервер, поток 1
	TaskHandle_t xHandleUpdateWeb2;     // веб-сервер, поток 2
	TaskHandle_t xHandleUpdateWeb3;     // веб-сервер, поток 3
	SemaphoreHandle_t xCommandSemaphore;// семафор команды
	volatile bool Task_vUpdate_run;     // задача vUpdate работает (уровень: проверяется после каждого пробуждения)
	volatile uint32_t Task_UpdateEEV_gen; // счётчик пусков ЭРВ: растёт при каждом пуске компрессора (фронт)
};
extern HeatPumpTasks Tasks;             // создаётся в Control.ino

// Разбудить задачу уведомлением. В отличие от vTaskResume уведомление не теряется, если задача
// ещё не дошла до ожидания, и ничего не делает с задачей, которая не ждёт. Задачи ждут
// ulTaskNotifyTake с таймаутом и сами проверяют условие работы, поэтому лишнее уведомление безвредно.
static inline void WakeTask(TaskHandle_t h) { if(h != NULL) xTaskNotifyGive(h); }
// Пуск задачи UpdateHP: флаг ставится ДО уведомления, задача проверяет его после каждого пробуждения.
static inline void Wake_UpdateHP(void) { Tasks.Task_vUpdate_run = true; WakeTask(Tasks.xHandleUpdate); }
// Пуск задачи UpdateEEV: пуск - событие, а не состояние, поэтому считается. Вызывать ПОСЛЕ включения компрессора.
static inline void Wake_UpdateEEV(void) { __atomic_add_fetch(&Tasks.Task_UpdateEEV_gen, 1, __ATOMIC_RELEASE); WakeTask(Tasks.xHandleUpdateEEV); }

// Задержка (мсек) в зависимости от того, запущен ли шедулер.
static inline void _delay(int t)
{
  if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) vTaskDelay(t / portTICK_PERIOD_MS);
  else delay(t);
}

// Захват семафора. До старта шедулера возвращает pdTRUE без захвата: задачи
// ещё не работают, конкуренции нет. После старта это обычный блокирующий
// захват, xBlockTime - настоящее время ожидания (тик 1 мс = 1 мсек).
// При ОСТАНОВЛЕННОМ планировщике (между vTaskSuspendAll и xTaskResumeAll) возвращает pdFALSE и считает
// попытку в Diag.num_semSuspended: задачи там тоже не работают, но держатель замка может стоять посреди
// транзакции, и "успех без захвата" пускал бы вызывающего на шину поверх чужой операции.
BaseType_t SemaphoreTake(QueueHandle_t xSemaphore, TickType_t xBlockTime);

// Освободить семафор с проверкой, что шедулер работает.
static inline void SemaphoreGive(QueueHandle_t xSemaphore)
{
	if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) xSemaphoreGive(xSemaphore);
}


// Короткая критическая секция: запрет прерываний с сохранением PRIMASK (вложенность допустима).
// Для нескольких слов, которые должны читаться и меняться вместе; блокировок внутри быть не может.
static inline uint32_t irqSave(void) { uint32_t p = __get_PRIMASK(); __disable_irq(); return p; }
static inline void irqRestore(uint32_t p) { __set_PRIMASK(p); }

// Остановить шедулер задач, возврат 1 если получилось. Определена в Control.ino.
uint8_t TaskSuspendAll(void);

// Отметиться в наблюдателе задач внутри длинной операции (DHCP, работа с SD).
// Реализация - связка в Control.ino: она знает таблицу задач наблюдателя.
// Чистая часть наблюдателя, которую проверяют тесты, лежит в Watchdog.h.
void Watchdog_AliveHere(void);

// Длинная пауза с отметкой в наблюдателе, для общего кода ТН, который не знает
// своего номера задачи (один и тот же код зовётся из UpdateHP и из CommandHP).
// Сон нарезается на ломти, перед каждым идёт отметка по текущему хендлу.
// Ставится только на паузы, которые по замыслу длинные и настраиваются
// пользователем: циклы повторных попыток обязаны доводить дело до перезагрузки.
void Watchdog_SleepHere(uint32_t ms);

/*
 * Счётчики состояния контроллера: то, что показывается на странице
 * диагностики и по чему судят о здоровье железа и сети.
 *
 * Лежали полями HeatPump вперемешку с управлением тепловым насосом, хотя к
 * насосу отношения не имеют: это загрузка процессора, свободная память,
 * напряжение питания и счётчики восстановлений - сетевого чипа, замков шин,
 * MQTT-клиента, пингов и сокетов. Рост любого из счётчиков восстановления -
 * первый признак того, что что-то отваливается; поэтому их держат вместе и
 * смотрят вместе.
 */
struct ControllerDiag
{
	uint8_t  CPU_LOAD;         // загрузка процессора, %
	uint32_t mRTOS;            // память, занимаемая задачами
	uint32_t startRAM;         // свободная память при старте FreeRTOS
	uint16_t AdcVcc;           // напряжение питания
	uint16_t num_resW5200;     // сбросов сетевого чипа
	uint16_t num_resMutexSPI;  // сбросов замка шины SPI
	uint16_t num_resMutexI2C;  // сбросов замка шины I2C
	uint16_t num_semSuspended; // попыток взять замок при ОСТАНОВЛЕННОМ планировщике (должно быть 0)
	uint16_t num_resMQTT;      // повторных инициализаций MQTT-клиента
	uint16_t num_resPing;      // не прошедших пингов
	uint16_t num_dmaTimeout;   // чтений по DMA шины SPI (W5500, SD) с таймаутом (данные не дошли)
	uint16_t num_dmaOverrun;   // переполнений приёмника SPI при чтении по DMA (W5500, SD)
	uint32_t countResSocket;   // сбросов сокетов
};
extern ControllerDiag Diag;    // создаётся в Control.ino

#endif
