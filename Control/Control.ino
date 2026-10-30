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

#include "Arduino.h"
#include "Constant.h"                       // Должен быть первым !!!! кое что берется в стандартных либах для настройки либ, Config.h входит в него

#include <WireSam.h>                        // vad711 доработанная библиотека https://github.com/vad7/Arduino-DUE-WireSam
#include <DS3232.h>                         // vad711 Работа с часами i2c - используются при выключении питания. модифицированная https://github.com/vad7/Arduino-DUE-WireSam
#include <extEEPROM.h>                      // vad711 Работа с eeprom и fram i2c библиотека доработана для DUE!!! https://github.com/vad7/Arduino-DUE-WireSam

#include <SPI.h>                            // SPI - стандартная
#include "SdFat.h"                          // SdFat - модифицированная
#include "FreeRTOS_ARM.h"                   // модифицированная
#include <rtc_clock.h>                      // работа со встроенными часами  Это основные часы!!!
#include <ModbusMaster.h>                   // Используется МОДИФИЦИРОВАННАЯ для омрона мх2 либа ModbusMaster https://github.com/4-20ma/ModbusMaster

// Глубоко переделанные библиотеки
#include <Ethernet.h>                       // Ethernet - модифицированная
#include <Dns.h>                            // DNS - модифицированная
#include <SerialFlash.h>                    // Либа по работе с spi флешом как диском

#include "src/hw/Hardware.h"
#include "src/core/HeatPump.h"
#include "src/hw/Nextion.h"
#include "src/net/Message.h"
#include "src/core/Profile.h"
#include "src/net/MQTT.h"
#include "src/log/Statistics.h"
#include "src/core/Watchdog.h"

void vUpdateStepperEEV(void *);
#include "src/hw/StepMotor.h"
#include "src/net/Network.h"
#include "src/hw/Clock.h"                   // транспорт и переключатели SPI
#include "src/core/Rtos.h"                      // обёртки FreeRTOS: _delay, SemaphoreTake/Give
#include "src/net/WebShared.h"                 // объявление буферов веб-потоков (Socket[])

// ---------------------------------------------------------------------------
// Контроль раскладки сохраняемых в EEPROM структур.
//
// Формат сохранения позиционный: save()/load() пишут и читают структуры целиком
// через memcpy, а порядок и размеры полей и есть формат. Поэтому случайная
// правка поля (вставка, смена типа, перестановка) тихо ломает совместимость
// с уже сохранёнными настройками во всех работающих установках, и обнаружится
// это только у владельца ТН при загрузке настроек.
//
// Проверки ниже превращают такую правку в ошибку сборки. Если изменение
// раскладки сделано намеренно - обновите число здесь и заведите НОВЫЙ ТЕГ
// записи NTAG_*: в формате 150 раскладку означает тег, а не версия. Поднимать
// VER_SAVE для этого не нужно и не достаточно.
// ---------------------------------------------------------------------------
#define CHECK_SAVE_SIZE(t, n) static_assert(sizeof(t) == (n), \
    "Изменилась раскладка " #t ": формат EEPROM несовместим со старыми настройками. " \
    "Обновите размер здесь и заведите новый тег записи NTAG_*")

CHECK_SAVE_SIZE(type_optionHP,    58);
CHECK_SAVE_SIZE(type_DateTimeHP,  68);
CHECK_SAVE_SIZE(type_NetworkHP,  120);
CHECK_SAVE_SIZE(type_SecurityHP, 164);
CHECK_SAVE_SIZE(type_settingHP,   38);
CHECK_SAVE_SIZE(type_boilerHP,    64);
CHECK_SAVE_SIZE(type_SaveON,      12);
CHECK_SAVE_SIZE(type_motoHour,    64);
CHECK_SAVE_SIZE(type_dataProfile,120);
// Расписание: 5 календарей. Новый тег не заводился осознанно:
// запись опознаётся по длине в заголовке, читаются обе (Scheduler::region_len).
CHECK_SAVE_SIZE(Scheduler_Data,  667);
// SchedulerCalc не видит Constant.h, биты режима у него свои
static_assert(SCHDLR_MODE_OFF == pOFF && SCHDLR_MODE_HEAT == pHEAT && SCHDLR_MODE_COOL == pCOOL, "SCHDLR_MODE_* разошлись с pOFF/pHEAT/pCOOL");
CHECK_SAVE_SIZE(type_messageHP,  276);
CHECK_SAVE_SIZE(type_mqtt,       196);

// ---------------------------------------------------------------------------
// Сторожевой таймер по отметкам задач. Описание - в Watchdog.h.
// ---------------------------------------------------------------------------
static WatchdogTask  wdtTasks[WDT_TASK_COUNT];
static TaskHandle_t *wdtHandles[WDT_TASK_COUNT];
static uint8_t       wdtReady = 0;      // наблюдатель включается после старта шедулера
#ifdef TEST_WATCHDOG
volatile int8_t      wdtHangTask = -1;  // номер задачи, которую просят "зависнуть"
#endif
static const char *wdtNames[WDT_TASK_COUNT] = {
	"ReadSensor", "ServiceHP", "UpdateHP", "UpdateEEV", "CommandHP",
	"StepperEEV", "Web0", "Web1", "Web2", "Web3"
};

// Мютексы блокираторы железа
SemaphoreHandle_t xModbusSemaphore;                 // Семафор Modbus, инвертор запас на счетчик
SemaphoreHandle_t xWebThreadSemaphore;              // Семафор потоки вебсервера,  деление сетевой карты
SemaphoreHandle_t xI2CSemaphore;                    // Семафор шины I2C, часы, память, мастер OneWire
SemaphoreHandle_t xSPISemaphore;                    // Семафор шины SPI  сетевая карта, память. SD карта // пока не используется
uint16_t lastErrorFreeRtosCode;                     // код последней ошибки операционки нужен для отладки
uint32_t startSupcStatusReg;                        // Состояние при старте SUPC Supply Controller Status Register - проверяем что с питание


#if defined(W5500_ETHERNET_SHIELD)                  // Задание имени чипа для вывода сообщений
  const char nameWiznet[] ={"W5500"};
#elif defined(W5200_ETHERNET_SHIELD)
  const char nameWiznet[] ={"W5200"};
#else
  const char nameWiznet[] ={"W5100"};
#endif

// Глобальные переменные
EthernetServer server1(80);                         // сервер
EthernetUDP Udp;                                    // Для NTP сервера
EthernetClient ethClient(W5200_SOCK_SYS);           // для MQTT


#ifdef MQTT                                 // признак использования MQTT
#include <PubSubClient.h>                   // передаланная под многозадачность  http://knolleary.net
PubSubClient w5200_MQTT(ethClient);  	    // клиент MQTT
#endif

// I2C eeprom Размер в килобитах, число чипов, страница в байтах, адрес на шине, тип памяти:
extEEPROM eepromI2C(I2C_SIZE_EEPROM,I2C_MEMORY_TOTAL/I2C_SIZE_EEPROM,I2C_PAGE_EEPROM,I2C_ADR_EEPROM,I2C_FRAM_MEMORY);
RTC_clock rtcSAM3X8(XTAL);                                               // Внутренние часы, используется часовой кварц
DS3232  rtcI2C;                                                          // Часы 3231 на шине I2C
Journal  journal;                                                        // системный журнал, отдельно т.к. должен инициализоваться с начала старта
ControllerDiag Diag;                                                     // Счётчики состояния контроллера, объявлены в Diagnostics.h
HeatPumpTasks Tasks;                                                     // Хендлы задач ТН и семафор команд, объявлены в Rtos.h
HeatPump HP;                                                             // Класс тепловой насос (в конструкторе плохо проходит инициализация пинов????)
devModbus Modbus;                                                        // Класс Modbus - управление инвертором
SdFat card;                                                              // Карта памяти
#ifdef NEXTION   
Nextion myNextion;                                                     // Дисплей
#endif

// Use the Arduino core to set-up the unused USART2 on Serial4 (without serial events)

// Структура для хранения одного сокета, нужна для организации многопотоковой обработки
// Флаги потока перенесены в WebShared.h, к самой структуре.

// Буферы веб-потоков. Структура объявлена в WebShared.h.
type_socketData Socket[W5200_THREAD];   // Требует много памяти 4*W5200_MAX_LEN*W5200_THREAD=24 кб

// Установка вачдога таймера вариант vad711 - адаптация для DUE 1.6.11
// WDT_TIME период Watchdog таймера секунды но не более 16 секунд!!! ЕСЛИ WDT_TIME=0 то Watchdog будет отключен!!!
volatile unsigned long ulHighFrequencyTimerTicks;   // vad711 - адаптация для DUE 1.6.11
void watchdogSetup(void)
{
#if WDT_TIME == 0
	watchdogDisable();
#else 
	watchdogEnable(WDT_TIME * 1000);
#endif
}

// Отметиться живым. Вызывается самой задачей раз в цикл.
__attribute__((always_inline)) inline void Watchdog_Beat(uint8_t id)
{
#ifdef TEST_WATCHDOG
	// Проверка срабатывания наблюдателя: задача перестаёт отмечаться, оставаясь
	// при этом работоспособной. Наблюдатель обязан это заметить и не сбрасывать вачдог.
	if(wdtHangTask == (int8_t) id) return;
#endif
	wdtTasks[id].beat++;
}

/*
 * Отметиться и уснуть. Штатная замена vTaskDelay внутри задачи, за которой
 * следит наблюдатель.
 *
 * ЗАЧЕМ. Отметка обязана стоять в точке блокировки задачи, а не на витке её
 * цикла. Отметка на витке цикла верна ровно до первого continue, goto или
 * внутреннего цикла, который до конца витка не доходит, - и именно это
 * случилось с UpdateEEV: рабочий путь задачи замыкался goto мимо отметки,
 * задача исправно управляла ЭРВ, а наблюдатель считал её молчащей и
 * перезагружал контроллер на каждом пуске компрессора.
 *
 * КАК. Длинный сон нарезается на ломти по WDT_SLEEP_CHUNK мсек, перед каждым
 * ломтем задача отмечается. Поэтому законная долгая пауза (у ЭРВ период ПИД
 * настраивается до 1000 с) больше не выглядит зависанием, а предел молчания
 * можно держать близко к периоду наблюдателя. Настоящее зависание - вечный
 * цикл, взаимоблокировка на замке, обрыв внутри вызова - до следующего ломтя
 * не доходит и ловится ровно как прежде.
 */
#define WDT_SLEEP_CHUNK   5000      // мсек, шаг нарезки длинного сна
void Watchdog_Sleep(uint8_t id, uint32_t ms)
{
	do {
		Watchdog_Beat(id);
		uint32_t chunk = ms > WDT_SLEEP_CHUNK ? WDT_SLEEP_CHUNK : ms;
		vTaskDelay(chunk / portTICK_PERIOD_MS);
		ms -= chunk;
	} while(ms);
}

// Заменяет прямой WDT_Restart внутри длинных операций (DHCP, SD, 1-Wire, почта).
// До старта шедулера сбрасывает вачдог напрямую - наблюдателя ещё нет.
// После старта только отмечает вызывающую задачу: решение сбрасывать вачдог
// остаётся за наблюдателем, иначе длинная операция обходила бы проверку.
void Watchdog_AliveHere(void)
{
	if(!wdtReady) { WDT_Restart(WDT); return; }
	TaskHandle_t cur = xTaskGetCurrentTaskHandle();
	for(uint8_t i = 0; i < WDT_TASK_COUNT; i++) {
		if(wdtHandles[i] && *wdtHandles[i] == cur) { Watchdog_Beat(i); return; }   // через Watchdog_Beat: test.wdt глушит и эту отметку
	}
	WDT_Restart(WDT);   // вызов не из зарегистрированной задачи - как раньше
}

/*
 * То же, что Watchdog_Sleep, но для кода, который не знает своего номера
 * задачи: отметка идёт через Watchdog_AliveHere по текущему хендлу.
 *
 * Нужна там, где длинная пауза лежит в общем коде ТН, а не в теле задачи:
 * паузы пуска и останова компрессора, ожидание минимальной паузы,
 * переключение ГВС, разморозка. Один и тот же код зовётся из UpdateHP и из
 * CommandHP, и у каждой отметиться надо за свою - поиск по хендлу это и делает.
 *
 * Ставится ТОЛЬКО на паузы, которые по замыслу длинные и настраиваются
 * пользователем. Короткие _delay в циклах повторных попыток (Modbus, I2C,
 * восстановление сети) остаются как есть: зависший цикл повторов обязан
 * приводить к перезагрузке, ради этого наблюдатель и делался.
 */
void Watchdog_SleepHere(uint32_t ms)
{
	do {
		Watchdog_AliveHere();
		uint32_t chunk = ms > WDT_SLEEP_CHUNK ? WDT_SLEEP_CHUNK : ms;
		_delay(chunk);
		ms -= chunk;
	} while(ms);
}

// Наблюдатель: сбрасывает вачдог только если отметились все незаприостановленные
// задачи. Вынесен в отдельную функцию, чтобы тело vServiceHP не росло.
void Watchdog_Supervise(uint32_t now_ms)
{
	uint32_t susp = 0;
	for(uint8_t i = 0; i < WDT_TASK_COUNT; i++)
		if(wdtHandles[i] && *wdtHandles[i] && eTaskGetState(*wdtHandles[i]) == eSuspended)
			susp |= 1UL << i;

	int8_t stuck;
	uint32_t records = 0;
	WatchdogVerdict verdict = Watchdog_Check(wdtTasks, WDT_TASK_COUNT, now_ms, susp, &stuck, &records);

	// Замер запаса. В журнал идёт только новый рекорд молчания и только начиная
	// с половины предела: строк получается единицы за всё время работы, но
	// сближение с пределом видно заранее, а не по факту перезагрузки. Полная
	// таблица по всем задачам - в веб-диагностике, get_sysWDT.
	for(uint8_t i = 0; records && i < WDT_TASK_COUNT; i++) {
		if(!(records & (1UL << i))) continue;
		if(!wdtTasks[i].limit_ms || wdtTasks[i].max_silence_ms * 2 < wdtTasks[i].limit_ms) continue;   // предел 0 - задача не наблюдается
		journal.jprintf_time("WDT: %s silent %.1d of %.1d s\n", wdtNames[i],
		                     (int)(wdtTasks[i].max_silence_ms / 100),
		                     (int)(wdtTasks[i].limit_ms / 100));
	}

	if(verdict == WDT_ALL_ALIVE) {
		WDT_Restart(WDT);
		return;
	}
	// Не сбрасываем - через WDT_TIME секунд контроллер перезагрузится. Причину с индексом задачи
	// при старте прочитает Boot_Decide.
	GPBR->SYS_GPBR[GPBR_REASON] = RESET_STUCK_MARK | (uint32_t) stuck;
	journal.jprintf_time("WATCHDOG: task %s stuck, reboot\n",
	                     stuck >= 0 ? wdtNames[stuck] : "?");
	journal.FlushAll();             // до сброса строка должна дойти до EEPROM (Н-20)
}

/*
 * Замеренный запас по каждой задаче: строки вида
 * "имя|молчание of предела (процент);". Пишет не более
 * WDT_TASK_COUNT*WDT_STATWEB_ROW байт - столько же обязан проверить
 * вызывающий (ключ sys.wdt и действие wdt.reset в WebApiGet.cpp).
 *
 * Текст строк английский намеренно. Ключ sys.wdt смотрят не через веб-морду,
 * а открывая адрес напрямую, а ответ отдаётся с заголовком "Content-Type:
 * text/ajax" без charset (Constant.h). Объявить кодировку браузеру некому, и он
 * откатывается на однобайтовую локаль - русский UTF-8 превращается в кашу.
 * Это касается любого русского текста в любом ajax-ответе, не только этого;
 * здесь просто нет причин рисковать. Заодно совпадает со стилем самого журнала:
 * сообщения вачдога там английские.
 *
 * clear - обнулить замеры после вывода, чтобы следующий опыт начинался с чистого.
 */
void Watchdog_StatWeb(char *buf, uint8_t clear)
{
	for(uint8_t i = 0; i < WDT_TASK_COUNT; i++) {
		uint8_t susp = wdtHandles[i] && *wdtHandles[i] && eTaskGetState(*wdtHandles[i]) == eSuspended;
		buf += m_snprintf(buf, WDT_STATWEB_ROW, "%s|%.1d of %.1d s (%d%%)%s;", wdtNames[i],
		                  (int)(wdtTasks[i].max_silence_ms / 100),
		                  (int)(wdtTasks[i].limit_ms / 100),
		                  wdtTasks[i].limit_ms ? (int)(wdtTasks[i].max_silence_ms * 100 / wdtTasks[i].limit_ms) : 0,
		                  susp ? ", suspended now" : "");
		if(clear) wdtTasks[i].max_silence_ms = 0;
	}
}

// Худшая задача по доле выбранного предела - для сводки одной строкой.
// Возвращает её имя; percent_out - её доля предела в процентах.
// Доля считается в uint16: у задачи, только что перешагнувшей предел, она
// бывает больше 255, и в uint8 такое число завернулось бы в маленькое.
const char *Watchdog_Worst(uint16_t *percent_out)
{
	uint8_t worst = 0;
	uint16_t worst_pc = 0;
	for(uint8_t i = 0; i < WDT_TASK_COUNT; i++) {
		if(!wdtTasks[i].limit_ms) continue;
		uint16_t pc = wdtTasks[i].max_silence_ms / (wdtTasks[i].limit_ms / 100 ? wdtTasks[i].limit_ms / 100 : 1);
		if(pc > worst_pc) { worst_pc = pc; worst = i; }
	}
	if(percent_out) *percent_out = worst_pc;
	return wdtNames[worst];
}

// _delay перенесён в Rtos.h

// Захват семафора. До старта шедулера возвращает pdTRUE без захвата:
// задачи ещё не работают, конкуренции нет.
//
// Захват блокирующий: задача спит в очереди семафора и будет разбужена ядром
// (а не опрашивает его через vTaskDelay(1): это стоило пробуждения на каждую
// попытку, превращало порядок захвата в гонку и делало xBlockTime счётчиком
// попыток). Таймауты на местах вызова заданы в тиках; при тике в 1 мс число
// тиков совпадает с числом миллисекунд.
BaseType_t SemaphoreTake(QueueHandle_t xSemaphore, TickType_t xBlockTime)
{
	BaseType_t st = xTaskGetSchedulerState();
	if(st == taskSCHEDULER_NOT_STARTED) return pdTRUE;                  // старт: задач ещё нет, конкуренции нет
	if(st == taskSCHEDULER_SUSPENDED) { Diag.num_semSuspended++; return pdFALSE; }   // замок при остановленном планировщике не берётся
	return xSemaphoreTake(xSemaphore, xBlockTime);
}

// Освободить семафор с проверкой, что шедуллер работает
// SemaphoreGive перенесён в Rtos.h

// Остановить шедулер задач, возврат 1, если получилось
uint8_t TaskSuspendAll(void) {
	if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
		vTaskSuspendAll(); // запрет других задач
		return 1;
	}
	return 0;
}

// Тела задач - обычные функции, а не __attribute__((naked)): GCC не гарантирует naked для кода на C, а
// исходящие аргументы вызовов (double в jprintf) писались в верхние слова стека задачи без выделенного кадра -
// безопасно лишь при чётной глубине стека и выравнивании кучи (шаг 5 разбора, 23.10.2026).
void vWeb0(void *);
void vWeb1(void *);
void vWeb2(void *);
void vWeb3(void *);
void vReadSensor(void *);
void vUpdate( void * );
void vUpdateEEV(void *);
void vUpdateStepperEEV(void *);
void vUpdateCommand(void *);
void vServiceHP(void *);

// ---------------------------------------------------------------------------
// Та ли микросхема памяти стоит, что выбрана в Config.h. Различаем по ЧИСЛУ БАНКОВ на шине:
// AT24C512 отвечает только на 0x50, FM24V10 - на 0x50 и 0x51 (два банка по 64 КБ). Это работает,
// пока из двух микросхем одна 64 КБ одним банком, а другая 128 КБ двумя; для FRAM 64 КБ (FM24V05)
// понадобится другой признак: записать байт и сразу прочитать - EEPROM на время внутреннего
// цикла записи (~5 мс) не отвечает, FRAM отвечает немедленно (на этом построен цикл ожидания в
// extEEPROM::write).
// Звать НАДО до journal.Init(): журнал читается по длине JOURNAL_LEN, выведенной из выбранного
// макроса; при завышенной длине чтение уходит за конец чипа, Init выставляет
// ERR_READ_I2C_JOURNAL, и setup принимает это за сбой шины (переподнимает I2C трижды с паузами).
enum { I2C_MEM_OK = 0, I2C_MEM_NO_CHIP, I2C_MEM_TOO_SMALL, I2C_MEM_TOO_BIG };
static uint8_t i2cMemCheck = I2C_MEM_OK;

static uint8_t i2c_check_memory(void)
{
	Wire.begin();                                  // идемпотентно, Init ниже зовёт то же
	Wire.beginTransmission(I2C_ADR_EEPROM);
	if(Wire.endTransmission() != 0) return I2C_MEM_NO_CHIP;
	Wire.beginTransmission(I2C_ADR_EEPROM + 1);    // второй банк FM24V10
	bool second = Wire.endTransmission() == 0;
#if I2C_MEMORY_TOTAL != I2C_SIZE_EEPROM
	if(!second) return I2C_MEM_TOO_SMALL;          // объявлено 128 КБ, а банк один
#else
	if(second)  return I2C_MEM_TOO_BIG;            // объявлено 64 КБ, а банка два
#endif
	return I2C_MEM_OK;
}

// Словами для get_CONST1 - чтобы расхождение было видно не только в журнале,
// который при нём и ломается.
const char *i2c_mem_check_note(void)
{
	switch(i2cMemCheck) {
	case I2C_MEM_TOO_SMALL: return " - НО ВТОРОЙ БАНК НЕ ОТВЕЧАЕТ, выбран не тот чип!";
	case I2C_MEM_TOO_BIG:   return " - НО НА ПЛАТЕ ВТОРОЙ БАНК ЕСТЬ, используется половина чипа";
	case I2C_MEM_NO_CHIP:   return " - НО ЧИП НЕ ОТВЕЧАЕТ";
	}
	return "";
}

// Режим загрузки, выбранный кнопкой KEY1. Замер идёт в самом начале setup, а в HP переносится
// только на шаге 3: initHeatPump() обнуляет safeNetwork, и запись в HP до него была бы затёрта
// молча.
static KeyBootMode keyBootMode = KEY_BOOT_NONE;
static uint32_t    keyBootHeld = 0;         // измеренное удержание, мсек, для журнала
static bool        keyWasDownAtBoot = false;// кнопка была нажата при включении
// Вкл/выкл ТН по кнопке: готовность принять нажатие и момент отпускания.
// Начальное значение выставляется на шаге 3 загрузки, опрос - в
// vReadSensor_delay1ms, то есть только после старта шедулера.
static bool        keyOnOffReady = false;
static uint32_t    keyReleasedAt = 0;

void setup() {
	// 1. Инициализация
	// Баг разводки дуе (вероятность). Есть проблема с инициализацией spi.  Ручками прописываем
	// https://groups.google.com/a/arduino.cc/forum/#!topic/developers/0PUzlnr7948
	// http://forum.arduino.cc/index.php?topic=243778.0;nowap
	pinMode(PIN_SPI_SS0,INPUT_PULLUP);          // Eth Pin 77
	pinMode(PIN_SPI_SS1,INPUT_PULLUP);          // SD Pin  87
	pinMode(PIN_SPI_CS_SD,INPUT_PULLUP);        // сигнал CS управление SD картой
	pinMode(PIN_SPI_CS_W5XXX,INPUT_PULLUP);     // сигнал CS управление сетевым чипом

#ifdef SPI_FLASH
	pinMode(PIN_SPI_CS_FLASH,INPUT_PULLUP);     // сигнал CS управление чипом флеш памяти
	pinMode(PIN_SPI_CS_FLASH,OUTPUT);           // сигнал CS управление чипом флеш памяти
#endif
	digitalWriteDirect(PIN_SPI_CS_SD,HIGH);     // все устройства SPI выключены (драйверы шины их ещё не настроили - spiBusDeselectAll рано)
	digitalWriteDirect(PIN_SPI_CS_W5XXX,HIGH);
#ifdef SPI_FLASH
	digitalWriteDirect(PIN_SPI_CS_FLASH,HIGH);
#endif
	// Отключить питание (VUSB) на Native USB
	Set_bits(UOTGHS->UOTGHS_CTRL, UOTGHS_CTRL_VBUSHWC);
	PIO_Configure(PIOB, PIO_OUTPUT_0, PIO_PB10A_UOTGVBOF, PIO_DEFAULT);
	PIOB->PIO_CODR = PIO_PB10A_UOTGVBOF; // =0
	//

#ifdef POWER_CONTROL                        // Включение питания платы если необходимо НАДП здесь, иначе I2C память рабоать не будет
	pinMode(PIN_POWER_ON,OUTPUT);
	digitalWriteDirect(PIN_POWER_ON, LOW);
#endif

	// Борьба с зависшими устройствами на шине  I2C (в первую очередь часы) неудачный сброс
	Recover_I2C_bus();
	delay(1);

	// Настройка сервисных выводов
	pinMode(PIN_KEY1, INPUT_PULLUP);        // Кнопка 1
	pinMode(PIN_BEEP, OUTPUT);              // Выход на пищалку
	pinMode(PIN_LED_OK, OUTPUT);            // Зелёный светодиод, инверсный: LOW - горит
	digitalWriteDirect(PIN_BEEP,LOW);       // Выключить пищалку
	digitalWriteDirect(PIN_LED_OK,HIGH);    // Выключить светодиод

	// Выбор режима загрузки по кнопке KEY1. Самое раннее место, где это возможно:
	// от сброса прошли единицы миллисекунд, а пищалка со светодиодом уже настроены,
	// значит выбранный режим можно подтвердить оператору сигналом. Дальше по setup
	// точки нет: до шага 3 проходят журнал со сканированием памяти и повторы I2C,
	// и сколько там времени - заранее неизвестно.
	// GetTickCount() - миллисекунды с подачи питания, до старта шедулера тоже идёт.
	if(!digitalReadDirect(PIN_KEY1)) {
		keyWasDownAtBoot = true;
		uint32_t t0 = GetTickCount();
		// КОРОТКИЙ СИГНАЛ СРАЗУ: "кнопка видна, отсчёт пошёл, отпускай -
		// будет встроенная морда". Без него оператор держит кнопку вслепую:
		// отсчёт идёт от момента, когда загрузка дошла сюда, а момент этот
		// ничем не показан.
		//
		// Эти 60 мс лежат ВНУТРИ окна замера, поэтому любое засчитанное
		// нажатие не короче сигнала, и порог KEY_PRESS_MIN на практике ни на
		// что не влияет: дребезг отсекается уже тем, что кнопка была нажата в
		// момент опроса. Порог оставлен контрактом key_boot_mode и покрыт
		// проверками на хосте.
		digitalWriteDirect(PIN_BEEP, HIGH);
		digitalWriteDirect(PIN_LED_OK, LOW);
		delay(TIME_BEEP_KEY_START);
		digitalWriteDirect(PIN_BEEP, LOW);
		digitalWriteDirect(PIN_LED_OK, HIGH);
		do {
			Watchdog_AliveHere();           // до шедулера сбрасывает вачдог напрямую
			delay(10);
			keyBootHeld = GetTickCount() - t0;
		} while(!digitalReadDirect(PIN_KEY1) && keyBootHeld < KEY_HOLD_SAFENET);
		keyBootMode = key_boot_mode(keyBootHeld);
		// ДЛИННЫЙ СИГНАЛ - только когда додержали до аварийного режима. Звучит
		// в момент выбора, пока кнопку ещё держат: "взведено, можно отпускать".
		// После отпускания не пищит ничто: один сигнал - встроенная морда, два
		// (короткий, затем длинный) - safeNetwork. Прежде оба режима давали по
		// одному писку, и различались они только длительностью, которую на слух
		// сравнить не с чем.
		if(keyBootMode == KEY_BOOT_SAFENET) {
			digitalWriteDirect(PIN_BEEP, HIGH);
			digitalWriteDirect(PIN_LED_OK, LOW);
			delay(TIME_BEEP_KEY_SAFENET);
			digitalWriteDirect(PIN_BEEP, LOW);
			digitalWriteDirect(PIN_LED_OK, HIGH);
		}
	}

	// 2. Инициализация журнала
	uint8_t b;
	uint8_t ret = eepromI2C.read(I2C_COUNT_EEPROM, &b, 1);
	if(ret == 0 && b != I2C_COUNT_EEPROM_HEADER && b != 0xFF) ret = 0xFF;
	SerialDbg.begin(UART_SPEED);                   // Если надо инициализировать отладочный порт
	// Подтверждение чужого или молчащего чипа памяти принимается ПО ФРОНТУ:
	// нужно отпускание и новое нажатие. По уровню нельзя - кнопкой на старте
	// теперь выбирают режим загрузки, и удержание ради safeNetwork молча
	// принимало бы чужую микросхему, записав в неё заголовок.
	bool keyReleasedHere = !keyWasDownAtBoot;
	while(ret) {
		SerialDbg.print("Wrong I2C EEPROM or setup, release and press KEY[D");
		SerialDbg.print(PIN_KEY1);
		SerialDbg.println("] to continue...");
		if(!keyReleasedHere) keyReleasedHere = digitalReadDirect(PIN_KEY1);
		else if(!digitalReadDirect(PIN_KEY1)) {
			Watchdog_AliveHere();
			b = I2C_COUNT_EEPROM_HEADER;
			ret = eepromI2C.write(I2C_COUNT_EEPROM, &b, 1);
			if(ret) {
				SerialDbg.print("Error ");
				SerialDbg.print(ret);
				SerialDbg.println(" write to EEPROM!");
			} else SerialDbg.println("Wait...");
			while(1) ;
		}
		for(uint8_t i = 0; i < 1000 / TIME_LED_ERR; i++) {
			digitalWriteDirect(PIN_BEEP, i & 1);
			digitalWriteDirect(PIN_LED_OK, i & 1);
			delay(TIME_LED_ERR);
			// Отпускание ловим здесь, а не раз в оборот: иначе короткое
			// "отпустил-нажал" попадало бы между опросами и терялось.
			if(!keyReleasedHere) keyReleasedHere = digitalReadDirect(PIN_KEY1);
		}
	}
	i2cMemCheck = i2c_check_memory();
	journal.Init();
	// Сообщение только здесь: до Init журнал писать некуда.
	// Строки КОРОТКИЕ и раздельные: jprintf собирает одну строку в pbuf
	// размером PRINTF_BUF (256 байт), а кириллица в utf-8 занимает два байта
	// на букву. Длинный многострочный формат обрезается молча - проверено на
	// стенде, сообщение оборвалось на полуслове.
	if(i2cMemCheck == I2C_MEM_TOO_SMALL) {
		journal.jprintf("$ERROR I2C memory: выбран I2C_FRAM_128KB,\n");
		journal.jprintf(" но второй банк (0x%X) не отвечает - на плате чип на 64 КБ.\n", I2C_ADR_EEPROM + 1);
		journal.jprintf(" Журнал объявлен длиной %d, вдвое больше чипа.\n", JOURNAL_LEN);
		journal.jprintf(" Обмен за границей 65536 не проходит.\n");
		journal.jprintf(" Выберите I2C_EEPROM_64KB в Config.h и перезалейте.\n");
	} else if(i2cMemCheck == I2C_MEM_TOO_BIG) {
		journal.jprintf("I2C memory: отвечает второй банк (0x%X),\n", I2C_ADR_EEPROM + 1);
		journal.jprintf(" на плате FM24V10 на 128 КБ, а выбран I2C_EEPROM_64KB.\n");
		journal.jprintf(" Используется половина чипа, журнал %d вместо 122880.\n", JOURNAL_LEN);
		journal.jprintf(" Запись ждёт цикл, которого у FRAM нет.\n");
	}
#ifdef POWER_CONTROL
	delay(200);  // Не понятно но без нее иногда на старте срабатывает вачдог.  возможно проблема с буфером
#endif
#ifdef DEMO
	journal.jprintf("DEMO - DEMO - DEMO - DEMO - DEMO - DEMO - DEMO\n");
#endif
#ifdef TEST_BOARD
	journal.jprintf("\n---> TEST BOARD!!!\n\n");
#endif
	journal.jprintf("Firmware version: %s\n",VERSION);
	showID();                                                                  // информация о чипе
	getIDchip((char*)Socket[0].inBuf);
	journal.jprintf("Chip ID SAM3X8E: %s\n", Socket[0].inBuf);// информация об серийном номере чипа
	// Причина прошлой перезагрузки и счёт аварийных подряд - одно решение для всех причин (С-18)
	static_assert(RSTC_SOFTWARE_RESET == (RST_SOFTWARE << RSTC_SR_RSTTYP_Pos) && RSTC_USER_RESET == (RST_USER << RSTC_SR_RSTTYP_Pos), "RST_* - это RSTC_SR.RSTTYP");
	BootRegs boot_regs = { GPBR->SYS_GPBR[GPBR_RTOS], GPBR->SYS_GPBR[GPBR_REASON], GPBR->SYS_GPBR[GPBR_BOOT] };
	const BootInfo boot = Boot_Decide(&boot_regs, rstc_get_reset_cause(RSTC) >> RSTC_SR_RSTTYP_Pos);
	GPBR->SYS_GPBR[GPBR_RTOS]   = boot_regs.rtos;
	GPBR->SYS_GPBR[GPBR_REASON] = boot_regs.reason;
	GPBR->SYS_GPBR[GPBR_BOOT]   = boot_regs.boot;
	lastErrorFreeRtosCode = boot.rtos;
	journal.jprintf("Last reason for reset SAM3x: %s\n", ResetCause());
	journal.jprintf("Last FreeRTOS task + error: 0x%04x", lastErrorFreeRtosCode);
	if(GPBR->SYS_GPBR[GPBR_DBG]) journal.jprintf(" (%d)", (int)(GPBR->SYS_GPBR[GPBR_DBG]));
	if(GPBR->SYS_GPBR[GPBR_WEBDBG]) journal.jprintf(" Web(%d)", (int)(GPBR->SYS_GPBR[GPBR_WEBDBG]));
	journal.jprintf("\n");
	if(Boot_Abnormal(boot.cause)) {
		const char *detail = boot.cause == BOOT_STUCK ? (boot.task < WDT_TASK_COUNT ? wdtNames[boot.task] : "?")
		                   : boot.cause == BOOT_KERNEL ? ResetCause() : "";
		journal.jprintf("$Аварийная перезагрузка: %s%s%s, %d подряд (предел %d)\n",
		                Boot_CauseText(boot.cause), *detail ? " " : "", detail, boot.resets, BOOT_MAX_RESETS);
	}

#ifdef PIN_LED1                            // Включение (точнее индикация) питания платы если необходимо
	pinMode(PIN_LED1,OUTPUT);
	digitalWriteDirect(PIN_LED1, HIGH);
#endif
#ifdef POWER_CONTROL
	journal.jprintf("Power +5V, +3.3V on board: ON\n");
#endif

	SupplyMonitorON(SUPC_SMMR_SMTH_3_0V);           // включение монитора питания

#ifdef DRV_EEV_L9333                     // Контроль за работой драйвера ЭРВ
	pinMode(PIN_STEP_DIAG,INPUT_PULLUP);
	journal.jprintf("Control EEV driver L9333: ON \n");
#endif


	// 3. Инициализация и проверка шины i2c
	journal.jprintf("1. Setting and checking I2C devices . . .\n");

	uint8_t eepStatus=0;
	// При I2C_MEM_TOO_SMALL ошибка журнала объяснена: чип меньше объявленного.
	// Переподнимать шину бессмысленно - это 1.5 с пауз и три ложных сообщения.
	if(journal.get_err() && i2cMemCheck != I2C_MEM_TOO_SMALL) { // I2C память и журнал в ней уже пытались инициализировать
		Wire.begin();
		for(uint8_t i=0; i<I2C_NUM_INIT; i++ )
		{
			if ((eepStatus=eepromI2C.begin(I2C_SPEED))>0)    // переходим на I2C_SPEED и пытаемся инициализировать
			{
				journal.jprintf("$ERROR - I2C mem failed, status = %d\n", eepStatus);
#ifdef POWER_CONTROL
				digitalWriteDirect(PIN_POWER_ON, HIGH);
				digitalWriteDirect(PIN_LED1, LOW);
				_delay(2000);
				digitalWriteDirect(PIN_POWER_ON, LOW);
				digitalWriteDirect(PIN_LED1, HIGH);
				_delay(500);
				Wire.begin();
#else
				Wire.begin();
				_delay(500);
#endif
				Watchdog_AliveHere();                       // Сбросить вачдог
			}  else break;   // Все хорошо
		} // for
	}
	if(eepStatus)  // если I2C память не инициализирована, делаем попытку запустится на малой частоте один раз!
	{
		if((eepStatus=eepromI2C.begin(twiClock100kHz))>0) {
			journal.jprintf("$ERROR - I2C mem init failed on speed %d kHz, status = %d\n",twiClock100kHz/1000,eepStatus);
			eepromI2C.begin(I2C_SPEED);
			goto x_I2C_init_std_message;
		} else journal.jprintf("I2C bus low speed, init on %d kHz - OK\n",twiClock100kHz/1000);
	} else {
x_I2C_init_std_message:
		journal.jprintf("I2C init on %d kHz - OK\n",I2C_SPEED/1000);
	}

	// Сканирование шины i2c
	{
		byte error, address;
		const byte start_address = 8;       // lower addresses are reserved to prevent conflicts with other protocols
		const byte end_address = 119;       // higher addresses unlock other modes, like 10-bit addressing

		for(address = start_address; address < end_address; address++ )
		{
#ifdef ONEWIRE_DS2482         // если есть мост
			if(address == I2C_ADR_DS2482) {
				error = OneWireBus.Init();
			} else
#endif
			{
				Wire.beginTransmission(address); error = Wire.endTransmission();
			}
			if(error==0)
			{
				journal.jprintf("I2C device found at address %s",byteToHex(address));
				switch (address)
				{
#ifdef ONEWIRE_DS2482
				case I2C_ADR_DS2482_4:
				case I2C_ADR_DS2482_3:
				case I2C_ADR_DS2482_2:
				case I2C_ADR_DS2482:  		journal.jprintf(" - OneWire DS2482-100 bus: %d\n", address - I2C_ADR_DS2482 + 1); break;
#endif
#if I2C_FRAM_MEMORY == 1
				case I2C_ADR_EEPROM:	journal.jprintf(" - FRAM FM24V%02d\n", I2C_MEMORY_TOTAL*10/1024); break;
#if I2C_MEMORY_TOTAL != I2C_SIZE_EEPROM
				case I2C_ADR_EEPROM+1:	journal.jprintf(" - FRAM second 64k page\n"); break;
#endif
#else
				case I2C_ADR_EEPROM:	journal.jprintf(" - EEPROM AT24C%d\n", I2C_SIZE_EEPROM);break; // 0x50 возможны варианты
#if I2C_MEMORY_TOTAL != I2C_SIZE_EEPROM
				case I2C_ADR_EEPROM+1:	journal.jprintf(" - EEPROM second 64k page\n"); break;
#endif
#endif
				case I2C_ADR_RTC   :		journal.jprintf(" - RTC DS3231\n"); break; // 0x68
				default            :		journal.jprintf(" - Unknow\n"); break; // не определенный тип
				}
				_delay(100);
			}
		} // for
	} //  (eepStatus==0)

#ifndef ONEWIRE_DS2482         // если нет моста
	if(OneWireBus.Init()) journal.jprintf("Error init 1-Wire: %d\n", OneWireBus.GetLastErr());
	else journal.jprintf("1-Wire init Ok\n");
#endif



	// 4. Инициализировать основной класс
	journal.jprintf("2. Init %s main class . . .\n",(char*)nameHeatPump);
	HP.initHeatPump();                           // Основной класс
	// Ошибку поднимаем ТОЛЬКО здесь: set_Error пишет в HP, а initHeatPump выше
	// обнуляет HP.error - поднятая раньше пропала бы. Ошибка нужна, чтобы
	// расхождение было видно в морде и в get_error, а не только в журнале.
	// Второй случай (чип больше объявленного) ошибкой не считается: всё
	// работает, просто половина микросхемы простаивает.
	if(i2cMemCheck == I2C_MEM_TOO_SMALL) set_Error(ERR_I2C_MEM_CONF, (char*)"I2C memory");

	// 5. Режим загрузки, выбранный кнопкой KEY1 (замер сделан в начале setup)
	//    короткое нажатие  - встроенная морда из кода
	//    удержание от KEY_HOLD_SAFENET - ещё и safeNetwork: сеть по умолчанию
	//    192.168.0.177, шлюз 192.168.0.1, пароль на вход в морду не спрашивается
	// Кнопка читается не здесь: время от сброса до этой точки непостоянно
	// (журнал сканирует память, I2C повторяется по 2 с), и длительность удержания
	// тут уже не измерить. Замер сделан в начале setup, в HP переносится только
	// сейчас: initHeatPump выше сам обнуляет safeNetwork.
	journal.jprintf("3. Boot mode key . . .\n");
	HP.safeNetwork  = keyBootMode == KEY_BOOT_SAFENET;
	HP.forceMinWeb  = keyBootMode != KEY_BOOT_NONE;    // морда из кода в обоих режимах
	// Состояние кнопки печатается отдельным словом: по одному "held 0 ms"
	// не видно, не нажимали её или нажали короче KEY_PRESS_MIN, а разбираться
	// в этом придётся по чужому журналу и без платы под рукой.
	journal.jprintf(" Key %s, held %u ms: safeNetwork %s, internal web %s\n",
			keyWasDownAtBoot ? "pressed" : "not pressed", (unsigned int)keyBootHeld,
			HP.safeNetwork ? "ON" : "OFF", HP.forceMinWeb ? "ON" : "OFF");
	// Кнопку держат - вкл/выкл запрещаем до первого отпускания
	// (опрос в vReadSensor_delay1ms)
	keyOnOffReady = !keyWasDownAtBoot;

	// 6. Чтение ЕЕПРОМ, надо раньше чем инициализация носителей веб морды, что бы знать откуда грузить
	journal.jprintf("4. Load data from I2C memory . . .\n");
	if(HP.load_motoHour()==ERR_HEADER2_EEPROM)           // Загрузить счетчики ТН,
	{
		journal.jprintf(" I2C memory is empty, a default settings will be used!\n");
		HP.save_motoHour();
	} else {
		HP.load_all((uint8_t *)Socket[0].outBuf);     // читатель выбирается по версии, см. SettingsLegacy.cpp
	}
	// обновить хеш для пользователей
	HP.set_hashUser();
	HP.set_hashAdmin();

	// 7. Инициализация СД карты и запоминание результата 3 попытки
#ifndef NO_SD_CARD
	journal.jprintf("5. Init SD card . . .\n");
	HP.set_fSD(initSD());
	Watchdog_AliveHere();                          // Сбросить вачдог  иногда карта долго инициализируется
#else
	journal.jprintf("5. No SD card in config.\n");
#endif

	// 8. Инициализация spi флеш диска
#ifdef SPI_FLASH
	journal.jprintf("6. Init SPI flash disk . . .\n");
	HP.set_fSPIFlash(initSpiDisk(true));  // проверка диска с выводом инфо
#else
	journal.jprintf("6. No SPI flash in config.\n");
#endif
	digitalWriteDirect(PIN_LED_OK, LOW);        // Включить светодиод

	//  HP.set_optionHP((char*)option_WebOnSPIFlash,0);  // Установить принудительно загрузку морды с карточки (надо раскомментировать если грузится из флеш не надо)
	journal.jprintf("Web interface source: ");
	switch (HP.get_SourceWeb())
	{
	case pMIN_WEB:   journal.jprintf("internal\n"); break;
	case pSD_WEB:    journal.jprintf("SD card\n"); break;
	case pFLASH_WEB: journal.jprintf("SPI Flash\n"); break;
	default:         journal.jprintf("unknown\n"); break;
	}

	journal.jprintf("7. Start read ADC sensors\n");
	start_ADC(); // после инициализации HP

	// 10. Сетевые настройки
	journal.jprintf("8. Setting Network . . .\n");
	if(initW5200(true)) {   // Инициализация сети с выводом инфы в консоль
		W5100.getMACAddress((uint8_t *)Socket[0].outBuf);   // SEM-OK: setup, планировщик ещё не запущен
		journal.jprintf(" MAC: %s\n", MAC2String((uint8_t *)Socket[0].outBuf));
	}
	digitalWriteDirect(PIN_BEEP,LOW);          // Выключить пищалку

	// 11. Разбираемся со всеми часами и синхронизацией
	journal.jprintf("9. Setting time and clock . . .\n");
	set_time();

	// 12. Инициалазация уведомлений
	journal.jprintf("10. Message update IP from DNS . . .\n");
	HP.message.dnsUpdate();

	// 13. Инициалазация MQTT
#ifdef MQTT
	journal.jprintf("11. Client MQTT update IP from DNS . . .\n");
	HP.clMQTT.dnsUpdate();
#else
	journal.jprintf("11. Client MQTT disabled by config\n");
#endif

	// 14. Инициалазация Statistics
	journal.jprintf("12. Statistics ");
	if(HP.get_fSD()) {
		journal.jprintf("writing on SD card\n");
		Stats.Init();             // Инициализовать статистику
	} else journal.jprintf("not available\n");

	int8_t _profile = HP.Schdlr.calc_active_profile();
	if(_profile > SCHDLR_Profile_off && _profile != HP.Prof.get_idProfile()) {
		HP.Prof.load(_profile);
		HP.set_profile();
		journal.jprintf("Profile changed to #%d\n", _profile);
	}

	if(HP.get_SaveON()==0)  HP.set_HP_OFF();    // Сбросить флаг включение ТН если стоит соответсвующий флаг в опциях
	journal.jprintf("13. Delayed start %s: ",(char*)nameHeatPump); if(HP.get_HP_ON()) journal.jprintf("YES\n"); else journal.jprintf("NO\n");

#ifdef NEXTION
	journal.jprintf("14. Nextion display: ");
	if(GETBIT(HP.Option.flags, fNextion)) {
		myNextion.init();
	} else {
		journal.jprintf(" Disabled\n");
	}
#else
	journal.jprintf("14. Nextion display is absent in config\n");
#endif

#ifdef TEST_BOARD
	// Scan oneWire - TEST.
#endif

	// ---- Создание задач FreeRTOS ----
	journal.jprintf("15. Create tasks FreeRTOS . . .\n");
	Diag.mRTOS=236;  //расчет памяти для задач 236 - размер данных шедулера, каждая задача требует 64 байта+ стек (он в словах!!)
	Diag.mRTOS=Diag.mRTOS+64+4*configMINIMAL_STACK_SIZE;  // задача бездействия

	// ПРИОРИТЕТ 4 Высший приоритет датчики читаются всегда и шаговик ЭРВ всегда шагает если нужно
	if (xTaskCreate(vReadSensor,"ReadSensor",STACK_vReadSensor,NULL,4,&Tasks.xHandleReadSensor)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY)    set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	Diag.mRTOS=Diag.mRTOS+64+4*STACK_vReadSensor;// 200

#ifdef EEV_DEF
	if (xTaskCreate(vUpdateStepperEEV,"StepperEEV",STACK_vUpdateStepperEEV,NULL,4,&HP.dEEV.stepperEEV.xHandleStepperEEV)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY)  set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	Diag.mRTOS=Diag.mRTOS+64+4*STACK_vUpdateStepperEEV; // 50, 100, 150
	HP.dEEV.stepperEEV.xCommandQueue = xQueueCreate( EEV_QUEUE, sizeof( int ) );  // Создать очередь комманд для ЭРВ
#endif

	// ПРИОРИТЕТ 3 Очень высокий приоритет Выполнение команд управления (разбор очереди комманд) - должен быть выше чем задачи обновления ТН и ЭРВ
	if(xTaskCreate(vUpdateCommand,"CommandHP", STACK_vUpdateCommand,NULL,3,&Tasks.xHandleUpdateCommand)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY)     set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	Diag.mRTOS=Diag.mRTOS+64+4*STACK_vUpdateCommand;// 200


	// ПРИОРИТЕТ 2 высокий - это управление ТН управление ЭРВ, сервис
	if(xTaskCreate(vServiceHP, "ServiceHP", STACK_vServiceHP, NULL, 2, &Tasks.xHandleSericeHP)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	Diag.mRTOS=Diag.mRTOS+64+4*STACK_vServiceHP;// 200

	Tasks.xCommandSemaphore = xSemaphoreCreateMutex();  // мьютекс: захват и отдача всегда в одной задаче                       // Создание семафора
	if (Tasks.xCommandSemaphore==NULL) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);

	if (xTaskCreate(vUpdate,"UpdateHP",STACK_vUpdate,NULL,2,&Tasks.xHandleUpdate)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY)    set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	Diag.mRTOS=Diag.mRTOS+64+4*STACK_vUpdate;// см. STACK_vUpdate в Config.h
	Tasks.Task_vUpdate_run = false;

#ifdef EEV_DEF
	if (xTaskCreate(vUpdateEEV,"UpdateEEV",STACK_vUpdateEEV,NULL,2,&Tasks.xHandleUpdateEEV)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY)     set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	Diag.mRTOS=Diag.mRTOS+64+4*STACK_vUpdateEEV;
#endif  

	// ПРИОРИТЕТ 1 средний - обслуживание вебморды в несколько потоков и дисплея Nextion
	// ВНИМАНИЕ первый поток должен иметь больший стек для обработки фоновых сетевых задач
	// 1 - поток
	if ( xTaskCreate(vWeb0,"Web0", STACK_vWebX+14,NULL,1,&Tasks.xHandleUpdateWeb0)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	Diag.mRTOS=Diag.mRTOS+64+4*(STACK_vWebX+14);
#if W5200_THREAD >= 2 // - потока
	if ( xTaskCreate(vWeb1,"Web1", STACK_vWebX,NULL,1,&Tasks.xHandleUpdateWeb1)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	Diag.mRTOS=Diag.mRTOS+64+4*STACK_vWebX;
#endif
#if W5200_THREAD >= 3 // - потока
	if ( xTaskCreate(vWeb2,"Web2", STACK_vWebX,NULL,1,&Tasks.xHandleUpdateWeb2)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	Diag.mRTOS=Diag.mRTOS+64+4*STACK_vWebX;
#endif
#if W5200_THREAD >= 4 // - потока
	if ( xTaskCreate(vWeb3,"Web3", STACK_vWebX,NULL,1,&Tasks.xHandleUpdateWeb3)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	Diag.mRTOS=Diag.mRTOS+64+4*STACK_vWebX;
#endif

	xWebThreadSemaphore = xSemaphoreCreateMutex();   // мьютекс: сеть + шина SPI               // Создание мютекса
	if (xWebThreadSemaphore==NULL) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	xI2CSemaphore = xSemaphoreCreateMutex();         // мьютекс: FRAM, часы, мастер OneWire                     // Создание мютекса
	if (xI2CSemaphore==NULL) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	journal.CreateLocks();                           // замки журнала: L1 строки/кольцо, L2 писатель EEPROM
	// Дополнительные семафоры (почему то именно здесь) Создается когда есть Modbus
	if(Modbus.get_present())
	{
		xModbusSemaphore = xSemaphoreCreateMutex();   // мьютекс: нужна передача приоритета                       // Создание мютекса
		if (xModbusSemaphore==NULL) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	}

	journal.jprintf(" Create tasks - OK, size %d bytes\n",(int)(Diag.mRTOS));

	if(HP.get_HP_ON()>0) {                           // если надо запустить ТН - отложенный старт
		if(Boot_NoAutoStart(boot)) {
			journal.jprintf("$Delayed start canceled: %d abnormal resets in a row\n", boot.resets);
			// Поле ошибки занимает первая (Error_Claim), а при старте его часто уже заняла ошибка инициализации
			// (на стенде -39 «инвертор не найден»): причину отмены не было бы видно в морде. Она важнее - вытесняем.
			// Планировщик ещё не запущен, прямая запись безопасна; прежняя ошибка осталась в журнале.
			HP.drop_error_at_boot();
			set_Error(ERR_RESET_LOOP, (char*)nameFREERTOS);
		} else HP.sendCommand(pRESTART);
	}

	journal.jprintf("17. Information:\n");
	freeRamShow();
	Diag.startRAM=freeRam()-Diag.mRTOS;   // оценка свободной памяти до пуска шедулера, поправка на 1054 байта
	journal.jprintf("FREE MEMORY %d bytes\n",(int)(Diag.startRAM));
	journal.jprintf("Temperature DS2331: %.2d\n",getTemp_RtcI2C());
	if(Is_otg_vbus_high()) journal.jprintf("USB connected\n");
	journal.jprintf("Start FreeRTOS scheduler :-))\n");
	journal.jprintf("READY ----------------------\n");
	eepromI2C.use_RTOS_delay = 1;       //vad711
	//
	// Наблюдатель сторожевого таймера. Пределы молчания заданы с большим запасом
	// относительно периодов задач: ложная перезагрузка в поле хуже, чем поздно
	// обнаруженное зависание. Ужесточать по результатам эксплуатации.
	{
		// Наблюдатель считает время в тиках FreeRTOS: vServiceHP передаёт ему
		// xTaskGetTickCount(), а тот начинается с нуля в момент старта шедулера.
		// Здесь, до старта, это ноль и есть.
		//
		// Не GetTickCount: это миллисекунды с подачи питания, то есть ДРУГАЯ шкала.
		// Разность now_ms - last_seen_ms на первом же тике наблюдателя ушла бы в минус и по
		// модулю 2^32 превратилась бы в ~4294958 с ("молчит 4294958.2 с" в журнале),
		// испортила бы замер запаса, а при задержке первой отметки дала бы ложную
		// перезагрузку на старте.
		uint32_t now = 0;
		Watchdog_Init(&wdtTasks[WDT_ReadSensor], 30000,  now);  // период 2 с, внутри ожидание DS18B20 и SDM
		Watchdog_Init(&wdtTasks[WDT_ServiceHP],  10000,  now);  // период 50 мс / 1 с
		Watchdog_Init(&wdtTasks[WDT_UpdateHP],   120000, now);  // период 5-10 с, внутри пуск компрессора с задержками
#ifdef EEV_DEF
		Watchdog_Init(&wdtTasks[WDT_UpdateEEV],  60000,  now);  // период 1-4 с
#else
		Watchdog_Init(&wdtTasks[WDT_UpdateEEV],  0,      now);  // без ЭРВ задач нет: предел 0 = не наблюдается (иначе перезагрузка каждую минуту)
#endif
		Watchdog_Init(&wdtTasks[WDT_CommandHP],  120000, now);  // событийная, runCommand бывает долгой
#ifdef EEV_DEF
		Watchdog_Init(&wdtTasks[WDT_StepperEEV], 60000,  now);  // событийная
#else
		Watchdog_Init(&wdtTasks[WDT_StepperEEV], 0,      now);  // без ЭРВ задач нет: не наблюдается
#endif
		Watchdog_Init(&wdtTasks[WDT_Web0],       120000, now);  // почта, DHCP, MQTT - законно долгие
		Watchdog_Init(&wdtTasks[WDT_Web1],       60000,  now);  // отдача файлов с SD
		Watchdog_Init(&wdtTasks[WDT_Web2],       60000,  now);
		Watchdog_Init(&wdtTasks[WDT_Web3],       60000,  now);
		wdtHandles[WDT_ReadSensor] = &Tasks.xHandleReadSensor;
		wdtHandles[WDT_ServiceHP]  = &Tasks.xHandleSericeHP;
		wdtHandles[WDT_UpdateHP]   = &Tasks.xHandleUpdate;
		wdtHandles[WDT_UpdateEEV]  = &Tasks.xHandleUpdateEEV;
		wdtHandles[WDT_CommandHP]  = &Tasks.xHandleUpdateCommand;
#ifdef EEV_DEF
		wdtHandles[WDT_StepperEEV] = &HP.dEEV.stepperEEV.xHandleStepperEEV;
#endif
		wdtHandles[WDT_Web0]       = &Tasks.xHandleUpdateWeb0;
		wdtHandles[WDT_Web1]       = &Tasks.xHandleUpdateWeb1;
		wdtHandles[WDT_Web2]       = &Tasks.xHandleUpdateWeb2;
		wdtHandles[WDT_Web3]       = &Tasks.xHandleUpdateWeb3;
		wdtReady = 1;
	}
	vTaskStartScheduler();              // СТАРТ !!
	journal.jprintf("FreeRTOS FAILURE!\n");
}

void loop() {}

// ---- З А Д А Ч И ----
// Хук простоя FreeRTOS - поток с минимальным приоритетом: мигание светодиода и звуковой сигнал ошибки
static bool Error_Beep_confirmed = false;

extern "C" void vApplicationIdleHook(void)  // FreeRTOS expects C linkage
{
	static unsigned long countLED = 0;
	static unsigned long countBeep = 0;
	static unsigned long countLED1 = 0;

	// Сброс WDT отсюда УБРАН. Idle-хук выполняется, пока хоть одна задача спит,
	// поэтому сброс вачдога здесь означал, что зависание любой рабочей задачи
	// не обнаруживается: ТН оставался с работающим компрессором без надзора.
	// Теперь вачдог сбрасывает только Watchdog_Supervise из vServiceHP и только если
	// отметились все незаприостановленные задачи.
	register unsigned long ticks = GetTickCount();
	// Светодиод мигание в зависимости от ошибки и подача звукового сигнала при ошибке
	if(HP.get_errcode() != OK) {          // Ошибка
		if(ticks - countLED > TIME_LED_ERR) {
			digitalWriteDirect(PIN_LED_OK, !digitalReadDirect(PIN_LED_OK));
			countLED = ticks;
		}
		if(HP.get_Beep() && !Error_Beep_confirmed && ticks - countBeep > TIME_BEEP_ERR) {
			digitalWriteDirect(PIN_BEEP, !digitalReadDirect(PIN_BEEP)); // звуковой сигнал
			countBeep = ticks;
		}
	} else if(ticks - countLED > TIME_LED_OK) {   // Ошибок нет и время пришло
		Error_Beep_confirmed = false;
		digitalWriteDirect(PIN_BEEP, LOW);
		digitalWriteDirect(PIN_LED_OK, !digitalReadDirect(PIN_LED_OK));
		countLED = ticks;
	}
#ifdef PIN_LED1
	// Красный: ровно горит в обычной работе, мигает в специальном режиме,
	// выбранном кнопкой KEY1 при включении. Смысл - чтобы спецрежим был виден
	// от платы, не заходя в журнал и не поднимая морду: в аварию идут как раз
	// тогда, когда морда недоступна. Период равен периоду зелёного в норме,
	// обоснование - при TIME_LED1_MODE в Constant.h. Режим за время работы не
	// меняется, но ветку восстановления держим: в setup красный гасится при
	// перезапуске питания платы, и ровное свечение обязано вернуться.
	if(HP.safeNetwork || HP.forceMinWeb) {
		if(ticks - countLED1 > TIME_LED1_MODE) {
			digitalWriteDirect(PIN_LED1, !digitalReadDirect(PIN_LED1));
			countLED1 = ticks;
		}
	} else if(!digitalReadDirect(PIN_LED1)) digitalWriteDirect(PIN_LED1, HIGH);
#endif
	pmc_enable_sleepmode(0);
}

// ---- W E B ----
// Задача обслуживания web сервера
// Сюда надо пихать все что связано с сетью иначе конфликты неизбежны
// Здесь также обслуживается посылка уведомлений MQTT
// Первый поток веб сервера - дополнительно нагружен различными сервисами
void vWeb0(void *)
{
	static unsigned long timeResetW5200 = 0;
	static unsigned long thisTime;
	static unsigned long resW5200 = 0;
	static unsigned long iniW5200 = 0;
	static unsigned long pingt = 0;
#ifdef MQTT
	static unsigned long narmont=0;
	static unsigned long mqttt=0;
#endif
	static boolean active = false;  // ФЛАГ Одно дополнительное действие за один цикл - распределяем нагрузку, если действие проделано то active = false и новый цикл

	HP.timeNTP = thisTime = xTaskGetTickCount();        // В первый момент не обновляем
	for(;;)
	{
		Watchdog_Beat(WDT_Web0);   // отметка живости для наблюдателя
		WEB_STORE_DEBUG_INFO(1);
		web_server(MAIN_WEB_TASK);
		WEB_STORE_DEBUG_INFO(2);
		active = true;                                                         // Можно работать в этом цикле (дополнительная нагрузка на поток)
		vTaskDelay(TIME_WEB_SERVER / portTICK_PERIOD_MS); // задержка чтения уменьшаем загрузку процессора

		// СЕРВИС: Этот поток работает на любых настройках, по этому сюда ставим работу с сетью
		HP.message.sendMessage();                                            // Отработать отсылку сообщений (внутри скрыта задержка после включения)
		active = HP.message.dnsUpdate();                                     // Обновить адреса через dns если надо, dnsUpdate() возвращает true если обновления не было
#ifdef MQTT
		if(active) active=HP.clMQTT.dnsUpdate();                             // Обновить адреса через dns если надо для MQTT если обновления не было то возвращает true
#endif
		if(xTaskGetTickCount() - thisTime > WEB0_OTHER_JOB_PERIOD)
		{
			thisTime = xTaskGetTickCount();                                      // Запомнить тики
			// 1. Проверка захваченного семафора сети ожидаем  3 времен W5200_TIME_WAIT если мютекса не получаем то сбрасываем мютекс
			// Проверка доступности замков шин.
			//
			// Замок шины не отдаётся принудительно при неудачном захвате: его держит другая
			// задача, а принудительная выдача ломает взаимное исключение (две задачи окажутся
			// на шине одновременно). Настоящий дедлок поймает сторожевой таймер по отметкам
			// задач: задача перестанет отмечаться, контроллер перезагрузится и запишет в
			// журнал, кто именно встал. Здесь только диагностика: счётчики видны в get_sysInfo.
			// Во время заливки морды замок подолгу держит приём файла: проверку пропускаем, а не ждём до 99 с (Н-25),
			// и сетевую работу ниже (сброс чипа) не начинаем.
			if(webUploadBusy()) active = false;
			else if(SemaphoreTake(xWebThreadSemaphore, (3 * W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {
				journal.jprintf_time("BUSY mutex xWebThread\n");
				active = false;
				Diag.num_resMutexSPI++;
			} else SemaphoreGive(xWebThreadSemaphore);

			if(SemaphoreTake(xI2CSemaphore, (3 * I2C_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {
				journal.jprintf_time("BUSY mutex xI2CSemaphore\n");
				Diag.num_resMutexI2C++;
			} else SemaphoreGive(xI2CSemaphore);

			// 2. Чистка сокетов
			if(HP.time_socketRes() > 0) {
				WEB_STORE_DEBUG_INFO(3);
				checkSockStatus();                   // Почистить старые сокеты  если эта позиция включена
			}

			// 3. Сброс сетевого чипа по времени
			if((HP.time_resW5200() > 0) && (active))                             // Сброс W5200 если включен и время подошло
			{
				WEB_STORE_DEBUG_INFO(4);
				resW5200 = xTaskGetTickCount();
				if(timeResetW5200 == 0) timeResetW5200 = resW5200;      // Первая итерация не должна быть сразу
				if(resW5200 - timeResetW5200 > HP.time_resW5200() * 1000UL) {
					journal.jprintf_time("Reset %s by timer . . .\n", nameWiznet);
					HP.sendCommand(pNETWORK);                          // Послать команду сброса и применения сетевых настроек
					timeResetW5200 = resW5200;                         // Запомить время сброса
					active = false;
				}
			}
			// 4. Контроль связи с чипом раз в NETCHIP_TEST_S секунд (галочка net.init): регистры чипа против снимка после инициализации, см. netChipSelfTest.
			// Прежняя проверка читала бит линка PHYCFGR раз в минуту БЕЗ замка шины и не видела чип, у которого линк горит, а IP-стек мёртв.
			if((HP.get_fInitW5200()) && (thisTime - iniW5200 >= NETCHIP_TEST_S * 1000UL) && (active))
			{
				WEB_STORE_DEBUG_INFO(5);
				iniW5200 = thisTime;
				if(!HP.NO_Power) {
					if(netChipSelfTest()) active = false;          // чип сброшен - остальные дела этого цикла отложены
				}
			}
			// 5.Обновление времени 1 раз в сутки или по запросу (HP.timeNTP==0)
			if((HP.timeNTP == 0) || ((HP.get_updateNTP()) && (thisTime - HP.timeNTP > 60 * 60 * 24 * 1000UL) && (active))) // Обновление времени раз в день 60*60*24*1000 в тиках HP.timeNTP==0 признак принудительного обновления
			{
				WEB_STORE_DEBUG_INFO(6);
				HP.timeNTP = thisTime;
				set_time_NTP();                                                 // Обновить время
				active = false;
			}
			// 6. ping сервера если это необходимо
			if((HP.get_pingTime() > 0) && (thisTime - pingt > HP.get_pingTime() * 1000UL) && (active)) {
				WEB_STORE_DEBUG_INFO(7);
				pingt = thisTime;
				pingServer();
				active = false;
			}

#ifdef MQTT                                     // признак использования MQTT
			// 7. Отправка на народный мониторинг
			// Внеочередная отправка по кнопке из веб морды. Это ОТЛАДОЧНЫЙ режим - им пользуются, когда
			// показания не проходят, поэтому интервал здесь НЕ проверяется и жать кнопку можно подряд.
			// Ответственность за частоту на том, кто отлаживает: если с прошлой отправки не прошло
			// TIME_NARMON, сервер ответит INTERVAL...<5m, а за систематические злоупотребления банит прибор.
			// narmont все равно сдвигаем: иначе сразу за ручной отправкой могла бы уйти штатная, которую
			// никто не заказывал. На саму кнопку это не влияет.
			if ((HP.clMQTT.get_NarodMonUse())&&(HP.clMQTT.getTestNARMON())&&(active))
			{
				WEB_STORE_DEBUG_INFO(55);
				HP.clMQTT.clearTestNARMON();
				if(thisTime-narmont<=TIME_NARMON*1000UL) journal.jprintf_time("NarodMon test: less than %d sec since last send, expect INTERVAL error\n",TIME_NARMON);
				narmont=thisTime;
				sendNarodMon2022(true);          // с выводом пакета и ответа сервера в журнал
				active=false;
			}
			// Штатная отправка по времени. Период зависит от режима: после серии
			// неудач подряд отправка уходит на редкий NARMON_SLOW и продолжается
			// там до первой удачи - выключаться ей нельзя, см. updateErrMQTT.
			if ((HP.clMQTT.get_NarodMonUse())&&(thisTime-narmont>(HP.clMQTT.get_NarodMonSlow()?NARMON_SLOW:TIME_NARMON)*1000UL)&&(active))
			{
				WEB_STORE_DEBUG_INFO(55);
				// Посылка данных на народный мониторинг без использования MQTT потребовалось в декабре 2022 года.
				// Если пакет не доставлен (нет связи или нет ответа) - протокол предписывает повторить отправку,
				// когда связь восстановится. Повторяем через NARMON_RETRY, но не чаще: иначе прибор попадет в бан.
				// В редком режиме торопиться некуда: связи нет уже давно, и учащать нечем -
				// ждём полный NARMON_SLOW, как при удачной отправке.
				if(sendNarodMon2022(false) || HP.clMQTT.get_NarodMonSlow()) narmont=thisTime;
				else narmont=thisTime-(TIME_NARMON-NARMON_RETRY)*1000UL;
				active=false;
			}  // if ((HP.clMQTT.get_NarodMonUse()))

			// 8. Отправка на MQTT сервер
			if ((HP.clMQTT.get_MqttUse())&&(thisTime-mqttt>HP.clMQTT.get_ttime()*1000UL)&&(active))// если нужно & время отправки пришло
			{
				WEB_STORE_DEBUG_INFO(56);
				mqttt=thisTime;
				if(HP.clMQTT.get_TSUse()) sendThingSpeak(false);
				else sendMQTT(false);
				active=false;
			}
#endif   // MQTT
			taskYIELD();
		} // if (xTaskGetTickCount()-thisTime>10000)

	} //for
	vTaskDelete( NULL);
}

// Второй поток
void vWeb1(void *)
{
	for(;;) {
		Watchdog_Beat(WDT_Web1);   // отметка живости для наблюдателя
		web_server(1);
		vTaskDelay(TIME_WEB_SERVER / portTICK_PERIOD_MS); // задержка чтения уменьшаем загрузку процессора

	}
	vTaskDelete( NULL);
}
// Третий поток
void vWeb2(void *)
{
	for(;;) {
		Watchdog_Beat(WDT_Web2);   // отметка живости для наблюдателя
		web_server(2);
		vTaskDelay(TIME_WEB_SERVER / portTICK_PERIOD_MS); // задержка чтения уменьшаем загрузку процессора

	}
	vTaskDelete( NULL);
}
// Четвертый поток
void vWeb3(void *)
{
	for(;;) {
		Watchdog_Beat(WDT_Web3);   // отметка живости для наблюдателя
		web_server(3);
		vTaskDelay(TIME_WEB_SERVER / portTICK_PERIOD_MS); // задержка чтения уменьшаем загрузку процессора
	}
	vTaskDelete( NULL);
}

//////////////////////////////////////////////////////////////////////////
// Задача чтения датчиков
void vReadSensor(void *)
{
	static unsigned long readFC = 0;
#ifdef USE_ELECTROMETER_SDM
#if (SDM_READ_PERIOD > 0)
	static unsigned long readSDM = 0;
#endif
#endif
	static uint32_t ttime;
	static uint32_t oldTime = GetTickCount();
	static uint8_t  prtemp = 0;
	
	for(;;) {
		Watchdog_Beat(WDT_ReadSensor);   // отметка живости для наблюдателя
		int8_t i;

		ttime = GetTickCount();
		if(OW_scan_flags == 0) {
#ifndef DEMO  // Если не демо
			prtemp = HP.Prepare_Temp(0);
#endif     // не DEMO
		}
		for(i = 0; i < ANUMBER; i++) HP.sADC[i].Read();                  // Прочитать данные с датчиков давления
		for(i = 0; i < INUMBER; i++) HP.sInput[i].Read();                // Прочитать данные сухой контакт
#ifdef SGENERATOR
		if(GETBIT(HP.Option.flags2, f2BackupPowerAuto)) HP.check_fBackupPower();
#endif
		for(i = 0; i < FNUMBER; i++) HP.sFrequency[i].Read();			// Получить значения датчиков потока

#ifdef USE_ELECTROMETER_SDM   // Опрос состояния счетчика
		  HP.dSDM.get_readState(0); // Основная группа регистров
#endif

		vReadSensor_delay1ms(cDELAY_DS1820 - (int32_t)(GetTickCount() - ttime)); 	// Ожидать время преобразования

		if(OW_scan_flags == 0) {
			uint8_t flags = 0;
			for(i = 0; i < TNUMBER; i++) {                                   // Прочитать данные с температурных датчиков
				if((prtemp & (1<<HP.sTemp[i].get_bus())) == 0) {
					if(HP.sTemp[i].Read() == OK) flags |= HP.sTemp[i].get_setup_flags();
				}
				_delay(1);     												// пауза
			}
			int32_t temp;
			if(GETBIT(flags, fTEMP_as_TIN_average)) { // Расчет средних датчиков для TIN
				temp = 0;
				uint8_t cnt = 0;
				for(i = 0; i < TNUMBER; i++) {
					if(HP.sTemp[i].get_setup_flag(fTEMP_as_TIN_average) && HP.sTemp[i].get_Temp() != STARTTEMP) {
						temp += HP.sTemp[i].get_Temp();
						cnt++;
					}
				}
				if(cnt) temp /= cnt; else temp = STARTTEMP;
			} else temp = STARTTEMP;
			int16_t temp2 = temp;
			if(GETBIT(flags, fTEMP_as_TIN_min)) { // Выбор минимальной температуры для TIN
				for(i = 0; i < TNUMBER; i++) {
					if(HP.sTemp[i].get_setup_flag(fTEMP_as_TIN_min) && temp2 > HP.sTemp[i].get_Temp()) temp2 = HP.sTemp[i].get_Temp();
				}
			}
			if(temp2 != STARTTEMP) HP.sTemp[TIN].set_Temp(temp2);
		}

#ifdef USE_ELECTROMETER_SDM   // Опрос состояния счетчика
#if (SDM_READ_PERIOD > 0)
			if((HP.dSDM.get_present()) && (GetTickCount() - readSDM > SDM_READ_PERIOD)) {
				readSDM=GetTickCount();
				HP.dSDM.get_readState(2);     // Последняя группа регистров ТОК
			}
#endif
#endif

		HP.calculatePower();  // Расчет мощностей и СОР
		Stats.Update();

		vReadSensor_delay1ms((TIME_READ_SENSOR - (int32_t)(GetTickCount() - ttime)) / 2);     // 1. Ожидать время нужное для цикла чтения

		// Вычисление перегрева используются РАЗНЫЕ датчики при нагреве и охлаждении
		// Режим работы определяется по состоянию четырехходового клапана при его отсутвии только нагрев
#ifdef EEV_DEF
		HP.dEEV.set_Overheat(HP.is_heating()); // нагрев(1) или охлаждение(0)
#endif

		//  Опрос состояния инвертора
	#ifdef USE_UPS
		if(!HP.NO_Power)
	#endif
			if((HP.dFC.get_present()) && (GetTickCount() - readFC > FC_TIME_READ)) {
				readFC = GetTickCount();
				HP.dFC.get_readState();
			}

#ifdef DRV_EEV_L9333  // Опрос состяния драйвера ЭРВ
		if (digitalReadDirect(PIN_STEP_DIAG)) // Перечитываем два раза
		{
			vReadSensor_delay1ms(5);
			if (digitalReadDirect(PIN_STEP_DIAG)) set_Error(ERR_DRV_EEV,(char*)__FUNCTION__); // Контроль за работой драйвера ЭРВ
		}
#endif

#ifdef FLOW_CONTROL                // если надо проверяем потоки (защита от отказа насосов) ERR_MIN_FLOW
	if(HP.is_compressor_on())      // Только если компрессор включен 
		for(uint8_t i = 0; i < FNUMBER; i++){   // Проверка потока по каждому датчику
			if(HP.sFrequency[i].get_checkFlow() && HP.sFrequency[i].get_Value() < HP.sFrequency[i].get_minValue()) {     // Поток меньше минимального ошибка останавливаем ТН
				journal.jprintf("%s low flow: %.3f\n",(char*) HP.sFrequency[i].get_name(), (float) HP.sFrequency[i].get_Value() / 1000);
				set_Error(ERR_MIN_FLOW, (char*) HP.sFrequency[i].get_name());
			}
		}
#endif
#ifdef SEVA  //Если определен лепестковый датчик протока - это переливная схема ТН - надо контролировать проток при работе
 	if(HP.dRelay[RPUMPI].get_Relay())                                                                                             // Только если включен насос геоконтура  (PUMP_IN)
		if (HP.sInput[SEVA].get_Input()==SEVA_OFF) set_Error(ERR_SEVA_FLOW,(char*)"SEVA");                                        // Нет протока: ошибка останавливает ТН. НЕ return - это тело задачи, возврат из неё аварийный выход FreeRTOS
#endif

		//  Синхронизация часов с I2C часами если стоит соответсвующий флаг
		if(HP.get_updateI2C())  // если надо обновить часы из I2c
		{
			if(GetTickCount() - oldTime > (uint32_t)TIME_I2C_UPDATE) // время пришло обновляться надо Период синхронизации внутренних часов с I2C часами (сек)
			{
				uint32_t t = TimeToUnixTime(getTime_RtcI2C());       // Прочитать время из часов i2c тут проблема
				if(t) {
					rtcSAM3X8.set_clock(t);                		 // Установить внутренние часы по i2c; интервалы по Uptime_s сдвига не требуют
					journal.jprintf((const char*) "Sync from I2C RTC: %s %s\n", NowDateToStr(), NowTimeToStr());
				} else {
					journal.jprintf("Error read I2C RTC\n");
				}
				oldTime = GetTickCount();
			}
		}
		// Проверка и сброс митекса шины I2C
		// Проверки граничных температур для уведомлений, если разрешено!
		static uint16_t countTEMP = 0;        // Для проверки критических температур для рассылки уведомлений
		if(HP.message.get_fMessageTemp()) {
			if(countTEMP > TIME_MESSAGE_TEMP) {
				countTEMP = 0;
				// Только по исправному и привязанному датчику: у отсутствующего Temp = 0, то есть «холодно» без причины
				if(sensor_usable(HP.sTemp[TIN]) && HP.message.get_mTIN() > HP.sTemp[TIN].get_Temp()) HP.message.setMessage(pMESSAGE_TEMP,
						(char*) "Критическая температура в доме,", HP.sTemp[TIN].get_Temp());
				if(sensor_usable(HP.sTemp[TBOILER]) && HP.message.get_mTBOILER() > HP.sTemp[TBOILER].get_Temp()) HP.message.setMessage(pMESSAGE_TEMP,
						(char*) "Критическая температура ГВС,", HP.sTemp[TBOILER].get_Temp());
				if(sensor_usable(HP.sTemp[TCOMP]) && HP.message.get_mTCOMP() < HP.sTemp[TCOMP].get_Temp()) HP.message.setMessage(pMESSAGE_TEMP,
						(char*) "Критическая температура компрессора,", HP.sTemp[TCOMP].get_Temp());
			} else countTEMP += TIME_READ_SENSOR / 100; // в 0.1 сек
		}
		static uint8_t last_life_h = 255;
		if(HP.message.get_fMessageLife()) // Подача сигнала жизни если разрешено!
		{
			uint8_t hour = rtcSAM3X8.get_hours();
			if(hour == HOUR_SIGNAL_LIFE && hour != last_life_h) {
				HP.message.setMessage(pMESSAGE_LIFE, (char*) "Контроллер работает . . .", 0);
			}
			last_life_h = hour;
		}
		//
		vReadSensor_delay1ms(TIME_READ_SENSOR - (int32_t)(GetTickCount() - ttime));     // Ожидать время нужное для цикла чтения

	}  // for
	vTaskDelete( NULL);
}

// Вызывается во время задержек в задаче чтения датчиков
void vReadSensor_delay1ms(int32_t ms)
{
	if(ms <= 0) return;
	if(ms < 10) {
		vTaskDelay(ms);
		return;
	}
	ms -= 10;
	uint32_t tm = GetTickCount();
	do {
#ifdef  KEY_ON_OFF // Если надо проверяем кнопку включения ТН
		// Работа ПО ФРОНТУ: нажатие принимается только после того, как кнопку
		// отпустили. Если её держали на старте (выбор режима загрузки), то
		// отпускание не считается командой - иначе долгое удержание ради
		// аварийного режима заканчивалось бы пуском или остановом ТН.
		// Выдержка считается по времени, а не по оборотам этого цикла: за секунду их
		// бывает и десять, и тысяча.
		// Этот опрос идёт только из задачи датчиков, то есть после старта шедулера.
		if(!digitalReadDirect(PIN_KEY1)) {                            // нажата
			if(keyOnOffReady && GetTickCount() - keyReleasedAt > KEY_RELEASE_IGNORE) {
				keyOnOffReady = false;                                // одно действие на нажатие
				journal.jprintf("ON/OFF Key pressed!\n");
				if(HP.get_errcode() && !Error_Beep_confirmed) {
					Error_Beep_confirmed = true;
				} else if(HP.get_State() == pOFF_HP) {
					HP.sendCommand(pSTART);
				} else if((HP.get_State() == pWORK_HP) || (HP.get_State() == pWAIT_HP)) {
					HP.sendCommand(pSTOP);
				}
			}
		} else if(!keyOnOffReady) {                                   // отпущена
			keyOnOffReady = true;
			keyReleasedAt = GetTickCount();       // выдержку считаем от отпускания
		}
#endif
#ifdef USE_UPS
		HP.sInput[SPOWER].Read(true);
		if(HP.sInput[SPOWER].is_alarm()) { // Электричество кончилось
			if(!HP.NO_Power) {
				HP.NO_Power = 1;
				HP.save_motoHour();
				Stats.SaveStats(0);
				Stats.SaveHistory(0);
				journal.jprintf_date( "POWER LOST!\n");
				if(HP.get_State() == pSTARTING_HP || HP.get_State() == pWORK_HP) {
					HP.sendCommand(pWAIT);
					HP.NO_Power = 2;
				}
			}
			HP.NO_Power_delay = NO_POWER_ON_DELAY_CNT;
		} else if(HP.NO_Power) { // Включаемся
			if(HP.NO_Power_delay) {
				if(--HP.NO_Power_delay == 0) HP.sendCommand(pNETWORK);
			} else {
				journal.jprintf_date( "POWER RESTORED!\n");
				if(!HP.Schdlr.IsSchedulerOn()) {  // Расписание не активно, иначе включаемся через расписание
					if(HP.NO_Power == 2 && HP.get_State() == pWAIT_HP) {
						HP.NO_Power = 0;
						journal.jprintf("Resuming work...\n");
						HP.sendCommand(pRESUME);
					}
				}
				HP.NO_Power = 0;
			}
		}
#endif
		int32_t tm2 = GetTickCount() - tm;
		if((tm2 -= ms) >= 0) {
			if(tm2 < 10) vTaskDelay(10 - tm2);
			break;
		}
		vTaskDelay(tm2 > -10 ? 3 : 10);
	} while(true);
}

// Исполнить решение ClimateControl о реле (CLIMATE_RELAY_ACT)
static void relayAct(devRelay &r, uint8_t act)
{
	switch(act) {
	case RELAY_ON:       r.set_ON(); break;
	case RELAY_OFF:      r.set_OFF(); break;
	case RELAY_ON_PLAN:  r.set_ON(true); break;
	case RELAY_OFF_PLAN: r.set_OFF(true); break;
	}
}

//////////////////////////////////////////////////////////////////////////
// Задача Управление тепловым насосом (Tasks.xHandleUpdate) "UpdateHP"
 void vUpdate( void * )
{
	#ifdef RPUMPB
	static uint32_t RPUMPBTick=0;
	#endif
	static uint8_t  Scheduler_check_day = 0;
	for(;;)
	{
		Watchdog_Beat(WDT_UpdateHP);   // отметка живости для наблюдателя
		if(!Tasks.Task_vUpdate_run) {
			// Стоим, пока ТН не работает. Пуск ставит флаг и шлёт уведомление (Wake_UpdateHP); флаг проверяется
			// после каждого пробуждения, а таймаут в секунду держит отметку в наблюдателе и страхует от потери уведомления.
			ulTaskNotifyTake(pdTRUE, 1000 / portTICK_PERIOD_MS);
			continue;
		}
		if (HP.get_State()==pWORK_HP){ //Код обслуживания работы ТН выполняется только если состяние ТН - работа а вот расписание всегда выполняется
			// 1. Обновится, В это время команды управления не выполняются!!!!!
			if (SemaphoreTake(Tasks.xCommandSemaphore,0)==pdPASS)                                           // Семафор  захвачен
			{
				if (HP.get_State()==pWORK_HP)  HP.vUpdate();                                               // ТН работает и идет процесс контроля
				SemaphoreGive(Tasks.xCommandSemaphore);                                                       // Семафор отдан
			}
			// 2. Управление циркуляционным насосом ГВС
#ifdef RPUMPB
			{
				CirculIn ci;
				ci.allowed        = HP.scheduleBoiler() && HP.get_BoilerON();  // бойлер разрешён и разрешено греть по расписанию (или оно выключено)
				ci.boiler_mode    = HP.get_modWork() & pBOILER;
				ci.circulation    = HP.get_Circulation();
				ci.heating_boiler = (HP.dRelay[RCOMP].get_Relay() || HP.dFC.isfOnOff()) && HP.get_onBoiler();
				ci.relay_on       = HP.dRelay[RPUMPB].get_Relay();
				ci.work_ticks     = HP.get_CirculWork() * configTICK_RATE_HZ;
				ci.pause_ticks    = HP.get_CirculPause() * configTICK_RATE_HZ;
				ci.now            = xTaskGetTickCount();
				relayAct(HP.dRelay[RPUMPB], ClimateCirculStep(ci, &RPUMPBTick));   // решение - в ClimateControl
			}
#endif // #ifdef RPUMPB
		} // НЕ РЕЖИМ ОЖИДАНИЕ if HP.get_State()==pWORK_HP)
	
		if(!Tasks.Task_vUpdate_run) goto delayTask;/* continue;*/

		// 3. Расписание проверка всегда
		{  // error: jump to label [-fpermissive] GCC
			// Переключение расписания, когда текущий месяц и десятидневка совпадают; если пропустили из-за выключенного НК или работы,
			// то пропустили. Расписание выбирается один раз, если вручную перевыбрать, то еще раз автоматически выбираться не будет до следующего года
			if(HP.Schdlr.IsSchedulerOn()) {
				uint8_t d = rtcSAM3X8.get_days();
				if(Scheduler_check_day != d) {
					Scheduler_check_day = d;
					uint8_t was = HP.Schdlr.sch_data.Active;
					if(Scheduler_AutoSelect(HP.Schdlr.sch_data.AutoSelectMonthWeek, MAX_CALENDARS, &HP.Schdlr.sch_data.Active, rtcSAM3X8.get_months(), d)) {
						if(HP.Schdlr.sch_data.Active != was) journal.jprintf_time("Schedule %d selected\n", HP.Schdlr.sch_data.Active + 1);
						HP.Schdlr.save();
					}
				}
			}
			// Какой профиль ДОЛЖЕН быть сейчас активен и что для этого сделать - решение в SchedulerCalc
			int8_t _profile = HP.Schdlr.calc_active_profile();
			uint8_t _state = HP.get_State();
			switch(Scheduler_ProfileStep(_profile, HP.Prof.get_idProfile(), _state == pWORK_HP, _state == pWAIT_HP, HP.isCommand() == pEMPTY, HP.NO_Power)) {
			case SCHDLR_DoWait:
				HP.sendCommand(pWAIT);
				break;
			case SCHDLR_DoResume:
				HP.sendCommand(pRESUME);
				break;
			case SCHDLR_DoSwitch: {
				int32_t _mode;
				if((_mode = HP.Prof.load_from_EEPROM_SaveON_mode(_profile)) >= 0) {
					bool frestart = Scheduler_NeedRestart(_mode, HP.get_modWork()); // Если направление работы ТН разное
					if(frestart) {
						HP.sendCommand(pWAIT);
						uint16_t i = DELAY_BEFORE_STOP_IN_PUMP + HP.Option.delayOffPump + 1;   // до 911 с: в uint8_t усекалось
						// Ждём отработки команды. Ожидание длиной до delayOffPump (до 900 с) - отмечаемся в наблюдателе
						// на каждом шаге, иначе предел UpdateHP в 120 с перезагружал контроллер посреди смены профиля.
						while(HP.isCommand()) { Watchdog_Sleep(WDT_UpdateHP, 1000); if(!--i) break; }
						if(!Tasks.Task_vUpdate_run) continue;
					}
					HP.switch_profile(_profile);   // слот копируется одним блоком внутри, журнал - после него
					journal.jprintf_time("Profile changed to #%d\n", _profile);
					if(frestart) HP.sendCommand(pRESUME);
				}
				break;
			}
			}
		}
		// 4. Отработка пауз всегда они разные в зависимости от состояния ТН!!
delayTask:	// чтобы задача отдавала часть времени другим
		switch (HP.get_State())  // Состояние ТН
		{
		case pOFF_HP:                          // 0 ТН выключен
		case pSTOPING_HP:                      // 2 Останавливается
			journal.jprintf((const char*)" Stop task UpdateHP\n");
			Tasks.Task_vUpdate_run = false;
			break;
		case pSTARTING_HP: Watchdog_Sleep(WDT_UpdateHP, 10000); break; // 1 Стартует  - этого не должно быть в этом месте
		case pWORK_HP:                           // 3 Работает   - анализ режима работы get_modWork()
			if((HP.get_modWork() & pHEAT)) {	// Отопление
				if(HP.get_ruleHeat()==pHYSTERESIS)  Watchdog_Sleep(WDT_UpdateHP, TIME_CONTROL);    // Гистерезис
				else
					Watchdog_Sleep(WDT_UpdateHP, (uint32_t) HP.dFC.get_Uptime() * 1000);             // Время интегрирования ПИД  секунды
			} else if((HP.get_modWork() & pCOOL)) { // охлаждение
				if(HP.get_ruleCool()==pHYSTERESIS)  Watchdog_Sleep(WDT_UpdateHP, TIME_CONTROL);    // Гистерезис
				else
					Watchdog_Sleep(WDT_UpdateHP, (uint32_t) HP.dFC.get_Uptime() * 1000);             // Время интегрирования ПИД секунды
			} else if((HP.get_modWork() & pBOILER)) { // бойлер
				Watchdog_Sleep(WDT_UpdateHP, (uint32_t) HP.dFC.get_Uptime() * 1000);                 // Время интегрирования ПИД секунды
			} else { // Пауза
				Watchdog_Sleep(WDT_UpdateHP, TIME_CONTROL);                                         // Гистерезис
			}
			break;
		case pWAIT_HP:                          // 4 Ожидание ТН (расписание - пустое место)   проверям раз в 5 сек
		case pERROR_HP: 						// 5 Ошибка ТН
			Watchdog_Sleep(WDT_UpdateHP, UPDATE_HP_WAIT_PERIOD);
			break;
		case pERROR_CODE:                       // 6 - Эта ошибка возникать не должна!
		default:
			journal.jprintf((const char*)" $ERROR: Bad state HP in function %s\n",(char*)__FUNCTION__);
		} //  switch (HP.get_State())

		if(!Tasks.Task_vUpdate_run) continue;
		// Солнечный коллектор
	}// for
	vTaskDelete( NULL );
}

// Задача Управление ЭРВ, "UpdateEEV"
#ifdef EEV_DEF
// Ожидание пуска ЭРВ. Счётчик Task_UpdateEEV_gen растёт при каждом пуске компрессора; eevSeen - сколько пусков
// задача уже учла. Ожидание кончается, когда появился новый пуск; таймаут в секунду даёт отметку в наблюдателе.
static uint32_t eevSeen = 0;
static void eevWaitStart(void)
{
	while(__atomic_load_n(&Tasks.Task_UpdateEEV_gen, __ATOMIC_ACQUIRE) == eevSeen) {
		Watchdog_Beat(WDT_UpdateEEV);
		ulTaskNotifyTake(pdTRUE, 1000 / portTICK_PERIOD_MS);
	}
	eevSeen = __atomic_load_n(&Tasks.Task_UpdateEEV_gen, __ATOMIC_ACQUIRE);
}

void vUpdateEEV(void *)
{
	eevWaitStart();   // до первого пуска компрессора ЭРВ не управляется
	// Отметки живости для наблюдателя стоят в точках блокировки - в каждом
	// Watchdog_Sleep, - а не в начале for. Начало for рабочий путь задачи
	// проходит один раз: и ожидание delayOnPid, и цикл ПИД ниже замкнуты
	// внутри витка. Подробности - у Watchdog_Sleep.
	for(;;) {
		while(!(HP.get_startCompressor() && (Uptime_s() - HP.get_startCompressor() > HP.dEEV.get_delayOnPid() && HP.dEEV.get_delayOnPid() != 255))) { // ЭРВ контролирует если прошла задержка после включения компрессора (пауза перед началом работы ПИД) и задержка != 255
			// delayOnPid == 255 означает "ПИД ЭРВ отключён совсем", и условие
			// выхода выше при этом не становится истинным никогда. Без явной
			// проверки задача не доходит до ветки останова и продолжает двигать
			// ЭРВ уже при выключенном ТН. Пока пауза не отмечалась, это
			// кончалось перезагрузкой по молчанию задачи - то есть было видно;
			// теперь, когда отмечается, осталось бы незаметным.
			// При любом другом значении delayOnPid условие ложно всегда, и
			// поведение задачи не меняется ни на шаг.
			if(HP.dEEV.get_delayOnPid() == 255 && !HP.is_compressor_on()) break;
			Watchdog_Sleep(WDT_UpdateEEV, TIME_EEV_BEFORE_PID); // Период управления ЭРВ (цикл управления)
			if(GETBIT(HP.dEEV.get_flags(), fEEV_StartPosByTemp)) { // Скорректировать ЭРВ по температуре подачи
				HP.dEEV.set_EEV(HP.dEEV.get_StartPos());
			}
		}
		HP.dEEV.resetPID();
		// Рабочий цикл ПИД: пока компрессор работает, повторяем итерацию, не
		// возвращаясь к ожиданию delayOnPid и не сбрасывая ПИД. Цикл явный (без goto
		// назад через весь блок): начала for он не достигает. Равносильность прежней форме
		// с goto проверена перебором: tests/test_eev_flow.cpp.
		while(HP.is_compressor_on()) {
			HP.dEEV.CorrectOverheat();
			// Обновить и выполнить итерацию по контролю ЭРВ Для алгоритма таблица передаем СРЕДНИЕ (IN+OUT)/2 температуры
			HP.dEEV.Update();
			// Штатная пауза (в зависимости от настроек). Неположительный период
			// проверяется явно: иначе такое значение уходило в vTaskDelay как
			// огромное беззнаковое и задача замолкала до перезагрузки, то есть
			// ошибка настройки была видна. С нарезкой сна она стала бы не видна -
			// задача отмечалась бы каждые 5 с, вися при этом на полвека.
			Watchdog_Sleep(WDT_UpdateEEV, HP.dEEV.get_PID_time() > 0 ? (uint32_t) HP.dEEV.get_PID_time() * 1000 : TIME_EEV);  // ПИД
		}
		switch((uint8_t)HP.get_State()) {
		case pOFF_HP:
		case pSTOPING_HP:
		case pWAIT_HP:
			// Если компрессор не работает, то остановить задачу Обновления ЭРВ
			// Пуски считает счётчик (Wake_UpdateEEV). Все пуски до этой точки учитывает проверка компрессора ниже:
			// счётчик растёт только после его включения. Пуск после этой точки изменит счётчик и прервёт ожидание.
			eevSeen = __atomic_load_n(&Tasks.Task_UpdateEEV_gen, __ATOMIC_ACQUIRE);
			if(HP.is_compressor_on()) continue;   // компрессор успели включить снова: идём в рабочий цикл
			journal.jprintf((const char*) " Stop task UpdateEEV\n");
			eevWaitStart();
			continue; // продолжение задачи работы ЭРВ начинается с этого места, по этому сразу на начало цикла контроля
		}
		Watchdog_Sleep(WDT_UpdateEEV, TIME_EEV);
	} // for
	vTaskDelete( NULL);
}
#endif

#ifdef EEV_DEF
// Задача обеспечения движения шаговика EEV (StepperEEV)
void vUpdateStepperEEV(void *)
{
  // Размер стека не позволяет использовать внутри jprintf.* !!!
	static int cmd = 0;  // размер элемента очереди - sizeof(int): int16_t затирал бы соседние данные
	for(;;) {
		Watchdog_Beat(WDT_StepperEEV);   // отметка живости для наблюдателя
		// Команд нет - ждём уведомление от StepMotor::step. Очередь проверяется до ожидания и после каждого
		// пробуждения, поэтому команда не теряется, а таймаут в секунду держит отметку в наблюдателе.
		if(xQueuePeek(HP.dEEV.stepperEEV.xCommandQueue, &cmd, 0) == errQUEUE_EMPTY) {
			ulTaskNotifyTake(pdTRUE, 1000 / portTICK_PERIOD_MS);
			continue;
		}
		// Полный цикл движения шаговика с разгребанием очереди команд,
		// В очереди лежат АБСОЛЮТНЫЕ координаты
		// При этом если очередь содержит более одной команды - просто суммируем все команды и двигаемся по итоговой сумме
		// Это значит что шаговик не успевает за темпом выдачи команд программой. Экономим время

		// 1. Чтение очереди команд, для выяснения все таки куда надо двигаться, переходим на относительные координаты
		int16_t steps_left, *step_number = &HP.dEEV.EEV; // получить текущее положение шаговика абсолютное в начале очереди
		if(*step_number < 0) *step_number = 0;
		while(xQueueReceive(HP.dEEV.stepperEEV.xCommandQueue,&cmd,0) == pdPASS) ;   // Читаем очередь пока есть чего читать
		if(HP.dEEV.setZero) {
			*step_number = (HP.dEEV.stepperEEV.number_of_steps + EEV_SET_ZERO_OVERRIDE + 7) / 8 * 8; // Если выполняется команда установки 0 то все остальные команды игнорируются до ее выполнения.
			cmd = 0;
		}
		steps_left = cmd - *step_number;
		// 2. Движение
		while(steps_left != 0) {
			if(steps_left > 0) {// направление в увеличение
				(*step_number)++;
				steps_left--;
			} else { 			// направление в уменьшение
				(*step_number)--;
				steps_left++;
			}
			if(*step_number < 0) {
				HP.dEEV.set_error(ERR_MIN_EEV);
				break;
			}
// Часовой: если StepMotorPhase.h не включён, препроцессор посчитает ВСЕ
// имена PHASE_* нулями, сравнение ниже окажется истинным, и сборка молча
// возьмёт не ту последовательность фаз ЭРВ. Неопределённые имена равны
// друг другу - на этом их и ловим.
#if PHASE_4 == PHASE_8s
#error "PHASE_* не определены: нужен #include \"StepMotorPhase.h\" до сравнений с EEV_PHASE"
#endif
#if EEV_PHASE == PHASE_4  // 4 фазы движения
			HP.dEEV.stepperEEV.stepOne(*step_number % 4);                  // Сделать один шаг //
#else                     // остальные варианты  8 фаз движения
			HP.dEEV.stepperEEV.stepOne(*step_number % 8);                   // Сделать один шаг //
#endif
			// Отметка на каждом шаге: цикл движения до конца витка for не доходит,
			// а полный ход ЭРВ на медленной скорости длиннее предела молчания -
			// 490 шагов при настраиваемом минимуме 5 шагов/с это почти 100 с
			// против предела 60 с (при умолчании 80 шагов/с - всего 6 с).
			Watchdog_Sleep(WDT_StepperEEV, HP.dEEV.stepperEEV.step_delay);      // Ожидать step_delay для следующего шага.
		}
		if(HP.dEEV.setZero) { // если стоит признак установки нуля, обнулить и сбросить признак
			*step_number = 0;
			HP.dEEV.setZero = false;
		}

		// 3. Остановить выполнение команд, если очередь пуста, но могли накидать пока двигались
		if(xQueuePeek(HP.dEEV.stepperEEV.xCommandQueue,&cmd,0) == errQUEUE_EMPTY) {
			if(!HP.dEEV.get_HoldMotor()) HP.dEEV.stepperEEV.off();                                   // выключить двигатель если нет удержания
			HP.dEEV.stepperEEV.offBusy();                                                            // признак Мотор остановлен
		}
		// Дошли до сюда новая, очередь не пуста и новая итерация по разбору очереди
	} // for
	vTaskDelete( NULL);
}
#endif

// vUpdateCommand /////////////////////////////////////////////////////// 
// Задача Разбор очереди команд
void vUpdateCommand(void *)
{
	for(;;) {
		Watchdog_Beat(WDT_CommandHP);   // отметка живости для наблюдателя
		// Ждём уведомление от sendCommand, но не дольше секунды: задача отмечается в наблюдателе и не спит вечно.
		// Уведомление, пришедшее до ожидания, не теряется - ожидание завершится сразу.
		ulTaskNotifyTake(pdTRUE, 1000 / portTICK_PERIOD_MS);
		if(HP.takeCommand()) HP.runCommand();      // runCommand сама выбирает остальные команды из очереди
	}
	vTaskDelete( NULL);
}

// ServiceHP ///////////////////////////////////////////////
// Графики в ОЗУ, счетчики моточасов, сохранение статистики, работа насосов в простое, дисплей Nextion
void vServiceHP(void *)
{
#ifdef NEXTION
	static uint32_t NextionTick = 0;
#endif
	static uint8_t  task_updstat_countm = rtcSAM3X8.get_minutes();
	static TickType_t timer_sec = xTaskGetTickCount(), timer_idle = 0, timer_total = 0;
	static uint16_t restart_cnt;
	static uint16_t pump_in_pause_timer = 0;
	for(;;) {
		Watchdog_Beat(WDT_ServiceHP);   // отметка живости для наблюдателя
		STORE_DEBUG_INFO(70);
		register uint32_t t = xTaskGetTickCount();
		if(t - timer_sec >= 1000) { // 1 sec
			Watchdog_Supervise(t);
			if(GPBR->SYS_GPBR[GPBR_BOOT] && t > BOOT_HEALTHY_S * 1000UL) GPBR->SYS_GPBR[GPBR_BOOT] = 0;   // проработали без перезагрузки - счёт аварийных подряд с нуля
			{	// Счётчик повторных пусков: час работы без ошибок - попытки снова все (раньше обнулялся только ручным пуском)
				static RepeatWatch repeat_watch = { 0 };
				if(Error_RepeatReset(repeat_watch, HP.num_repeat, HP.get_State() == pWORK_HP && HP.get_errcode() == OK, t, REPEAT_RESET_S * 1000UL)) {
					journal.jprintf("Repeat start counter reset: %d attempts used, %d h without errors\n", HP.num_repeat, REPEAT_RESET_S / 3600);
					HP.num_repeat = 0;
				}
			}
#ifdef TEST_KERNEL_FAULT
			if(t > 120000UL) configASSERT(0);                                            // стенд: провал configASSERT -> errorBlink -> аварийный хук
#endif
			journal.Drain();      // выгрузить пачку журнала в EEPROM, см. JournalBuf.h
			extern TickType_t xTickCountZero;
			TickType_t ttmpt = t - xTickCountZero - timer_total;
			if(ttmpt > 5000) {
				if(t < timer_sec) {
					vTaskResetRunTimeCounters();
					timer_idle = timer_total = 0;
				} else {
					TickType_t ttmp = timer_idle;
					Diag.CPU_LOAD = 100 - (((timer_idle = vTaskGetRunTimeCounter(xTaskGetIdleTaskHandle())) - ttmp) / (ttmpt / 100UL));
					timer_total = t - xTickCountZero;
				}
			}
			timer_sec = t;
			{	// История на SD: раз в минуту, а в окне детализации ВКЛ/ВЫКЛ компрессора каждые HISTORY_DETAIL_PERIOD секунд.
				// Хвост окна после останова пишется и тогда, когда ТН уже выключен.
				static HistoryDetail hist_detail;
				static HistoryTick hist_tick;
				uint32_t now = rtcSAM3X8.unixtime();
				bool open = GETBIT(HP.Option.flags2, f2HistoryDetail) &&
						HistoryDetailUpdate(hist_detail, HP.get_State() == pSTARTING_HP, HP.is_compressor_on(), now, HISTORY_DETAIL_DURATION);
				if(open && !Stats.HistoryDetailRoom()) open = false;
				if(HistoryDue(hist_tick, now, open, HISTORY_DETAIL_PERIOD) && (open || HP.IsWorkingNow())) Stats.History();
				Stats.HistoryFlushPending();
			}
			if(HP.IsWorkingNow()) {
				if(((Charts_when_comp_on && HP.is_compressor_on()) || (!Charts_when_comp_on && HP.get_State() != pOFF_HP)) && ++task_updstat_chars >= HP.get_tChart()) { // пришло время
					task_updstat_chars = 0;
					STORE_DEBUG_INFO(71);
					HP.updateChart();                                       // Обновить графики
					STORE_DEBUG_INFO(72);
				}
				uint8_t m = rtcSAM3X8.get_minutes();
				if(m != task_updstat_countm) { 								// Через 1 минуту
					task_updstat_countm = m;
					STORE_DEBUG_INFO(73);
					HP.updateCount();                                       // Обновить счетчики моточасов
					STORE_DEBUG_INFO(74);
					if(task_updstat_countm == 59) HP.save_motoHour();		// сохранить раз в час
				}
				STORE_DEBUG_INFO(75);
			}
			if(HP.PauseStart) {
				if(HP.PauseStart == 1) {
					restart_cnt = HP.isCommand() == pRESTART ? HP.Option.delayStartRes : HP.Option.delayRepeadStart;  // Определение времени задержки
					journal.jprintf((const char*) "Start over %d sec . . .\n", restart_cnt);
					HP.PauseStart = 2;
				} else if(restart_cnt-- == 0) {
					HP.PauseStart = 0;
					HP.sendCommand(pAUTOSTART);
				}
			}
			if(HP.startPump) {  // Насосы в паузе ТН (0 - автомат стоит, 1 - запуск, 2 - насосы выключены, 3 - включены), решение - в ClimateControl
				uint8_t sp = HP.startPump;
				uint8_t act = ClimatePumpPauseStep(&sp, &pump_in_pause_timer, HP.get_workPump(), HP.get_pausePump());
				if(act != RELAY_KEEP) {
					relayAct(HP.dRelay[PUMP_OUT], act);              // насос отопления (плановое, пауза ТН)
					HP.Pump_HeatFloor(act == RELAY_ON_PLAN);         // насос ТП
					HP.startPump = sp;
				}
			}
			STORE_DEBUG_INFO(76);
			Stats.CheckCreateNewFile();
		}
		STORE_DEBUG_INFO(77);
#ifdef NEXTION
		myNextion.readCommand();                 // прочитать сообщения от дисплея
		STORE_DEBUG_INFO(78);
		if(xTaskGetTickCount() - NextionTick > NEXTION_UPDATE) {
			STORE_DEBUG_INFO(79);
			myNextion.Update();                  // Обновление дисплея
			STORE_DEBUG_INFO(80);
			NextionTick = xTaskGetTickCount();
		}
#endif
		vTaskDelay(NEXTION_READ / portTICK_PERIOD_MS); // задержка чтения уменьшаем загрузку процессора. Прежде считалась разность GetTickCount() (мс от включения) и timer_sec (тики планировщика): шкалы разные, результат всегда был NEXTION_READ
	}
	vTaskDelete(NULL);
}
