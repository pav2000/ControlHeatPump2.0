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
#ifndef __CONTROL_TYPES_H__
#define __CONTROL_TYPES_H__

#include <stdint.h>

/*
 * Типы, которыми разговаривает алгоритм управления: что делать компрессору,
 * по какому правилу регулировать и чем настраивается ПИД.
 *
 * Лежат отдельным листом, чтобы ими могли пользоваться
 * проверяемые на ПК модули. Сам Constant.h для этого не годится: он тянет
 * Config.h, а тот - Ethernet и весь слой железа.
 */

//  Перечисляемый тип - действия над компрессором
enum MODE_COMP          
{
  pCOMP_OFF,                     // компрессор был выключен
  pCOMP_ON,                      // компрессор был включен
  pCOMP_NONE,                    // компрессор ничего не делаем - не меняем статус
  pEND9                          // Обязательно должен быть последним, добавляем ПЕРЕД!!!
};

//  Перечисляемый тип - алгоритмы отопления
enum RULE_HP       
{
    pHYSTERESIS,      // алгоритм гистерезис, интервальный режим
    pPID,             // алгоритм использование ПИД регулятора
    pHYBRID,          // алгоритм смешанный алгоритм, предложил  Ljutik
    pEND1             // Обязательно должен быть последним, добавляем ПЕРЕД!!!
};

struct PID_STRUCT {   		// Настройки ПИД регулятора
	  int16_t Kp;           // ПИД Коэф. пропорц, в тысячных (отриц)
	  int16_t Ki;           // ПИД Коэф. интегральный, в тысячных (отриц)
	  int16_t Kd;           // ПИД Коэф. дифференциальный,в тысячных (отриц)
} __attribute__((packed));


#define trOH_default	2
#define trOH_TCOMP		3

struct PID_WORK_STRUCT {	// Переменные ПИД регулятора
	union {
		int32_t sum;		// сумма
		int16_t pre_err2[2];// i=0
		int8_t  trend[4];	// i=trOH_*
	};
	int16_t pre_err;		// предыдущая ошибка для дифференцирования
	int32_t max;
	union {
		int16_t Kp_dmin;	// Разница (в сотых градуса) при которой происходит уменьшение пропорциональной составляющей ПИД ЭРВ
		uint8_t hyst[2];	// i=0..1
	};
};

#endif
