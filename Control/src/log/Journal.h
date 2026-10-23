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
//  Описание вспомогательных лассов данных, предназначенных для получения информации о ТН
#ifndef Information_h
#define Information_h
#include "../../Constant.h"                       // Вся конфигурация и константы проекта Должен быть первым !!!!
#include "JournalBuf.h"                     // Подавление повторов и кольцо отложенной записи
#include "DebugFlags.h"                     // DBG_ON: флаги отладки, что пишется в журнал
#include "mini-printf.h"                    // MINI_PRINTF_FMT - проверка форматов компилятором
// Если mini-printf.h взят из копии БЕЗ этого макроса (например из sketchbook,
// который не выровняли), собираем без проверки форматов - но собираем. Иначе
// IDE выдаёт три десятка ошибок на ровном месте, что уже случилось.
// Полная проверка включается, когда копии библиотеки совпадают; tools/build.sh
// предупреждает о расхождении, но IDE его не запускает.
#ifndef MINI_PRINTF_FMT
#define MINI_PRINTF_FMT(n, m)
#endif
#include "utility/w5100.h"
#include "utility/socket.h"

// ---------------------------------------------------------------------------
// ---- Класс системный журнал пишет в консоль и в память ----
// ---------------------------------------------------------------------------
//  Необходим для получения лога контроллера
//  http://arduino.ru/forum/programmirovanie/etyudy-dlya-nachinayushchikh-interfeis-printable
//  Для записи ТОЛЬКО в консоль использовать функции printf
//  Для записи в консоль И в память (журнал) использовать jprintf
//  По умолчанию журнал пишется в RAM размер JOURNAL_LEN
//  Журнал пишется в I2C память, адрес начала записи 0x0fff до I2C_STAT_EEPROM
//  Размер журнала I2C_STAT_EEPROM-I2C_JOURNAL_START

#define PRINTF_BUF 256                           // размер буфера для одной строки - большая длина нужна при отправке уведомлений, там длинные строки (видел 178)

#include "../net/Network.h"   // sendPacketRTOS и прочий транспорт
STRCONST(MessageLongString,  "Jornal: Input string too long, skip string!");  
STRCONST(errorReadI2C,  "$ERROR - read I2C memory\n");
STRCONST(errorWriteI2C,  "$ERROR - write I2C memory\n");
STRCONST(promtUser,  "> ");   

class Journal :public Print
{
public:
 // SemaphoreHandle_t xJournalSemaphore;                    // Семафор Journal
  void Init();                                            // Инициализация
  // MINI_PRINTF_FMT - проверка форматных строк компилятором. У метода класса
  // первым аргументом идёт this, поэтому номера сдвинуты: формат второй,
  // переменная часть третья.
  void printf(const char *format, ...) MINI_PRINTF_FMT(2, 3);                  // Печать только в консоль
  void jprintf(const char *format, ...) MINI_PRINTF_FMT(2, 3);                 // Печать в консоль и журнал возвращает число записанных байт
  void jprintf_time(const char *format, ...) MINI_PRINTF_FMT(2, 3);			// +Time, далее печать в консоль и журнал
  void jprintf_date(const char *format, ...) MINI_PRINTF_FMT(2, 3);			// +DateTime, далее печать в консоль и журнал
  int32_t send_Data(uint8_t thread);                     // отдать журнал в сеть клиенту  Возвращает число записанных байт
  int32_t available(void);                               // Возвращает размер журнала
  int8_t   get_err(void) { return err; };
  virtual size_t write (uint8_t c);                       // чтобы print работал для это класса
  void Format(void);                           		    // форматирование журнала в еепром
  void CreateLocks(void);                                 // создать замки журнала, зовётся из setup() до старта шедулера
private:
  int8_t err;                                             // ошибка журнала
  int32_t bufferHead, bufferTail;                        // Начало и конец
  uint8_t full;                                           // признак полного буфера
  void _write(char *dataPtr);                            // Записать строку в журнал
  // Замки журнала. L1 (xJournalSemaphore) - короткий, листовой: pbuf, кольцо, dedup, ring.lost.
  // L2 (xJournalWriteSemaphore) - писатель: Drain, FlushAll и Format на время записи в EEPROM.
  // Порядок: xWebThread -> L2 -> L1 | xI2C; L1 берётся последним, L2 продюсеры не берут никогда.
  bool _lock(void);                                      // взять L1; при отказе строка засчитана в потери
  void _unlock(void);
  uint16_t _take(uint8_t *chunk, bool housekeeping);     // под L1 забрать кусок из кольца

  // Отложенная запись и подавление повторов. Обоснование - в JournalBuf.h.
  void _post(char *line, const char *body);              // Положить строку в кольцо (или записать сразу)
  void _putRepeats(uint16_t repeats, const char *msg);    // Запись о подавленных повторах, с текстом повторявшегося сообщения
public:
  void Drain(void);                                      // Выгрузить пачку из кольца в EEPROM, вызывается из vServiceHP
  void FlushAll(void);                                   // Выгрузить кольцо целиком - перед чтением журнала
  uint16_t get_lost(void) { return ring.lost; }          // Сколько строк потеряно при переполнении кольца
private:
  JournalDedup  dedup;
  JournalRing   ring;
  uint8_t       ringbuf[JOURNAL_RING_SIZE];
  uint32_t      lastPostTick;                            // когда последний раз что-то клали - для выдачи повторов по таймауту
  uint16_t      lostReported;                            // сколько потерь уже показано в журнале
   // Переменные
  char pbuf[PRINTF_BUF+2];                                // Буфер для одной строки + маркеры
    void writeTAIL();                                     // Записать символ "конец" значение bufferTail должно быть установлено
    void writeHEAD();                                     // Записать символ "начало" значение bufferHead должно быть установлено
    void writeREADY();                                    // Записать признак "форматирования" журнала - журналом можно пользоваться
    boolean checkREADY();                                 // Проверить наличие журнала
 };

// ---------------------------------------------------------------------------
// ---- Класс Профиль ТН ----
// ---------------------------------------------------------------------------
// Класс предназначен для работы с настройками ТН. в пямяти хранится текущий профиль, ав еепром можно хранить до 10 профилей, и загружать их по одному в пямять
// также есть функции по сохраннию и удалению из еепром
// номер текущего профиля хранится в структуре type_SaveON
#define PROFILE_EMPTY 0x55     // Признак пустого профиля

// ---- внутренние структуры ----
// Структура для хранения статуса работы ТН EEPROM  - часть профиля!!!
//  Определение флагов в  type_SaveON
#define fBoilerON 1         // флаг Включения бойлера  true - включено
struct type_SaveON {
	byte magic;               // признак данных, должно быть  0x55
	uint8_t flags;           // флаги состояния ТН
	MODE_HP mode;             // режим работы отопление|охлаждение
	uint32_t startTime;       // дата и время включения ТН
	uint32_t P1;              // Резервный параметр 1
};

// Структуры для хранения настроек бойлера
//  Определение флагов в type_boilerHP
#define fScheduleAddHeat 	0           // флаг Расписание только для ТЭНа
#define fSchedule        	1           // флаг Использование расписания
#define fSalmonella      	2           // флаг Сальмонела раз внеделю греть бойлер
#define fCirculation     	3           // флаг Управления циркуляционным насосом ГВС
#define fResetHeat       	4           // флаг Сброса лишнего тепла в СО
#define fTurboBoiler     	5           // флаг Ускоренный нагрев бойлера (ТН + ТЭН)
#define fAddHeating      	6           // флаг ДОГРЕВА ГВС ТЭНом
#define fBoilerTogetherHeat	7			// флаг одновременного нагрева бойлера с отоплением до температуры догрева
#define fBoilerPID			8			// ПИД
#define fSalmonellaHold		9			// флаг Длительная обработка сальмонеллы: поддерживать температуру до конца цикла (раньше fBoilerUseSun)
#define fAddHeatingForce	10			// флаг Включать догрев, если компрессор не нагрел бойлер до температуры догрева
// бит 11 не занимать: раньше fWorkOnGenerator (Греть от генератора без ТЭНа), в старых профилях он может быть выставлен
#define fBoilerHeaterOnly   12			// Бойлер греет только ТЭН (ТН бойлер не греет); на генераторе не действует

struct type_boilerHP {
	uint8_t DischargeDelta;            // Сброс тепла в отопление, если температура подачи/конденсации приблизилась к максимуму/догреву, в десятых градуса
	int16_t TempTarget;                // Целевая температура бойлера
	int16_t dTemp;                     // гистерезис целевой температуры
	int16_t tempInLim;                 // Температура подачи максимальная/минимальная если охлаждение ЗАЩИТА
	uint16_t flags;                    // Флаги
	uint32_t Schedule[7];              // Расписание бойлера
	uint16_t Circul_Work;              // Время  работы насоса ГВС секунды (fCirculation)
	uint16_t Circul_Pause;             // Пауза в работе насоса ГВС  секунды (fCirculation)
	uint16_t Reset_Time;               // время сброса излишков тепла в секундах (fResetHeat)
	uint16_t pid_time; 				   // Период расчета ПИД в секундах
	PID_STRUCT pid;                    // Настройки и переменные ПИД регулятора
	int16_t dAddHeat;                  // Гистерезис нагрева бойлера до температуры догрева, в сотых градуса
	int16_t tempRBOILER;               // Температура догрева бойлера
	int16_t add_delta_temp; 	       // Добавка температуры к установке бойлера, в сотых градусах
	uint8_t add_delta_hour;	           // Начальный Час добавки температуры к установке бойлера
	uint8_t add_delta_end_hour; 	   // Конечный Час добавки температуры к установке
	int16_t tempPID;                   // Целевая температура ПИД
};

// Структуры для хранения настроек Отопления и Охлаждения (одинаковые)
// Работа с отдельными флагами type_settingHP
#define fTarget      0                // Что является целью  - 0 (температура в доме), 1 (температура обратки).
#define fWeather     1                // флаг Погодозависимости
#define fReserved2   2				  // РЕЗЕРВ: бит 2 флагов профиля, раньше "теплый пол" (нигде не использовался). Не занимать без нужды

struct type_DailySwitch {
	uint8_t Device;					// Реле; 0 - нет и конец массива
	uint8_t TimeOn;					// Время включения hh:m0
	uint8_t TimeOff;				// Время выключения hh:m0
} __attribute__((packed));

struct type_settingHP {
	uint8_t flags;                       // Флаги опций до 8 флагов
	RULE_HP Rule;                        // алгоритм работы отопления.
	int16_t Temp1;                       // Целевая температура дома
	int16_t Temp2;                       // Целевая температура обратки
	int16_t dTemp;                       // Гистерезис целевой температуры
	uint16_t pid_time; 				     // Период расчета ПИД в секундах
	PID_STRUCT pid;                      // Настройки и переменные ПИД регулятора
	int16_t dTempGen;                    // Гистерезис целевой температуры от генератора
	int16_t tempInLim;                   // Температура подачи (минимальная для охлажления или максимальная для нагрева)
	int16_t tempOutLim;                  // Температура обратки (максимальная для охлажления или минимальная для нагрева)
	uint16_t reserved2;                  // Резерв
	int16_t dt;                          // Максимальная разность температур конденсатора.
	int16_t kWeatherPID;                 // Коэффициент погодозависимости PID, в ТЫСЯЧНЫХ градуса на градус
	int8_t  WeatherBase;                 // Базовая температура для погодозависимости, градусы
	uint8_t WeatherTargetRange;          // Предел изменения цели, десятые градусы
	int16_t add_delta_temp;		         // Добавка температуры к установке отопления, в сотых градусах
	uint8_t add_delta_hour;		         // Начальный Час добавки температуры к установке
	uint8_t add_delta_end_hour; 		 // Конечный Час добавки температуры к установке
	int16_t tempPID;                     // Целевая температура ПИД
	int16_t kWeatherTarget;              // Коэффициент погодозависимости цели (дом/обратка), в ТЫСЯЧНЫХ градуса на градус
};

#define LEN_PROFILE_NAME       25     // Длина имени профиля
#define fEnabled               0      // Разрешение данного профайла использоваться в коротком списке
struct type_dataProfile               // Хранение общих данных
{
	int8_t id;                          // Номер профайла 0..I2C_PROFIL_NUM-1 (1 элемент структуры!)
	uint8_t flags;                      // Флаги профайла (2 элемент структуры!)
	uint16_t len;                       // Длина данных
	uint32_t saveTime;                  // Время сохранения профиля
	char name[LEN_PROFILE_NAME];        // Имя профайла
	char note[85]; // Описание профайла кодирование описания профиля 40 русских букв (одна буква 6 байт (двойное кодирование) это входная строка)  описание переводится и хранится в utf8 по 2 байта символ
};

// Системный журнал. Создаётся в Control.ino, чтобы не менять
// порядок построения объектов. Конструктора у класса нет, работа
// начинается с journal.Init() в setup(), поэтому порядок здесь ничего не решает;
// объявление нужно, чтобы журнал был виден из отдельных единиц трансляции -
// его зовёт 21 файл проекта из 22.
extern Journal journal;
#endif
