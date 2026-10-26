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
/*
 * Датчики и реле на выводах платы: аналоговые датчики давления (sensorADC) с таймером TC0 и
 * прерыванием АЦП, дискретные входы (sensorDigitalInput), частотные датчики (sensorFrequency)
 * с пятью обработчиками прерываний и реле (devRelay).
 *
 * ADC_Handler перекрывает СЛАБЫЙ символ ядра SAM. Если он пропадёт из сборки или получит
 * украшенное имя, компоновщик молча подставит заглушку ядра: прошивка соберётся, но давление
 * замрёт на последнем отсчёте (стережёт правило в tools/ci.sh). extern "C" ниже - подстраховка:
 * связывание C даёт объявление в sam3x8e.h, оно уже внутри extern "C".
 * InterruptFreq0..4 передаются по адресу в attachInterrupt из sensorFrequency::initFrequency
 * (этот же файл); объявлений в заголовках им не нужно.
 *
 * Hardware.h тянет Constant.h, а с ним Config.h: условия конфигурации разрешаются так же,
 * как в главной единице трансляции.
 */
#include "Hardware.h"     // объявления классов этого модуля, start_ADC, adc_setup
#include "../core/HeatPump.h"     // HP - обработчики прерываний пишут прямо в него
#include "../core/Rtos.h"         // _delay, Diag
#include "../net/ParamRegistry.h" // таблицы параметров веб-API (внизу файла)

// ---------------------------------------------------------------------------
// Настройка таймера и ацп для чтения датчика давления
// Зависит от чипа
// ---------------------------------------------------------------------------
// Старт считывания АЦП
void start_ADC()
{
	adc_setup();                                        // setup ADC
	pmc_enable_periph_clk(TC_INTERFACE_ID + 2);         // clock TC0_CHA2, WARNING: interfere with output - analogWrite(58[AD4],n)
	TcChannel * t = TC0->TC_CHANNEL + 2;                // pointer to TC0 registers for its channel 2
	t->TC_CCR = TC_CCR_CLKDIS;                          // disable internal clocking while setup regs
	t->TC_IDR = 0xFFFFFFFF;                             // disable interrupts
	t->TC_SR;                                           // read int status reg to clear pending
	t->TC_CMR = TC_CMR_TCCLKS_TIMER_CLOCK1 |            // use TCLK1 (prescale by 2, = 42MHz)
			TC_CMR_WAVE |                               // waveform mode
			TC_CMR_WAVSEL_UP_RC |                       // count-up PWM using RC as threshold
			TC_CMR_EEVT_XC0 |                           // Set external events from XC0 (this setup TIOB as output)
			TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_SET |		// set clear and set from RA and RC compares
			TC_CMR_BCPB_NONE | TC_CMR_BCPC_NONE;
	t->TC_RC = SystemCoreClock / 2 / ADC_FREQ;          // counter resets on RC, so sets period in terms of 42MHz clock
	t->TC_RA = SystemCoreClock / 2 / ADC_FREQ / 2;      // roughly square wave
	t->TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG;            // re-enable local clocking and switch to hardware trigger source.
}

// Установка АЦП
void adc_setup()
{
	uint32_t adcMask = 0;
	uint32_t adcGain = 0x55555555; // All gains set to x1 Channel Gain Register
	uint8_t max = 0;
#ifdef VCC_CONTROL                             // если разрешено чтение напряжение питания
	adcMask |= (1<<PIN_ADC_VCC);               // Добавить маску контроля питания
	max = PIN_ADC_VCC;
#endif

	// Расчет маски каналов
	for(uint8_t i = 0; i < ANUMBER; i++) {   // по всем датчикам
		if(HP.sADC[i].get_present() && !HP.sADC[i].get_fmodbus()) {
			uint32_t ach = HP.sADC[i].get_pinA();
			if(max < ach) max = ach;
			adcMask |= 1 << ach;
		}
	}

	NVIC_EnableIRQ(ADC_IRQn);        // enable ADC interrupt vector
	ADC->ADC_IDR = 0xFFFFFFFF;       // disable interrupts IDR Interrupt Disable Register
	ADC->ADC_IER = 1 << max;         // Самый старший канал
	ADC->ADC_CHDR = 0xFFFF;          // Channel Disable Register CHDR disable all channels
	ADC->ADC_CHER = adcMask;         // Channel Enable Register CHER enable just A11  каналы здесь SAMX3!!
	ADC->ADC_CGR = adcGain;
	ADC->ADC_COR = 0x00000000;       // All offsets off Channel Offset Register
	// 12bit, 14MHz, trig source TIOA Output of the Timer Counter Channel 2
	ADC->ADC_MR = ADC_MR_PRESCAL(ADC_PRESCAL) | ADC_MR_LOWRES_BITS_12 | ADC_MR_USEQ_NUM_ORDER | ADC_MR_STARTUP_SUT16 | ADC_MR_TRACKTIM(16) | ADC_MR_SETTLING_AST17 | ADC_MR_TRANSFER(2) | ADC_MR_TRGSEL_ADC_TRIG3 | ADC_MR_TRGEN | ADC_MR_ANACH;
	adc_set_bias_current(ADC, 0);    // for sampling frequency: 0 - below 500 kHz, 1 - between 500 kHz and 1 MHz.
}


// Обработчик прерывания, как можно короче
#ifdef __cplusplus
extern "C"
{
#endif
void ADC_Handler(void)
{
#ifdef VCC_CONTROL  // если разрешено чтение напряжение питания
	Diag.AdcVcc = (uint32_t)(*(ADC->ADC_CDR + PIN_ADC_VCC));
#endif
	for(uint8_t i = 0; i < ANUMBER; i++)    // по всем датчикам
	{
		sensorADC *adc = &HP.sADC[i];
#ifdef ADC_SKIP_EXTREMUM
		int32_t a = ADC->ADC_CDR[adc->get_pinA()]; // get conversion result
		if(adc->adc_lastVal != 0xFFFF && abs(a - adc->adc_lastVal) > ADC_SKIP_EXTREMUM) {
			adc->adc_lastVal = 0xFFFF;
			continue;
		}
		adc->adc_lastVal = a;
#else
		adc->adc_lastVal = (uint32_t)ADC->ADC_CDR[adc->get_pinA()];  // get conversion result
#endif
		// Усреднение значений
		adc->adc_sum = adc->adc_sum + adc->adc_lastVal - adc->adc_filter[adc->adc_last];   // Добавить новое значение, Убрать самое старое значение
		adc->adc_filter[adc->adc_last] = adc->adc_lastVal;			                       // Запомнить новое значение
		if(adc->adc_last < adc->adc_filter_max) adc->adc_last++;
		else {
			// Порядок важен: читатель (sensorADC::Read) делит adc_sum на adc_last,
			// когда adc_flagFull ещё не выставлен. Если обнулить индекс раньше
			// флага, читатель между двумя присваиваниями получит adc_last == 0
			// и выполнит деление на ноль.
			adc->adc_flagFull = true;
			__asm__ volatile ("" ::: "memory");
			adc->adc_last = 0;
		}
	}
}

#ifdef __cplusplus
}
#endif
    
// ---------------------------------------------------------------------------
// ---- Аналоговые датчики ----
// Значение хранится в СОТЫХ (БАР)
void sensorADC::initSensorADC(uint8_t sensor, uint8_t pinA, uint16_t filter_size)
{
	// Инициализация структуры для хранения "сырых"данных с аналогового датчика.
	if(AnalogSetup[sensor].present) adc_filter_max = filter_size;   // отводим память если используем датчик под сырые данные
	else adc_filter_max = 1;
	adc_filter = (uint16_t*) malloc(sizeof(uint16_t) * adc_filter_max);
	if(adc_filter == NULL) {   // ОШИБКА если память не выделена
		set_Error(ERR_OUT_OF_MEMORY, (char*) "sensorADC");
		return;
	}
	memset(adc_filter, 0, sizeof(uint16_t) * adc_filter_max);
	adc_filter_max--;
	adc_sum = 0;                                                                   // сумма
	adc_last = 0;                                                                  // текущий индекс
	adc_flagFull = false;                                                          // буфер полный
	adc_lastVal = 0;                                                               // последнее считанное значение
	clearBuffer();

	testMode = NORMAL;                           // Значение режима тестирования
	cfg.minValue = AnalogSetup[sensor].min;                 // минимально разрешенное значение
	cfg.maxValue = AnalogSetup[sensor].max;                 // максимально разрешенное значение
	cfg.testValue = AnalogSetup[sensor].test;               // Значение при тестировании
	cfg.zeroValue = AnalogSetup[sensor].zero;               // отсчеты АЦП при нуле датчика
	cfg.transADC = AnalogSetup[sensor].transADC;                 // коэффициент пересчета АЦП в значение
	cfg.number = sensor;
	pin = pinA;
	flags = AnalogSetup[sensor].present << fPresent;	 // наличие датчика
	err = OK;                                     // ошибка датчика (работа)
	Value = 0;                                    // давление датчика (обработанная)
	Temp = ERROR_TEMPERATURE;
	note = (char*) AnalogSetup[sensor].note;              // присвоить наименование датчика
	name = (char*) AnalogSetup[sensor].name;              // присвоить имя датчика
}
    
 // очистить буфер АЦП
 void sensorADC::clearBuffer()
 {
#if P_NUMSAMLES > 1
      for(uint16_t i=0;i<P_NUMSAMLES;i++) p[i]=0;         // обнуление буфера значений
      sum=0;
      last=0;
#endif
      SETBIT0(flags,fFull);                      // Буфер не полный
 }
  
// чтение данных c аналогового датчика (АЦП) возвращает код ошибки, делает все преобразования
 int8_t sensorADC::Read()
 {
	 if(!(GETBIT(flags, fPresent))) return err;        // датчик запрещен в конфигурации ничего не делаем
	 int16_t lastPress;
	 if(testMode != NORMAL) {
		 lastPress = cfg.testValue;                // В режиме теста
	 } else {                                      // Чтение датчика
#ifdef DEMO
		 lastADC=random(1350,2500);                // В демо режиме генерим значение
#else
		 {
			 // adc_last наполняется в прерывании ADC_Handler. До первого прерывания
			 // (adc_last == 0, adc_flagFull == false) деление adc_sum / adc_last было
			 // делением на ноль: на Cortex-M3 без DIV_0_TRP это молча даёт 0,
			 // то есть нулевое давление и возможную ложную ошибку диапазона.
			 // Сумма, индекс, флаг и последнее значение снимаются ОДНИМ срезом под запретом прерываний. По отдельности
			 // читатель мог проверить adc_last != 0, а перед делением прерывание обнуляло индекс (деление на ноль, на
			 // Cortex-M3 молча давшее 0 - нулевое давление и ложную ошибку диапазона), либо получал сумму уже с новым
			 // отсчётом при старом индексе. Запрет прерываний целиком, а не taskENTER_CRITICAL: прерывание АЦП работает
			 // выше барьера FreeRTOS (см. sensorFrequency::Read).
			 uint32_t s; uint16_t n; bool full; uint16_t raw;
			 {
				 uint32_t primask = __get_PRIMASK();
				 __disable_irq();
				 s = adc_sum; n = adc_last; full = adc_flagFull; raw = adc_lastVal;
				 __set_PRIMASK(primask);
			 }
			 if(full) lastADC = s / (adc_filter_max + 1);
			 else if(n) lastADC = s / n;
			 else lastADC = raw;           // ещё нет усреднения - берём сырое значение
		 }
#endif
		 lastPress = (int32_t) lastADC * cfg.transADC / 1000 - cfg.zeroValue;
	 }
#if P_NUMSAMLES > 1
	 // Усреднение значений
	 sum=sum+lastPress;// Добавить новое значение
	 sum=sum-p[last];// Убрать самое старое значение
	 p[last]=lastPress;// Запомить новое значение
	 if (last<P_NUMSAMLES-1) last++; else {last=0; SETBIT1(flags,fFull);}
	 if (GETBIT(flags,fFull)) lastPress=sum/P_NUMSAMLES; else lastPress=sum/last;
#endif
	 if(Value != lastPress) {
		 Value = lastPress;
		 Temp = ERROR_TEMPERATURE;
	 }
	 // Проверка на ошибки именно здесь обрабатывются ошибки и передаются на верх
	 if(Value < cfg.minValue) {
		 err = ERR_ANALOG_MIN;
		 set_Error(err, name);
		 return err;
	 }
	 if(Value > cfg.maxValue) {
		 err = ERR_ANALOG_MAX;
		 set_Error(err, name);
		 return err;
	 }
	 // Дошли до сюда значит ошибок нет
	 err = OK;                                         // Новый цикл новые ошибки
	 return err;
 }

// ---------------------------------------------------------------------------
// Переходники к модулю свойств хладагента (Freon.h).
//
// Сам модуль про HeatPump ничего не знает - это сознательно, он собирается
// отдельной единицей трансляции и проверяется тестами без заглушек. Переходники
// (кеш температуры в объекте датчика, выбор режима нагрев/охлаждение по
// четырёхходовому клапану) без HeatPump смысла не имеют: PressToTemp лежит в
// HeatPump.h, выбор режима - в EEV.cpp.
//
// PressToTemp always_inline, и из отдельной единицы трансляции её иначе не позвать -
// тела не видно. Кеш, который она заполняет, сбрасывает sensorADC::Read
// (Temp = ERROR_TEMPERATURE при изменении давления).
// ---------------------------------------------------------------------------

// Установка 0 датчика температуры
int8_t sensorADC::set_zeroValue(int16_t p)
{
		clearBuffer();
		cfg.zeroValue = p;
		return OK;
}

//Получить значение давления датчика в сотых бара
int16_t sensorADC::get_Value()
{
	if (!(GETBIT(flags,fPresent))) return -100;                  // датчик запрещен в конфигурации то давление -100
	return Value;    
}

// Установить значение коэффициента преобразования напряжение (отсчеты ацп)-температура
int8_t sensorADC::set_transADC(float p)
{
		clearBuffer();   // Суммы обнулить надо
		cfg.transADC = rd(p, 1000);
		return OK;
}

// Установить значение давления датчика в режиме теста
int8_t sensorADC::set_testValue(int16_t p)            
{
	cfg.testValue=p;
	return OK;
}

void sensorADC::after_load(void)
{
}

// ---------------------------------------------------------------------------
// ---- Цифровые контактные датчики (есть 2 состяния 0 и 1) ----

// Инициализация контактного датчика
void  sensorDigitalInput::initInput(int sensor)
{
   Input=false;                    // Состояние датчика
   cfg.number = sensor;
   cfg.testInput=InputSetup[sensor].test;    // Состояние датчика в режиме теста
   testMode=NORMAL;                // Значение режима тестирования
   cfg.alarmInput=InputSetup[sensor].alarm;  // Состояние датчика в режиме аварии
   err=OK;                         // ошибка датчика (работа) при ошибке останов ТН
   flags=0x00;                     // сброс флагов
   // флаги  0 - наличие датчика,  1- режим теста
   SETBIT1(flags,fPresent);        // наличие датчика в текушей конфигурации
   type=InputSetup[sensor].type;         // тип датчика
   pin=InputSetup[sensor].pin;           // пин датчика
   pinMode(pin, INPUT);             // Настроить ножку на вход
   note=(char*)InputSetup[sensor].note;   // присвоить наименование датчика
   name=(char*)InputSetup[sensor].name;   // присвоить имя датчика
   Input=digitalReadDirect(pin);    // Состояние датчика прочитать датчик но не анализировать аварию (хотя этого можно не делать)
};

// Чтение датчика возвращает ошибку или ОК
int8_t sensorDigitalInput::Read(boolean fast)
{
	err = OK;                                            // Ошибки сбросить
	if(testMode != NORMAL) Input = cfg.testInput;            // В режиме теста
	else {
		boolean in = digitalReadDirect(pin);
		if(!fast && in != Input) {
			uint8_t i;
			for(i = 0; i < 2; i++) {
				_delay(1);
				if(in != digitalReadDirect(pin)) break;
			}
			if(i == 2) Input = in;
		}
	}
	if(type == pALARM && Input == cfg.alarmInput)     // Срабатывание аварийного датчика (только его!)
	{
		err = ERR_DINPUT;
		set_Error(err, name);
	}
	return err;
}
    
// Установить Состояние датчика в режиме теста
int8_t sensorDigitalInput::set_testInput( int16_t i)         
{
   if(i==1)  { cfg.testInput=true; return OK;} 
    else  if (i==0)  { cfg.testInput=false; return OK;} 
     else return WARNING_VALUE;
}
 
// Установить Состояние датчика в режиме аварии
int8_t sensorDigitalInput::set_alarmInput( int16_t i)         
{
  if(i==1)  { cfg.alarmInput=true; return OK;} 
    else  if(i==0)  { cfg.alarmInput=false; return OK;} 
     else return WARNING_VALUE;
}

// ---------------------------------------------------------------------------
// ---- Цифровые частотные датчики (значение кодируется в выходной частоте) ----
// основное назначение - датчики потока
// Частота кодируется в тысячных герца
// Число импульсов рассчитывается за базовый период (BASE_TIME), т.к частоты малы период надо савить не менее 5 сек

// Обработчики прерываний для подсчета частоты
#if FNUMBER > 0
void InterruptFreq0() { HP.sFrequency[0].InterruptHandler(); }
#if FNUMBER > 1
void InterruptFreq1() { HP.sFrequency[1].InterruptHandler(); }
#if FNUMBER > 2
void InterruptFreq2() { HP.sFrequency[2].InterruptHandler(); }
#if FNUMBER > 3
void InterruptFreq3() { HP.sFrequency[3].InterruptHandler(); }
#if FNUMBER > 4
void InterruptFreq4() { HP.sFrequency[4].InterruptHandler(); }
#if FNUMBER > 5
	#error FNUMBER is too big! Maximum 5 allowed!
#endif
#endif
#endif
#endif
#endif
#endif

// Инициализация частотного датчика, на входе номер сенсора по порядку
void sensorFrequency::initFrequency(int sensor)                     
{
   cfg.number = sensor;
   Frequency=0;                                   // значение частоты
   Value=0;                                       // значение датчика в ТЫСЯЧНЫХ (умножать на 1000)
   cfg.Capacity=HEAT_CAPACITY;                        // значение теплоемкости теплоносителя в конутре где установлен датчик [Cp, Дж/(кг·град)]
   cfg.minValue=FrequencySetup[sensor].min;                      // минимальное значение датчика
   cfg.testValue=FrequencySetup[sensor].test;                    // Состояние датчика в режиме теста
   cfg.kfValue=FrequencySetup[sensor].kf;                     // коэффициент пересчета частоты в значение
   testMode=NORMAL;                               // Значение режима тестирования
   count=0;                                       // число импульсов за базовый период (то что меняется в прерывании)
   err=OK;                                        // ошибка датчика (работа) при ошибке останов ТН
   cfg.flags=0x00;                                    // Обнулить флаги
   SETBIT1(cfg.flags,fPresent);                       // наличие датчика в текушей конфигурации - датчик всегда есть в конфигурации добавлено для единообразия
   pin=FrequencySetup[sensor].pin;                     // Ножка куда прицеплен датчик
   note=(char*)FrequencySetup[sensor].note;             // наименование датчика
   name=(char*)FrequencySetup[sensor].name;             // Имя датчика
   reset();
   // Привязывание обработчика прерываний к методу конкретного класса
   //   LOW вызывает прерывание, когда на порту LOW
   //   CHANGE прерывание вызывается при смене значения на порту, с LOW на HIGH и наоборот
   //   RISING прерывание вызывается только при смене значения на порту с LOW на HIGH
   //   FALLING прерывание вызывается только при смене значения на порту с HIGH на LOW
#if FNUMBER > 0
   if(sensor == 0) attachInterrupt(pin, InterruptFreq0, CHANGE);
#if FNUMBER > 1
   else if(sensor == 1) attachInterrupt(pin, InterruptFreq1, CHANGE);
#if FNUMBER > 2
   else if(sensor == 2) attachInterrupt(pin, InterruptFreq2, CHANGE);
#if FNUMBER > 3
   else if(sensor == 3) attachInterrupt(pin, InterruptFreq3, CHANGE);
#if FNUMBER > 4
   else if(sensor == 4) attachInterrupt(pin, InterruptFreq4, CHANGE);
#endif
#endif
#endif
#endif
#endif
}

// Получить (точнее обновить) значение датчика, возвращает 1, если новое значение рассчитано
int8_t sensorFrequency::Read()
{
	if(testMode != NORMAL) {
		Value = cfg.testValue;
#if defined(RPUMPI) && defined(FLOWEVA)
		if(number == FLOWEVA && !HP.dRelay[RPUMPI].get_Relay()) Value = 0;
#endif
#if defined(RPUMPO) && defined(FLOWCON)
		if(cfg.number == FLOWCON && !HP.dRelay[RPUMPO].get_Relay()) Value = 0;
#endif
		Frequency = Value * cfg.kfValue / 360;
		return 0;
	}   // В режиме теста
#ifdef DEMO
	Frequency=random(2500,9000);
	count=0;
	Value=Frequency * 360 / cfg.kfValue;// ЛИТРЫ В ЧАС (ИЛИ ТЫСЯЧНЫЕ КУБА) частота в тысячных, и коэффициент правим
#else
	if(GetTickCount() - sTime >= (uint32_t)BASE_TIME_READ * 1000) {  // если только пришло время измерения
		uint32_t tickCount, cnt;
		// Пара "прочитать и обнулить" обязана быть атомарной: импульс, пришедший
		// между двумя операторами, терялся бы. taskENTER_CRITICAL здесь не годится -
		// он маскирует прерывания только до configMAX_SYSCALL_INTERRUPT_PRIORITY,
		// а обработчик PIO работает с приоритетом Arduino по умолчанию, то есть выше
		// барьера. Поэтому запрещаем прерывания целиком, с сохранением PRIMASK.
		{
			uint32_t primask = __get_PRIMASK();
			__disable_irq();
			tickCount = GetTickCount();
			cnt = count;
			count = 0;
			__set_PRIMASK(primask);
		}
		__asm__ volatile ("" ::: "memory");
		Frequency = (cnt * 500 * 1000) / (tickCount - sTime); // ТЫСЯЧНЫЕ ГЦ время в миллисекундах частота в тысячных герца *2 (прерывание по обоим фронтам)!!!!!!!!
		sTime = tickCount;
		Value = Frequency * 360 / cfg.kfValue; // ЛИТРЫ В ЧАС (ИЛИ ТЫСЯЧНЫЕ КУБА) частота в тысячных, и коэффициент правим
		return 1;
	}
#endif
	return 0;
}

void sensorFrequency::reset(void)
{

	// см. комментарий в Read(): барьер FreeRTOS не маскирует прерывание PIO
	{
		uint32_t primask = __get_PRIMASK();
		__disable_irq();
		sTime = GetTickCount();
		count = 0;
		__set_PRIMASK(primask);
	}
}

// Установить Состояние датчика в режиме теста
int8_t  sensorFrequency::set_testValue(int16_t i) 
{
   cfg.testValue=i;
   return OK;
}
// Установить теплоемкость больше 5000 не устанавливается
int8_t sensorFrequency::set_Capacity(uint16_t c)                      
{  
  if (c<=5000) {cfg.Capacity=c; return OK;} else return WARNING_VALUE;    
}   

	// Установить минимальное значение датчика
void sensorFrequency::set_minValue(float f)
{
	cfg.minValue = rd(f, 10);
}

// ---------------------------------------------------------------------------
// ---- Исполнительное устройство РЕЛЕ (есть 2 состяния 0 и 1) ----
// Relay = true - это означает включение исполнительного механизма. 
// При этом реальный выход и состояние (физическое реле) определяется дефайнами RELAY_INVERT и R4WAY_INVERT
// ВНИМАНИЕ: По умолчанию (не определен RELAY_INVERT) выход инвертируется - Влючение реле (Relay=true) соответствует НИЗКИЙ уровень на выходе МК
void devRelay::initRelay(int sensor)
{
   flags=0x00;
   number = sensor;
   testMode=NORMAL;                // Значение режима тестирования
   flags=0x01;                     // наличие датчика в текушей конфигурации (остатки прошлого, реле сейчас есть всегда)  флаги  0 - наличие датчика,  1- режим теста
   pin=RelaySetup[sensor].pin;  
   pinMode(pin, OUTPUT);           // Настроить ножку на выход
   Relay=false;                    // Состояние реле - выключено
#ifndef RELAY_INVERT            // Нет инвертирования реле -  Влючение реле (Relay=true) соответсвует НИЗКИЙ уровень на выходе МК
	#ifdef R4WAY_INVERT              // Признак инвертирования 4х ходового
   	   digitalWriteDirect(pin, number != R4WAY);  // Установить значение
	#else
   	   digitalWriteDirect(pin, true);  // Установить значение
	#endif
#else
	#ifdef R4WAY_INVERT              // Признак инвертирования 4х ходового
   	   digitalWriteDirect(pin, number == R4WAY);  // Установить значение
	#else
   	   digitalWriteDirect(pin, false);  // Установить значение
	#endif
#endif
   note=(char*)RelaySetup[sensor].note;  // присвоить описание реле
   name=(char*)RelaySetup[sensor].name;  // присвоить имя реле
}


// Установить реле в состояние r, базовая функция все остальные функции используют ее
// Если состояния совпадают то ничего не делаем, 0/-1 - выкл основной алгоритм, fR_Status* - включить, -fR_Status* - выключить)
int8_t devRelay::set_Relay(int8_t r, bool planned)
{
	if(!(flags & (1 << fPresent))) return ERR_DEVICE;  // Реле не установлено  и пытаемся его включить
	if(r == 0) r = -fR_StatusMain;
	else if(r == fR_StatusAllOff) {
		flags &= ~fR_StatusMask;
		r = -fR_StatusMain;
	}
	flags = (flags & ~(1 << abs(r))) | ((r > 0) << abs(r));
	r = (flags & fR_StatusMask) != 0;
	if(Relay == r) return OK;   // Ничего менять не надо выходим
    Relay = r;                  // Все удачно, сохранить
	if(testMode == NORMAL || testMode == HARD_TEST || (testMode == TEST && number != RCOMP)) {
#ifndef RELAY_INVERT            // Нет инвертирования реле -  Влючение реле (Relay=true) соответсвует НИЗКИЙ уровень на выходе МК
		r = !r;
#endif
#ifdef R4WAY_INVERT              // Признак инвертирования 4х ходового
		if(number == R4WAY) r = !r;
#endif
		digitalWriteDirect(pin, r);
	}
#ifdef RELAY_WAIT_SWITCH
	uint8_t tasks_suspended = TaskSuspendAll();
	delay(RELAY_WAIT_SWITCH);
	if(tasks_suspended) xTaskResumeAll();
#endif
	if(!planned || DBG_ON(DBG_RELP)) journal.jprintf_time("Relay %s: %s\n", name, Relay ? "ON" : "OFF");
	return OK;
}

// ---------------------------------------------------------------------------
// Реестр параметров веб-API: реле (relay), аналоговые датчики давления (press),
// частотные датчики расхода (flow), сухие контакты (input). Ключи
// <группа>.<ИМЯ>.<атрибут>; имя - как отдаёт get_name(). Значения идут через
// методы классов, прежние проверки (OK или код ошибки класса) сохранены.
// Устройств нет в конфигурации - ключ даёт na (в meta.devs они не перечисляются).
// ---------------------------------------------------------------------------
#define SA(nm, fl, kind, sc, hk) { nm, 0, 0, 0, 0, sc, PT_CALL, fl, kind, hk, 0 }
#define SR(nm, fl, kind, sc, hk, lo, hi) { nm, 0, (float)(lo), (float)(hi), 0, sc, PT_CALL, fl, kind, hk, 0 }

// ---- реле ----
enum { R_ON = 1, R_PRESENT, R_NOTE, R_PIN };
static const ApiParam apiRelayTab[] = {
	SA("on",      0,     MK_BOOL, 1, R_ON),        // состояние; запись: включить вручную / выключить по всем алгоритмам
	SA("present", PF_RO, MK_BOOL, 1, R_PRESENT),
	SA("note",    PF_RO, MK_TEXT, 1, R_NOTE),
	SA("pin",     PF_RO, MK_INT,  1, R_PIN),
};
static uint8_t api_rl_n(void) { return RNUMBER; }
static const char *api_rl_name(uint8_t i) { return HP.dRelay[i].get_name(); }
static uint8_t api_rl_ok(uint8_t i) { return HP.dRelay[i].get_present(); }
static int api_get_rl(uint8_t hook, uint8_t dev, ApiOut *o)
{
	devRelay &r = HP.dRelay[dev];
	switch(hook) {
	case R_ON:      api_put(o, r.get_Relay() ? "true" : "false"); break;
	case R_PRESENT: api_put(o, r.get_present() ? "true" : "false"); break;
	case R_NOTE:    api_put_str(o, r.get_note()); break;
	case R_PIN:     api_put_int(o, r.get_pinD()); break;
	default: return AE_FAIL;
	}
	return AE_OK;
}
static int api_set_rl(uint8_t hook, uint8_t dev, const char *val)
{
	if(hook != R_ON) return AE_FAIL;
	bool on;
	if(!strcmp(val, "1") || !strcmp(val, "true")) on = true; else if(!strcmp(val, "0") || !strcmp(val, "false")) on = false; else return AE_BAD_VALUE;
	return HP.dRelay[dev].set_Relay(on ? fR_StatusManual : fR_StatusAllOff) == OK ? AE_OK : AE_FAIL;
}
static const ApiOps apiOpsRelay = { 0, api_get_rl, api_set_rl, 0, 0, api_rl_n, api_rl_name, api_rl_ok };
extern const ApiGroup ApiGrpRelay;
const ApiGroup ApiGrpRelay = { "relay", apiRelayTab, sizeof(apiRelayTab) / sizeof(apiRelayTab[0]), &apiOpsRelay };

// ---- аналоговые датчики давления ----
enum { P_VALUE = 1, P_TEMP, P_ADC, P_MIN, P_MAX, P_ZERO, P_TRANS, P_PIN, P_TEST, P_ECODE, P_PRESENT, P_NOTE };
static const ApiParam apiPressTab[] = {
	SA("value",   PF_RO, MK_NUM, 100,  P_VALUE),                  // бар
	SA("temp",    PF_RO, MK_NUM, 100,  P_TEMP),                   // температура насыщения по давлению, °C (только датчики испарителя при EEV_DEF); null - нет
	SA("adc",     PF_RO, MK_INT, 1,    P_ADC),                    // отсчёты АЦП с фильтром
	SR("min",     0,     MK_NUM, 100,  P_MIN, -327.68f, 327.67f),
	SR("max",     0,     MK_NUM, 100,  P_MAX, -327.68f, 327.67f),
	SR("zero",    0,     MK_INT, 1,    P_ZERO, -32768, 32767),    // отсчёты АЦП при нуле датчика
	SA("trans",   0,     MK_NUM, 1000, P_TRANS),                  // коэффициент пересчёта АЦП в бар
	SA("pin",     PF_RO, MK_INT, 1,    P_PIN),                    // канал АЦП
	SA("test",    0,     MK_NUM, 100,  P_TEST),
	SA("ecode",   PF_RO, MK_INT, 1,    P_ECODE),
	SA("present", PF_RO, MK_BOOL, 1,   P_PRESENT),
	SA("note",    PF_RO, MK_TEXT, 1,   P_NOTE),
};
static uint8_t api_pr_n(void) { return ANUMBER; }
static const char *api_pr_name(uint8_t i) { return HP.sADC[i].get_name(); }
static uint8_t api_pr_ok(uint8_t i) { return HP.sADC[i].get_present(); }
static int api_get_pr(uint8_t hook, uint8_t dev, ApiOut *o)
{
	sensorADC &s = HP.sADC[dev];
	switch(hook) {
	case P_VALUE:   api_put_dec(o, s.get_Value(), 100); break;
	case P_TEMP:
#ifdef EEV_DEF
		if(dev < 2) { api_put_dec(o, PressToTemp(dev), 100); break; }
#endif
		api_put(o, "null"); break;
	case P_ADC:     api_put_int(o, s.get_lastADC()); break;
	case P_MIN:     api_put_dec(o, s.get_minValue(), 100); break;
	case P_MAX:     api_put_dec(o, s.get_maxValue(), 100); break;
	case P_ZERO:    api_put_int(o, s.get_zeroValue()); break;
	case P_TRANS:   api_put_dec(o, s.get_transADC(), 1000); break;
	case P_PIN:     api_put_int(o, s.get_pinA()); break;
	case P_TEST:    api_put_dec(o, s.get_testValue(), 100); break;
	case P_ECODE:   api_put_int(o, s.get_lastErr()); break;
	case P_PRESENT: api_put(o, s.get_present() ? "true" : "false"); break;
	case P_NOTE:    api_put_str(o, s.get_note()); break;
	default: return AE_FAIL;
	}
	return AE_OK;
}
static int api_set_pr(uint8_t hook, uint8_t dev, const char *val)
{
	sensorADC &s = HP.sADC[dev];
	float x = my_atof(val);
	if(x == ATOF_ERROR) return AE_BAD_VALUE;
	switch(hook) {
	case P_MIN:   return s.set_minValue(rd(x, 100)) == OK ? AE_OK : AE_RANGE;
	case P_MAX:   return s.set_maxValue(rd(x, 100)) == OK ? AE_OK : AE_RANGE;
	case P_ZERO:  return s.set_zeroValue((int16_t) x) == OK ? AE_OK : AE_RANGE;
	case P_TRANS: return s.set_transADC(x) == OK ? AE_OK : AE_RANGE;
	case P_TEST:  return s.set_testValue(rd(x, 100)) == OK ? AE_OK : AE_RANGE;
	}
	return AE_FAIL;
}
static const ApiOps apiOpsPress = { 0, api_get_pr, api_set_pr, 0, 0, api_pr_n, api_pr_name, api_pr_ok };
extern const ApiGroup ApiGrpPress;
const ApiGroup ApiGrpPress = { "press", apiPressTab, sizeof(apiPressTab) / sizeof(apiPressTab[0]), &apiOpsPress };

// ---- частотные датчики (расход) ----
enum { F_VALUE = 1, F_FREQ, F_MIN, F_KF, F_CAP, F_TEST, F_PIN, F_ECODE, F_NOTE, F_CHECK, F_PRESENT };
static const ApiParam apiFlowTab[] = {
	SA("value",   PF_RO, MK_NUM, 1000, F_VALUE),                  // м³/ч
	SA("freq",    PF_RO, MK_NUM, 1000, F_FREQ),                   // Гц
	SR("min",     0,     MK_NUM, 10,   F_MIN, 0.1f, 25.5f),          // м³/ч, контроль минимума
	SR("kf",      0,     MK_NUM, 100,  F_KF,  0.01f, 327.67f),    // коэффициент пересчёта частоты; 0 - деление на ноль в расчёте расхода
	SR("cap",     0,     MK_INT, 1,    F_CAP, 0, 5000),           // теплоёмкость теплоносителя
	SR("test",    0,     MK_NUM, 1000, F_TEST, 0, 32.767f),
	SA("pin",     PF_RO, MK_INT, 1,    F_PIN),
	SA("ecode",   PF_RO, MK_INT, 1,    F_ECODE),
	SA("note",    PF_RO, MK_TEXT, 1,   F_NOTE),
	SA("check",   0,     MK_BOOL, 1,   F_CHECK),                  // контроль минимального потока
	SA("present", PF_RO, MK_BOOL, 1,   F_PRESENT),
};
static uint8_t api_fl_n(void) { return FNUMBER; }
static const char *api_fl_name(uint8_t i) { return HP.sFrequency[i].get_name(); }
static uint8_t api_fl_ok(uint8_t i) { return HP.sFrequency[i].get_present(); }
static int api_get_fl(uint8_t hook, uint8_t dev, ApiOut *o)
{
	sensorFrequency &s = HP.sFrequency[dev];
	switch(hook) {
	case F_VALUE:   api_put_dec(o, s.get_Value(), 1000); break;
	case F_FREQ:    api_put_dec(o, s.get_Frequency(), 1000); break;
	case F_MIN:     api_put_dec(o, s.get_minValue() / 100, 10); break;
	case F_KF:      api_put_dec(o, (int32_t) s.get_kfValue(), 100); break;
	case F_CAP:     api_put_int(o, s.get_Capacity()); break;
	case F_TEST:    api_put_dec(o, s.get_testValue(), 1000); break;
	case F_PIN:     api_put_int(o, s.get_pinF()); break;
	case F_ECODE:   api_put_int(o, s.get_lastErr()); break;
	case F_NOTE:    api_put_str(o, s.get_note()); break;
	case F_CHECK:   api_put(o, s.get_checkFlow() ? "true" : "false"); break;
	case F_PRESENT: api_put(o, s.get_present() ? "true" : "false"); break;
	default: return AE_FAIL;
	}
	return AE_OK;
}
static int api_set_fl(uint8_t hook, uint8_t dev, const char *val)
{
	sensorFrequency &s = HP.sFrequency[dev];
	if(hook == F_CHECK) {
		if(!strcmp(val, "1") || !strcmp(val, "true")) s.set_checkFlow(true); else if(!strcmp(val, "0") || !strcmp(val, "false")) s.set_checkFlow(false); else return AE_BAD_VALUE;
		return AE_OK;
	}
	float x = my_atof(val);
	if(x == ATOF_ERROR) return AE_BAD_VALUE;
	switch(hook) {
	case F_MIN:  s.set_minValue(x); return AE_OK;
	case F_KF:   s.set_kfValue(rd(x, 100)); return AE_OK;
	case F_CAP:  return s.set_Capacity((uint16_t) x) == OK ? AE_OK : AE_RANGE;
	case F_TEST: return s.set_testValue(rd(x, 1000)) == OK ? AE_OK : AE_RANGE;
	}
	return AE_FAIL;
}
static const ApiOps apiOpsFlow = { 0, api_get_fl, api_set_fl, 0, 0, api_fl_n, api_fl_name, api_fl_ok };
extern const ApiGroup ApiGrpFlow;
const ApiGroup ApiGrpFlow = { "flow", apiFlowTab, sizeof(apiFlowTab) / sizeof(apiFlowTab[0]), &apiOpsFlow };

// ---- сухие контакты ----
enum { I_VALUE = 1, I_PRESENT, I_NOTE, I_TEST, I_ALARM, I_ECODE, I_PIN, I_TYPE };
static const ApiParam apiInputTab[] = {
	SA("value",   PF_RO, MK_BOOL, 1, I_VALUE),
	SA("present", PF_RO, MK_BOOL, 1, I_PRESENT),
	SA("note",    PF_RO, MK_TEXT, 1, I_NOTE),
	SA("test",    0,     MK_BOOL, 1, I_TEST),       // состояние в режиме теста
	SA("alarm",   0,     MK_BOOL, 1, I_ALARM),      // при каком состоянии контакта авария
	SA("ecode",   PF_RO, MK_INT,  1, I_ECODE),
	SA("pin",     PF_RO, MK_INT,  1, I_PIN),
	SA("type",    PF_RO, MK_TEXT, 1, I_TYPE),       // Alarm - авария, Work - рабочий, Pulse - импульсный
};
static uint8_t api_in_n(void) { return INUMBER; }
static const char *api_in_name(uint8_t i) { return HP.sInput[i].get_name(); }
static uint8_t api_in_ok(uint8_t i) { return HP.sInput[i].get_present(); }
static int api_get_in(uint8_t hook, uint8_t dev, ApiOut *o)
{
	sensorDigitalInput &s = HP.sInput[dev];
	switch(hook) {
	case I_VALUE:   api_put(o, s.get_Input() ? "true" : "false"); break;
	case I_PRESENT: api_put(o, s.get_present() ? "true" : "false"); break;
	case I_NOTE:    api_put_str(o, s.get_note()); break;
	case I_TEST:    api_put(o, s.get_testInput() ? "true" : "false"); break;
	case I_ALARM:   api_put(o, s.get_alarmInput() ? "true" : "false"); break;
	case I_ECODE:   api_put_int(o, s.get_lastErr()); break;
	case I_PIN:     api_put_int(o, s.get_pinD()); break;
	case I_TYPE:    api_put_str(o, s.get_typeInput() == pALARM ? "Alarm" : s.get_typeInput() == pSENSOR ? "Work" : s.get_typeInput() == pPULSE ? "Pulse" : "err_type"); break;
	default: return AE_FAIL;
	}
	return AE_OK;
}
static int api_set_in(uint8_t hook, uint8_t dev, const char *val)
{
	sensorDigitalInput &s = HP.sInput[dev];
	int b;
	if(!strcmp(val, "1") || !strcmp(val, "true")) b = 1; else if(!strcmp(val, "0") || !strcmp(val, "false")) b = 0; else return AE_BAD_VALUE;
	if(hook == I_TEST)  return s.set_testInput(b) == OK ? AE_OK : AE_RANGE;
	if(hook == I_ALARM) return s.set_alarmInput(b) == OK ? AE_OK : AE_RANGE;
	return AE_FAIL;
}
static const ApiOps apiOpsInput = { 0, api_get_in, api_set_in, 0, 0, api_in_n, api_in_name, api_in_ok };
extern const ApiGroup ApiGrpInput;
const ApiGroup ApiGrpInput = { "input", apiInputTab, sizeof(apiInputTab) / sizeof(apiInputTab[0]), &apiOpsInput };
