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
 * Драйвер шагового двигателя ЭРВ.
 *
 * Отдельная единица трансляции (StepMotor.cpp). О проекте ничего не знает:
 * ни HP, ни journal, ни Config.h. Выбор последовательности фаз и инверсия
 * драйвера приходят параметрами из главной единицы трансляции, потому что
 * задаются в Config.h, которого модуль не видит, - #ifdef внутри модуля
 * молча выбрал бы не ту ветку (так уже ломались Freon, DateTime и stepOne).
 *
 * Сами последовательности фаз - в StepMotorPhase.cpp, проверяются тестами.
 */
#ifndef StepMotor_h
#define StepMotor_h

// Заголовок самодостаточен: включает ядро Arduino и FreeRTOS напрямую.
// Оба включаются во вторую единицу трансляции без дублей символов.
#include "Arduino.h"
#include "FreeRTOS_ARM.h"
#include "GpioDirect.h"
#include "StepMotorPhase.h"

// library interface description
class StepMotor {
  public:
   // phase_variant - PHASE_4/PHASE_8/PHASE_8s, invert - инверсия выходов
   // (драйвер L9333). Оба приходят из Config.h через вызывающего.
   void  initStepMotor(int number_of_steps, int motor_pin_1, int motor_pin_2,int motor_pin_3, int motor_pin_4,
                       uint8_t phase_variant, bool invert);                                                     // Инициализация
   void setSpeed(long whatSpeed);                                                                               // Установка скорости шагов в секунду
   bool step(int number_of_steps);                                                                              // Движение; false - очередь команд полна
   inline boolean isBusy()  {return busy;}                                                                      // True если мотор шагает
   inline void offBusy()    {busy=false;}                                                                       // Установка признака - мотор остановлен                                                                      
   void off();                                                                                                  // выключить двигатель
   TaskHandle_t xHandleStepperEEV;                                                                              // Заголовок задачи шаговый двигатель двигается 
   QueueHandle_t xCommandQueue;                                                                                 // очередь комманд на управление двигателем
  private:
    void stepOne(int this_step);
    inline void setPinMotor(int pin, boolean val);
    inline void applyMask(uint8_t mask);   // выставить все четыре обмотки разом
    boolean busy;
    uint8_t phase_variant;                 // PHASE_4/PHASE_8/PHASE_8s, задан в initStepMotor
    bool    invert;                        // инверсия выходов для драйвера L9333
    unsigned long step_delay;              // delay between steps, in ms, based on speed
    int number_of_steps;                   // total number of steps this motor can take
    int pin_count;                         // how many pins are in use.
    // motor pin numbers:
    int motor_pin_1;
    int motor_pin_2;
    int motor_pin_3;
    int motor_pin_4;
    friend void vUpdateStepperEEV(void *);
};
#endif
