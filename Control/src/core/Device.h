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
 * Общее для всех датчиков и устройств.
 *
 * ЗАЧЕМ ОТДЕЛЬНЫЙ ЗАГОЛОВОК. Hardware.h включает SensorTemp.h: если этот блок лежит в
 * Hardware.h, получается петля - датчикам нужны флаги из Hardware.h, а Hardware.h
 * нужен класс датчика. Здесь лежит ровно то, что нужно обеим сторонам и не тянет
 * за собой ничего.
 */
#ifndef Device_h
#define Device_h

#include "../../Constant.h"
#include <rtc_clock.h>

// Флаги датчиков (единые для всех датчиков!!!!!)
#define fPresent      0               // флаг наличие датчика
#define fTest         1               // флаг режим теста
#define fFull         2               // флаг полного буфера для усреднения
#define fAddress      3               // флаг правильного адреса для температурного датчика
#define fcheckRange	  4				  // флаг Проверка граничного значения
#define fsensModbus	  5				  // флаг дистанционного датчика по Modbus
#define fWarnNoAddr	  6				  // флаг: о подмене непривязанного датчика тестовым значением в журнал уже написано (только во время работы, не сохраняется)

extern RTC_clock rtcSAM3X8;
extern void set_Error(int8_t err, char *nam);

/*
 * Общая часть датчиков и исполнительных устройств.
 *
 * Описание, имя, режим теста и байт флагов переобъявлял каждый класс, вместе
 * с одними и теми же аксессорами. Здесь они объявлены один раз, и флаги
 * fPresent/fTest перестают быть соглашением - становятся контрактом.
 *
 * Виртуальных функций тут нет намеренно. Полиморфизм этим классам не нужен,
 * а таблица виртуальных методов стоила бы по 4 байта оперативной памяти на
 * каждый объект массива датчиков - это чистый расход без применения.
 *
 * Класс НЕ содержит ничего, что попадает в EEPROM. Каждый производный класс
 * отдаёт на сохранение свою упакованную группу полей, и её
 * раскладка не меняется - формат настроек остаётся прежним, VER_SAVE не растёт.
 */
class Device
{
  public:
    char*   get_note(){ return note; }                         // Описание объекта
    char*   get_name(){ return name; }                         // Имя объекта, из него строятся топики MQTT
    inline boolean get_present(){ return GETBIT(flags, fPresent); } // Наличие в текущей конфигурации
    inline boolean get_flag(uint8_t bit){ return GETBIT(flags, bit); } // Произвольный бит флагов
    TEST_MODE get_testMode(){ return testMode; }               // Получить текущий режим работы
    void    set_testMode(TEST_MODE t){ testMode = t; }         // Установить текущий режим работы

  protected:
    // Порядок полей подобран по расходу памяти: указатели идут первыми, байтовые
    // поля последними. Иначе выравнивание вставляет дыру после flags, и она
    // умножается на число объектов в массивах датчиков.
    char    *note;                                             // Описание
    char    *name;                                             // Имя
    TEST_MODE testMode;                                        // Значение режима тестирования
    byte    flags;                                             // Флаги объекта, биты f* выше
};

/*
 * Датчик - устройство, которое умеет отказывать. Реле сюда не попадает:
 * у него нет кода ошибки, и заводить его "для симметрии" значило бы тратить
 * байт на объект ради поля, которое никто не читает.
 */
class Sensor : public Device
{
  public:
    int8_t  get_lastErr(){ return err; }                       // Получить последнюю ошибку

  protected:
    int8_t  err;                                               // Ошибка датчика, при ошибке останов ТН
};

#endif
