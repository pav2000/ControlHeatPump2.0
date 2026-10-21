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
 * Электронно-регулируемый вентиль: единственный экземпляр devEEV. Пуск и
 * обнуление, расчёт перегрева и ПИД, коррекция, веб-параметры.
 *
 * Весь файл внутри #ifdef EEV_DEF - в конфигурации без ЭРВ не остаётся
 * ничего. Датчики и реле - в Sensors.cpp, шина Modbus - в Modbus.cpp, плата - в
 * Board.cpp.
 *
 * Hardware.h тянет Constant.h, а с ним Config.h, поэтому условия
 * конфигурации разрешаются так же, как в главной единице трансляции.
 * Проверено сверкой размеров методов.
 */
#include "Hardware.h"     // devEEV, stepperEEV
#include "../core/HeatPump.h"     // HP, PressToTemp, updatePID, UpdatePIDbyTime
#include "../core/OverheatCalc.h" // чистый расчёт перегрева, см. заголовок
#include "../net/ParamRegistry.h"   // таблица параметров веб-API (внизу файла)

// ---------------------------------------------------------------------------
// ---- ЭРВ ТОЛЬКО ОДНА ШТУКА ВСЕГДА (не массив) ----
 #ifdef EEV_DEF     
// Положение ЭРВ по температурам испарителя и конденсатора. Вызывающий один -
// devEEV::Update, внутри этого же #ifdef: без ЭРВ функция не нужна вовсе.
// Проверка R4WAY обязана быть здесь: Config.h виден этой единице трансляции,
// а в Freon.cpp такой #ifdef молча отключал охлаждение.
int16_t TempToEEV(int16_t teva, int16_t tcon)
{
#ifdef R4WAY
	return Freon_TempToEEV(teva, tcon, HP.is_heating());
#else
	return Freon_TempToEEV(teva, tcon, true);   // без клапана всегда нагрев
#endif
}

const char *noteEEV = {"Электронно регулируемый вентиль" };// Описание
const char *nameEEV = {"EEV"} ;  //  Имя
// Инициализация ЭРВ
void devEEV::initEEV()
{
	EEV = -1;                               // шаговик в непонятном положении
	setZero = false;                        // Признак процесса обнуления (шаговик ищет 0)
	err = OK;                               // Ошибок нет
	Resume(); 			                // Обнулить рабочие переменные
	testMode = NORMAL;                      // Значение режима тестирования

	// Установка настроек по умолчанию (структура данных _data)
	_data.tOverheat = DEFAULT_OVERHEAT;                 // Перегрев ЦЕЛЬ (сотые градуса)
	_data.pid_time = DEFAULT_EEV_TIME;                  // Постоянная интегрирования времени в секундах ЭРВ СЕКУНДЫ
	_data.pid.Kp = -DEFAULT_EEV_Kp * 10;               // ПИД Коэф пропорц, в тысячных
	_data.pid.Ki = -DEFAULT_EEV_Ki * 10;               // ПИД Коэф интегр.,  в тысячных
	_data.pid.Kd = -DEFAULT_EEV_Kd * 10;               // ПИД Коэф дифф., в тысячных
	_data.Correction = 0; // 0.855 ПЕРЕДЕЛАНО  зона не чувствительности перегрева в "плюсе" в этой зоне на каждом шаге эрв закрывается на 1 шаг
	_data.manualStep = (EEV_STEPS - _data.minSteps) / 2 + _data.minSteps; // Число шагов открытия ЭРВ для правила работы ЭРВ «Manual» - половина диапазона ЭРВ
	_data.ruleEEV = DEFAULT_RULE_EEV;                   // правило работы ЭРВ
#ifdef DEF_OHCor_OverHeatStart						 // Корректировка перегрева
	_data.OHCor_Delay = DEF_OHCor_Delay;			     // Задержка после старта компрессора, сек
	_data.OHCor_TDIS_TCON = DEF_OHCor_TDIS_TCON;		 // Температура нагнетания - конденсации при 30С и 0 конденсации
	_data.OverheatMin = DEF_OHCor_OverHeatMin;		     // Минимальный перегрев (сотые градуса)
	_data.OverheatMax = DEF_OHCor_OverHeatMax;		     // Максимальный перегрев (сотые градуса)
	_data.OverHeatStart = DEF_OHCor_OverHeatStart; 	 // Начальный перегрев (сотые градуса)
	_data.OHCor_Period = DEF_OHCor_Period;
#endif
	_data.speedEEV = DEFAULT_SPEED_EEV;                 // Скорость шагового двигателя ЭРВ (импульсы в сек.)
	_data.preStartPos = DEFAULT_PRE_START_POS;     // ПУСКОВАЯ позиция ЭРВ (ТО что при старте компрессора ПРИ РАСКРУТКЕ)
	_data.StartPos = DEFAULT_START_POS; // СТАРТОВАЯ позиция ЭРВ после раскрутки компрессора т.е. ПОЗИЦИЯ С КОТОРОЙ НАЧИНАЕТСЯ РАБОТА проходит DelayStartPos сек
	_data.minSteps = EEV_CLOSE_STEP;                    // Минимальное число шагов открытия ЭРВ
	_data.maxSteps = EEV_STEPS;                         // Максимальное число шагов ЭРВ (диапазон)
	_data.pid_max = 45;
	_data.trend_threshold = 4;
	_data.tOverheatTCOMP = 850;
	_data.tOverheatTCOMP_delta = 300;
	_data.PosAtHighTemp = 0;
	_data.pid2_delta = 70;
	_data.trend_mul_threshold = 65;
	_data.tOverheat2_low = 200;
	_data.tOverheat2_low_hyst = 10;

	// ЭРВ Времена и задержки
	_data.delayOnPid = DEFAULT_DELAY_ON_PID; // Задержка включения EEV после включения компрессора (сек).  Точнее после выхода на рабочую позицию Общее время =delayOnPid+DelayStartPos
	_data.delayOn = DEFAULT_DELAY_ON; // Задержка между открытием (для старта) ЭРВ и включением компрессора, для выравнивания давлений (сек). Если ЭРВ закрывлось при остановке
	_data.DelayStartPos = DEFAULT_DELAY_START_POS; // Время после старта компрессора когда EEV выходит на стартовую позицию - облегчение пуска вначале ЭРВ
	_data.delayOff = DEFAULT_DELAY_OFF; // Задержка закрытия EEV после выключения насосов (сек). Время от команды стоп компрессора до закрытия ЭРВ = delayOffPump+delayOff
	_data.flags = 0x00;                                  // флаги ЭРВ,
#ifdef EEV_DEF
	SETBIT1(_data.flags, fPresent);                      // наличие ЭРВ в текушей конфигурации
#endif
	if(DEFAULT_HOLD_MOTOR) SETBIT1(_data.flags, fHoldMotor);
	SETBIT0(_data.flags, fCorrectOverHeat);              // Включен режим корректировки перегрева
	SETBIT1(_data.flags, fOneSeekZero);            //  Флаг однократного поиска "0" ЭРВ (только при первом включении ТН)
	SETBIT0(_data.flags, fEevClose);                     // Флаг закрытие ЭРВ при выключении компрессора
	SETBIT0(_data.flags, fLightStart); // флаг Облегчение старта компрессора   приоткрытие ЭРВ в момент пуска компрессора
	SETBIT1(_data.flags, fStartFlagPos);                 // флаг Всегда начинать работу ЭРВ со стратовой позици
	SETBIT1(_data.flags, fEEV_StartPosByTemp);

	name = (char*) nameEEV;                  // Присвоить имя
	note = (char*) noteEEV;                  // Присвоить описание

	InitStepper();
}

// Инициализация шагового двигателя ЭРВ   ВАЖНО ПРАВИЛЬНОЕ ПОДКЛЮЧЕНИЕ!!!
// Рабочие комбинации для подключения шаговика на 5 вольт 28BYJ-48
// So in the end, four entries worked: (8,10,11,9), (9,11,8,10), (10,8,9,11), and (11,9,10,8).
// http://www.utopiamechanicus.com/article/arduino-stepper-motor-setup-troubleshooting/
// Подключение двигателя 28BYJ-48. Драйвер ULN-2003 схема (8,10,11,9)
// http://42bots.com/tutorials/28byj-48-stepper-motor-with-uln2003-driver-and-arduino-uno/
// ---- ШАГОВИК на 5 вольт 28BYJ-48 5V ----
// нога 1 синий       - В
// нога 2 фиолетовый  + А
// нога 3 желтый      + В
// нога 4 оранжевый   - А
// нога 5 красный     + общий
// ---- ЭРВ 12 вольт ----
// нога 1 оранжевый  + А
// нога 2 красный    + В
// нога 3 желтый     - А
// нога 4 черный     - В
// нога 5 синий      + общий
// ---- Распиновка LM9333 ----
// D24     нога 1
// D26     нога 2
// D25     нога 3
// D27     нога 4
// ---- Распиновка ULN2003 ----
// D24     нога 1
// D25     нога 2
// D26     нога 3
// D27     нога 4
// Инициализация библиотеки порядок указания фаз в функции initStepMotor +A +B -A -B
void devEEV::InitStepper(void)
{
#ifdef DEMO
    stepperEEV.initStepMotor(_data.maxSteps, PIN_EEV3_D26,PIN_EEV2_D25,PIN_EEV4_D27,PIN_EEV1_D24, EEV_PHASE, EEV_DRV_INVERT);          // для тестирования  на шаговике 5 вольт вроде работает
#else  
    #ifdef DRV_EEV_L9333                                                                          // использование драйвера L9333
      #ifdef  EEV_INVERT                                                                          // Признак инвертирования движения ЭРВ
         stepperEEV.initStepMotor(_data.maxSteps,PIN_EEV4_D27,PIN_EEV2_D25,PIN_EEV3_D26,PIN_EEV1_D24, EEV_PHASE, EEV_DRV_INVERT);    // на 8 фазном работает 480 шагов обратное подключение
      #else
         stepperEEV.initStepMotor(_data.maxSteps,PIN_EEV1_D24,PIN_EEV3_D26,PIN_EEV2_D25,PIN_EEV4_D27, EEV_PHASE, EEV_DRV_INVERT);    // на 8 фазном работает 480 шагов прямое подключение
      #endif
    #else    
      #ifdef  EEV_INVERT                                                                          // Признак инвертирования движения ЭРВ
         stepperEEV.initStepMotor(_data.maxSteps,PIN_EEV4_D27,PIN_EEV3_D26,PIN_EEV2_D25,PIN_EEV1_D24, EEV_PHASE, EEV_DRV_INVERT);    // на 8 фазном работает 480 шагов обратное подключение
      #else
         stepperEEV.initStepMotor(_data.maxSteps,PIN_EEV1_D24,PIN_EEV2_D25,PIN_EEV3_D26,PIN_EEV4_D27, EEV_PHASE, EEV_DRV_INVERT);    // на 8 фазном работает 480 шагов прямое подключение
      #endif
    #endif  // DRV_EEV_L9333
#endif // DEMO   
    stepperEEV.setSpeed(_data.speedEEV);   // Установить скорость движения
}

void devEEV::after_load(void)
{
	legacy_eev_flags(HP.Option.ver, &_data.flags, &_data.OHCor_TDIS_TCON_MAX);
#ifdef EEV_DEF
	SETBIT1(_data.flags,fPresent);                      // наличие ЭРВ в текушей конфигурации
#else
	SETBIT0(_data.flags,fPresent);                      // отсутствие ЭРВ в текушей конфигурации
#endif
	InitStepper();
}

// Восстановление слежения ЭРВ, если конечно задача запущена
void devEEV::Resume()
{
  fPause=false;                          // не пауза
  resetPID();                            // Сброс служебных переменных ПИД
}
 
// Запустить ЭРВ - возвращает ок или код ошибки
// На стартовую позицию не выводит
int8_t devEEV::Start()
{
	Resume();
	err = OK;                               // Ошибок нет
	if(!GETBIT(_data.flags, fPresent)) {
		journal.jprintf(" EEV is not available\n");
		return err;
	}  // если ЭРВ нет то ничего не делаем
	if((!GETBIT(_data.flags, fOneSeekZero)) || (GETBIT(_data.flags,fOneSeekZero) && (EEV < 0))) { // есть вариант однократного поиска "0" ЭРВ
		set_zero();
	}                          // установить 0
	return OK;
}
// Гарантированно (шагов больше чем диапазон) закрыть ЭРВ возвращает код ошибки
int8_t devEEV::set_zero()
{
	if(!setZero) {
		journal.jprintf(" EEV: Set zero\n");
		setZero = true;                                             // Признак ПРОЦЕССА обнуления счетчика шагов EEV  Ставить в начале!!
		if(testMode != SAFE_TEST) { if(!stepperEEV.step(0)) journal.jprintf("$ERROR - Step motor command queue is FULL!\n"); }  // не  SAFE_TEST - работаем
		else EEV = 0;                                               // SAFE_TEST только координаты меняем
	}
	return OK;
}

 // Перейти на позицию абсолютную возвращает код ошибки
 // Отрицательные значения ЛЮБЫЕ признак установки 0
int8_t devEEV::set_EEV(int16_t x)
{
	err = OK;
	if(!(GETBIT(_data.flags, fPresent))) { // ЭРВ не установлен
		err = ERR_DEVICE;
		return err;
	}
	if(x < EEV_CLOSE_STEP) x = EEV_CLOSE_STEP; else if(x > _data.maxSteps) x = _data.maxSteps;
	if(testMode != SAFE_TEST) { if(!stepperEEV.step(x)) journal.jprintf("$ERROR - Step motor command queue is FULL!\n"); }                   // не  SAFE_TEST - работаем
	else EEV = x;                                                    // SAFE_TEST только координаты меняем
	return err;
}

// Стартовая позиция ЭРВ
uint16_t devEEV::get_StartPos()
{
	if(GETBIT(_data.flags, fEEV_StartPosByTemp)) {
		int16_t t = HP.sTemp[HP.is_heating() ? TCONOUTG : TEVAOUTG].get_Temp();
		if(t <= EEV_START_POS_LOW_TEMP) return _data.StartPos;
		else if(t >= EEV_START_POS_HIGH_TEMP) return _data.PosAtHighTemp;
		return (int32_t)_data.StartPos - (t - EEV_START_POS_LOW_TEMP) * (_data.StartPos - _data.PosAtHighTemp) / (EEV_START_POS_HIGH_TEMP - EEV_START_POS_LOW_TEMP);
	} else return _data.StartPos;
}

// Перевод правила из RULE_EEV в постоянную нумерацию OverheatCalc.
// У RULE_EEV номера зависят от конфигурации - значения появляются только при
// своих #ifdef, поэтому одно и то же число означает в разных сборках разные
// правила. Перевод живёт здесь, рядом с #ifdef, а не в расчёте.
// Правило, которого в этой сборке нет, попадёт в default и будет считаться
// ручным перебором.
static uint8_t ruleToOverheatRule(RULE_EEV rule)
{
	switch(rule) {
	case TEVAOUT_PEVA:		return OHR_TEVAOUT_PEVA;
#ifdef TCOMPIN
	case TCOMPIN_PEVA:		return OHR_TCOMPIN_PEVA;
#endif
#ifdef TEVAIN
	case TEVAOUT_TEVAIN:	return OHR_TEVAOUT_TEVAIN;
	case TCOMPIN_TEVAIN:	return OHR_TCOMPIN_TEVAIN;
	case TABLE:				return OHR_TABLE;
#endif
	default:				return OHR_MANUAL;
	}
}

// Вычислить текущий перегрев, вычисляется каждое измерение (опрос датчиков)
// На входе флаг нагрева или охлаждения
// Если ЭРВ запрещена в конфигурации, то перегрев не вычисляется =0 и сразу выходим
// Если датчика давления нет, вместо температуры по давлению идёт OVERHEAT_NO_PRESS,
// и по этому признаку расчёт выбирает формулу.
// Сама формула - в OverheatCalc, здесь только сбор входа и разбор исхода.
int16_t devEEV::set_Overheat(boolean fHeating)
{
	if(!get_present()) {
		Overheat = 0;
		err = OK;
		return Overheat;
	} // ЭРВ в конфиге нет
	int16_t tPEVA = HP.sADC[PEVA].get_present() ? PressToTemp(PEVA) : OVERHEAT_NO_PRESS;
#ifdef DEMO
	Overheat = random(100,400);
#else
	// Сбор входа: наличие датчиков в сборке и в конфигурации разрешается здесь,
	// расчёт получает готовые признаки и ни одного #ifdef не видит.
	OverheatIn in;
	in.rule       = ruleToOverheatRule(_data.ruleEEV);
	in.heating    = fHeating;
	in.correction = _data.Correction;

	in.hasEvaOut = HP.sTemp[TEVAOUT].get_present();
	in.tEvaOut   = HP.sTemp[TEVAOUT].get_Temp();
#ifdef TEVAIN
	in.cfgEvaIn  = true;
	in.hasEvaIn  = HP.sTemp[TEVAIN].get_present();
	in.tEvaIn    = HP.sTemp[TEVAIN].get_Temp();
#else
	in.cfgEvaIn  = false;
	in.hasEvaIn  = false;
	in.tEvaIn    = 0;
#endif
#ifdef TCOMPIN
	in.cfgCompIn = true;
	in.hasCompIn = HP.sTemp[TCOMPIN].get_present();
	in.tCompIn   = HP.sTemp[TCOMPIN].get_Temp();
#else
	in.cfgCompIn = false;
	in.hasCompIn = false;
	in.tCompIn   = 0;
#endif
#ifdef TCONIN
	in.cfgConIn  = true;
	in.tConIn    = HP.sTemp[TCONIN].get_Temp();
#else
	in.cfgConIn  = false;
	in.tConIn    = 0;
#endif
#ifdef TCONOUT
	in.tConOut   = HP.sTemp[TCONOUT].get_Temp();
#else
	in.tConOut   = 0;
#endif
	in.tPeva     = tPEVA;

	bool overheatErr;
	Overheat = Overheat_Calc(in, Overheat, &overheatErr);
	if(overheatErr) {
		err = ERR_TYPE_OVERHEAT;
		set_Error(err, name);
	}
#endif

#ifdef TCOMPIN
	OverheatTCOMP = HP.sTemp[TCOMPIN].get_Temp() - tPEVA + _data.Correction;
#endif
	err = OK;
	return Overheat;
}

// Сброс пид регулятора
void devEEV::resetPID()
{
	// Инициализировать служебные переменные
	pidw.sum = 0;
	pidw.pre_err = 0;
	pidw.max = EEV_MAX_INT_PID * 1000*100; // максимальное воздействие интегральной составляющей на ПИД
	if(GETBIT(_data.flags, fEEV_DirectAlgorithm)) {
		pidw.max = 0;
		pidw.sum = 0;
		pidw.pre_err = _data.tOverheat - Overheat;
		pidw.pre_err2[0] = _data.tOverheatTCOMP - OverheatTCOMP;
		pidw.hyst[0] = 0;
		pidw.hyst[1] = 0;
	}
}

#define sign_dif(a,b) (a > 0 ? 1 : a < 0 ? -1 : -sign(b))

// Обновление ЭРВ - одна итерация алгоритма отслеживания
// на входе две температуры, используется для алгоритма table
int8_t devEEV::Update(void) //boolean fHeating)
{
	if(!GETBIT(_data.flags, fPresent)) return err;  // если ЭРВ нет то ничего не делаем
	if(fPause) return err;    // если пауза то выходим
	int16_t newEEV = 0;
	switch(_data.ruleEEV)     // В зависимости от правила вычисления перегрева
	{
#if defined(TEVAIN)
	case TEVAOUT_TEVAIN:
#endif
#if defined(TCOMPIN)
	case TCOMPIN_TEVAIN:
#endif
	case TEVAOUT_PEVA:
#ifdef TCOMPIN
	case TCOMPIN_PEVA:
#endif
		if(GETBIT(_data.flags, fEEV_DirectAlgorithm)) {
#if defined(TCOMPIN)
			int16_t diff = _data.tOverheat - Overheat;
			pidw.trend[trOH_default] += signm(pidw.pre_err - diff, _data.trend_mul_threshold); //sign_dif(pidw.pre_err - diff, pidw.trend[trOH_default]); // +1 - растет, -1 - падает
			pidw.pre_err = diff;
			if(pidw.trend[trOH_default] > _data.trend_threshold * 2) {
				pidw.trend[trOH_default] = _data.trend_threshold * 2;
			} else if(pidw.trend[trOH_default] < -_data.trend_threshold * 2) {
				pidw.trend[trOH_default] = -_data.trend_threshold * 2;
			}
			diff = _data.tOverheatTCOMP - OverheatTCOMP;
			int8_t fast = signm(pidw.pre_err2[0] - diff, _data.trend_mul_threshold); // 0, +-1, +-2
			pidw.trend[trOH_TCOMP] += fast;
			if(pidw.trend[trOH_TCOMP] > _data.trend_threshold * 2) {
				pidw.trend[trOH_TCOMP] = _data.trend_threshold * 2;
				if(fast) fast--; // = 0, 1
			} else if(pidw.trend[trOH_TCOMP] < -_data.trend_threshold * 2) {
				pidw.trend[trOH_TCOMP] = -_data.trend_threshold * 2;
				if(fast) fast++; // = -1, 0
			} else fast = 0;
			if(DBG_ON(DBG_EEV)) {
				journal.jprintf("EEV: %d/%d,", Overheat, pidw.trend[trOH_default]);
				journal.jprintf("%d/%d", OverheatTCOMP, pidw.trend[trOH_TCOMP]);
				if(fast) journal.jprintf(",fast:%d", fast);
			}
			if(pidw.max) {
				if(DBG_ON(DBG_EEV)) journal.jprintf(",skip:%d\n", (int)(pidw.max));
				pidw.max--;
			} else {	// Основной перегрев
				if(pidw.pre_err < -_data.pid2_delta) { // Перегрев больше, проверка порога - открыть ЭРВ
					if(pidw.trend[trOH_default] >= _data.trend_threshold) {
						if(DBG_ON(DBG_EEV)) journal.jprintf(";#1");
						newEEV = pidw.pre_err * _data.pid.Kp / (100*1000);
						pidw.max = 1;
						pidw.trend[trOH_default] = 0;
						pidw.trend[trOH_TCOMP] = 0;
					} else {
						if(pidw.trend[trOH_default] > 0) {
							if(DBG_ON(DBG_EEV)) journal.jprintf(";#2");
							newEEV = 1;
							pidw.max = 1;
						}
						goto xSecond;
					}
				} else if(pidw.pre_err > _data.pid2_delta) { // Перегрев меньше, проверка порога - закрыть ЭРВ
					if(pidw.trend[trOH_default] <= -_data.trend_threshold) {
						if(DBG_ON(DBG_EEV)) journal.jprintf(";#3");
						newEEV = pidw.pre_err * _data.pid.Kp / (100*1000);
						pidw.max = 1;
						pidw.trend[trOH_default] = 0;
						pidw.trend[trOH_TCOMP] = 0;
					} else if(pidw.trend[trOH_default] > 0) {
						if(DBG_ON(DBG_EEV)) journal.jprintf(";#4");
						newEEV = -1;
						pidw.max = 1;
					} else goto xSecond;
				} else {	// Перегрев 2
xSecond:			if(diff < -_data.tOverheatTCOMP_delta) { // Перегрев больше, проверка порога - открыть ЭРВ
						if(pidw.trend[trOH_TCOMP] >= _data.trend_threshold) {
							if(DBG_ON(DBG_EEV)) journal.jprintf(";#5");
							newEEV = 1;
							pidw.trend[trOH_TCOMP] = 0;
						}
					} else if(diff > _data.tOverheatTCOMP_delta) {
						if(OverheatTCOMP < _data.tOverheat2_low) {
							if(OverheatTCOMP < EEV_OVERHEAT2_CRITICAL && pidw.trend[trOH_TCOMP] <= 0) {
								if(DBG_ON(DBG_EEV)) journal.jprintf(";#6");
								newEEV = diff - pidw.pre_err2[0];
								if(newEEV >= 0) { //newEEV > _data.tOverheat2_low_hyst) {
									if(DBG_ON(DBG_EEV)) journal.jprintf(";#7");
									newEEV = diff * _data.pid.Kp / (100*1000);
									pidw.max = 1;
									pidw.trend[trOH_default] = 0;
								    pidw.trend[trOH_TCOMP] = 0;
								} else {
									if(DBG_ON(DBG_EEV)) journal.jprintf(";#9");
									newEEV = -2;
								}
							} else if(pidw.trend[trOH_TCOMP] < 0) {
						    	if(pidw.trend[trOH_TCOMP] < -_data.trend_threshold) newEEV = 2;
						    	else if(diff - pidw.pre_err2[0] > _data.tOverheat2_low_hyst) newEEV = 4;
						    	else goto xSecond_sub_1;
								if(DBG_ON(DBG_EEV)) journal.jprintf(";N%d", newEEV);
								newEEV = diff * _data.pid.Kp / (100*1000) / newEEV - 1;
								pidw.max = 1;
								pidw.trend[trOH_default] = 0;
							    pidw.trend[trOH_TCOMP] = 0;
							} else if(diff - pidw.pre_err2[0] > -_data.tOverheat2_low_hyst) {
								if(DBG_ON(DBG_EEV)) journal.jprintf(";#A");
								newEEV = -1;
							}
						} else if(pidw.trend[trOH_TCOMP] <= -_data.trend_threshold) {
xSecond_sub_1:				if(pidw.hyst[0] > 0) {
								if(DBG_ON(DBG_EEV)) journal.jprintf(";#B");
								newEEV = -1;
								pidw.trend[trOH_TCOMP] = 0;
							}
						}
					} else if(fast) {
						if(DBG_ON(DBG_EEV)) journal.jprintf(";#F");
						newEEV = fast;
					}
				}
				if(DBG_ON(DBG_EEV)) journal.jprintf("=%d\n", newEEV);
			}
			pidw.pre_err2[0] = diff;
			if(newEEV > _data.pid_max) newEEV = _data.pid_max;
			else if(newEEV < -_data.pid_max) newEEV = -_data.pid_max;
			if(newEEV) pidw.hyst[0] = 0; else if(++pidw.hyst[0] == 0) pidw.hyst[0]--;
			newEEV += EEV;
#endif
		} else {
			newEEV = _data.tOverheat - Overheat;   // Расчет ошибки для пида
            pidw.Kp_dmin=_data.pid2_delta; // передать параметр - уменьшение пропорциональной при определенной ошибке
			newEEV = updatePID(newEEV, _data.pid, pidw)/100; // Рассчитать итерацию: Перевод в шаги (выход ПИДА в сотых) + округление вниз
			if ((abs(newEEV)>_data.pid_max)&&(_data.pid_max>0)) {if (newEEV>0) newEEV=EEV+_data.pid_max; else newEEV=EEV-_data.pid_max;} else newEEV+=EEV;   // Ограничение пида +  добавление предыдущего значения
			// Проверка управляющего воздействия, возможно отказ ЭРВ
		}
		break;
#if defined(TEVAIN)
	case TABLE:
		newEEV = TempToEEV((HP.sTemp[TEVAOUT].get_Temp() + HP.sTemp[TEVAIN].get_Temp()) / 2,
#if defined(TCONIN)
				(HP.sTemp[TCONOUT].get_Temp() + HP.sTemp[TCONIN].get_Temp()) / 2);
#elif defined(TCOMPIN)
				(HP.sTemp[TCONOUT].get_Temp() + HP.sTemp[TCOMPIN].get_Temp()) / 2);
#endif
		break;
#endif
	case MANUAL:
		newEEV = _data.manualStep;
		break;
	default:
		return err;
	}
	if(newEEV < _data.minSteps) {
#if !defined(DEMO) && defined(EEV_MIN_CONTROL) // Контролировать достижение минимального открытия, ошибка генерится
		if(HP.is_compressor_on()) {   // во время работы - Сообщение
			err = _data.minSteps;
			set_Error(err, (char*) name);
			return err;
		}
#endif
		newEEV = _data.minSteps;            // ограничение
	}
	if(newEEV > _data.maxSteps) {
#if !defined(DEMO) && defined(EEV_MAX_CONTROL)   // если задан контроль верхнего диапазона
		if(HP.is_compressor_on()) {   // во время работы - Сообщение
			err = ERR_MAX_EEV;
			set_Error(err, (char*) name);
			return err;
		}
#endif
		newEEV = _data.maxSteps;            // ограничение
	}
	//  Передвинуть шаговик ЭРВ в позицию (абсолютную) EEV если есть изменения
	if(newEEV != EEV) set_EEV(newEEV);
	return err;
}

void devEEV::CorrectOverheat(void)
{
#ifdef DEF_OHCor_OverHeatStart
	static uint16_t OverHeatCor_period = 0; // Только для одного ЭРВ.
	int16_t t = HP.get_temp_condensing();
	int16_t t2 = HP.get_temp_evaporating();
	OHCor_tdelta = (int32_t)_data.OHCor_TDIS_TCON + (t - 3000) * DEF_OHCor_CONDENSING_30_MUL / 1000 - (int32_t)t2 * DEF_OHCor_EVAPORATING_0_MUL / 1000
#ifdef TCOMPIN
					+ (HP.sTemp[TCOMPIN].get_Temp() - t2
#else
					+ (Overheat
#endif
					- DEF_OHCor_OverHeatStart);
	if(fPause || !GETBIT(_data.flags, fCorrectOverHeat)) return;
	if(rtcSAM3X8.unixtime() - HP.get_startCompressor() > _data.OHCor_Delay && ++OverHeatCor_period >= _data.OHCor_Period) {
		OverHeatCor_period = 0;
		t = (HP.sTemp[TCOMP].get_Temp() - t) - OHCor_tdelta;
		if(t > _data.OHCor_TDIS_TCON_Thr) { // Разница большая - уменьшаем перегрев. o = omin + d_curr * (ost - omin) / d_max
			if(OHCor_tdelta_prev <= OHCor_tdelta) { // дельта не изменилась или растет
				t2 = (int32_t) OHCor_tdelta + OHCor_tdelta * _data.OHCor_TDIS_TCON_MAX / 100;
				if(t >= t2) {
					_data.tOverheat = _data.OverHeatStart;
				} else {
					_data.tOverheat = (int32_t) _data.OverheatMin + t * (_data.OverHeatStart - _data.OverheatMin) / t2;
				}
			}
		} else if(t < -_data.OHCor_TDIS_TCON_Thr) { // Разница маленькая - увеличиваем перегрев
			if(_data.tOverheat >= _data.OverHeatStart) _data.tOverheat = _data.OverheatMax;
			else _data.tOverheat = _data.OverHeatStart;
			if(DBG_ON(DBG_EEV)) { journal.jprintf("OHCor: delta too low: %.2f, set ОН: %.2f\n", (float)OHCor_tdelta / 100, (float)_data.tOverheat / 100.0); }
		} else {
			_data.tOverheat = _data.OverHeatStart;
		}
		OHCor_tdelta_prev = OHCor_tdelta;
	}
#endif
}

void devEEV::CorrectOverheatInit(void)
{
    if(GETBIT(_data.flags, fCorrectOverHeat)) {    // Установка начального перегрева
	    _data.tOverheat = _data.OverHeatStart;
    }
	OHCor_tdelta = 0;
}

 // Получить параметр ЭРВ в виде строки
 // var - строка с параметром ret-выходная строка, ответ ДОБАВЛЯЕТСЯ
void devEEV::get_paramEEV(char *var, char *ret)
{
	if(strcmp(var, eev_POS)==0) {
		_itoa(EEV,ret);
	} else if(strcmp(var, eev_POSp)==0){
		ret = dptoa(ret + m_strlen(ret), get_EEV() > 0 ? calc_percent(get_EEV()) : 0, 2);
		*(ret - 1) = '\0';
	} else if(strcmp(var, eev_POSpp)==0){
		_itoa(EEV,ret);
		strcat(ret," (");
		ret = dptoa(ret + m_strlen(ret), get_EEV() > 0 ? calc_percent(get_EEV()) : 0, 2);
		strcpy(ret - 1, "%)");
		if(stepperEEV.isBusy()) strcat(ret, "⇔");  // признак движения
	} else if(strcmp(var, eev_OVERHEAT)==0){
		_dtoa(ret, Overheat, 2);
	} else if(strcmp(var, eev_ERROR)==0){  _itoa(err,ret);
	} else if(strcmp(var, eev_MIN)==0){    _itoa(_data.minSteps,ret);
	} else if(strcmp(var, eev_MAX)==0){	   _itoa(_data.maxSteps,ret);
	} else if(strcmp(var, eev_TIME)==0){   _itoa(_data.pid_time,ret);
	} else if(strcmp(var, eev_TARGET)==0){ _dtoa(ret, _data.tOverheat, 2);
	} else if(strcmp(var, eev_tOverheatTCOMP)==0){ _dtoa(ret, _data.tOverheatTCOMP, 2);
	} else if(strcmp(var, eev_tOverheatTCOMP_delta)==0){ _dtoa(ret, _data.tOverheatTCOMP_delta, 2);
	} else if(strcmp(var, eev_PID2_delta)==0){ _dtoa(ret, _data.pid2_delta, 2);
	} else if(strcmp(var, eev_KP)==0){     _dtoa(ret, -_data.pid.Kp, 3);
	} else if(strcmp(var, eev_KP2)==0){    _dtoa(ret, -_data.pid2.Kp, 3);
	} else if(strcmp(var, eev_KI2)==0){	   _dtoa(ret, -_data.pid2.Ki / _data.pid_time, 3);
	} else if(strcmp(var, eev_KD2)==0){	   _dtoa(ret, -_data.pid2.Kd * _data.pid_time, 3);
	} else if(strcmp(var, eev_KI)==0){	   _dtoa(ret, -_data.pid.Ki,3);
	} else if(strcmp(var, eev_KD)==0){	   _dtoa(ret,-_data.pid.Kd,3);
	} else if(strcmp(var, eev_ERR_KP)==0){ 	_dtoa(ret, _data.pid2_delta, 2);
	} else if(strcmp(var, eev_CONST)==0){  _dtoa(ret, _data.Correction,2);
	} else if(strcmp(var, eev_FREON)==0){
		strcat(ret, noteFreon[FREON]);
	}   else if(strcmp(var, eev_RULE)==0){
		web_fill_tag_select(ret, noteRuleEEV, get_ruleEEV());
	} else if(strcmp(var, eev_NAME)==0){    strcat(ret,name);
	} else if(strcmp(var, eev_NOTE)==0){    strcat(ret,note);
	} else if(strcmp(var, eev_REMARK)==0){  strcat(ret,noteRemarkEEV[get_ruleEEV()]);
	} else if(strcmp(var, eev_PINS)==0){
		strcat(ret,"D");  _itoa(PIN_EEV1_D24,ret);
		strcat(ret," D"); _itoa(PIN_EEV2_D25,ret);
		strcat(ret," D"); _itoa(PIN_EEV3_D26,ret);
		strcat(ret," D"); _itoa(PIN_EEV4_D27,ret);
	} else if(strcmp(var, eev_cCORRECT)==0){_itoa((_data.flags & (1<<fCorrectOverHeat)) != 0, ret);
	} else if(strcmp(var, eev_cDELAY)==0){ 	_itoa(_data.OHCor_Delay, ret);
	} else if(strcmp(var, eev_cPERIOD)==0){	_itoa(_data.OHCor_Period, ret);
	} else if(strcmp(var, eev_cDELTA)==0){ 	_dtoa(ret, _data.OHCor_TDIS_TCON, 2);
	} else if(strcmp(var, eev_cOH_MIN)==0){	_dtoa(ret, _data.OverheatMin, 2);
	} else if(strcmp(var, eev_cOH_MAX)==0){	_dtoa(ret, _data.OverheatMax, 2);
	} else if(strcmp(var, eev_cOH_START)==0){_dtoa(ret, _data.OverHeatStart, 2);
	} else if(strcmp(var, eev_cOH_TDELTA)==0){if(OHCor_tdelta) _dtoa(ret, OHCor_tdelta, 2); else strcat(ret, "-");
	} else if(strcmp(var, eev_cOH_START)==0){_dtoa(ret, _data.OverHeatStart/100, 2);
    } else if(strcmp(var, eev_cDELTA_Thr)==0){	_dtoa(ret, _data.OHCor_TDIS_TCON_Thr, 2);
    } else if(strcmp(var, eev_cOH_cDELTA_MAX)==0){	_itoa(_data.OHCor_TDIS_TCON_MAX, ret);
    } else if(strcmp(var, eev_PID_MAX)==0){	_itoa(_data.pid_max, ret); // ограничение ПИД в шагах ЭРВ
	} else if(strcmp(var, eev_SPEED)==0){  	_itoa(_data.speedEEV, ret);
	} else if(strcmp(var, eev_CLOSE)==0){           _itoa((_data.flags & (1<<fEevClose))!=0, ret);
	} else if(strcmp(var, eev_LIGHT_START)==0){    	_itoa((_data.flags & (1<<fLightStart))!=0, ret);
	} else if(strcmp(var, eev_START )==0){          _itoa((_data.flags & (1<<fStartFlagPos))!=0, ret);
#ifdef EEV_PREFER_PERCENT
	} else if(strcmp(var, eev_MANUAL)==0){ 			_dtoa(ret, calc_percent(_data.manualStep), 2);
	} else if(strcmp(var, eev_PRE_START_POS)==0){	_dtoa(ret, calc_percent(_data.preStartPos), 2);
	} else if(strcmp(var, eev_START_POS)==0){    	_dtoa(ret, calc_percent(_data.StartPos), 2);
	} else if(strcmp(var, eev_PosAtHighTemp)==0){ 	_dtoa(ret, calc_percent(_data.PosAtHighTemp), 2);
#else
	} else if(strcmp(var, eev_MANUAL)==0){ 			_itoa(_data.manualStep, ret);
	} else if(strcmp(var, eev_PRE_START_POS)==0){	_itoa(_data.preStartPos, ret);
	} else if(strcmp(var, eev_START_POS)==0){    	_itoa(_data.StartPos, ret);
	} else if(strcmp(var, eev_PosAtHighTemp)==0){ 	_itoa(_data.PosAtHighTemp, ret);
#endif
	} else if(strcmp(var, eev_DELAY_ON_PID)==0){  	_itoa(_data.delayOnPid, ret);
	} else if(strcmp(var, eev_DELAY_START_POS)==0){	_itoa(_data.DelayStartPos, ret);
	} else if(strcmp(var, eev_DELAY_OFF)==0){	  	_itoa(_data.delayOff, ret);
	} else if(strcmp(var, eev_DELAY_ON)==0){	   	_itoa(_data.delayOn, ret);
	} else if(strcmp(var, eev_HOLD_MOTOR)==0){   	_itoa((_data.flags & (1<<fHoldMotor))!=0, ret);
	} else if(strcmp(var, eev_PRESENT)==0){         _itoa((_data.flags & (1<<fPresent))!=0, ret);
	} else if(strcmp(var, eev_SEEK_ZERO)==0){   	_itoa((_data.flags & (1<<fOneSeekZero))!=0, ret);
	} else if(strcmp(var, eev_PID_P_ON_M )==0){     _itoa((_data.flags & (1<<fPID_PropOnMeasure))!=0, ret);
	} else if(strcmp(var, eev_fEEVStartPosByTemp)==0){ _itoa((_data.flags & (1<<fEEV_StartPosByTemp))!=0, ret);
	} else if(strcmp(var, eev_fEEV_DirectAlgorithm)==0){ _itoa((_data.flags & (1<<fEEV_DirectAlgorithm))!=0, ret);
	} else if(strcmp(var, eev_trend_threshold)==0){	_itoa(_data.trend_threshold, ret);
	} else if(strcmp(var, eev_trend_mul_threshold)==0){	_dtoa(ret, _data.trend_mul_threshold, 2);
	} else if(strcmp(var, eev_tOverheat2_low)==0){	_dtoa(ret, _data.tOverheat2_low, 2);
	} else if(strcmp(var, eev_tOverheat2_low_hyst)==0){	_dtoa(ret, _data.tOverheat2_low_hyst, 2);
	} else strcat(ret,"E10");
}

void devEEV::get_ruleEEVtext(char *strReturn)
{
	switch((int)HP.dEEV.get_ruleEEV()) {
	case TEVAOUT_PEVA:
		strcat(strReturn, "TEVAOUT-PEVA");
		break;
#ifdef TCOMPIN
	case TCOMPIN_PEVA:
		strcat(strReturn, "TCOMPIN-PEVA");
		break;
#endif
#ifdef TEVAIN
	case TEVAOUT_TEVAIN:
		strcat(strReturn, "TEVAOUT-TEVAIN");
		break;
	case TCOMPIN_TEVAIN:
		strcat(strReturn, "TCOMPIN-TEVAIN");
		break;
	case TABLE:
		strcat(strReturn, "TABLE");
		break;
#endif
	case MANUAL:
		strcat(strReturn, "MANUAL");
		break;
	default:
		strcat(strReturn, "*ERROR*");
		break;
	}
}


// ---------------------------------------------------------------------------
// Реестр параметров веб-API: ЭРВ (eev.*). Ключ - прежняя строка eev_*: eev.trg, eev.kp ...
// Простые поля описаны смещениями в type_settingsEEV, остальное (положение, шаги с динамическими границами,
// коэффициенты ПИД с обратным знаком, текст) - через get/set. Прежние ключи ПИД2 (KP2, KI2, KD2, ERR_KP) не
// переносятся: консервативного ПИДа в алгоритме нет, поля структуры остались только ради формата EEPROM.
// ---------------------------------------------------------------------------
static_assert(sizeof(RULE_EEV) == 1, "правило ЭРВ хранится одним байтом");
#define EO(f) offsetof(type_settingsEEV, f)
#define ES(nm, fl, kind, sc, hk, lo, hi) { nm, 0, (float)(lo), (float)(hi), 0, sc, PT_CALL, fl, kind, hk, 0 }
enum { E_POS = 1, E_POSP, E_POSPP, E_OH, E_ERR, E_MIN, E_MAX, E_TIME, E_KP, E_KI, E_KD, E_FREON, E_NAME, E_NOTE, E_REMARK, E_PINS,
       E_CTDELTA, E_MANUAL, E_PSP, E_SP, E_PHT, E_SPEED, E_RULE, E_POM, E_DIR };
static const char *const apiLabRuleEEV[] = { "TEVAOUT-T[PEVA]",
#ifdef TCOMPIN
	"TCOMPIN-T[PEVA]",
	#ifdef TEVAIN
	"TEVAOUT-TEVAIN", "TCOMPIN-TEVAIN", "TABLE[EVA,CON]",
	#endif
#endif
	"MANUAL", 0 };
#ifdef EEV_PREFER_PERCENT
	#define EPOS(nm, hk) ES(nm, 0, MK_NUM, 100, hk, 0, 100)       // положение в процентах от хода
#else
	#define EPOS(nm, hk) ES(nm, 0, MK_INT, 1, hk, 0, 8192)        // положение в шагах
#endif
static const ApiParam apiEevTab[] = {
	ES(eev_POS,    0,     MK_INT, 1,   E_POS,   0, 8192),
	ES(eev_POSp,   0,     MK_NUM, 100, E_POSP,  0, 100),
	ES(eev_POSpp,  PF_RO, MK_TEXT, 1,  E_POSPP, 0, 0),                // «шаги (проценты%)» и ⇔ при движении
	ES(eev_OVERHEAT, PF_RO, MK_NUM, 100, E_OH,  0, 0),
	ES(eev_ERROR,  PF_RO, MK_INT, 1,   E_ERR,   0, 0),
	ES(eev_MIN,    0,     MK_INT, 1,   E_MIN,   0, 255),              // меньше максимума
	ES(eev_MAX,    0,     MK_INT, 1,   E_MAX,   0, 8192),             // не меньше минимума; смена перенастраивает драйвер
	ES(eev_TIME,   0,     MK_INT, 1,   E_TIME,  1, 1000),             // с; смена пересчитывает коэффициенты ПИД
	PN_I16(eev_TARGET,       0.01f, 50, 100, EO(tOverheat), 0),
	PN_I16(eev_tOverheatTCOMP, -327.68f, 327.67f, 100, EO(tOverheatTCOMP), 0),
	PN_I16(eev_tOverheat2_low, -327.68f, 327.67f, 100, EO(tOverheat2_low), 0),
	PN_I16(eev_tOverheat2_low_hyst, -327.68f, 327.67f, 100, EO(tOverheat2_low_hyst), 0),
	PN_I16(eev_tOverheatTCOMP_delta, -327.68f, 327.67f, 100, EO(tOverheatTCOMP_delta), 0),
	ES(eev_KP,     0,     MK_NUM, 1000, E_KP,   -32.767f, 32.767f),   // в структуре хранятся отрицательными тысячными
	ES(eev_KI,     0,     MK_NUM, 1000, E_KI,   -32.767f, 32.767f),
	ES(eev_KD,     0,     MK_NUM, 1000, E_KD,   -32.767f, 32.767f),
	PN_I16(eev_PID2_delta,   -327.68f, 327.67f, 100, EO(pid2_delta), 0),
	PN_U16(eev_PID_MAX,      0, 200, 1, EO(pid_max), 0),
	PN_I16(eev_CONST,        -10, 10, 100, EO(Correction), 0),
	EPOS(eev_MANUAL, E_MANUAL),
	{ eev_FREON, 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, E_FREON, 0 },
	{ eev_RULE, apiLabRuleEEV, 0, 0, (uint16_t)EO(ruleEEV), 1, PT_E8, 0, 0, E_RULE, 0 },
	{ eev_NAME,   0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, E_NAME, 0 },
	{ eev_NOTE,   0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, E_NOTE, 0 },
	{ eev_REMARK, 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, E_REMARK, 0 },
	{ eev_PINS,   0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, E_PINS, 0 },
	PN_BIT(eev_cCORRECT, EO(flags), fCorrectOverHeat, PT_B16, 0, 0, 0, 0),
	PN_U16(eev_cDELAY,       0, 10000, 1, EO(OHCor_Delay), 0),
	PN_U16(eev_cPERIOD,      0, 10000, 1, EO(OHCor_Period), 0),
	PN_I16(eev_cDELTA,       -10, 50, 100, EO(OHCor_TDIS_TCON), 0),
	PN_I16(eev_cOH_MIN,      0, 50, 100, EO(OverheatMin), 0),
	PN_I16(eev_cOH_MAX,      0, 50, 100, EO(OverheatMax), 0),
	PN_I16(eev_cOH_START,    0, 50, 100, EO(OverHeatStart), 0),
	ES(eev_cOH_TDELTA, PF_RO, MK_NUM, 100, E_CTDELTA, 0, 0),           // null - целевая дельта не рассчитана
	PN_I16(eev_cDELTA_Thr,   -327.68f, 327.67f, 100, EO(OHCor_TDIS_TCON_Thr), 0),
	PN_U8 (eev_cOH_cDELTA_MAX, 0, 255, 1, EO(OHCor_TDIS_TCON_MAX), 0),
	PNH_U8(eev_SPEED,        5, 120, 1, EO(speedEEV), 0, E_SPEED),      // шагов в секунду
	PN_BIT(eev_CLOSE,        EO(flags), fEevClose,       PT_B16, 0, 0, 0, 0),
	PN_BIT(eev_LIGHT_START,  EO(flags), fLightStart,     PT_B16, 0, 0, 0, 0),
	PN_BIT(eev_START,        EO(flags), fStartFlagPos,   PT_B16, 0, 0, 0, 0),
	EPOS(eev_PRE_START_POS, E_PSP),
	EPOS(eev_START_POS, E_SP),
	EPOS(eev_PosAtHighTemp, E_PHT),
	PN_U8 (eev_DELAY_ON_PID, 0, 255, 1, EO(delayOnPid), 0),
	PN_U8 (eev_DELAY_START_POS, 0, 255, 1, EO(DelayStartPos), 0),
	PN_U8 (eev_DELAY_OFF,    0, 255, 1, EO(delayOff), 0),
	PN_U8 (eev_DELAY_ON,     0, 255, 1, EO(delayOn), 0),
	PN_BIT(eev_HOLD_MOTOR,   EO(flags), fHoldMotor,      PT_B16, 0, 0, 0, 0),
	PN_BIT(eev_PRESENT,      EO(flags), fPresent,        PT_B16, 0, 0, PF_RO, 0),
	PN_BIT(eev_SEEK_ZERO,    EO(flags), fOneSeekZero,    PT_B16, 0, 0, 0, 0),
	PN_BIT(eev_PID_P_ON_M,   EO(flags), fPID_PropOnMeasure, PT_B16, 0, E_POM, 0, 0),
	PN_BIT(eev_fEEVStartPosByTemp, EO(flags), fEEV_StartPosByTemp, PT_B16, 0, 0, 0, 0),
	PN_BIT(eev_fEEV_DirectAlgorithm, EO(flags), fEEV_DirectAlgorithm, PT_B16, 0, E_DIR, 0, 0),
	PN_I8 (eev_trend_threshold, -128, 127, 1, EO(trend_threshold), 0),
	PN_U16(eev_trend_mul_threshold, 0, 655.35f, 100, EO(trend_mul_threshold), 0),
};
static void *api_base_eev(uint8_t) { return HP.dEEV.get_save_addr(); }
static type_settingsEEV *api_eev(void) { return (type_settingsEEV *) HP.dEEV.get_save_addr(); }
static int api_get_eev(uint8_t hook, uint8_t, ApiOut *o)
{
	type_settingsEEV *d = api_eev();
	char t[40]; t[0] = 0;
	switch(hook) {
	case E_POS:   api_put_int(o, HP.dEEV.get_EEV()); break;
	case E_POSP:  api_put_dec(o, HP.dEEV.get_EEV() > 0 ? HP.dEEV.calc_percent(HP.dEEV.get_EEV()) : 0, 100); break;
	case E_POSPP: {
		int16_t p = HP.dEEV.get_EEV();
		_itoa(p, t); strcat(t, " (");
		char *e = dptoa(t + strlen(t), p > 0 ? HP.dEEV.calc_percent(p) : 0, 2);
		strcpy(e - 1, "%)");
		if(HP.dEEV.stepperEEV.isBusy()) strcat(t, "⇔");           // признак движения
		api_put_str(o, t); break; }
	case E_OH:    api_put_dec(o, HP.dEEV.get_Overheat(), 100); break;
	case E_ERR:   api_put_int(o, HP.dEEV.get_lastErr()); break;
	case E_MIN:   api_put_int(o, d->minSteps); break;
	case E_MAX:   api_put_int(o, d->maxSteps); break;
	case E_TIME:  api_put_int(o, d->pid_time); break;
	case E_KP:    api_put_dec(o, -d->pid.Kp, 1000); break;
	case E_KI:    api_put_dec(o, -d->pid.Ki, 1000); break;
	case E_KD:    api_put_dec(o, -d->pid.Kd, 1000); break;
	case E_FREON: api_put_str(o, noteFreon[HP.dEEV.get_typeFreon()]); break;
	case E_NAME:  api_put_str(o, HP.dEEV.get_name()); break;
	case E_NOTE:  api_put_str(o, HP.dEEV.get_note()); break;
	case E_REMARK: api_put_str(o, noteRemarkEEV[HP.dEEV.get_ruleEEV()]); break;
	case E_PINS:
		strcpy(t, "D"); _itoa(PIN_EEV1_D24, t); strcat(t, " D"); _itoa(PIN_EEV2_D25, t);
		strcat(t, " D"); _itoa(PIN_EEV3_D26, t); strcat(t, " D"); _itoa(PIN_EEV4_D27, t);
		api_put_str(o, t); break;
	case E_CTDELTA: if(HP.dEEV.get_OHCor_tdelta()) api_put_dec(o, HP.dEEV.get_OHCor_tdelta(), 100); else api_put(o, "null"); break;
#ifdef EEV_PREFER_PERCENT
	case E_MANUAL: api_put_dec(o, HP.dEEV.calc_percent(d->manualStep), 100); break;
	case E_PSP:    api_put_dec(o, HP.dEEV.calc_percent(d->preStartPos), 100); break;
	case E_SP:     api_put_dec(o, HP.dEEV.calc_percent(d->StartPos), 100); break;
	case E_PHT:    api_put_dec(o, HP.dEEV.calc_percent(d->PosAtHighTemp), 100); break;
#else
	case E_MANUAL: api_put_int(o, d->manualStep); break;
	case E_PSP:    api_put_int(o, d->preStartPos); break;
	case E_SP:     api_put_int(o, d->StartPos); break;
	case E_PHT:    api_put_int(o, d->PosAtHighTemp); break;
#endif
	default: return AE_FAIL;
	}
	return AE_OK;
}
static int api_set_eev(uint8_t hook, uint8_t, const char *val)
{
	type_settingsEEV *d = api_eev();
	float x = my_atof(val);
	if(x == ATOF_ERROR) return AE_BAD_VALUE;
	switch(hook) {
	case E_POS:  if(x < d->minSteps || x > d->maxSteps) return AE_RANGE; HP.dEEV.set_EEV(x); return AE_OK;
	case E_POSP: x = x * d->maxSteps / 100; if(x < d->minSteps || x > d->maxSteps) return AE_RANGE; HP.dEEV.set_EEV(x); return AE_OK;
	case E_MIN:  if(x >= d->maxSteps) return AE_RANGE; d->minSteps = x; return AE_OK;
	case E_MAX: {
		int16_t xx = x;
		if(xx < d->minSteps) return AE_RANGE;
		if(xx != d->maxSteps) { d->maxSteps = xx; HP.dEEV.InitStepper(); }
		return AE_OK; }
	case E_TIME: {
		uint16_t t = x;
		if(d->pid_time != t) {
			UpdatePIDbyTime(t, d->pid_time, d->pid);
			UpdatePIDbyTime(t, d->pid_time, d->pid2);
			d->pid_time = t;
		}
		return AE_OK; }
	case E_KP: d->pid.Kp = -rd(x, 1000); return AE_OK;
	case E_KI: d->pid.Ki = -rd(x, 1000); return AE_OK;
	case E_KD: d->pid.Kd = -rd(x, 1000); return AE_OK;
#ifdef EEV_PREFER_PERCENT
	case E_MANUAL: d->manualStep = HP.dEEV.calc_pos(rd(x, 100)); if(d->ruleEEV == MANUAL) HP.dEEV.set_EEV(d->manualStep); return AE_OK;
	case E_PSP:    d->preStartPos = HP.dEEV.calc_pos(rd(x, 100)); return AE_OK;
	case E_SP:     d->StartPos = HP.dEEV.calc_pos(rd(x, 100)); return AE_OK;
	case E_PHT:    d->PosAtHighTemp = HP.dEEV.calc_pos(rd(x, 100)); return AE_OK;
#else
	case E_MANUAL: if(x < d->minSteps || x > d->maxSteps) return AE_RANGE; d->manualStep = x; if(d->ruleEEV == MANUAL) HP.dEEV.set_EEV(d->manualStep); return AE_OK;
	case E_PSP:    if(x < d->minSteps || x > d->maxSteps) return AE_RANGE; d->preStartPos = x; return AE_OK;
	case E_SP:     if(x < d->minSteps || x > d->maxSteps) return AE_RANGE; d->StartPos = x; return AE_OK;
	case E_PHT:    d->PosAtHighTemp = x; return AE_OK;
#endif
	}
	return AE_FAIL;
}
// Побочные эффекты записи обычных полей - те же, что были в ветках прежнего set_paramEEV
static void api_after_eev(uint8_t hook)
{
	switch(hook) {
	case E_SPEED: HP.dEEV.stepperEEV.setSpeed(api_eev()->speedEEV); break;
	case E_RULE: case E_POM: case E_DIR: HP.dEEV.resetPID(); break;
	}
}
static const ApiOps apiOpsEev = { api_base_eev, api_get_eev, api_set_eev, api_after_eev, 0 };
extern const ApiGroup ApiGrpEev;
const ApiGroup ApiGrpEev = { "eev", apiEevTab, sizeof(apiEevTab) / sizeof(apiEevTab[0]), &apiOpsEev };

#endif

