/**
 * file
 * brief FreeRTOS for Teensy 3.x and Due
 */
#ifndef FreeRTOS_ARM_h
#define FreeRTOS_ARM_h

// Вывод, которым errorBlink мигает при фатальном отказе: assert, нехватка
// памяти, переполнение стека, отказ ядра. Это LED1 - красный светодиод платы
// народного контроллера, PIN_LED1 в Config.h.
//
// Здесь стояло 13 (приписка "// 38" показывала замысел, но ничего не меняла).
// Вывод 13 в этой плате - PIN_DEVICE_RPUMPO, реле насоса выходного контура,
// и при отказе ядра импульсы шли на реле, а не на светодиод: инверсия реле
// отключена, значит каждый импульс включал насос. Исправлено 30.09.2026.
//
// Значение стоит ИМЕННО ЗДЕСЬ, а не в Config.h: errorBlink лежит в
// FreeRTOS_ARM.c, а библиотечная единица трансляции конфигурацию проекта не
// видит, поэтому #define со стороны проекта до неё не доходит. Совпадение с
// PIN_LED1 проверяет правило гигиены в tools/ci.sh.
#ifndef PIN_LED_ERROR
#define PIN_LED_ERROR  38
#endif

#ifndef __arm__
  #error ARM Due or Teensy 3.x required
#else  // __arm__
//------------------------------------------------------------------------------
/** FreeRTOS_ARM version YYYYMMDD */
#define FREE_RTOS_ARM_VERSION 20151113
//------------------------------------------------------------------------------
#include "utility/FreeRTOS.h"
#include "utility/task.h"
#include "utility/queue.h"
#include "utility/semphr.h"
#include "utility/portmacro.h"
//#include "utility/cmsis_os.h"

#include "mini-printf.h"

#endif  // __arm__
#endif  // FreeRTOS_ARM_h
