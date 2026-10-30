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
// Описание базовых классов для работы c "железом"
// (датчики и исполнительные устройства) зависит от контроллера
// ---------------------------------------------------------------------------
#ifndef Hardware_h
#define Hardware_h
#include "../../Constant.h"                       // Вся конфигурация и константы проекта Должен быть первым !!!!
#include <WireSam.h>                        // twiClock400kHz - им задан I2C_SPEED
#include <extEEPROM.h>                      // тип объекта eepromI2C
#include <DS3232.h>                         // tmElements_t, часы реального времени по I2C
#include <rtc_clock.h>                      // работа со встроенными часами  Это основные часы!!!
#include <ModbusMaster.h>                   // Используется МОДИФИЦИРОВАННАЯ либа ModbusMaster https://github.com/4-20ma/ModbusMaster
#include "StepMotor.h" 
#include "../core/Profile.h"
#include "../net/MQTT.h"
#include "BusOneWire.h" 

#include "../core/Device.h"   // общие флаги датчиков, часы, set_Error

// ---------------------------------------------------------------------------
// ---- Д А Т Ч И К И ----
// ---------------------------------------------------------------------------
#include "SensorTemp.h"

// ---------------------------------------------------------------------------
// Класс аналоговый датчик управление токовая петля + нагрузочный резистор + делитель
// Может быть несколько датчиков
// Значение хранится в СОТЫХ (БАРА)
#define PRESS_TRANS_MIN  0.001f    // коэффициент пересчёта АЦП: хранится в тысячных в uint16, rd() считает в int16 -
#define PRESS_TRANS_MAX  32.767f   // за этими границами значение заворачивалось (Н-14)
class sensorADC : public Sensor
{
  public:
    void initSensorADC(uint8_t sensor, uint8_t pinA, uint16_t filter_size); // Инициализация датчика  порядковый номер датчика и нога он куда прикреплен
    int8_t  Read();                                      // чтение данных c аналогового датчика давления (АЦП) возвращает код ошибки, делает все преобразования
    inline int16_t get_minValue(){return cfg.minValue;}     // Минимальное значение датчика - нижняя граница диапазона, при выходе из него ошибка
    inline int16_t get_maxValue(){return cfg.maxValue;}     // Максимальное значение датчика - верхняя граница диапазона, при выходе из него ошибка
    int16_t get_zeroValue(){return cfg.zeroValue;}       // Выход датчика (отсчеты ацп)  соответсвующий 0
    int8_t  set_zeroValue(int16_t p);                    // Установка Выход датчика (отсчеты ацп)  соответсвующий 0
    uint16_t get_lastADC(){ return lastADC; }            // Последнее считанное значение датчика в отсчетах с фильтром
    int16_t get_Value();                                 // Получить значение давления датчика - это то что используется
    uint16_t get_transADC(){return cfg.transADC;}        // Получить значение коэффициента преобразования
    int8_t set_transADC(float p);                        // Установить значение коэффициента преобразования
    inline boolean get_present(){return GETBIT(flags,fPresent);} // Наличие датчика в текущей конфигурации
    inline boolean get_fmodbus(){return GETBIT(flags,fsensModbus);} // Подключен по Modbus
    inline uint8_t  get_pinA(){return pin;}              // Получить канал АЦП (нумерация SAM3X) куда прицеплен датчик
    int16_t get_testValue(){return cfg.testValue;}       // Получить значение датчика в режиме теста
    int8_t  set_testValue(int16_t p);                    // Установить значение датчика в режиме теста
    int8_t  set_minValue(int16_t p) { cfg.minValue = p; return OK; }
    int8_t  set_maxValue(int16_t p)  { cfg.maxValue = p; return OK; }
     
    uint8_t *get_save_addr(void) { return (uint8_t *)&cfg; } // Адрес структуры сохранения
    uint16_t get_save_size(void) { return sizeof(cfg); } // Размер структуры сохранения
    void	after_load(void);
   
    //type_rawADC adc;                                  // структура для хранения сырых данных с АЦП
    // Поля adc_* пишет обработчик прерывания АЦП, читает sensorADC::Read: volatile, а читать парой - только под запретом прерываний
    volatile uint32_t adc_sum;                 			// сумма
    uint16_t *adc_filter;              					// массив накопленных значений
    uint16_t adc_filter_max;
    volatile uint16_t adc_last;       	                // текущий индекс
    volatile boolean  adc_flagFull;     			    // буфер полный
    volatile uint16_t adc_lastVal;             			// последнее считанное значение
    int16_t  Temp;										// Температура в сотых
    
  private:
    int16_t Value;                                       // значение датчика (обработанное) в сотых
    struct {     // Save GROUP, firth number
		uint8_t number;									 // Номер
		int16_t zeroValue;                               // отсчеты АЦП при нуле датчика
		uint16_t transADC;                               // коэффициент пересчета АЦП в значение, тысячные
		uint16_t _reserved_2;
		int16_t testValue;                               // давление датчика в режиме тестирования
		int16_t minValue;                                // минимально разрешенное значение
		int16_t maxValue;                                // максимально разрешенное значение
    } __attribute__((packed)) cfg;// Save Group end
  static_assert(sizeof(cfg) == 13, "изменилась раскладка настроек датчика давления - это формат EEPROM");
    uint16_t lastADC;                                    // Последние значение отсчета ацп
       
    uint8_t pin;                                         // Канал АЦП (AD*) куда прицеплен датчик
    // Кольцевой буфер для усреднения
    void clearBuffer();                                  // очистить буфер
#if P_NUMSAMLES > 1
    int16_t p[P_NUMSAMLES];                              // буфер для усреднения показаний давления
    int32_t sum;                                         // Накопленная сумма
    uint8_t last;                                        // указатель на последнее (самое старое) значение в буфере диапазон от 0 до P_NUMSAMLES-1
#endif
};


// ---------------------------------------------------------------------------
// ---- Цифровые контактные датчики (есть 2 состяния 0 и 1) ----
class sensorDigitalInput : public Sensor
{
public:
  void initInput(int sensor);                            // Инициализация контактного датчика
  int8_t  Read(boolean fast = false);                    // Чтение датчика возвращает ошибку или ОК
  inline boolean get_Input(){return Input;}   // Получить значение датчика при последнем чтении
  inline boolean get_present(){return GETBIT(flags,fPresent);} // Наличие датчика в текущей конфигурации
  boolean get_testInput(){return cfg.testInput;}             // Получить Состояние датчика в режиме теста
  int8_t  set_testInput(int16_t i);                      // Установить Состояние датчика в режиме теста
  boolean get_alarmInput(){return cfg.alarmInput;}           // Состояние аварии датчика
  boolean is_alarm() { return Input == cfg.alarmInput; }	// Датчик сработал?
  int8_t  set_alarmInput(int16_t i);                     // Установить Состояние аварии датчика
  inline int8_t  get_pinD(){return pin;}                 // Получить ногу куда прицеплен датчик
  TYPE_SENSOR get_typeInput(){return type;}              // Получить тип датчика
  uint8_t *get_save_addr(void) { return (uint8_t *)&cfg; } // Адрес структуры сохранения
  uint16_t get_save_size(void) { return sizeof(cfg); } // Размер структуры сохранения
    
private:
   boolean Input;                                        // Состояние датчика
   // Группа сохранения. Имя даёт возможность проверить раскладку на сборке:
   // безымянную структуру sizeof не видит, и до этого её границы считались
   // арифметикой указателей по первому и последнему полю.
   struct { // Save GROUP
   uint8_t number;										 // номер
   boolean testInput;                                    // !save! Состояние датчика в режиме теста
   boolean alarmInput;                                   // !save! Состояние датчика в режиме аварии
   } __attribute__((packed)) cfg;// Save Group end
   static_assert(sizeof(cfg) == 3, "изменилась раскладка контактного датчика - это формат EEPROM");
   TYPE_SENSOR type;                                     // Тип датчика
   uint8_t  pin;                                         // Ножка куда прицеплен датчик
};
// ---------------------------------------------------------------------------
// ---- Цифровые частотные датчики (значение кодируется в выходной частоте) ----
// основное назначение - датчики потока
// Частота кодируется в тысячных герца
// Число импульсов рассчитывается за базовый период (BASE_TIME_READ), т.к частоты малы период надо савить не менее 5 сек
// Выходная величина кодируется в тысячных от целого

class sensorFrequency
{
public:
  void initFrequency(int sensor);                   // Инициализация частотного датчика
  void reset(void);									// Сброс счетчика
  inline void InterruptHandler(){count++;} // обработчик прерываний
  int8_t  Read();                                         // Чтение датчика (точнее расчет значения) возвращает ошибку или ОК
  inline uint32_t get_Frequency(){return Frequency;}   // Получить ЧАСТОТУ датчика при последнем чтении
  inline uint16_t get_Value(){return Value;}           // Получить Значение датчика при последнем чтении, литры в час
  inline boolean get_present(){return GETBIT(cfg.flags,fPresent);} // Наличие датчика в текущей конфигурации
  inline uint16_t get_minValue(){return cfg.minValue * 100;}     // Получить минимальное значение датчика, литры в час
  void set_minValue(float f);							// Установить минимальное значение датчика
  inline boolean get_checkFlow(){return GETBIT(cfg.flags,fcheckRange);}// Проверка граничного значения
  void set_checkFlow(boolean f) { cfg.flags = (cfg.flags & ~(1<<fcheckRange)) | (f<<fcheckRange); }
  int8_t  get_lastErr(){return err;}                     // Получить последнюю ошибку
  char*   get_note(){return note;}                       // Получить описание датчика
  char*   get_name(){return name;}                       // Получить имя датчика
  uint16_t get_testValue(){return cfg.testValue;}            // Получить Состояние датчика в режиме теста
  int8_t  set_testValue(int16_t i);                      // Установить Состояние датчика в режиме теста
  float   get_kfValue(){return cfg.kfValue;}                 // Получить коэффициент пересчета
  bool    set_kfValue(uint16_t f) { if(f < FLOW_KF_MIN) return false; cfg.kfValue=f; return true; } // Установить коэффициент пересчета, сотые
  void    after_load(void);                                  // после загрузки настроек: недопустимый kf -> из FrequencySetup
  uint16_t get_Capacity(){return cfg.Capacity;}              // Получить теплоемкость
  int8_t set_Capacity(uint16_t c);                       // Установить теплоемкость больше 5000 не устанавливается
  TEST_MODE get_testMode(){return testMode;}             // Получить текущий режим работы
  void  set_testMode(TEST_MODE t){testMode=t;}           // Установить значение текущий режим работы
  inline int8_t  get_pinF(){return pin;}                 // Получить ногу куда прицеплен датчик
  uint8_t *get_save_addr(void) { return (uint8_t *)&cfg; } // Адрес структуры сохранения
  uint16_t get_save_size(void) { return sizeof(cfg); } // Размер структуры сохранения
    
private:
   uint32_t Frequency;                                   // значение частоты в тысячных герца
   uint16_t Value;                                       // значение датчика ЛИТРЫ В ЧАС (ИЛИ ТЫСЯЧНЫЕ КУБА)
   // Группа сохранения, названа ради проверки раскладки на сборке.
   struct { // SAVE GROUP
   uint8_t  number;										 // номер
   uint16_t testValue;                                   // !save! Состояние датчика в режиме теста
   uint16_t kfValue; 								 	 // коэффициент пересчета частоты в значение, сотые
   uint8_t  flags;                                       // флаги  датчика
   uint8_t  minValue;							     	 // десятые m3/h (0..25,5)
   uint16_t Capacity;                                    // значение теплоемкости теплоносителя в конутре где установлен датчик [Cp, Дж/(кг·град)]
   } __attribute__((packed)) cfg;// END SAVE GROUP
   static_assert(sizeof(cfg) == 9, "изменилась раскладка частотного датчика - это формат EEPROM");
   TEST_MODE testMode;                                   // Значение режима тестирования
   volatile uint16_t count;                              // число импульсов за базовый период (то что меняется в прерывании)
   uint32_t sTime;                                       // время начала базового периода в тиках
   int8_t err;                                           // ошибка датчика (работа) при ошибке останов ТН
   uint8_t  pin;                                         // Ножка куда прицеплен датчик
   char *note;                                           // наименование датчика
   char *name;                                           // Имя датчика
};




// ---------------------------------------------------------------------------
// ---- И С П О Л Н И Т Е Л Ь Н Ы Е   У С Т Р О Й С Т В А ----
// ---------------------------------------------------------------------------
#define fR_StatusMain		1			// b1: Состояние Вкл/Выкл Основного алгоритма
#define fR_StatusManual		3			// b3: Состояние Вкл/Выкл Вручную
#define fR_StatusDaily		4			// b3: Состояние Вкл/Выкл Дневное включение
#define fR_StatusMask		((1<<fR_StatusMain)|(1<<fR_StatusManual)|(1<<fR_StatusDaily))	// битовая маска
#define fR_StatusAllOff		-127		// выключить по всем алгоритмам

class devRelay : public Device
{
public:
  void initRelay(int sensor);                            // Инициализация реле
  __attribute__((always_inline)) inline int8_t  set_ON(bool planned = false) {return set_Relay(fR_StatusMain, planned);}    // Включить реле (planned - плановое переключение, в журнал только при флаге RELP)
  __attribute__((always_inline)) inline int8_t  set_OFF(bool planned = false){return set_Relay(-fR_StatusMain, planned);}   // Выключить реле
  int8_t  set_Relay(int8_t r, bool planned = false);                           // Установить реле в состояние (0/-1 - выкл основной алгоритм, fR_Status* - включить, -fR_Status* - выключить)
  inline boolean get_Relay(){return Relay;}                    // Прочитать состояние реле
  int8_t  get_pinD(){return pin;}                        // Получить ногу куда прицеплено реле
  // Флаги реле: бит fPresent - наличие, биты fR_Status* - состояние по алгоритмам.
  // Читаются через унаследованный get_flag(bit).
private:
   uint8_t number;										// Номер массива реле
   boolean Relay;                                        // Состояние реле
   uint8_t  pin;                                         // Ножка куда прицеплено реле
};  

// ---- ЭРВ ТОЛЬКО ОДНА ШТУКА ВСЕГДА (не массив) ----
#define fPresent         		0   // флаг наличие Объекта един для всего!!!! определение смотри выше
#define fOneSeekZero     		1	// Флаг однократного поиска "0" ЭРВ (только при первом включении ТН)
#define fEnterInPercent  		2	// Ввод на веб-странице в %, иначе в шагах
#define fCorrectOverHeat 		3	// Включен режим корректировки перегрева
#define fHoldMotor       		4	// Режим "удержания" шагового двигателя ЭРВ
#define fEevClose        		5   // Флаг закрытие ЭРВ при выключении компрессора
#define fLightStart      		6   // флаг Облегчение старта компрессора   приоткрытие ЭРВ в момент пуска компрессора
#define fStartFlagPos    		7   // флаг Всегда начинать работу ЭРВ со стратовой позици
#define fPID_PropOnMeasure		8   // ПИД пропорционально измерению (Proportional on Measurement), иначе пропорционально ошибке (Proportional on Error)
#define fEEV_StartPosByTemp		9	// Стартовая позиция ЭРВ определяется по температуре подачи (пропорционально между EEV_START_POS_LOW_TEMP=StartPos и EEV_START_POS_HIGH_TEMP=PosAtHighTemp)
#define fEEV_DirectAlgorithm	10	// Прямое управление ЭРВ без ПИД

// Настройки ЭРВ, формат хранения (EEPROM). Тип вынесен из класса, чтобы таблица параметров веб-API видела смещения полей.
struct type_settingsEEV {                      // Первая переменная => timeIn
	uint16_t pid_time;        				// Период расчета ПИД в секундах
	PID_STRUCT pid;                         // Настройки и переменные ПИД регулятора
	int16_t  pid2_delta;					// Дельта для консервативных вычислений ПИДа
	int16_t	 Correction;                     // Величина корректировки перегрева (систематическая ошибка расчета перегрева)
	int16_t	 manualStep;                     // Число шагов открытия ЭРВ для правила работы ЭРВ «Manual»
	uint8_t  _reserved1;                    // было: Тип фреона
	RULE_EEV  ruleEEV;                       // правило работы ЭРВ
	PID_STRUCT pid2;						// Консервативный ПИД
	uint16_t OHCor_Period;					// Период расчета корректировки перегрева в циклах ЭРВ
	int16_t  OHCor_TDIS_TCON_Thr;			// Порог, после превышения которого начинаем менять перегрев, в сотых градуса
	int16_t	 OverheatMin;					// Минимальный перегрев (сотые градуса)
	int16_t	 OverheatMax;					// Максимальный перегрев (сотые градуса)
	int16_t	 tOverheat;                      // Перегрев ЦЕЛЬ (сотые градуса)
	uint8_t	 speedEEV;                       // Скорость шагового двигателя ЭРВ (импульсы в сек.)
	uint8_t	 minSteps;                       // Минимальное число шагов открытия ЭРВ
	uint16_t preStartPos;                   // ПУСКОВАЯ позиция ЭРВ (ТО что при старте компрессора ПРИ РАСКРУТКЕ)
	uint16_t StartPos;                      // СТАРТОВАЯ позиция ЭРВ после раскрутки компрессора т.е. ПОЗИЦИЯ С КОТОРОЙ НАЧИНАЕТСЯ РАБОТА проходит DelayStartPos сек
	// ЭРВ Времена и задержки
	uint8_t	 delayOnPid;                     // Задержка включения EEV после включения компрессора (сек).  Точнее после выхода на рабочую позицию Общее время =delayOnPid+DelayStartPos
	uint8_t	 DelayStartPos;                  // Время после старта компрессора когда EEV выходит на стартовую позицию - облегчение пуска вначале ЭРВ
	uint8_t	 delayOff;                       // Задержка закрытия EEV после выключения насосов (сек). Время от команды стоп компрессора до закрытия ЭРВ = delayOffPump+delayOff
	uint8_t	 delayOn;                        // Задержка между открытием (для старта) ЭРВ и включением компрессора, для выравнивания давлений (сек). Если ЭРВ закрылось при остановке
	uint8_t  OHCor_TDIS_TCON_MAX;			// верхняя граница для пропорционального изменения перегрева, % от OHCor_TDIS_TCON
	int16_t	 OverHeatStart;                  // Начальный перегрев (сотые градуса)
	int16_t	 maxSteps;                       // Максимальное число шагов ЭРВ (диапазон)
	uint16_t OHCor_Delay;				    // Задержка корректировки перегрева после старта компрессора, сек
	int16_t	 OHCor_TDIS_TCON;				// Температура нагнетания - конденсации (/0.01) при конденсации 30 градусов, 0 испарения, и OHCor_OverHeatStart
	uint16_t flags;                         // флаги ЭРВ
	uint16_t pid_max;						// ограничение ПИД в шагах ЭРВ
	uint16_t PosAtHighTemp;					// Положение при EEV_START_POS_HIGH_TEMP
	int16_t  tOverheatTCOMP;				// Целевой перегрев2 TCOMPIN-T[PEVA]
	int16_t  tOverheatTCOMP_delta;			// Дельта целевого перегрева2 TCOMPIN-T[PEVA]
	int8_t   trend_threshold;				// Порог детектирования тренда
	uint16_t trend_mul_threshold;			// Порог для *2, сотые градуса
	int16_t  tOverheat2_low;				// Нижняя граница перегрева 2 для быстрого закрытия ЭРВ
	int16_t  tOverheat2_low_hyst;			// Гистерезис для tOverheat2_low
};

class devEEV
{
public:
	void initEEV();                                        // Инициализация ЭРВ
	void InitStepper(void);
	int8_t Start();                                        // Запуск ЭРВ - начало алгоритма отслеживания - параметр текущее время
	void Pause(){fPause=true;}                             // Пауза ЭРВ - останов отслеживания
	void Resume();                                         // Пауза ЭРВ - возобновление отслеживания
	int8_t Update(void);			             			// Обновление ЭРВ - одна итерация алгоритма отслеживания на входе
	boolean isWork();                                      // Получить состояние ЭРВ
	int16_t get_Overheat(){return Overheat;}               // Получить текущий перегрев
	int16_t get_OHCor_tdelta(){return OHCor_tdelta;}       // Расчитанная целевая дельта Нагнетание-Конденсации (0 - нет)
	int16_t set_Overheat(boolean fHeating); 				// Вычислить текущий перегрев, вычисляется каждое измерение (проводится в опросе датчиков)
	void   CorrectOverheat(void);							 // Корректировка перегрева
	void   CorrectOverheatInit(void);						 // Перед стартом компрессора

	// Движение ЭРВ
	inline int16_t get_EEV() {return  EEV;} // Прочитать МГНОВЕННУЮ!! позицию шагового двигателя ЭРВ двигатель может двигаться
	inline int16_t calc_pos(int16_t percent) { return (int32_t)_data.maxSteps * percent / 10000; } // пересчитать % -> шаги, сотые
	inline int16_t calc_percent(int16_t pos) { return (int32_t) pos * 10000 / _data.maxSteps; } // пересчитать % -> шаги, сотые
	int8_t  set_EEV(int16_t x);                             // Перейти на позицию абсолютную  возвращает код ошибки
	int8_t  set_zero();                                    // Гарантированно (шагов больше чем диапазон) закрыть ЭРВ возвращает код ошибки


	void get_paramEEV(char *var, char *ret);              // Получить параметр ЭРВ в виде строки

	// Функции чтения настроек ЭРВ в бинарном виде
	int16_t get_tOverheat(){return  _data.tOverheat;}     // Получить целевой перегрев
	inline int16_t get_PID_time() { return  _data.pid_time; } // Получить ЗАДАННУЮ постоянную времени в секундах ЭРВ
	int16_t get_Correction(){return _data.Correction;}     // Получить поправку в градусах для правила работы ЭРВ «TEVAOUT-TEVAIN».  СОТЫЕ ГРАДУСА
	int16_t get_manualStep(){return _data.manualStep;}     // Получить число шагов открытия ЭРВ для правила работы ЭРВ «Manual»
	uint8_t get_typeFreon(){return FREON; }     			// Получить тип фреона
	RULE_EEV get_ruleEEV(){return _data.ruleEEV;}          // Получить правило перегрева
	void get_ruleEEVtext(char *strReturn);				 // Получить правило перегрева как строку
	uint8_t sensors_needed();							 // Датчики, обязанные быть привязанными для правила перегрева (OHN_*)
	boolean get_HoldMotor(){return GETBIT(_data.flags,fHoldMotor);}      // Получить флаг Удержания шагового двигателя
	boolean get_OneSeekZero(){return GETBIT(_data.flags,fOneSeekZero);}  // Получить флаг однократного поиска "0" ЭРВ
	boolean get_EevClose(){return GETBIT(_data.flags,fEevClose);}        // Получить флаг закрытие ЭРВ при выключении компрессора
	boolean get_LightStart(){return GETBIT(_data.flags,fLightStart);}    // Получить флаг Облегчение старта компрессора   приоткрытие ЭРВ в момент пуска компрессора
	boolean get_StartFlagPos(){return GETBIT(_data.flags,fStartFlagPos);}// Получить флаг Всегда начинать работу ЭРВ со стратовой позици
	boolean get_fEEVStartPosByTemp() { return GETBIT(_data.flags,fEEV_StartPosByTemp); }
	uint16_t get_flags() { return _data.flags; }

	uint8_t get_delayOnPid(){return _data.delayOnPid;}
	uint8_t get_delayOff(){return _data.delayOff;}
	uint8_t get_delayOn(){return _data.delayOn;}
	uint16_t get_preStartPos(){return _data.preStartPos;}
	uint8_t get_DelayStartPos() {return _data.DelayStartPos;}
	uint16_t get_StartPos();

	char*   get_note(){ return note;}                      // Прочитать описание ЭРВ
	char*   get_name(){ return name;}                      // Прочитать имя ЭРВ
	int8_t  get_lastErr(){return err;}                     // Получить последнюю ошибку
	void    set_error(int8_t error) { err = error; }		// Установить ошибку
	int16_t get_minEEV(){return  _data.minSteps;}          // Число шагов при котором ЭРВ начинает открываться (холостой ход) может быть равным 0
	int16_t get_maxEEV(){return  _data.maxSteps;}          // Максимальное число шагов ЭРВ (диапазон)
	inline boolean get_present(){return GETBIT(_data.flags,fPresent);} // Наличие EEV в текущей конфигурации

	TEST_MODE get_testMode(){return testMode;}             // Получить текущий режим работы
	void set_testMode(TEST_MODE t){testMode=t;}            // Установить значение текущий режим работы

	// Сохранение
	uint8_t *get_save_addr(void) { return (uint8_t *)&_data; } // Адрес структуры сохранения
	uint16_t get_save_size(void) { return sizeof(_data); } // Размер структуры сохранения
	void after_load(void);
	void resetPID();                                       // Сброс пид регулятора

	StepMotor stepperEEV;                                  // Шаговый двигатель ЭРВ
	boolean setZero;                                       // признак ПРОЦЕССА обнуления EEV;
	int16_t EEV;                                           // Текущая  АБСОЛЮТНАЯ позиция
	int16_t OverheatTCOMP;								// перегрев TCOMPIN-T[PEVA]

private:
	boolean fPause;                                        // пауза алгоритма отслеживания true
	inline int8_t jump(int x){return set_EEV(EEV+x);} // Перейти на позицию относительную возвращает код ошибки
	int8_t stepDown();                                     // 1 Шаг в минус возвращает код ошибки
	int8_t stepUp();                                       // 1 Шаг в плюс возвращает код ошибки

	PID_WORK_STRUCT pidw;  								// переменные пид регулятора
	int16_t Overheat;                                    // Перегрев текущий (сотые градуса)
	int16_t OHCor_tdelta;								 // Расчитанная целевая дельта Нагнетание-Конденсации
	int16_t OHCor_tdelta_prev;							 // Расчитанная целевая дельта Нагнетание-Конденсации
	TEST_MODE testMode;                                  // Значение режима тестирования
	int8_t  err;                                         // ошибка ЭРВ (работа) при ошибке останов ТН

	char *note;                                          // Описание
	char *name;                                          // Имя

	type_settingsEEV _data;                     // Структура для сохранения настроек!
	static_assert(sizeof(_data) == 70, "изменилась раскладка настроек ЭРВ - это формат EEPROM");
};

// ---- Частотный преобразователь ТОЛЬКО ОДНА ШТУКА ВСЕГДА (не массив) ----
// Флаги Инвертора, рабочие (flags)
#define fFC         	0        // флаг наличие инвертора
#define fFC_RetOil 		1        // Возврат масла
#define fPower      	2        // флаг режим ограничения мощности (резерв - сейчас ограничение всегда)
#define fOnOff      	3        // флаг включения-выключения инвертора
#define fErrFC      	4        // флаг глобальная ошибка инвертора - работа инвертора запрещена
#define fAutoResetFault	5        // флаг Автосброс не критичного сбоя инвертора
#define fLogWork		6		 // УСТАРЕЛО: перенесён в флаг отладки FCW (DebugFlags.h), читается только при переносе настроек
#define fFC_RetOilSt 	7        // Возврат масла рабочий
#define FC_SAVED_FLAGS 	((1<<fAutoResetFault) | (1<<fFC_RetOil))

STRCONST(noteFC_OK,  " связь по Modbus установлена");                  // Все впорядке
STRCONST(noteFC_NO,  " связь по Modbus потеряна, инвертор заблокирован");
STRCONST(noteFC_NONE,  " отсутствует в данной конфигурации");


// ---- Класс Электрический счетчик SDM ----
STRCONST(noteSDM,  "Электрический счетчик с Modbus"); // Описание счетчика
STRCONST(noteSDM_NONE,  "Отсутствует в конфигурации"); //

// Флаги Электросчетчика
#define fSDM           0              // флаг наличие счетчика
#define fSDMLink       1              // флаг связь установлена

// Структура для хранения настроек счетчика
struct type_settingSDM
{
uint16_t maxVoltage;                    // максимальное напряжение (вольты) иначе ошибка если 0 то не работает
uint16_t minVoltage;                    // минимальное напряжение (вольты) иначе ПРЕДУПРЕЖДЕНИЕ если 0 то не работает
uint16_t maxPower;                      // максимальная мощность (ватты) напряжение иначе ошибка если 0 то не работает
};
// Input register Function code 04 to read input parameters:
#ifdef USE_PZEM004T	// Использовать PZEM-004T v3 Modbus (UART)
	static const char nameSDM[] = "PZEM-004";   // Имя счетчика
	#define SDM_VOLTAGE          0x0000			// int16, 0.1V
	#define SDM_CURRENT          0x0001			// int32, 0.001A
	#define SDM_AC_POWER         0x0003			// int32, 0.1W
	#define SDM_AC_ENERGY        0x0005			// int32, 1Wh
	#define SDM_FREQUENCY        0x0007			// int16, 0.1Hz
	#define SDM_POW_FACTOR       0x0008			// int16, 0.01
	#define SDM_ALARM            0x0009			// 0xFFFF - Alarm, 0 - ok
	// Holding Registers
	#define SDM_ALARM_THRESHOLD  0x0001			// 1W
	#define SDM_MODBUS_ADDR      0x0002			// 1..F7
	// Special command
	#define PWM_RESET_ENERGY	 0x42
#else                                      // Регистры однофазного счетчика sdm120
  #ifdef USE_SDM630    // Регистры 3-х фазного счетчика SDM630.
	static const char nameSDM[] = "SDM630";                         // Имя счетчика
	// Адрес уже уменьшен на 1
    #define SDM_VOLTAGE     42
    #define SDM_CURRENT     48
    #define SDM_AC_POWER    52
    #define SDM_POWER       56
    #define SDM_RE_POWER    60
    #define SDM_POW_FACTOR  62
    #define SDM_PHASE       66
  #else
	static const char nameSDM[] = "SDM120";                         // Имя счетчика
    #define SDM_VOLTAGE          0x0000    // (2 слова 4 байта формат float) Line to neutral volts.  Напряжение
    #define SDM_CURRENT          0x0006    // (2 слова 4 байта формат float) Current.  Ток
    #define SDM_AC_POWER         0x000c    // (2 слова 4 байта формат float) Active power.  Активная мощность
    #define SDM_POWER            0x0012    // (2 слова 4 байта формат float) Apparent power.  Полная мощность
    #define SDM_RE_POWER         0x0018    // (2 слова 4 байта формат float) Reactive power.  Реактивная мощность
    #define SDM_POW_FACTOR       0x001e    // (2 слова 4 байта формат float) Power factor. Коэффициент мощности
    #define SDM_PHASE            0x0024    // (2 слова 4 байта формат float) Phase angle Угол фазы
  #endif
	// Общие регистры
	#define SDM_FREQUENCY        0x0046        // (2 слова 4 байта формат float) Frequency частота
	#define SDM_IMPORT_AC_ENERGY 0x0048        // (2 слова 4 байта формат float) Import active energy Потребленная активная энергия
	#define SDM_EXPORT_AC_ENERGY 0x004a        // (2 слова 4 байта формат float) Export active energy Переданная активная энергия
	#define SDM_IMPORT_RE_ENERGY 0x004c        // (2 слова 4 байта формат float) Import reactive energy Потребленная реактивная энергия
	#define SDM_EXPORT_RE_ENERGY 0x004e        // (2 слова 4 байта формат float) Export reactive energy Переданная реактивная энергия
	#define SDM_ENERGY           0x0156        // (2 слова 4 байта формат float) Общая энергия

	#define SDM_AC_ENERGY        0x0156        // (2 слова 4 байта формат float) Total active energy Суммараная активная энергия
	#define SDM_RE_ENERGY        0x0158        // (2 слова 4 байта формат float) Total reactive energy Суммараная реактивная энергия

	// Modbus Protocol Holding Registers and Digital meter set up
	// Function code 10 to set holding parameter ,function code 03 to read holding parameter
	#define SDM_WIDTH_RELAY      0x000c        // (2 слова 4 байта формат float) Write relay on period in Width GetTickCounteconds: 60, 100 or 200, Ширина импульса на импульстом выходе
	#define SDM_PARITY_STOP      0x0012        // (2 слова 4 байта формат float) Write the network port parity/stop bits for MODBUS Protocol, where: 0 = One stop bit and no parity, Network Parity Stop default. 1 = One stop bit and even parity. 2 = One stop bit and odd parity.3 = Two stop bits and no parity. Requires a restart to become effective.
	#define SDM_ADR_MODBUS       0x0014        // (2 слова 4 байта формат float) Адрес на шине Modbus
	#define SDM_BAUD_RATE        0x001C        // (2 слова 4 байта формат float) Write the network port baud rate for MODBUS Protocol, where:0 = 2400 bps. 1 = 4800 bps.2 = 9600 bps( default) 5=1200 bps Requires a restart to become effective Data Format:float
	#define SDM_CT_CURRENT       0x0032        // (2 слова 4 байта формат float) CT Primary current Ranges from 1-2000, Default ID is 5
	#define SDM_RELAY_MODE       0x0056        // (2 слова 4 байта формат HEX) 0001: Import active energy, 0002: Import + export active energy, 0004: Export active energy, (default). 0005: Import reactive energy, 0006: Import + export reactive energy, 0008: Export reactive energy,
	#define SDM_TIME_SCROLL      0xf500        // (2 слова 4 байта формат BCD) Time of scroll display Data Format: BCD 0-30s Default 0:does not display in turns
	#define SDM_RELAY_CONST      0xf910        // (2 слова 4 байта формат HEX) Pulse constant Data Format:Hex 0000: 0.001kwh(kvarh)/imp (default) 0001: 0.01kwh(kvarh)/imp 0002: 0.1kwh(kvarh)/imp 0003: 1kwh(kvarh)/imp
	#define SDM_MEASURE_MODE     0xf920        // (2 слова 4 байта формат HEX) Measurement mode Data Format:Hex 0001:mode 1(total = import) 0002:mode 2 (total = import + export)(default) 0003:mode 3 (total = import - export)
#endif

class devSDM
{
   public:  
       int8_t initSDM();                               // Инициализация счетчика и проверка и если надо программирование
       inline boolean get_present(){return GETBIT(flags,fSDM);} // Наличие счетчика в текущей конфигурации
       inline boolean get_link(){return GETBIT(flags,fSDMLink);} // Наличие соединения (true - связь есть)

      int8_t  get_readState(uint8_t group);            // Прочитать инфо с счетчика
      int8_t  get_lastErr(){return err;}               // Получить последнюю ошибку счетчика
      uint16_t get_numErr(){return numErr;}            // Получить число ошибок чтения счетчика
      char*   get_note(){return note;}                 // Получить описание датчика
      char*   get_name(){return name;}                 // Получить имя датчика
      float   get_power(){return AcPower;}
       inline float get_Voltage(){return Voltage;}          // Напряжение
       inline float get_Power(){return AcPower;}            // Активная мощность

      boolean uplinkSDM();                             // Проверить связь со счетчиком (связь действительно проверяется - чтение регистра скорости счетчика)
      boolean progConnect();                           // перепрограммировать счетчик на требуемые параметры связи SDM_SPEED SDM_MODBUS_ADR c DEFAULT_SDM_SPEED DEFAULT_SDM_MODBUS_ADR
      char* get_paramSDM(char *var, char *ret);        // Получить параметр SDM в виде строки
      boolean set_paramSDM(char *var,char *c);         // Установить параметр SDM из строки
      
      uint8_t  *get_save_addr(void) { return (uint8_t *)&settingSDM; } // Адрес структуры сохранения
      uint16_t  get_save_size(void) { return sizeof(settingSDM); } // Размер структуры сохранения
  private:
      int8_t  err;                                     // ошибка счётчика (работа)
      uint16_t numErr;                                 // число ошибок чтение по Modbus
      byte flags;                                      // флаги  0 - наличие счетчика,
       // Управление по 485
      float AcPower;                                   // активная мощность, Вт
      float Voltage;                                   // Напряжение в вольтах
      type_settingSDM  settingSDM;                     // Настройки
      char *note;                                      // Описание
      char *name;                                      // Имя
};


// ---- Класс устройство Modbus ----
#define fModbus    			0               // флаг наличие modbus
class devModbus
  {
  public:  
    int8_t initModbus();                                                                // Инициализация Modbus и проверка связи возвращает ошибку
     inline boolean get_present(){return GETBIT(flags,fModbus);} // Наличие Modbus в текущей конфигурации
    int8_t readInputRegisters16(uint8_t id, uint16_t cmd, uint16_t *ret);
    int8_t readInputRegisters32(uint8_t id, uint16_t cmd, uint32_t *ret);				   // LITTLE-ENDIAN!
    int8_t readInputRegistersFloat(uint8_t id, uint16_t cmd, float *ret);                  // Получить значение 2-x (Modbus function 0x04 Read Input Registers) регистров (4 байта) в виде float возвращает код ошибки данные кладутся в ret
    int8_t readHoldingRegisters16(uint8_t id, uint16_t cmd, uint16_t *ret);                 // Получить значение регистра (2 байта) МХ2 в виде целого  числа возвращает код ошибки данные кладутся в ret
    int8_t readHoldingRegisters32(uint8_t id, uint16_t cmd, uint32_t *ret);                // Получить значение 2-x регистров (4 байта) МХ2 в виде целого числа возвращает код ошибки данные кладутся в ret
    int8_t readHoldingRegistersFloat(uint8_t id, uint16_t cmd, float *ret);                // Получить значение 2-x регистров (4 байта) в виде float возвращает код ошибки данные кладутся в ret
    int8_t readHoldingRegistersNN(uint8_t id, uint16_t cmd, uint16_t num,uint16_t *buf);   // Получить значение N регистров (2*N байта) МХ2 (положить в buf) возвращает код ошибки
    int8_t writeSingleCoil(uint8_t id,uint16_t cmd, uint8_t u8State);                      // установить битовый вход, возвращает код ошибки Modbus function 0x05 Write Single Coil.
    int8_t readCoil(uint8_t id,uint16_t cmd, boolean *ret);                                // прочитать отдельный бит, возвращает ошибку Modbus function 0x01 Read Coils.
    int8_t writeHoldingRegisters16(uint8_t id, uint16_t cmd, uint16_t data);               // Установить значение регистра (2 байта) МХ2 в виде целого  числа возвращает код ошибки данные data
    int8_t writeHoldingRegistersFloat(uint8_t id, uint16_t cmd, float dat);                // Записать float как 2 регистра числа возвращает код ошибки данные data
    #ifndef FC_VACON
    int8_t LinkTestOmronMX2();                                                             // Тестирование связи c МХ2 (актуально только с omronom) возвращает код ошибки
    #endif
    int8_t writeHoldingRegisters32(uint8_t id,uint16_t cmd, uint32_t data); // Записать 2 регистра подряд возвращает код ошибки
    int8_t get_err() {return err;}                                                         // Получить код ошибки
    bool   link_lost(bool running);                                                        // Связь с инвертором потеряна (критерий в Error_LinkLost)
private:
    // Функция Modbus. Задаёт, какой метод ModbusMaster вызвать в transact().
    enum MB_FUNC { MB_READ_INPUT, MB_READ_HOLDING, MB_READ_COILS,
                   MB_WRITE_COIL, MB_WRITE_SINGLE, MB_WRITE_MULTI };
    // Единственное место, где берётся и отдаётся замок шины Modbus.
    // who   - имя вызвавшего метода, идёт в журнал при занятой шине;
    // num   - для чтения число регистров, для записи само записываемое значение;
    // rx    - куда скопировать num слов ответа (копирование идёт ещё под замком,
    //         иначе соседняя задача успеет перетереть буфер ответа);
    // tx    - num слов для передачи, заполняются до запроса, тоже под замком.
    // Возвращает err: OK либо код ошибки, уже переведённый translateErr().
    int8_t transact(const char *who, uint8_t id, MB_FUNC func, uint16_t cmd,
                    uint16_t num, uint16_t *rx = NULL, const uint16_t *tx = NULL);
    // Переменные
    int8_t flags;                           // Флаги
    int8_t err;                             // Ошибки Modbus
    uint8_t  linkFails;                     // Транспортных сбоев подряд при обмене с инвертором
    uint32_t linkLastOk;                    // Время последнего ответа инвертора (в том числе с исключением), мс
    ModbusMaster RS485;                     // Класс Modbus 485
    int8_t translateErr(uint8_t result);    // Перевод ошибки протокола Modbus в ошибки ТН
  }; // End class


// Шестнадцатеричный вывод - в Util.h, работа с IP - в Network.h.

// Память I2C (FRAM/EEPROM): настройки, профили, журнал. Создаётся в Control.ino.
extern extEEPROM eepromI2C;


// ---------------------------------------------------------------------------
// Свободные функции и объекты слоя железа.
// ---------------------------------------------------------------------------

// Часы DS3232 на шине I2C и диагностика старта. Определены в Control.ino.
extern DS3232   rtcI2C;
extern uint16_t lastErrorFreeRtosCode;   // код ошибки FreeRTOS, прочитанный из GPBR при старте
extern uint32_t startSupcStatusReg;      // SUPC_SR при старте - состояние питания

// АЦП датчиков давления: таймер TC0 канала 2 плюс сам АЦП по прерыванию.
void start_ADC();
void adc_setup();

// Инициализация носителей. Определены в Board.cpp.
uint8_t initSD(void);
void    SD_ReadMode(void);         // перед чтением с карты: настройка «Многоблочное чтение SD» -> SdFat, отказ CMD18 - в журнал
uint8_t initSpiDisk(boolean show);

// Диагностика и управление контроллером. Определены в Board.cpp.
void showID();                           // идентификатор чипа в журнал
void Software_Reset();                   // программный сброс SAM3X
void Reset_Controller(uint32_t reason);  // сброс с причиной RESET_*_MARK (Watchdog.h) и выгрузкой журнала
void SupplyMonitorON(uint32_t voltage);  // монитор питания на заданном пороге
void freeRamShow();
int  freeRam();


// Запись в EEPROM с подсчётом CRC на лету. Определены в Board.cpp.
int8_t save_struct(uint32_t &addr_to, uint8_t *addr_from, uint16_t size, uint16_t &crc);
int8_t save_rec(uint32_t &addr_to, uint16_t tag, const uint8_t *addr_from, uint16_t size, uint16_t &crc); // запись нового каркаса, см. Util.h
int8_t save_2bytes(uint32_t &addr_to, uint16_t data, uint16_t &crc);

// Часы DS3232 по I2C под замком шины. Определены в Board.cpp.
// Не inline: из отдельной единицы трансляции inline-функцию не позвать - компоновщику
// нечего вызывать. Чтение при занятом замке или ошибке возвращает структуру с
// Year == 0 - вызывающий обязан это проверять.
tmElements_t *getTime_RtcI2C();
int16_t getTemp_RtcI2C();               // встроенный термодатчик часов, в сотых градуса
void setTime_RtcI2C(uint8_t hour, uint8_t min, uint8_t sec);
void setDate_RtcI2C(uint8_t date, uint8_t mon, uint16_t year);

// Текстовая причина последнего сброса из регистра RSTC. Определена в Board.cpp.
char *ResetCause(void);


// Чтение и запись I2C-памяти под замком шины. Определены в Board.cpp.
// Не inline: компоновщик не нашёл бы тела. Журналу они нужны первым же вызовом.
byte writeEEPROM_I2C(unsigned long addr, byte *values, unsigned int nBytes);
byte readEEPROM_I2C(unsigned long addr, byte *values, unsigned int nBytes);

// Modbus - управление инвертором. Определён в Control.ino, конструктора нет.
extern devModbus Modbus;
 #endif
