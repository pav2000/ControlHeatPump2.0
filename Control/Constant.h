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
// ---------------------------------------------------------------------------
// Константы проекта которые описывают специфику КОНФИГУРАЦИИ ТН
// ---------------------------------------------------------------------------
#ifndef Constant_h
#define Constant_h
#include "src/core/Freon.h"
#include "Config.h"                         // Цепляем сразу конфигурацию
#include "src/util/Util.h"
#include "src/util/DateTime.h"
#include "src/hw/StepMotorPhase.h"
#include "src/hw/GpioDirect.h"
#include "src/core/ErrorCodes.h"
#include "src/core/ControlTypes.h"   // MODE_COMP, RULE_HP, структуры ПИД

/*
 * Имена веб-параметров и прочие короткие строки.
 *
 * ЗАЧЕМ МАКРОС. static const char X[] = "X" имеет внутреннее связывание - копию строки в
 * каждой единице трансляции, которая её коснулась (измерено: 600 экземпляров 334 имён, 4596
 * байт flash, из них 2079 - копии; дороже всех веб-слой: WebApiGet разбирает почти все имена).
 * Поэтому заголовок ОБЪЯВЛЯЕТ, а определения собираются в одной единице трансляции -
 * Config.cpp (CONFIG_DEFINE_OBJECTS). Имя и значение остаются на одной строке рядом с
 * комментарием владельца. Неиспользуемые имена не занимают места (-fdata-sections, --gc-sections).
 *
 * ПРО sizeof. Размер массива берётся из литерала: объявление получается с ПОЛНЫМ типом,
 * sizeof(X) работает во всех единицах, а расхождение объявления и определения ловит компилятор.
 */
#ifdef CONFIG_DEFINE_OBJECTS
#define STRCONST(name, value) extern const char name[sizeof(value)]; const char name[sizeof(value)] = value
#else
#define STRCONST(name, value) extern const char name[sizeof(value)]
#endif

// ---- ОПЦИИ КОМПИЛЯЦИИ ПРОЕКТА ----
#define VERSION			"2.20"	// Версия прошивки: приём Modbus включается до возврата планировщика
// Версия формата, которой прошивка ПИШЕТ настройки в I2C память.
// Должна совпадать с версией каркаса, которым пишутся настройки (150): иначе Option.ver по
// умолчанию неверен, первое сохранение на чистом контроллере печатает
// "Converting settings to v.150 (was 147)", а get_CONST отдаёт в морду неверное число.
//
// НЕ ПУТАТЬ с SETTINGS_VER_NEW (SettingsLegacy.h). Это ПОРОГ: данные с
// версией не ниже него разбираются новым читателем, всё что ниже - старым.
// Сегодня оба равны 150, но роли разные: когда формат внутри нового каркаса
// дойдёт до 151, VER_SAVE станет 151, а порог обязан остаться 150 - иначе
// уже записанные файлы версии 150 уедут в старый читатель. Несовместимое
// изменение РАСКЛАДКИ структуры версией не выражается вовсе, для этого есть
// новый тег записи NTAG_*.
#define VER_SAVE		150		// Версия формата сохраняемых данных в I2C память
#ifndef UART_SPEED
#define UART_SPEED		115200		// Скорость отладочного порта
#endif
#define SerialDbg	Serial                      // Отладочный порт
#define FAST_LIB                            // использование допиленной библиотеки Ethernet Обычно используется
#define TIME_ZONE         3                 // поправка на часовой пояс по ДЕФОЛТУ
#define NTP_SERVER        "time.nist.gov"   // NTP сервер для синхронизации времени по ДЕФОЛТУ (готовимся к отключению буржуинского инета - "ntp2.stratum2.ru")
#define NTP_SERVER_LEN    60                // максимальная длиная адреса NTP сервера
#define NTP_PORT	  123
#define NTP_LOCAL_PORT    8888              // локальный порт, который будет прослушиваться на предмет UDP-пакетов NTP сервера
#define NTP_REPEAT        3                 // Число попыток запросов NTP сервера
#define NTP_REPEAT_TIME   1000              // (мсек) Время между повторами ntp пакетов
#define PING_SERVER       "192.168.0.1"     // ping сервер по ДЕФОЛТУ
#define WDT_TIME          10                // период Watchdog таймера секунды но не более 16 секунд!!! ЕСЛИ установить 0 то Watchdog будет отключен!!!
#ifndef INDEX_FILE
#define INDEX_FILE        "index.html"       // стартовый файл по умолчанию для большой морды
#endif
#define INDEX_MOB_FILE    INDEX_FILE         // стартовый файл по умолчанию для мобильной морды
#define MOB_PATH          "/mob/"            // Путь к мобильной морде
STRCONST(HEADER_BIN,  "HP-SAVE-DATA");   // Заголовок (начало) файла при сохранении настроек. Необходим для поиска данных в буфере данных при восстановлении из файла
#define MAX_LEN_PM        250               // максимальная длина строкового параметра в запросе (расписание бойлера 175 байт) кодирование описания профиля 40 букв одна буква 6 байт (двойное кодирование)
// Детализация истории на SD вокруг пуска и останова компрессора (опция "Детализация ВКЛ/ВЫКЛ компрессора")
#ifndef HISTORY_DETAIL_PERIOD
#define HISTORY_DETAIL_PERIOD	10		// Период записей в окне детализации, секунды
#endif
#ifndef HISTORY_DETAIL_DURATION
#define HISTORY_DETAIL_DURATION	300		// Сколько секунд окно держится после включения и после выключения компрессора
#endif
#ifndef CHART_POINT                         // Можно определить себе специальный размер графика в своем конфиге 
#define CHART_POINT       300               // Максимальное число точек графика, одна точка это 2 байта * число графиков  //300 - работает
#endif
#ifndef ADC_PRESCAL
#define ADC_PRESCAL       2					// ADCClock = MCK / ( (PRESCAL+1) * 2 )
#endif

// Инверсия выходов драйвера ЭРВ как ЗНАЧЕНИЕ, а не условная компиляция:
// драйвер StepMotor.cpp - отдельная единица трансляции и Config.h не видит,
// поэтому признак передаётся ему параметром в initStepMotor.
#ifdef DRV_EEV_L9333
#define EEV_DRV_INVERT   true          // драйвер L9333 требует инвертирования
#else
#define EEV_DRV_INVERT   false
#endif

// ---- СЕТЕВЫЕ НАСТРОЙКИ ----
// По умолчанию и в демо режиме (действуют там и там)
// В рабочем режиме настройки берутся из ЕЕПРОМ, если прочитать не удалось, то действуют настройки по умолчанию
// Размер указан не для красоты: Telemetry.cpp берёт sizeof(defaultMAC).
extern byte defaultMAC[6];
#ifdef CONFIG_DEFINE_OBJECTS
byte defaultMAC[] = { 0xDE, 0xA1, 0x1E, 0x01, 0x02, 0x03 };// не менять
#endif
const uint16_t  defaultPort=80;

// Макросы работы с битами байта, используется для флагов
#define GETBIT(b,f)   ((b&(1<<(f)))?true:false)              // получить состяние бита
#define SETBIT1(b,f)  (b|=(1<<(f)))                          // установка бита в 1
#define SETBIT0(b,f)  (b&=~(1<<(f)))                         // установка бита в 0

// ---- SPI ----
// Карта памяти
#define SD_REPEAT         3                 // Число попыток чтения карты, открытия файлов, при неудаче переход на работу без карты

// чип W5200 (точнее любой используемый чип)
#ifndef W5200_THREAD
#define W5200_THREAD      3                 // Число потоков для сетевого чипа w5200 допустимо 1-4 потока, на 4 скорее всего не хватить места в оперативке
#endif
#define W5200_NUM_PING    4                 // Число попыток пинга до определения потери связи
#define W5200_TIME_PING   1000              // мсек Время между попытками пинга (если не удача)
#define W5200_MAX_LEN     2048 //=W5100.SSIZE // Максимальная длина буфера, определяется W5200 не более 2048 байт
#define W5200_NUM_LINK    2                 // Число попыток сброса чипа w5500 и проверки появления связи (кабель воткнут) используется для инициализаци чипа
#define W5200_TIME_LINK   4000              // Максимальное время ожидания установления связи (поднятие Link) кабель воткнут  используется для инициализаци чипа (мсек)
#define W5200_TIME_WAIT   3000              // Время ожидания захвата мютекса (переключение потоков) мсек
// Таблица замеров наблюдателя задач (sys.wdt): Watchdog_StatWeb пишет сколько нужно, размера буфера
// не спрашивает. WDT_STATWEB_ROW - потолок одной строки, он передаётся в m_snprintf; весь вывод
// (WDT_TASK_COUNT строк) проверяется до вызова: действие wdt.reset и ключ sys.wdt оставляют под него место.
#define WDT_STATWEB_ROW        96
// Потолок ожидания оттайки, секунды. Два цикла в defrost() ждут датчиков
// шагами по 10 с. Пока пауза не отмечалась, залипший датчик приводил
// к перезагрузке по молчанию задачи - грубо, но заметно. С отметкой это стало
// бы молчаливым вечным ожиданием с компрессором в режиме оттайки.
#define DEFROST_WAIT_MAX      1800
// Сколько оттаек подряд могут не закончиться за DEFROST_WAIT_MAX до останова ТН (ERR_DEFROST)
#define DEFROST_FAILS_MAX     2

// Отложенная запись журнала. На рабочем контроллере журнал пишется в обычную
// I2C EEPROM: одна строка - отдельная запись с ожиданием внутреннего цикла
// около 5 мс, всё это время удерживается xI2CSemaphore. Задачи кладут строку
// в кольцо в оперативной памяти и идут дальше, а пишет одна задача пачками
// по странице EEPROM - это убирает блокировки и снижает износ чипа.
#define JOURNAL_RING_SIZE      1024   // байт оперативной памяти под кольцо
#define JOURNAL_DRAIN_CHUNK    64     // размер пачки = странице EEPROM (I2C_PAGE_EEPROM)
#define JOURNAL_DRAIN_CHUNKS   4      // пачек за один вызов Drain: 256 Б/с при ~20 мс шины I2C
#define JOURNAL_REPEAT_TIMEOUT 60000  // мсек молчания, после которых выдаются накопленные повторы

// Предел ожидания подтверждения отправки пакета сетевым чипом, мсек.
// Штатно отправка укладывается в единицы миллисекунд; таймаут нужен только
// чтобы зависший чип не остановил web-сервер навсегда.
#define W5200_SEND_TIMEOUT    1000
#define W5200_SPI_SPEED   SPI_RATE          // ЭТО ДЕЛИТЕЛЬ (SPI_RATE определен в w5100.h)!!! Частота SPI w5200 = 84/W5200_SPI_SPEED т.е. 2-42МГц 3-28МГц 4-21МГц 6-14МГц Диапазон 2-6
#define W5200_SOCK_SYS    (MAX_SOCK_NUM-1)  // Номер системного сокета который не используется в вебсервере, это последний сокет, НЕ МЕНЯТЬ
#define W5200_RTR         (2*0x07D0)        // время таймаута в 100 мкс интервалах  (по умолчанию 200ms(100us X 2000(0x07D0))) актуально для комманд CONNECT, DISCON, CLOSE, SEND, SEND_MAC, SEND_KEEP
#define W5200_RCR         (0x04)            // число повторов передачи  (по умолчанию 0x08 раз))
#define MAIN_WEB_TASK      0                // какой поток вебсервера является основным (поток в котором идет отправка MQTT и уведомлений) обычно 0

// ---- SERIAL ----
//Nextion дисплей
#define NEXTION_PORT         Serial1        // Аппаратный порт куда прицеплен дисплей
#define NEXTION_PORT_SPEED   9600           // Скорость порта, бод
#define NEXTION_UPDATE       5000           // Время обновления информации на дисплее Nextion (мсек)
#define NEXTION_BOOT_TIME    300            // Время для загрузки дисплея, если при сбросе дисплей не находится надо увеличить (мсек)
#define NEXTION_READ         50             // Время опроса дисплея Nextion (мсек) разбор входной очереди

// Конфигурирование Modbus для инвертора и счетчика SDM
#ifndef MODBUS_PORT_NUM
#define MODBUS_PORT_NUM      Serial2        // Аппаратный порт куда прицеплен Modbus
#define MODBUS_PORT_SPEED    9600           // Скорость порта куда прицеплен инвертор и счетчик
#define MODBUS_PORT_CONFIG   SERIAL_8N1     // Конфигурация порта куда прицеплен инвертор и счетчик
#define MODBUS_TIME_WAIT     2000           // Время ожидания захвата мютекса для modbus мсек
#ifndef MODBUS_TIME_TRANSMISION
#define MODBUS_TIME_TRANSMISION 4           // Не используется с 2.20: приём включается сразу после передачи (postTransmission)
#endif
#endif

// Глобальные параметры инвертора инвертора на Modbus зависят от компрессора!!!!!!!!!
#define FC_MODBUS_ADR      1             // Адрес частотного преобразователя на шине не должно совпадать SMD_MODBUS_ADR
#define FC_TIME_READ      (8*1000)       // Время опроса инвертора в мск
#define FC_NUM_READ        3             // Число попыток чтения инвертора (подряд) по Modbus до его останова ТН по ошибке
#define FC_DELAY_REPEAT    40            // мсек Время между ПОВТОРНЫМИ попытками чтения
// Потеря связи с инвертором (ERR_FC_NO_LINK): сбоев подряд ИЛИ молчание шины дольше времени
#define FC_LINK_FAILS      6             // Транспортных сбоев Modbus подряд (чтения и записи вместе)
#define FC_LINK_TIME_RUN   (20*1000)     // Мсек без ответа инвертора, когда он в работе
#define FC_LINK_TIME_IDLE  (60*1000)     // Мсек без ответа инвертора, когда он остановлен
#define FC_DELAY_READ      5             // мсек Время между последовательными запросами
#define FC_WRITE_READ      10            // мсек Время между последовательной записью

// Глобальные переменные счетчика SDMxxx на Modbus
// настройки связи со счетчиком по умолчанию (из коробки см инструкцию) требуется для его программирования для работы
#define DEFAULT_SDM_SPEED       2400      // Скорость в бодах для счетчика по умолчанию
#define DEFAULT_SDM_MODBUS_ADR  1         // Адрес счетчика на шине не должно совпадать с FC_MODBUS_ADR при программировании инвертор ОТКЛЮЧИТЬ (адрес 1)
// Требуемые настройки связи (после программирования)
#define SDM_SPEED           2              // скорость счетчика в константах 0 = 2400 bps. 1 = 4800 bps. 2 = 9600 bps 5=1200 bps  Скорости обмена должны совпадать см MODBUS_PORT_SPEED
#define SDM_MODBUS_ADR      2              // Адрес счетчика на шине не должно совпадать с FC_MODBUS_ADR
#ifndef SDM_READ_PERIOD
#define SDM_READ_PERIOD     (10*1000)      // Время опроса счетчика в мск
#endif
#ifndef SDM_NUM_READ
#define SDM_NUM_READ        4              // Число попыток чтения счетчика (подряд) по Modbus до его отключения (ошибка не генерится)
#endif
#define SDM_DELAY_REPEAD    100            // мсек Время между ПОВТОРНЫМИ попытками чтения (меньше не имеет смысла - счетчик может не успеть)
//#define SDM_BLOCK                        // Блокировать чтение счетчика при потере связи, в противном случае будут периодически делаться попытки восстановить связь
    
// ---- TIME & DELAY ----
// Времена и задержки
#define cDELAY_DS1820     750              // мсек. Задержка для чтения DS1820 (время преобразования)
#ifndef TIME_READ_SENSOR 
#define TIME_READ_SENSOR  4000		       // мсек. Период опроса датчиков
#endif
#define TIME_WEB_SERVER   2                // мсек. Период опроса web servera
#define TIME_CONTROL      (10*1000)        // мсек. Период управления тепловым насосом (цикл управления в режиме Гистерезис)
#define TIME_EEV          (1*1000)         // мсек. Период задачи vUpdateEEV в переходных состояниях ТН
#define TIME_EEV_BEFORE_PID (4*1000)       // мсек.
#define TIME_COMMAND      500              // мсек. Период разбора команд управления ТН (скорее пауза перед обработкой команды)
#define TIME_I2C_UPDATE   (60*60)*1000     // мсек. Время обновления внутренних часов по I2С часам (если конечно нужно)
#define TIME_MESSAGE_TEMP 300			   // 1/10 секунды, Проверка граничных температур для уведомлений
#define TIME_LED_OK       1500             // Период мигания светодиода при ОК (мсек)
#define TIME_LED_ERR      200              // Период мигания светодиода при ошибке (мсек).
#define TIME_BEEP_ERR     1000             // Период звукового сигнала при ошибке, мсек
// Период мигания КРАСНОГО в специальном режиме (safeNetwork или встроенная
// морда, выбранные кнопкой KEY1). Намеренно равен периоду зелёного в норме:
// специальный режим мигает так же, как зелёный без ошибок - одно правило,
// которое легко запомнить. Задано через TIME_LED_OK, а не числом, чтобы
// равенство нельзя было разъехать по недосмотру.
// Одинаковым с TIME_LED_ERR его делать НЕЛЬЗЯ: полярности светодиодов
// противоположны, и при совпадении периодов они ходят в точной противофазе,
// сливаясь в один "бегущий огонёк" вместо двух читаемых признаков.
#define TIME_LED1_MODE    TIME_LED_OK
// Кнопка KEY1: после отпускания столько ждём, прежде чем принять нажатие как
// вкл/выкл ТН. Отсчёт ИМЕННО от отпускания: держать кнопку могут сколько угодно,
// и окно от старта шедулера истекло бы под пальцем, а отпускание сработало бы
// как команда пуска. Пороги выбора режима загрузки - в Util.h, у key_boot_mode.
#define KEY_RELEASE_IGNORE 1000            // Подавление дребезга после отпускания, мсек
// Сигналы выбора режима загрузки кнопкой KEY1. Оба звучат ВО ВРЕМЯ удержания,
// а не после: отсчёт начинается тогда, когда загрузка дошла до опроса кнопки,
// и показать оператору этот момент больше нечем. Короткий - "кнопка видна,
// отсчёт пошёл, отпускай и получишь встроенную морду". Длинный - "додержал до
// аварийного режима, можно отпускать".
#define TIME_BEEP_KEY_START  60            // мсек, короткий: отсчёт удержания начался
#define TIME_BEEP_KEY_SAFENET 400          // мсек, длинный: взведён safeNetwork
#define cDELAY_START_MESSAGE 60            // Задержка (сек) после старта на отправку сообщений
#define UPDATE_HP_WAIT_PERIOD 5000			// Период вызова vUpdate во время ожидания или ошибки, мсек
#define NO_POWER_ON_DELAY_CNT 10			// Задержка включения после появления питани, *TIME_READ_SENSOR
#define HTTP_REQ_TIMEOUT   2000				// ms
// ---- I2C ----
// Микросхема памяти выбирается в Config.h ОДНИМ макросом из трёх, там же
// написано, какие чипы поддерживаются и чем отличаются. Здесь из этого выбора
// выводится всё остальное: адрес на шине, объём устройства, размер страницы,
// суммарный объём и признак FRAM. Ни одно из этих чисел не должно появиться
// в Config.h - свойство платы там выражается именем чипа, а не раскладкой.
//
// Разделение введено после того, как выяснилось, что выбор памяти был завязан
// на DEMO: режим отладки переключал тип и объём разом. Правило гигиены в
// tools/ci.sh следит, чтобы связь не завелась снова.
#define I2C_SPEED         twiClock400kHz // Частота работы шины I2C
#define I2C_NUM_INIT         3           // Число попыток инициализации шины
#define I2C_TIME_WAIT        2000        // Время ожидания захвата мютекса шины I2C мсек
#define I2C_ADR_RTC          0x68        // Адрес чипа rtc на шине I2C
#define I2C_ADR_DS2482       0x18        // Адрес чипа OneWire на шине I2C 3-х проводная
#define I2C_ADR_DS2482_2     0x19        // Адрес чипа OneWire на 2-ой шине I2C
#define I2C_ADR_DS2482_3     0x1A        // Адрес чипа OneWire на 3-ей шине I2C
#define I2C_ADR_DS2482_4     0x1B        // Адрес чипа OneWire на 4-ой шине I2C

// ---- Выбор микросхемы памяти: проверка и вывод производных ----
// Старый Config.h (до 2.90) задавал раскладку сам, четырьмя макросами, и тип
// памяти брал из DEMO. Если он здесь - останавливаемся с внятным сообщением,
// иначе такая конфигурация тихо собралась бы как "флеш 64 КБ": для платы с
// FRAM это половина чипа и запись с чужими таймингами.
#if defined(I2C_FRAM_MEMORY) || defined(I2C_MEMORY_TOTAL) || defined(I2C_ADR_EEPROM) \
 || defined(I2C_SIZE_EEPROM) || defined(I2C_PAGE_EEPROM)
#error "Config.h старого образца (2.89 и раньше): уберите из него I2C_FRAM_MEMORY, I2C_MEMORY_TOTAL, I2C_ADR_EEPROM, I2C_SIZE_EEPROM, I2C_PAGE_EEPROM и оставьте ОДИН макрос - I2C_EEPROM_64KB или I2C_FRAM_128KB. Что выбрать, написано в Config.h над этой строкой."
#endif

// Ровно один, не ноль и не два: при двух препроцессор молча взял бы ту ветку,
// что написана первой.
#if defined(I2C_EEPROM_64KB) + defined(I2C_FRAM_128KB) != 1
#error "В Config.h должен быть выбран РОВНО ОДИН макрос памяти: I2C_EEPROM_64KB или I2C_FRAM_128KB"
#endif

// Обе поддерживаемые микросхемы стоят на 0x50 и держат журнал у себя,
// поэтому общая часть раскладки от выбора не зависит.
#define I2C_ADR_EEPROM    0x50         // Адрес чипа (у FM24V10 - первого банка)
#define I2C_SIZE_EEPROM    512         // Объем ОДНОГО устройства в килобитах = 64 КБ
#define I2C_PAGE_EEPROM     64         // Размер страницы записи, байты (у FRAM не используется)
#ifdef I2C_FRAM_128KB                  // FM24V10: два банка по 64 КБ, 0x50 и 0x51
	#define I2C_MEMORY_TOTAL  1024     // Число устройств на шине = TOTAL/SIZE = 2
	#define I2C_FRAM_MEMORY      1     // Страниц нет, ожидание записи не нужно
	#define I2C_MEMORY_NAME   "FM24V10, FRAM 128 KB - journal in chip"
#else                                  // AT24C512
	#define I2C_MEMORY_TOTAL   512     // Число устройств на шине = TOTAL/SIZE = 1
	#define I2C_FRAM_MEMORY      0
	#define I2C_MEMORY_NAME   "AT24C512, 64 KB - journal in chip"
#endif

// Размер микросхемы в байтах. Он же адрес конца журнала - журнал занимает
// всё до конца чипа. Отдельным именем нужен ещё и потому, что выгрузка
// settings_eeprom.bin читает чип целиком и о журнале
// ничего не знает.
#define I2C_JOURNAL_EEPROM_NEXT (I2C_MEMORY_TOTAL * 1024 / 8)

// ---------------------------------------------------------------------------
// ---- Стартовые адреса ----
// КАРТА ПАМЯТИ в чипе i2c объемом 64 кбайта
// 0х0000 - I2C_COUNT_EEPROM хранение счетчиков, максимальный размер 0x79 (127) байт. Сейчас используется 52 байта
// 0х0080 - I2C_SETTING_EEPROM хранение настроек ТН  максимальный размер 0х980 (2432) байт. Сейчас используется 1343 байт
// 0x0A00 - I2C_PROFILE_EEPROM хранение профилей, максимальный размер 0x1000 (4096) байт. Число профилей 5 шт. Размер профиля сейчас 294 байт (1470)
// 0x1A00 - I2C_SCHEDULER_EEPROM хранения расписаний, максимум 0x5FE (1534) байт (сейчас 350 байта)
// 0х1FFE - I2C_JOURNAL_EEPROM хранение журнала размер журнала область должна быть кратна W5200_MAX_LEN
	#define I2C_PROFIL_NUM			5            // Максимальное число сохраняемых профилей
	// Адреса областей EEPROM при сокращении НЕ двигаются - область
	// профилей просто используется наполовину; экономится кеш профилей в ОЗУ,
	// по sizeof(ProfileSlot) на слот.
	//
	// Слоты 5..9 отбрасываются на переходе 147 -> 150: цикл чтения старой
	// раскладки до них просто не доходит, а первое же сохранение записывает
	// область уже в сокращённом виде. В САМОМ формате 150 запись с номером
	// слота 5..9 - отказ чтения, а не отбрасывание: 150 существует только на
	// стенде, в поле его нет, и конверсия 150 -> 150 никому не нужна.
	//
	// Старая раскладка на носителе - массив из ДЕСЯТИ слотов подряд, и это
	// свойство уже записанной EEPROM, а не настройка. Длина той области
	// считается по этой константе, иначе выгрузка settings.bin до конверсии
	// обрежет половину.
	#define I2C_PROFIL_NUM_LEGACY	10           // Число слотов в СТАРОЙ раскладке области
	#define I2C_COUNT_EEPROM		0x0000      // Адрес внутри чипа eeprom от куда пишется счетчики с начала чипа 0
	#define I2C_SETTING_EEPROM		0x0080      // Адрес внутри чипа eeprom от куда пишутся настройки ТН  а перед ним пишется счетчики
	#define I2C_PROFILE_EEPROM		0x0A00      // Адрес внутри чипа eeprom от куда профили (адрес первого профиля)
	#define I2C_SCHEDULER_EEPROM	0x1A00		// Адрес внутри чипа eeprom для Расписаний
	#define MAX_CALENDARS			5   		// число расписаний (старые данные при чтении обрезаются)
	#define MAX_CALENDARS_OLD		9   		// число расписаний в записях, сделанных прежними прошивками
	#define TIMETABLES_MAXSIZE		500 		// bytes
	#define DAILY_SWITCH_MAX		10			// Максимальное количество записей ежедневного включения устройств (DailySwitch). MAX=10
	#define I2C_JOURNAL_EEPROM 		0x1FFE      // Адрес с которого начинается журнал в памяти i2c, вначале лежит признак форматирования журнала. Длина журнала JOURNAL_LEN
	#define I2C_JOURNAL_START 		(I2C_JOURNAL_EEPROM + 2)      // Адрес с которого начинается ДАННЫЕ журнал в памяти i2c ВНИМАНИЕ - 2 байт лежит признак форматирования журнала
	// Журнал
	#define JOURNAL_LEN 			((I2C_JOURNAL_EEPROM_NEXT-I2C_JOURNAL_START)/W5200_MAX_LEN*W5200_MAX_LEN)// Размер журнала - округление на целое число страниц W5200_MAX_LEN
	#define I2C_JOURNAL_HEAD   		(0x01)                                                                  // Признак головы журнала
	#define I2C_JOURNAL_TAIL   		(0x02)                                                                  // Признак хвоста журнала
	#define I2C_JOURNAL_FORMAT 		(0xff)                                                                  // Символ которым заполняется журнал при форматировании
	#define I2C_JOURNAL_READY  		(0x55aa)                                                                // Признак создания журнала - если его нет по адресу I2C_JOURNAL_START-2 то надо форматировать журнал (первичная инициализация)
// Тип записи сохранения, 16bit
#define SAVE_TYPE_END			0
#define SAVE_TYPE_sTemp			-1
#define SAVE_TYPE_sADC			-2
#define SAVE_TYPE_sInput		-3
#define SAVE_TYPE_sFrequency	-4
#define SAVE_TYPE_sIP			-5
#define SAVE_TYPE_dEEV			-6
#define SAVE_TYPE_dSDM			-7
#define SAVE_TYPE_clMQTT		-8
#define SAVE_TYPE_PwrCorr		-9
#define SAVE_TYPE_LIMIT			-100

// ---- НОВЫЙ формат сохранения, версия 150 ----
//
// Запись: [тег: uint16][длина: uint16][данные]. Тег означает РАСКЛАДКУ:
// несовместимое изменение структуры выражается новым тегом, а не подъёмом
// версии формата. Поле, добавленное в КОНЕЦ структуры, тега не меняет -
// читатель копирует min(что записано, что ждут), как и раньше.
//
// Начало блока подобрано так, чтобы версия легла на смещение 3 - туда же,
// где её держит старый формат. На этом держится выбор читателя:
//
//   [0..1] общая длина      [2] 0 (в старом формате здесь префикс длины, не 0)
//   [3] версия формата      [4] ЗАНЯТ, всегда 0        [5..7] резерв
//   [8..] записи
//
// Байт [4] держал версию, с которой конвертировали. Она оказалась ненужной:
// старая обработка нужна ТОЛЬКО до конверсии, а тогда её включает сам
// Option.ver; после конверсии все области уже перезаписаны в текущем формате,
// и применять её нельзя. Байт пишется нулём и ПЕРЕИСПОЛЬЗОВАТЬ ЕГО НЕЛЬЗЯ:
// прошивки до версии 2.87 читают его как эпоху и по нулю выключают проверку
// CRC расписания. Занять его можно будет, когда откат на них перестанет
// рассматриваться.
#define NEW_HDR_SIZE      8      // от начала блока до первой записи
#define NEW_OFS_VER       3      // смещение байта версии, см. SettingsLegacy.h
#define NEW_OFS_VERLEGACY 4      // занятое смещение, писать только 0

#define NTAG_END          0
#define NTAG_Option       1
#define NTAG_DateTime     2
#define NTAG_Network      3
#define NTAG_Message      4
#define NTAG_FC           5
#define NTAG_sTemp        6      // массив, первый байт данных - номер датчика
#define NTAG_sADC         7      // массив
#define NTAG_sInput       8      // массив
#define NTAG_sFrequency   9      // массив
#define NTAG_sIP         10      // массив
#define NTAG_dEEV        11
#define NTAG_dSDM        12
#define NTAG_clMQTT      13

// Теги записей в ДРУГИХ областях EEPROM. Значения подобраны так, чтобы их
// нельзя было спутать с началом старых данных - области читаются до того,
// как известна версия, и опознают себя сами.
//   Профиль: старый слот начинался с 0xaa, младший байт тега 0x5A.
//   Расписание: старые данные начинались с Flags и Active, а Active не
//   больше MAX_CALENDARS_OLD (9); старший байт тега 0x5A = 90, спутать нельзя.
#define NTAG_Profile     0x5A5A
#define NTAG_Scheduler   0x5AA5

// ---- EEV ----
// Константы фаз движения ЭРВ, три варианта (константы вариантов не менять!)
// Константы фаз лежат в StepMotorPhase.h: их нужно видеть и модулю
// StepMotorPhase.cpp, который Constant.h включить не может.

#ifndef DEFAULT_RULE_EEV
	#define DEFAULT_RULE_EEV   0			  // Формула по умолчанию
	#define DEFAULT_FREON_TYPE 0 			  // Типа фрона по умолчанию
	#define DEFAULT_OVERHEAT   400			  // Перегрев по умолчанию (сотые градуса)
#endif
#define EEV_MAX_INT_PID		    1             // Максимальный вклад интегральной составляющей в шагах

#define EEV_START_POS_LOW_TEMP	1000		// Нижняя граница температура для установки позиции при старте, в стотых
#define EEV_START_POS_HIGH_TEMP	4500		// Верхняя граница температура для установки позиции при старте, в стотых
#define EEV_STAT_ARRAY_SIZE		4
#ifndef EEV_SET_ZERO_OVERRIDE
#define EEV_SET_ZERO_OVERRIDE	40			// Добавка к полному закрытию при установке нуля, шаги
#endif
#ifndef EEV_OVERHEAT2_CRITICAL
#define EEV_OVERHEAT2_CRITICAL 30           // Критическое значение перегрева 2, сотые градуса
#endif

// ---- EVI ----
#define EVI_TEMP_CON      4000           // Температура конденсатора для включения EVI
#define EVI_TEMP_EVA      300            // Температура испарителя для включения EVI

// ---- ОБЩИЕ НАСТРОЙКИ ----
#define TEMP_WEATHER      0              // Температура при которой устанавливается целевая температура подачи для отопления (погодозависимость)
#define MIN_WEATHER       (7*100)        // Минимальная температура подачи при погодозависимости
#define MAX_WEATHER       (50*100)       // Максимальная температура подачи при погодозависимости
#define HYSTERESIS_RHEAD				20	// Гистерезис работы дополнительного тена отопления (вычитается из целевой) в сотых градуса
#define HYSTERESIS_RBOILER				30	// Гистерезис работы дополнительного тена ГВС догрева (вычитается из целевой) в сотых градуса
#define HYSTERESIS_BoilerTogetherHeatSt	500	// Гистерезис совместного нагрева бойлера с отоплением в сотых градуса
#define HYSTERESIS_BoilerTogetherHeatEn	200	// Гистерезис совместного нагрева бойлера с отоплением в сотых градуса
#define HYSTERESIS_BoilerAddHeat      	300	// Гистерезис нагрева бойлера до температуры догрева, в сотых градуса
#define HYSTERESIS_HeatFloor		 	30	// Гистерезис раздельного управления реле теплого пола
#define SALMONELLA_DAY    3              // День когда включается алгоритм обеззараживания воды (Понедельник 1 воскресенье 7)
#define SALMONELLA_HOUR   1              // Час когда включается алгоритм обеззараживания воды (должно быть 0 минут)
#define SALMONELLA_TEMP   (70*100)       // Температура которая поддерживается для обеззараживания (сотые градуса)
#define SALMONELLA_TIME   (240*60)       // Максимальная продолжительность цикла (сек), что бы цикл не длился бесконечно при не возможности достижения SALMONELLA_TEMP
//#define NIGHT_START_HOUR  23           // Начало ночного тарифа
//#define NIGHT_END_HOUR	  7              // Окончание точного тарифа
// Сброс тепла при нагреве ГВС
#ifndef BOILER_TEMP_COMP_RESET
#define BOILER_TEMP_COMP_RESET  500      // На сколько температура нагнетания (TCOMP) меньше максимальной при нагреве ГВС, при которой происходит сброс тепла в систему отопления
#endif
#define WEB0_OTHER_JOB_PERIOD   10*1000  // Периодичность других функций внутри задачи WEB0, мс
#define HTTP_REQUEST_ERR_REPEAT 120      // Повтор информирования при ошибке через n секунд
// ---- SENSOR TEMP ----
#ifndef T_NUMSAMLES
#define T_NUMSAMLES       1              // Число значений для усреднения показаний температуры
#define GAP_TEMP_VAL      300      		 // Допустимая разница (в сотых C) показаний между двумя считываниями (борьба с помехами) - при привышении ошибка не возникает, но данные пропускаются.
#define GAP_NUMBER        3  		     // Максимальное число идущих подряд показаний превышающих GAP_TEMP_VAL, после этого эти показания выдаются за действительные
#define GAP_TEMP_VAL_CRC  200     	     // Датчики с флагом игнорировать CRC. Допустимая разница (в сотых C) показаний между двумя считываниями (борьба с помехами) - при привышении ошибка не возникает, но данные пропускаются.
#define GAP_NUMBER_CRC    5       	     // Датчики с флагом игнорировать CRC. Максимальное число идущих подряд показаний превышающих на GAP_TEMP_VAL, после этого эти показания выдаются за действительные
#endif
#define MAX_TEMP_ERR      700            // Максимальная систематическая ошибка датчика температуры (сотые градуса)
#define NUM_READ_TEMP_ERR 10             // Число ошибок подряд чтения датчика температуры после которого считается что датчик не исправен
#define RES_ONEWIRE_ERR   3              // Число попыток сброса датчиков температуры перед генерацией ошибки шины

#define BASE_TIME_READ    10 //20        // Частотные датчики - время (сек) на котором измеряется число импульсов, в конце идет пересчет в частоту

#define UPDATE_IP         120            // Время с получения последнего пакета от удаленного датчика (в сек) после которого считается что датчик не активен и не используетс в расчетах

// ---- MQTT ----
#define MQTT_REPEAT                      // Делать попытку повторного соединения с сервером
#define MQTT_NUM_ERR_OFF   16             // Число ошибок отправки подряд при котором отключается сервис отправки MQTT (флаг сбрасывается)

#define DEFAULT_PORT_MQTT 1883           // Адрес порта сервера MQTT по умолчанию
#define DEFAULT_TIME_MQTT (3*60)         // период отправки на сервер в сек. 10...60000
#define DEFAULT_ADR_MQTT "mqtt.thingspeak.com"   // Адрес MQTT сервера по умолчанию
#define DEFAULT_ADR_NARMON "narodmon.ru" // Адрес сервера народного мониторинга
#define TIME_NARMON       (5*60)         // (сек) Период отправки на народный мониторинг (константа) меньше 5 минут не ставить
#define DEFAULT_PORT_NARMON 8283         // Адрес порта сервера народного мониторинга по умолчанию (TCP протокол с декабря 2022)
#define DEFAULT_LOGIN_NARMON "login"     // Логин владельца по умолчанию, пока он не изменен метрика OWNER не передается
#define NARMON_RETRY      60             // (сек) Повтор отправки если пакет не доставлен. МЕНЬШЕ 60 НЕ СТАВИТЬ - прибор попадет в бан
// Отправка на народный мониторинг НЕ ПРЕКРАЩАЕТСЯ никогда. После
// NARMON_NUM_ERR_SLOW неудач подряд она переходит на редкий период NARMON_SLOW и
// живёт так, пока связь не восстановится: в этом режиме неудачи уже не
// считаются. Первая удачная отправка возвращает штатный период и обнуляет
// счётчик. Число неудач то же, что у MQTT: MQTT_NUM_ERR_OFF.
#define NARMON_NUM_ERR_SLOW MQTT_NUM_ERR_OFF  // Неудач подряд до перехода на редкую отправку
#define NARMON_SLOW       (60*60)        // (сек) Период отправки в редком режиме
#define NARMON_PACKET_MAX 1024           // Предел размера пакета народного мониторинга (реально ~300 байт, буфер outBuf 6 кБ)
#define NARMON_ANSWER_MAX 96             // Буфер под ответ сервера, тексты ошибок протокола бывают длинными
#define NARMON_TO_ANSWER  5000UL         // Ожидание ответа сервера, мсек (сервер сам рвет соединение через 10 сек)
#define NARMON_POLL_FAST  2              // Шаг опроса сокета в начале ожидания ответа, мсек
#define NARMON_POLL_SLOW  20             // Шаг опроса сокета дальше, мсек
#define NARMON_POLL_FAST_N 10            // Сколько первых итераций опрашивать часто

// ---- HEAP ----
#define PASS_LEN          10             // Максимальная длина пароля для входа на контроллер
#define NAME_USER         "user"         // имя пользователя
#define NAME_ADMIN        "admin"        // имя администратора
//#define FILE_STATISTIC    "statistic.csv"// имя файла статистики за ТЕКУШИЙ период

#define HOUR_SIGNAL_LIFE  12             // Час когда генерится сигнал жизни

// ATOF_ERROR объявлен в Util.h - это контракт my_atof, а модуль Util.cpp
// включить Constant.h не может (тот определяет объекты).
#define K_VCC_POWER       338.2          // Коэффициент пересчета ацп в вольты для контроля питания (учет опоры) (UT71E результататы ЗИП 284.02 ТН 338.2)
#define HEAT_CAPACITY     4174           // теплоемкость жидкости в конутре по дефолту при 30 градусах [Cp, Дж/(кг·град)]

#define HTTP_REQ_BUFFER_SIZE 256

// ---- WEB ----
STRCONST(WEB_HEADER_OK_CT, 			"HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: ");
STRCONST(WEB_HEADER_TEXT_ATTACH, 	"text/plain\r\nContent-Disposition: attachment; filename=\"");
STRCONST(WEB_HEADER_BIN_ATTACH, 		"application/x-binary\r\nContent-Disposition: attachment; filename=\"");
STRCONST(WEB_HEADER_TXT_KEEP, 		"text/html\r\nConnection: keep-alive");
STRCONST(WEB_HEADER_END,				"\r\n\r\n");
STRCONST(HEADER_FILE_NOT_FOUND, "HTTP/1.1 404 Not Found\r\n\r\n<html>\r\n<head><title>404 NOT FOUND</title><meta charset=\"utf-8\" /></head>\r\n<body><h1>404 NOT FOUND</h1></body>\r\n</html>\r\n\r\n");
STRCONST(HEADER_FILE_WEB, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nCache-Control: max-age=3600, must-revalidate\r\n\r\n"); // КЕШ ИСПОЛЬЗУЕМ
STRCONST(HEADER_FILE_NOCACHE, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nCache-Control: no-cache\r\n\r\n"); // меню зависит от роли: по адресу кэшировать нельзя
STRCONST(HEADER_FILE_CSS, "HTTP/1.1 200 OK\r\nContent-Type: text/css\r\nConnection: keep-alive\r\nCache-Control: max-age=3600, must-revalidate\r\n\r\n"); // КЕШ ИСПОЛЬЗУЕМ
STRCONST(HEADER_ANSWER, "HTTP/1.1 200 OK\r\nContent-Type: text/ajax\r\nAccess-Control-Allow-Origin: *\r\n\r\n");    // начало ответа на запрос
// Ответ /api: "HTTP/1.1 " + статус + HEADER_API_TAIL + длина + "\r\n\r\n" + тело JSON. Content-Length обязателен: по нему браузер отличает целый ответ от оборванного.
STRCONST(HEADER_API_TAIL, "\r\nContent-Type: application/json; charset=utf-8\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: ");
extern volatile uint8_t fWebUploadingFilesTo;              // Куда грузим файлы: 1 - SPI flash, 2 - SD card

// ---- Константы регистров контроллера питания SOPC SAM3x ----
// Регистр SMMR
// Уровень контроля питания ядра
#define   SUPC_SMMR_SMTH_1_9V (0x0u << 0) 
#define   SUPC_SMMR_SMTH_2_0V (0x1u << 0) 
#define   SUPC_SMMR_SMTH_2_1V (0x2u << 0) 
#define   SUPC_SMMR_SMTH_2_2V (0x3u << 0) 
#define   SUPC_SMMR_SMTH_2_3V (0x4u << 0) 
#define   SUPC_SMMR_SMTH_2_4V (0x5u << 0) 
#define   SUPC_SMMR_SMTH_2_5V (0x6u << 0) 
#define   SUPC_SMMR_SMTH_2_6V (0x7u << 0) 
#define   SUPC_SMMR_SMTH_2_7V (0x8u << 0) 
#define   SUPC_SMMR_SMTH_2_8V (0x9u << 0) 
#define   SUPC_SMMR_SMTH_2_9V (0xAu << 0) 
#define   SUPC_SMMR_SMTH_3_0V (0xBu << 0) 
#define   SUPC_SMMR_SMTH_3_1V (0xCu << 0) 
#define   SUPC_SMMR_SMTH_3_2V (0xDu << 0) 
#define   SUPC_SMMR_SMTH_3_3V (0xEu << 0) 
#define   SUPC_SMMR_SMTH_3_4V (0xFu << 0) 
// Время контроля питания
#define   SUPC_SMMR_SMSMPL_SMD (0x0u << 8)       // запрещено
#define   SUPC_SMMR_SMSMPL_CSM (0x1u << 8)       // непрерывно
#define   SUPC_SMMR_SMSMPL_32SLCK (0x2u << 8) 
#define   SUPC_SMMR_SMSMPL_256SLCK (0x3u << 8) 
#define   SUPC_SMMR_SMSMPL_2048SLCK (0x4u << 8) 
#define   SUPC_SMMR_SMRSTEN (0x1u << 12) /**< \brief (SUPC_SMMR) Supply Monitor Reset Enable */
#define   SUPC_SMMR_SMRSTEN_NOT_ENABLE (0x0u << 12) /**< \brief (SUPC_SMMR) the core reset signal "vddcore_nreset" is not affected when a supply monitor detection occurs. */
#define   SUPC_SMMR_SMRSTEN_ENABLE (0x1u << 12) /**< \brief (SUPC_SMMR) the core reset signal, vddcore_nreset is asserted when a supply monitor detection occurs. */
#define   SUPC_SMMR_SMIEN (0x1u << 13) /**< \brief (SUPC_SMMR) Supply Monitor Interrupt Enable */
#define   SUPC_SMMR_SMIEN_NOT_ENABLE (0x0u << 13) /**< \brief (SUPC_SMMR) the SUPC interrupt signal is not affected when a supply monitor detection occurs. */
#define   SUPC_SMMR_SMIEN_ENABLE (0x1u << 13) /**< \brief (SUPC_SMMR) the SUPC interrupt signal is asserted when a supply monitor detection occurs. */
// Регистр MR
#define SUPC_MR_KEY_Pos 24
#define SUPC_MR_KEY_Msk (0xffu << SUPC_MR_KEY_Pos) // Ключ для записи!!!
#define SUPC_MR_KEY(value) ((SUPC_MR_KEY_Msk & ((value) << SUPC_MR_KEY_Pos)))
#define SUPC_MR_BODRSTEN (0x1u << 12) /**< \brief (SUPC_MR) Brownout Detector Reset Enable */
#define SUPC_MR_BODRSTEN_NOT_ENABLE (0x0u << 12) /**< \brief (SUPC_MR) the core reset signal "vddcore_nreset" is not affected when a brownout detection occurs. */
#define SUPC_MR_BODRSTEN_ENABLE (0x1u << 12) /**< \brief (SUPC_MR) the core reset signal, vddcore_nreset is asserted when a brownout detection occurs. */
#define SUPC_MR_BODDIS (0x1u << 13)     // Вот это /**< \brief (SUPC_MR) Brownout Detector Disable */
#define SUPC_MR_BODDIS_ENABLE (0x0u << 13) /**< \brief (SUPC_MR) the core brownout detector is enabled. */
#define SUPC_MR_BODDIS_DISABLE (0x1u << 13) /**< \brief (SUPC_MR) the core brownout detector is disabled. */

// FORMAT_DATE_STR определён в DateTime.cpp - используется только там.

// ---------------------------------------------------------------------------
// ---- Строковые константы многократно используемые по всем файлам ----
STRCONST(cYes, "Да");
STRCONST(cNo, "Нет");
STRCONST(cOne, "1");
STRCONST(cZero, "0");
STRCONST(cOk, "Ok");
STRCONST(cError, "error");
STRCONST(cInvalid, "invalid");
STRCONST(cStrEnd, "\n");
STRCONST(cErrorRS485, "%s: Read error %s, code=%d repeat . . .\n"); // имя, функция, код
STRCONST(cErrorMutex, "Function %s: %s, mutex is busy\n");   // функция, мютекс
STRCONST(cAddHeat, "+");                                     // Значек нагрева ГВС ТЭНом
STRCONST(http_get_str1,  "GET ");
STRCONST(http_get_str2,  " HTTP/1.0\r\nHost: ");
STRCONST(http_get_str3,  "\r\nAccept: text/html\r\n\r\n");
STRCONST(http_key_ok1,  "HTTP/"); // "1.1"
STRCONST(http_key_ok2,  " 200 OK\r\n");
const uint8_t save_end_marker[1] = { 0 };
#define WEBDELIM	"\x7f" // ALT+127 разделитель строки в вебе
STRCONST(SendMessageTitle,	"Народный контроллер теплового насоса");
STRCONST(SendSMSTitle, 		"Control");


// Многозадачность, деление аппаратных ресурсов
STRCONST(nameFREERTOS, "FreeRTOS");         // Имя источника ошибки (нужно для передачи в функцию) - операционная система
STRCONST(nameHeatPump, "Heat Pump");         // Имя теплового насоса (для лога ошибок) Здесь можно его поменять
STRCONST(MutexI2CBusy, "I2C"); 
STRCONST(MutexModbusBusy, "Modbus"); 
STRCONST(MutexWebThreadBusy, "WebThread"); 
STRCONST(MutexSPIBusy, "SPI"); 
STRCONST(MutexCommandBusy, "Command"); 

// Описание имен параметров ЭРВ для функций get_pEEV set_pEEV
STRCONST(eev_POS, "POS");                // Положение ЭРВ шаги
STRCONST(eev_POSp, "POSp");              // Положение ЭРВ %
STRCONST(eev_POSpp, "POSpp");            // Положение ЭРВ шаги+%
STRCONST(eev_OVERHEAT, "OH");            // Текущий перегрев ЭРВ, если
STRCONST(eev_ERROR, "ERROR");            // Ошибка ЭРВ
STRCONST(eev_MIN, "MIN");                // Минимум ЭРВ
STRCONST(eev_MAX, "MAX");                // Максимум ЭРВ
STRCONST(eev_TIME, "TIME");              // ПИД время в секундах ЭРВ СЕКУНДЫ
STRCONST(eev_TARGET, "TRG");           // Перегрев ЦЕЛЬ (сотые градуса)
STRCONST(eev_tOverheatTCOMP, "TRG2");    // Перегрев2 цель (сотые градуса)
STRCONST(eev_tOverheat2_low, "T2L");
STRCONST(eev_tOverheat2_low_hyst, "T2H");
STRCONST(eev_tOverheatTCOMP_delta, "TRG2D"); // Перегрев2 дельта цель (сотые градуса)
STRCONST(eev_KP, "KP");                  // ПИД Коэф пропорц.   В ТЫСЯЧНЫХ!!!
STRCONST(eev_KI, "KI");                  // ПИД Коэф интегр.  для настройки Ki=0   В ТЫСЯЧНЫХ!!!
STRCONST(eev_KD, "KD");                  // ПИД Коэф дифф.    В ТЫСЯЧНЫХ!!!
STRCONST(eev_KP2, "KP2");                // ПИД Коэф пропорц.   В ТЫСЯЧНЫХ!!!
STRCONST(eev_KI2, "KI2");                // ПИД Коэф интегр.  для настройки Ki=0   В ТЫСЯЧНЫХ!!!
STRCONST(eev_KD2, "KD2");                // ПИД Коэф дифф.    В ТЫСЯЧНЫХ!!!
STRCONST(eev_PID2_delta, "P2D");        // Дельта для консервативных вычислений ПИДа (Для формулы 1  это ошибка при которой начинает уменьшаться пропорциональная)
STRCONST(eev_PID_MAX, "PMAX");           // ограничение ПИД в шагах ЭРВ
STRCONST(eev_CONST, "CONST");            // Корректировка перегрева (постоянная ошибка)
STRCONST(eev_MANUAL, "MANUAL");          // Число шагов открытия ЭРВ для правила работы ЭРВ «Manual»
STRCONST(eev_FREON, "FREON");            // Тип фреона
STRCONST(eev_RULE, "RULE");              // Правило работы ЭРВ
STRCONST(eev_NAME, "NAME");              // Имя ЭРВ
STRCONST(eev_NOTE, "NOTE");              // Описание ЭРВ
STRCONST(eev_REMARK, "REMARK");          // Описание алгоритма ЭРВ
STRCONST(eev_PINS, "PINS");              // Перечисление ног куда привязана ЭРВ
STRCONST(eev_cCORRECT, "cCORRECT");      // Флаг включения корректировки перегрева от разности температур конденсатора и испарителя
STRCONST(eev_cDELAY, "cDELAY");          // Задержка после старта компрессора, сек
STRCONST(eev_cPERIOD, "cPERIOD");        // Период в циклах ЭРВ, сколько пропустить
STRCONST(eev_cDELTA, "cD");              // TDIS_TCON: Температура нагнетания - конденсации (сотые градуса)
STRCONST(eev_cDELTA_Thr, "cDT");         // Порог, после превышения которого начинаем менять перегрев, в сотых градуса
STRCONST(eev_cOH_cDELTA_MAX, "cDM");     // верхняя граница для пропорционального увеличения перегрева, % от OHCor_TDIS_TCON
STRCONST(eev_cOH_MIN, "cOH_MIN");        // Минимальный перегрев (сотые градуса)
STRCONST(eev_cOH_START, "cOH_START");    // Стартовый перегрев (сотые градуса)
STRCONST(eev_cOH_MAX, "cOH_MAX");        // Максимальный перегрев (сотые градуса)
STRCONST(eev_cOH_TDELTA, "cTDELTA");    // Расчитанная целевая дельта Нагнетание-Конденсации
STRCONST(eev_ERR_KP, "ERR_KP");          // Ошибка (в сотых градуса) при которой происходит уменьшение пропорциональной составляющей ПИД ЭРВ
STRCONST(eev_SPEED, "SPEED");            // Скорость шагового двигателя ЭРВ (импульсы в сек.)
STRCONST(eev_PRE_START_POS, "PSP");      // ПУСКОВАЯ позиция ЭРВ (ТО что при старте компрессора ПРИ РАСКРУТКЕ)
STRCONST(eev_START_POS, "SP");           // СТАРТОВАЯ позиция ЭРВ после раскрутки компрессора т.е. ПОЗИЦИЯ С КОТОРОЙ НАЧИНАЕТСЯ РАБОТА проходит DelayStartPos сек
STRCONST(eev_DELAY_ON_PID, "DOP");       // Задержка включения EEV после включения компрессора (сек).  Точнее после выхода на рабочую позицию Общее время =delayOnPid+DelayStartPos
STRCONST(eev_DELAY_START_POS, "DSP");    // Время после старта компрессора когда EEV выходит на стартовую позицию - облегчение пуска вначале ЭРВ
STRCONST(eev_DELAY_OFF, "DOFF");         // Задержка закрытия EEV после выключения насосов (сек). Время от команды стоп компрессора до закрытия ЭРВ = delayOffPump+delayOff
STRCONST(eev_DELAY_ON, "DON");           // Задержка между открытием (для старта) ЭРВ и включением компрессора, для выравнивания давлений (сек). Если ЭРВ закрывлось при остановке
STRCONST(eev_HOLD_MOTOR, "HM");    // Флаг удержания мотора
STRCONST(eev_PRESENT, "PRESENT");        // Флаг наличия ЭРВ в ТН
STRCONST(eev_SEEK_ZERO, "ZERO");         // Флаг однократного поиска "0" ЭРВ (только при первом включении ТН)
STRCONST(eev_CLOSE, "CLOSE");            // Флаг закрытие ЭРВ при выключении компрессора
STRCONST(eev_LIGHT_START, "LST");        // флаг Облегчение старта компрессора   приоткрытие ЭРВ в момент пуска компрессора
STRCONST(eev_START, "START");            // флаг Всегда начинать работу ЭРВ со стратовой позици
STRCONST(eev_PID_P_ON_M, "POM");         // флаг ПИД пропорционально измерению
STRCONST(eev_fEEVStartPosByTemp, "SPT"); // флаг fEEVStartPosByTemp
STRCONST(eev_PosAtHighTemp, "PHT"); // PosAtHighTemp
STRCONST(eev_fEEV_DirectAlgorithm, "DIR"); // флаг fEEV_DirectAlgorithm
STRCONST(eev_trend_threshold, "TTH");
STRCONST(eev_trend_mul_threshold, "TMT");

// Описание имен параметров MQTT для функций get_paramMQTT set_paramMQTT
STRCONST(mqtt_USE_TS, "USE_TS");              // флаг использования ThingSpeak - формат передачи для клиента
STRCONST(mqtt_USE_MQTT, "USE_MQTT");          // флаг использования MQTT
STRCONST(mqtt_BIG_MQTT, "BIG_MQTT");          // флаг отправки ДОПОЛНИТЕЛЬНЫХ данных на MQTT
STRCONST(mqtt_SDM_MQTT, "SDM_MQTT");          // флаг отправки данных электросчетчика на MQTT
STRCONST(mqtt_FC_MQTT, "FC_MQTT");            // флаг отправки данных инвертора на MQTT
STRCONST(mqtt_COP_MQTT, "COP_MQTT");          // флаг отправки данных COP на MQTT
STRCONST(mqtt_TIME_MQTT, "TIME_MQTT");        // период отправки на сервер в сек. 10...60000
STRCONST(mqtt_ADR_MQTT, "ADR_MQTT");          // Адрес сервера
STRCONST(mqtt_IP_MQTT, "IP_MQTT");            // IP Адрес сервера
STRCONST(mqtt_PORT_MQTT, "PORT_MQTT");        // Адрес порта сервера
STRCONST(mqtt_LOGIN_MQTT, "LOGIN_MQTT");      // логин сервера
STRCONST(mqtt_PASSWORD_MQTT, "PASSWORD_MQTT"); // пароль сервера
STRCONST(mqtt_ID_MQTT, "ID_MQTT");            // Идентификатор клиента на MQTT сервере
   // народный мониторинг
STRCONST(mqtt_USE_NARMON, "USE_NARMON");      // флаг отправки данных на народный мониторинг
STRCONST(mqtt_BIG_NARMON, "BIG_NARMON");      // флаг отправки данных на народный мониторинг ,большую версию
STRCONST(mqtt_SLOW_NARMON, "SLOW_NARMON");    // ТОЛЬКО ЧТЕНИЕ: 1 - отправка на редком периоде после серии неудач
STRCONST(mqtt_ADR_NARMON, "ADR_NARMON");      // Адрес сервера народного мониторинга
STRCONST(mqtt_IP_NARMON, "IP_NARMON");        // IP Адрес сервера народного мониторинга
STRCONST(mqtt_PORT_NARMON, "PORT_NARMON");    // Адрес порта сервера  народного мониторинга
STRCONST(mqtt_LOGIN_NARMON, "LOGIN_NARMON");  // логин сервера народного мониторинга
STRCONST(mqtt_PASSWORD_NARMON, "PASSWORD_NARMON");// пароль сервера народного мониторинга
STRCONST(mqtt_ID_NARMON, "ID_NARMON");        // Идентификатор клиента на MQTT сервере

// Описание имен параметров SDM счетчика для функций get_paramSDM ("get_SDM"), set_paramSDM ("set_SDM")
STRCONST(sdm_NAME, "NAME");                // Имя счетчика
STRCONST(sdm_NOTE, "NOTE");                // Описание счетчика
STRCONST(sdm_MAX_VOLTAGE, "MAXV");  // Контроль напряжения максимум
STRCONST(sdm_MIN_VOLTAGE, "MINV");  // Контроль напряжения минимум
STRCONST(sdm_MAX_POWER, "MAXP");      // Контроль мощности максимум
STRCONST(sdm_VOLTAGE, "VOLT");          // Напряжение
STRCONST(sdm_CURRENT, "CURRENT");          // Ток
STRCONST(sdm_REPOWER, "REPOWER");          // Реактивная мощность
STRCONST(sdm_ACPOWER, "ACPOWER");          // Активная мощность
STRCONST(sdm_POWER, "POWER");              // Полная мощность
STRCONST(sdm_POW_FACTOR, "PF");    // Коэффициент мощности
STRCONST(sdm_PHASE, "PHASE");              // Угол фазы (градусы)
STRCONST(sdm_FREQ, "FREQ");                // Частота
STRCONST(sdm_ACENERGY, "ACENERGY");        // Суммарная активная энергия
STRCONST(sdm_LINK, "LINK");                // Состояние связи со счетчиком
STRCONST(sdm_ERRORS, "ERR");                // Ошибок чтения Modbus

// Описание имен параметров профиля для функций get_paramProfile set_paramProfile	
STRCONST(prof_NAME_PROFILE, "NAME");   // Имя профиля до 10 русских букв
STRCONST(prof_ENABLE_PROFILE, "ENABLE"); // разрешение использовать в списке
STRCONST(prof_ID_PROFILE, "ID");       // номер профиля, нумерация c 1
STRCONST(prof_NOTE_PROFILE, "NOTE");   // описание профиля
STRCONST(prof_DATE_PROFILE, "DATE");   // дата профиля
STRCONST(prof_CRC16_PROFILE, "CRC16"); // контрольная сумма профиля
STRCONST(prof_NUM_PROFILE, "NUM");     // максимальное число профилей
STRCONST(prof_TBL_PROFILE, "TBL");     // таблица всех слотов профилей одним ответом
STRCONST(prof_SEL_PROFILE, "SEL");     // список профилей (пока не используется)
STRCONST(prof_DailySwitch, 	"DS");
const char prof_DailySwitchDevice = 'D';		// DSD
const char prof_DailySwitchOn  	= 'S';			// DSS
const char prof_DailySwitchOff 	= 'E';			// DSE

// Описание имен параметров уведомлений для функций set_messageSetting get_messageSetting
STRCONST(mess_MAIL, "MAIL");                  // флаг уведомления скидывать на почту
STRCONST(mess_MAIL_AUTH, "MAIL_AUTH");        // флаг необходимости авторизации на почтовом сервере
STRCONST(mess_MAIL_INFO, "MAIL_INFO");        // флаг необходимости добавления в письмо информации о состоянии ТН
STRCONST(mess_SMS, "SMS");                    // флаг уведомления скидывать на СМС
STRCONST(mess_MESS_RESET, "MESS_RESET");      // флаг уведомления Сброс
STRCONST(mess_MESS_ERROR, "MESS_ERROR");      // флаг уведомления Ошибка
STRCONST(mess_MESS_LIFE, "MESS_LIFE");        // флаг уведомления Сигнал жизни
STRCONST(mess_MESS_TEMP, "MESS_TEMP");        // флаг уведомления Достижение граничной температуры
STRCONST(mess_MESS_SD, "MESS_SD");            // флаг уведомления "Проблемы с sd картой"
STRCONST(mess_MESS_WARNING, "MESS_WARNING");  // флаг уведомления "Прочие уведомления"
STRCONST(mess_SMTP_SERVER, "SMTP_SERVER");    // Адрес сервера
STRCONST(mess_SMTP_IP, "SMTP_IP");            // IP Адрес сервера
STRCONST(mess_SMTP_PORT, "SMTP_PORT");        // Адрес порта сервера
STRCONST(mess_SMTP_LOGIN, "SMTP_LOGIN");      // логин сервера если включена авторизация
STRCONST(mess_SMTP_PASS, "SMTP_PASS");        // пароль сервера если включена авторизация
STRCONST(mess_SMTP_MAILTO, "SMTP_MAILTO");    // адрес отправителя
STRCONST(mess_SMTP_RCPTTO, "SMTP_RCPTTO");    // адрес получателя
STRCONST(mess_SMS_SERVICE, "SMS_list");       // сервис отправки смс
STRCONST(mess_SMS_IP, "SMS_IP");              // IP Адрес сервера для отправки смс
STRCONST(mess_SMS_PHONE, "SMS_PHONE");        // телефон куда отправляется смс
STRCONST(mess_SMS_P1, "SMS_P1");              // первый параметр для отправки смс
STRCONST(mess_SMS_P2, "SMS_P2");              // второй параметр для отправки смс
STRCONST(mess_SMS_NAMEP1, "SMS_NAMEP1");      // описание первого параметра для отправки смс
STRCONST(mess_SMS_NAMEP2, "SMS_NAMEP2");      // описание второго параметра для отправки смс
STRCONST(mess_MESS_TIN, "MESS_TIN");          // Критическая температура в доме (если меньше то генерится уведомление)
STRCONST(mess_MESS_TBOILER, "MESS_TBOILER");  // Критическая температура бойлера (если меньше то генерится уведомление)
STRCONST(mess_MESS_TCOMP, "MESS_TCOMP");      // Критическая температура компрессора (если больше то генериться уведомление)
STRCONST(mess_MAIL_RET, "scan_MAIL");         // Ответ на тестовую почту
STRCONST(mess_SMS_RET, "scan_SMS");           // Ответ на тестовую  sms

// Описание имен параметров бойлера для функций set_Boiler get_Boiler
STRCONST(boil_BOILER_ON, "ON");               // флаг Включения бойлера
STRCONST(boil_SCHEDULER_ON, "SCH_ON");     // флаг Использование расписания
STRCONST(boil_SCHEDULER_ADDHEAT, "SCH_AH");   // флаг Использование расписания только для ТЭНа
STRCONST(boil_TURBO_BOILER, "TURBO");         // флаг ТУРБО ГВС нагрев (нагрев=ТН+ТЭН)
STRCONST(boil_SALMONELLA, "SLMN");            // флаг Сальмонела раз в неделю греть бойлер
STRCONST(boil_fSalmonellaHold, "SLMNH");      // флаг Длительная обработка сальмонеллы (держать температуру до конца цикла)
STRCONST(boil_CIRCULATION, "CIRC");        // флаг Управления циркуляционным насосом ГВС
STRCONST(boil_TEMP_TARGET, "TRG");            // Целевая температура бойлера
STRCONST(boil_DTARGET, "DTRG");               // гистерезис целевой температуры
STRCONST(boil_TEMP_MAX, "MAX");               // Температура подачи максимальная
STRCONST(boil_SCHEDULER, "SCHEDULER");        // Расписание
STRCONST(boil_CIRCUL_WORK, "CIRCW");          // Время  работы насоса ГВС секунды (fCirculation)
STRCONST(boil_CIRCUL_PAUSE, "CIRCP");         // Пауза в работе насоса ГВС  секунды (fCirculation)
STRCONST(boil_RESET_HEAT, "RESH");            // флаг Сброса лишнего тепла в СО
STRCONST(boil_RESET_TIME, "RESHT");           // время сброса излишков тепла в СО в секундах (fResetHeat)
STRCONST(boil_BOIL_TIME, "PT");               // Постоянная интегрирования времени в секундах ПИД ТН
STRCONST(boil_BOIL_PRO, "PP");                // Пропорциональная составляющая ПИД ГВС
STRCONST(boil_BOIL_IN, "PI");                 // Интегральная составляющая ПИД ГВС
STRCONST(boil_BOIL_DIF, "PD");                // Дифференциальная составляющая ПИД ГВС
STRCONST(boil_BOIL_TEMP, "TEMP");             // Целевая температура ПИД ГВС
STRCONST(boil_ADD_HEATING, "ADDH");           // флаг ДОГРЕВА ГВС ТЭНом
STRCONST(boil_fAddHeatingForce, "AHF");       // флаг Включать догрев, если компрессор не нагрел бойлер до температуры догрева
STRCONST(boil_TEMP_RBOILER, "TEMPR");         // температура включения догрева бойлера
STRCONST(boil_TOGETHER_HEAT, "TGHEAT");       // флаг Греть совместно с отоплением, если ТН работает на отопление
STRCONST(boil_fBoilerPID, "PID");             // флаг ПИД вкл/выкл
STRCONST(boil_dAddHeat, "dAH");   // Гистерезис нагрева бойлера до температуры догрева, в сотых градуса
STRCONST(boil_HeatUrgently, "URG"); // флаг Срочно нужно ГВС
STRCONST(boil_DischargeDelta, "DD");          // Сброс тепла в отопление при приближении подачи к максимальной/догреву на °С
STRCONST(boil_fHeaterOnly, "HONLY");          // флаг Бойлер греет только ТЭН (ТН бойлер не греет)

// Дата время
STRCONST(time_TIME, "TIME");         // текущее время  12:45 без секунд
STRCONST(time_DATE, "DATE");         // текущая дата типа  12/04/2016
STRCONST(time_NTP, "NTP");           // адрес NTP сервера строка до 60 символов.
STRCONST(time_UPDATE, "UPDATE");     // Время синхронизации с NTP сервером.
STRCONST(time_TIMEZONE, "TIMEZONE"); // Часовой пояс
STRCONST(time_UPDATE_I2C, "UPDATE_I2C"); // Синхронизация времени раз в час с i2c часами

// Сеть
STRCONST(net_IP, "IP");                 // Адрес 
STRCONST(net_DNS, "DNS");               // DNS
STRCONST(net_GATEWAY, "GATEWAY");       // Шлюз
STRCONST(net_SUBNET, "SUBNET");         // Маска подсети
STRCONST(net_DHCP, "DHCP");             // Флаг использования DHCP
STRCONST(net_MAC, "MAC");               // МАС адрес чипа
STRCONST(net_RES_SOCKET, "NSLS");       // Время сброса зависших сокетов
STRCONST(net_RES_W5200, "NSLR");        // Время регулярного сброса сетевого чипа
STRCONST(net_PASS, "PASS");             // Использование паролей (флаг)
STRCONST(net_PASSUSER, "PASSUSER");     // Пароль пользователя 
STRCONST(net_PASSADMIN, "PASSADMIN");   // Пароль администратора  
STRCONST(net_SIZE_PACKET, "SIZE");      // размер пакета
STRCONST(net_INIT_W5200, "INIT");       // Ежеминутный контроль SPI для сетевого чипа
STRCONST(net_PORT, "PORT");             // Port веб сервера
STRCONST(net_NO_ACK, "NO_ACK");         // Не ожидать ответа ack
STRCONST(net_DELAY_ACK, "DELAY_ACK");   // Задержка перед посылкой следующего пакета
STRCONST(net_PING_ADR, "PING");         // адрес для пинга
STRCONST(net_PING_TIME, "NSLP");        // время пинга в секундах
STRCONST(net_NO_PING, "NO_PING");       // запрет пинга контроллера

// Описание имен параметров инвертора  для функций get_paramFC ("get_pFC") set_paramFC ("set_pFC")
STRCONST(fc_ON_OFF, "ON_OFF");                 // Флаг включения выключения (управление инвертором)
STRCONST(fc_INFO, "INFO");                     // Получить информацию из инвертора (таблица !!)
STRCONST(fc_NAME, "NAME");                     // Имя инвертора 
STRCONST(fc_NOTE, "NOTE");                     // Получение описания частотного преобразователя. Строка 80+1
STRCONST(fc_PIN, "PIN");                       // Получение номера пина куда прицеплен analog FC
STRCONST(fc_PRESENT, "PRESENT");               // Наличие FC в конфигурации. 
STRCONST(fc_STATE, "STATE");                   // Состояние ПЧ (чтение)
STRCONST(fc_FC, "FC");                         // Целевая частота инвертора в 0.01
STRCONST(fc_cFC, "cFC");                       // Текущая частота ПЧ (чтение)
STRCONST(fc_cPOWER, "cPOWER");                 // Текущая мощность (чтение)
STRCONST(fc_INFO1, "INFO1");                   // Первая строка под картинкой инвертора на схеме
STRCONST(fc_cCURRENT, "cCURRENT");             // Текущий ток (чтение)
STRCONST(fc_AUTO_RESET_FAULT, "ARSTFLT");      // Флаг автоматического сброса не критичной ошибки инвертора
STRCONST(fc_ANALOG, "AN");                     // Флаг аналогового управления
STRCONST(fc_DAC, "DAC");                       // Получение текущего значения ЦАП
STRCONST(fc_LEVEL0, "L0");                     // Уровень частоты 0 в отсчетах ЦАП
STRCONST(fc_LEVEL100, "L100");                 // Уровень частоты 100% в  отсчетах ЦАП
STRCONST(fc_LEVELOFF, "LOFF");                 // Минимальная мощность при котором инвертор отключается (ограничение минимальной мощности)
STRCONST(fc_BLOCK, "BLOCK");                   // флаг глобальная ошибка инвертора - работа инвертора запрещена блокировку можно сбросить установив в 0
STRCONST(fc_ERROR, "ERROR");                   // Получить код ошибки
STRCONST(fc_UPTIME, "UPTIME");                 // Время обновления алгоритма пид регулятора (мсек) Основной цикл управления
STRCONST(fc_PID_STOP, "PID_STOP");             // Проценты от уровня защит (мощность, ток, давление, температура) при которой происходит блокировка роста частоты пидом
STRCONST(fc_PID_FREQ_STEP, "PID_STEP");        // Максимальный шаг (на увеличение) изменения частоты при ПИД регулировании в 0.01 Гц Необходимо что бы ЭРВ успевал
STRCONST(fc_START_FREQ, "FRQ");                // Стартовая частота инвертора (см компрессор) в 0.01 ГЦ
STRCONST(fc_START_FREQ_BOILER, "FRQB"); // Стартовая частота инвертора (см компрессор) в 0.01 ГЦ ГВС
STRCONST(fc_MIN_FREQ, "MIN");               // Минимальная  частота инвертора (см компрессор) в 0.01 Гц
STRCONST(fc_MIN_FREQ_COOL, "MINC");         // Минимальная  частота инвертора при охлаждении в 0.01 Гц
STRCONST(fc_MIN_FREQ_BOILER, "MINB");         // Минимальная  частота инвертора при нагреве ГВС в 0.01 Гц
STRCONST(fc_MIN_FREQ_USER, "MINU");         // Минимальная  частота инвертора РУЧНОЙ РЕЖИМ (см компрессор) в 0.01 Гц
STRCONST(fc_MAX_FREQ, "MAX");                // Максимальная частота инвертора (см компрессор) в 0.01 Гц
STRCONST(fc_MAX_FREQ_COOL, "MAXC");           // Максимальная частота инвертора в режиме охлаждения  в 0.01 Гц
STRCONST(fc_MAX_FREQ_BOILER, "MAXB");         // Максимальная частота инвертора в режиме ГВС в 0.01 Гц поглощение бойлера обычно меньше чем СО
STRCONST(fc_MAX_FREQ_USER, "MAXU");          // Максимальная частота инвертора РУЧНОЙ РЕЖИМ (см компрессор) в 0.01 Гц
STRCONST(fc_MAX_FREQ_GEN, "MAXG");           // Максимальная частота инвертора на генераторе в 0.01 Гц
STRCONST(fc_STEP_FREQ, "STEP");             // Шаг уменьшения инвертора при достижении максимальной температуры, мощности и тока (см компрессор) в 0.01 Гц
STRCONST(fc_STEP_FREQ_BOILER, "STEPB"); // Шаг уменьшения инвертора при достижении максимальной температуры, мощности и тока ГВС в 0.01 Гц
STRCONST(fc_DT_COMP_TEMP, "DTC");       // Защита по температуре компрессора - сколько градусов не доходит до максимальной (TCOMP) и при этом происходит уменьшение частоты
STRCONST(fc_DT_TEMP, "DT");                  // Превышение температуры от уставок (подача) при которой срабатыват защита (уменьшается частота) в сотых градуса
STRCONST(fc_DT_TEMP_BOILER, "DTB");      // Превышение температуры от уставок (подача) при которой срабатыват защита ГВС в сотых градуса
STRCONST(fc_MB_ERR, "MB_ERR");     // Ошибок Modbus
STRCONST(fc_FC_TIME_READ, "TR");     // Время опроса
STRCONST(fc_fFC_RetOil, "FRO");              // Флаг возврат масла
STRCONST(fc_FC_RETOIL_FREQ, "FRF"); // Частота
STRCONST(fc_ReturnOilPeriod, "ROP");           // Время возврата масла
STRCONST(fc_ReturnOilPerDivHz, "ROPH");        // Частота при которой возвращается масло
STRCONST(fc_ReturnOilEEV, "ROE");              // Шаги ЭРВ при котором возвращается масло

// Описание имен параметров опций ТН  для функций get_optionHP ("get_oHP") set_optionHP ("set_oHP")
STRCONST(option_ADD_HEAT, "HEAT_list");                  // использование дополнительного нагревателя (значения 1 и 0)
STRCONST(option_TEMP_RHEAT, "TEMP_RHEAT");           // температура для управления RHEAT (градусы)
STRCONST(option_PUMP_WORK, "PUMP_WORK");             // работа насоса конденсатора при выключенном компрессоре секунды
STRCONST(option_PUMP_PAUSE, "PUMP_PAUSE");           // пауза между работой насоса конденсатора при выключенном компрессоре (секунды)
STRCONST(option_ATTEMPT, "ATTEMPT");                 // число попыток пуска
STRCONST(option_TIME_CHART, "TIME_CHART");           // период сбора статистики
STRCONST(option_BEEP, "BEEP");                       // включение звука
STRCONST(option_NEXTION, "NXT");                     // использование дисплея nextion
STRCONST(option_NEXTION_WORK, "NXTW");               // Включать дисплей, когда ТН работает
STRCONST(option_History, "HIST");                    // запись истории на SD карту
STRCONST(option_HistoryDetail, "HISTDET");           // детализация истории ВКЛ/ВЫКЛ компрессора
STRCONST(option_SAVE_ON, "SAVE_ON");                 // флаг записи в EEPROM включения ТН (восстановление работы после перезагрузки)
STRCONST(option_NEXT_SLEEP, "NXTS");                 // Время засыпания секунды NEXTION
STRCONST(option_NEXT_DIM, "NXTD");                   // Якрость % NEXTION
STRCONST(option_DELAY_ON_PUMP, "DLONP");       // Задержка включения компрессора после включения насосов (сек).
STRCONST(option_DELAY_OFF_PUMP, "DLOFP");      // Задержка выключения насосов после выключения компрессора (сек).
STRCONST(option_DELAY_START_RES, "DLSR");    // Задержка включения ТН после внезапного сброса контроллера (сек.)
STRCONST(option_DELAY_REPEAD_START, "DLRS"); // Задержка перед повторным включениме ТН при ошибке (попытки пуска) секунды
STRCONST(option_DELAY_DEFROST_ON, "DLDON"); // ДЛЯ ВОЗДУШНОГО ТН Задержка после срабатывания датчика перед включением разморозки (секунды)
STRCONST(option_DELAY_DEFROST_OFF, "DLDOFF"); // ДЛЯ ВОЗДУШНОГО ТН Задержка перед выключением разморозки (секунды)
STRCONST(option_DELAY_R4WAY, "DLTRV");           // Задержка между переключением 4-х ходового клапана и включением компрессора, для выравнивания давлений (сек). Если включены эти опции (переключение тепло-холод)
STRCONST(option_DELAY_BOILER_SW, "DLBSW");    // Пауза (сек) после переключение ГВС - выравниваем температуру в контуре отопления/ГВС что бы сразу защиты не сработали
STRCONST(option_DELAY_BOILER_OFF, "DLBOFF");  // Время (сек) на сколько блокируются защиты при переходе с ГВС на отопление и охлаждение слишком горяче после ГВС
STRCONST(option_WebOnSPIFlash, "WSPIF");             // флаг, что веб морда лежит на SPI Flash, иначе на SD карте
STRCONST(option_PAUSE, "PAUSE");                     // минимальное время простоя компрессора
STRCONST(option_MinCompressorOn, "MCON");            // Минимальное время работы компрессора в секундах
STRCONST(option_Charts_when_comp_on, "CWCO"); // Графики в памяти только во время работы компрессора
STRCONST(option_fBackupPower, "BPOW");    // флаг Использование резервного питания от генератора (ограничение мощности) 
STRCONST(option_fBackupPowerInfo, "BP"); // Работа от генератора
STRCONST(option_maxBackupPower, "MAXPOW");  // Максимальная мощность при питании от генератора
STRCONST(option_fBackupPowerAuto, "BPA");


// Отопление/охлаждение параметры
STRCONST(hp_RULE, "RULE");            // алгоритм работы
STRCONST(hp_TEMP1, "TEMP1");          // целевая температура в доме
STRCONST(hp_TEMP2, "TEMP2");          // целевая температура обратки
STRCONST(hp_TARGET, "TARGET");        // что является целью ПИД - значения  0 (температура в доме), 1 (температура обратки).
STRCONST(hp_DTEMP, "DTEMP");          // гистерезис целевой температуры
STRCONST(hp_dTempGen, "DTG");         // гистерезис целевой температуры на генераторе
STRCONST(hp_HP_TIME, "HP_TIME");      // Постоянная интегрирования времени в секундах ПИД ТН
STRCONST(hp_HP_PRO, "HP_PRO");        // Пропорциональная составляющая ПИД ТН
STRCONST(hp_HP_IN, "HP_IN");          // Интегральная составляющая ПИД ТН
STRCONST(hp_HP_DIF, "HP_DIF");        // Дифференциальная составляющая ПИД ТН
STRCONST(hp_TEMP_IN, "TEMP_IN");      // температура подачи (минимальная для охлажления или максимальная для нагрева)
STRCONST(hp_TEMP_OUT, "TEMP_OUT");    // температура обратки (максимальная для охлажления или минимальная для нагрева)
STRCONST(hp_D_TEMP, "D_TEMP");        // максимальная разность температур конденсатора.
STRCONST(hp_TEMP_PID, "TEMP_PID");    // Целевая температура ПИД
STRCONST(hp_WEATHER, "W");        // Использование погодозависимости
STRCONST(hp_targetPID, "TP");   // Цель PID
STRCONST(hp_K_WEATHER, "KW");     // Коэффициент погодозависимости PID
STRCONST(hp_kWeatherTarget, "KWT"); // Коэффициент погодозависимости
STRCONST(hp_WeatherBase, "KWB");
STRCONST(hp_WeatherTargetRange, "KWR");

// Действуют для отопления и ГВС
STRCONST(ADD_DELTA_TEMP, "ADT"); // Добавка температуры к установке, в градусах
STRCONST(ADD_DELTA_HOUR, "ADH"); // Начальный Час добавки температуры к установке
STRCONST(ADD_DELTA_END_HOUR, "ADEH"); // Конечный Час добавки температуры к установке


#ifdef SENSOR_IP // параметры удаленного датчика get_sensorIP
STRCONST(ip_SENSOR_TEMP, "SENSOR_TEMP"); // Темпеартура
STRCONST(ip_SENSOR_NUMBER, "SENSOR_NUMBER"); // Номер
STRCONST(ip_RSSI, "RSSI");               // Уровень сигнала
STRCONST(ip_VCC, "VCC");                 // Напряжение питания
STRCONST(ip_SENSOR_USE, "SENSOR_USE");   // Использование
STRCONST(ip_SENSOR_RULE, "SENSOR_RULE"); // Правило использования
STRCONST(ip_SENSOR_IP, "SENSOR_IP");     // Адрес
STRCONST(ip_SENSOR_COUNT, "SENSOR_COUNT"); // Счетчик
STRCONST(ip_STIME, "STIME");             // время с последнего считывания датчика
STRCONST(ip_SENSOR, "SENSOR");           // ---------------------------------------------------------------------------
#endif


// Названия типы фреонов
static const char * const noteFreon[] =   {"R22","R410A","R600","R134a","R407C","R12","R290","R404A","R717"};
// Названия правил работы ЭРВ для веба
const char noteRuleEEV[]   =	"TEVAOUT-T[PEVA]:0;"
	#ifdef TCOMPIN
								"TCOMPIN-T[PEVA]:0;"
		#ifdef TEVAIN
								"TEVAOUT-TEVAIN:0;"
								"TCOMPIN-TEVAIN:0;"
								"TABLE[EVA,CON]:0;"
		#endif
	#endif
								"MANUAL:0;";

// Описание правила работы ЭРВ
static const char * const noteRemarkEEV[] = {	"Перегрев равен: температура на выходе испарителя - температура по давлению на выходе испарителя.",
#ifdef TCOMPIN
								"Перегрев равен: температура на входе компрессора - температура по давлению на выходе испарителя.",
	#ifdef TEVAIN
								"Перегрев равен: температура на выходе испарителя - температура на входе испарителя.",
								"Перегрев равен: температура на входе компрессора - температура на входе испарителя.",
								"Перегрев не вычисляется. ЭРВ открывается по значению шага из таблицы температур испарителя и конденсатора.",
	#endif
#endif
								"Перегрев не вычисляется. Ручной режим, ЭРВ открывается на заданное число шагов."};

// Предупреждения
#define WARNING_VALUE        1         // Попытка установить значение за границами диапазона запрос типа SET

// GPBR  - восемь 32x битных регистров не обнуляющиеся при сбросе, Энергозависимы!
// адреса - 0x90-0xDC General Purpose Backup Register GPBR
// The System Controller embeds Eight general-purpose backup registers.
// Карта использования GPBR в НК
// GPBR->SYS_GPBR[0] текущую задача (сдвиг на 8 влево) +  номера ошибки RTOS
// GPBR->SYS_GPBR[1] причина сброса контроллера
// GPBR->SYS_GPBR[4] последня точка отладки перед сбросом максимальное значение точки отладки сейчас 56 (обновляем!)
#define STORE_DEBUG_INFO(s) GPBR->SYS_GPBR[4] = s  // Сохранение номера точки отладки в энергозависимой памяти sam3
#define WEB_STORE_DEBUG_INFO(s) GPBR->SYS_GPBR[5] = s  // Сохранение номера точки отладки в энергозависимой памяти sam3

// Коды ошибок, перечисление точек возврата TYPE_RET_HP и тексты к ним
// лежат в ErrorCodes.h: их нужно видеть и модулям, которые Constant.h
// включить не могут. Тексты спрятаны в ErrorCodes.cpp, доступ - через
// Error_Text/Ret_Text с проверкой границ.


// ---- ПЕРЕЧИСЛЯЕМЫЕ ТИПЫ ----
//  Перечисляемый тип - источник загрузки для веб морды
enum TYPE_SOURSE_WEB
{
 pMIN_WEB,                 // Надо грузить минимальную морду
 pSD_WEB,                  // Надо грузить морду с карты 
 pFLASH_WEB,               // Надо грузить морду с флеш диска
 pERR_WEB                  // Внутренняя ошибка? 
};


//  Перечисляемый тип - Типы ответов POST запросов
enum TYPE_RET_POST
{
 pSETTINGS_OK,                // Настройки из выбранного файла восстановлены, CRC16 OK 
 pSETTINGS_ERR,               // Ошибка восстановления настроек из файла (см. журнал) 
 pNULL,                       // "" - пустая строка
 pLOAD_OK,                    // Файлы загружены, подробности смотри в журнале 
 pLOAD_ERR,                   // Ошибка загрузки файла, подробности смотри в журнале      
 pPOST_ERR,                   // Внутреняя ошибка парсера post запросов 
 pNO_DISK,                    // Нет флеш диска
 pSETTINGS_MEM                // Настройки не влезают во внутренний буфер
};

//  Перечисляемый тип - Состояния теплового насоса
enum TYPE_STATE_HP         
{
  pOFF_HP,                          // 0 ТН выключен
  pSTARTING_HP,                     // 1 Стартует
  pSTOPING_HP,                      // 2 Останавливается
  pWORK_HP,                         // 3 Работает
  pWAIT_HP,                         // 4 Ожидание ТН (расписание - пустое место)
  pERROR_HP,                        // 5 Ошибка ТН
  pERROR_CODE,                      // 6 - Эта ошибка возникать не должна!
  pEND15                            // Обязательно должен быть последним, добавляем ПЕРЕД!!!
};


//  Перечисляемый тип - что надо делать или какой режим отопления выбран (первые три)
#define MODE_HP uint8_t
#define pOFF		0             // Выключить
#define pHEAT		0x01          // Отопление
#define pCOOL		0x02          // Охлаждение
#define pBOILER		0x04          // Бойлер
#define pDEFROST	0x08          // Разморозка воздушника
#define pCONTINUE	0x80          // флаг Продолжения

static const char * const MODE_HP_STR[] = { "Off", "Heat", "Cool", "Boil", "Defrost", "C" };

//  Перечисляемый тип - Команды управления ТН
enum TYPE_COMMAND         
{
  pEMPTY,                        // 0 Команд нет
  pSTART,                        // 1 Пуск теплового насоса (РУКАМИ)
  pAUTOSTART,                    // 2 Пуск теплового насоса (автоматический pRESTART pREPEAT)
  pSTOP,                         // 3 Стоп теплового насоса
  pRESET,                        // 4 Сброс контроллера
  pRESTART,                      // 5 Пуск ТН после сброса контроллера (отличается от pREPEAT только задержкой)
  pREPEAT,                       // 6 Повторный пуск ТН (определяется числом попыток повторного пуска)
  pNETWORK,                      // 7 перегрузить сетевой контроллер
  pJFORMAT,                      // 8 форматирование журнала в I2C флеше
  pSFORMAT,                      // 9 форматирование статистики в I2C флеше
  pSAVE,                         // 10 запись настроек ТН
  pWAIT,                         // 11 перевод в режим ожидания ТН (пустое расписание)
  pRESUME,                       // 12 Восстановление работы из режима ожидания
  pPROG_FC,                      // 13 Первоначальное программирование частотного преобразователя
  pEND14                         // Обязательно должен быть последним, добавляем ПЕРЕД!!!
};

static const char * const hp_commands_names[] = {"EMPTY", "START", "AUTOSTART", "STOP", "RESET", "RESTART", "REPEAT", "NETWORK", "JFORMAT", "SFORMAT", "SAVE", "WAIT", "RESUME", "PROG_FC", "UNKNOWN"};

//  Перечисляемый тип -ТИПЫ уведомлений
enum MESSAGE          
{
  pMESSAGE_NONE,                 // 0 Нет уведомлений
  pMESSAGE_TESTMAIL,             // 1 Тестовое уведомление по почте +
  pMESSAGE_TESTSMS,              // 2 Тестовое уведомление по SMS +
  pMESSAGE_RESET,                // 3 Уведомление Сброс +
  pMESSAGE_ERROR,                // 4 Уведомление Ошибка +
  pMESSAGE_LIFE,                 // 5 Уведомление Сигнал жизни +
  pMESSAGE_TEMP,                 // 6 Уведомление Достижение граничной температуры +
  pMESSAGE_SD,                   // 7 Уведомление "Проблемы с sd картой" +
  pMESSAGE_WARNING,              // 8 Уведомление "Прочие уведомления"
  pEND10                         // Обязательно должен быть последним, добавляем ПЕРЕД!!!
};

//  Перечисляемый тип - сервис для отправки смс
enum SMS_SERVICE 
{ 
  pSMS_RU,                       // Сервис sms.ru
  pSMSC_RU,                      // Сервис smsc.ru
  pSMSC_UA,                      // Сервис smsc.ua
  pSMSCLUB                    // Сервис smsclub.mobi
};

STRCONST(ADR_SMS_RU,   "sms.ru");
STRCONST(ADR_SMSC_RU,  "smsc.ru");
STRCONST(ADR_SMSC_UA,  "smsc.ua");
STRCONST(ADR_SMSCLUB,  "gate.smsclub.mobi");
STRCONST(SMS_SERVICE_WEB_SELECT,  "sms.ru:0;smsc.ru:0;smsc.ua:0;smsclub.mobi:0;");

// Константы хладагентов и выбор фреона - в Freon.h: их нужно видеть
// и в модуле Freon.cpp, который Constant.h включить не может.

//  Перечисляемый тип - правило работы ЭРВ пять вариантов выводятся в зависимости от наличия датчиков
enum RULE_EEV           
{
   TEVAOUT_PEVA,
#ifdef TCOMPIN
   TCOMPIN_PEVA,
#endif
#ifdef TEVAIN
   TEVAOUT_TEVAIN, 
   TCOMPIN_TEVAIN,
   TABLE,
#endif
   MANUAL           // Обязательно должен быть последним, добавляем ПЕРЕД!!!
};

//  Перечисляемый тип - режим тестирования ТН
enum TEST_MODE          
{
   NORMAL=0,        
   SAFE_TEST,
   TEST,
   HARD_TEST           // Обязательно должен быть последним, добавляем ПЕРЕД!!!
};

//  Перечисляемый тип - Время сброса сокетов
enum TIME_RES_SOCKET       
{
    pNONE1,           // нет сброса
    p30sec,           // сброс раз в 30 секунд
    p300sec,          // сброс раз в 300 секунд
    pEND4             // Обязательно должен быть последним, добавляем ПЕРЕД!!!
};

#endif
