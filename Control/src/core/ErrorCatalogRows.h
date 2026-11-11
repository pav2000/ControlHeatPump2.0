/*
 * Copyright (c) 2016-2026 by Pavel Panfilov <firstlast2007@gmail.com>  telegram @pav20000
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

// Номер, идентификатор, группа, источник, обычная регистрация, описание.

ERROR_DEF(TEMP_MIN, -1, SENSORS, "TEMP", Alarm, "температура ниже допустимой границы")
ERROR_DEF(TEMP_MAX, -2, SENSORS, "TEMP", Alarm, "температура выше допустимой границы")
ERROR_DEF(SENSOR_MISSING, -3, SENSORS, "SENSOR", Alarm, "Нужный датчик отсутствует, не привязан или не настроен")
ERROR_DEF(TEMP_READ, -4, SENSORS, "TEMP", Alarm, "Чтение датчика не удалось после повторов")
ERROR_DEF(TEMP_BUS, -5, SENSORS, "TEMP", Alarm, "Отказ OneWire или мастера DS2482")
ERROR_DEF(PRESS_RANGE_LOW, -6, SENSORS, "PRESS", Alarm, "Показание давления ниже диапазона датчика")
ERROR_DEF(PRESS_RANGE_HIGH, -7, SENSORS, "PRESS", Alarm, "Показание давления выше диапазона датчика")
ERROR_DEF(FLOW_LOW, -8, SENSORS, "FLOW", Alarm, "Поток ниже уставки или сработал датчик отсутствия протока SEVA")
ERROR_DEF(DINPUT_ALARM, -9, SENSORS, "INPUT", Alarm, "Сработал аварийный дискретный вход")

ERROR_DEF(EEV_LIMIT, -10, CONTROL, "EEV", Alarm, "Позиция ЭРВ вне диапазона. В деталях указывается направление")
ERROR_DEF(EEV_CONFIG, -11, CONTROL, "EEV", Alarm, "Выбранное правило ЭРВ требует другой конфигурации датчиков")
ERROR_DEF(EEV_DRIVER, -12, CONTROL, "EEV", Alarm, "Сработала диагностика драйвера ЭРВ")
ERROR_DEF(COMP_START_BLOCKED, -13, CONTROL, "COMP", Alarm, "Попытка пуска компрессора при активной ошибке")
ERROR_DEF(COMP_NO_PUMP, -14, CONTROL, "COMP", Alarm, "Попытка пуска компрессора при выключенном насосе")
ERROR_DEF(HARD_STATE, -15, CONTROL, "SYS", Alarm, "Оборудование находится в недопустимом состоянии")
ERROR_DEF(TEMP_DELTA_CON, -16, CONTROL, "TEMP", Alarm, "Разность температур на конденсаторе выше допустимой")
ERROR_DEF(DEFROST_TIMEOUT, -17, CONTROL, "SYS", Alarm, "Разморозка не завершилась в срок")
ERROR_DEF(PID_FEED_LIMIT, -18, CONTROL, "TEMP", Alarm, "Превышен предел температуры подачи при регулировании")
ERROR_DEF(SAFETY_INTERLOCK, -19, CONTROL, "SYS", Warning, "Правило межблокировки выявило недопустимую команду в режиме наблюдения")

ERROR_DEF(INVERTER_FAULT, -20, INVERTER, "FC", Alarm, "Инвертор сообщил собственный отказ, его код успешно прочитан")
ERROR_DEF(FC_TRIP, -21, INVERTER, "FC", Alarm, "Привод подтвердил аварийное отключение. Собственный код отказа недоступен")
ERROR_DEF(FC_NO_LINK, -22, INVERTER, "FC", Alarm, "Подтверждена потеря связи с ранее доступным инвертором")
ERROR_DEF(FC_INIT, -23, INVERTER, "FC", Alarm, "Инвертор не прошёл начальную проверку связи или контрольного обмена")
ERROR_DEF(FC_STATE, -24, INVERTER, "FC", Alarm, "Привод вернул недопустимое рабочее состояние или показание")
ERROR_DEF(FC_BLOCK, -25, INVERTER, "FC", Alarm, "Пуск запрещён из-за блокировки инвертора")
ERROR_DEF(FC_RESET, -26, INVERTER, "FC", Alarm, "Привод не подтвердил снятие отказа при исправном обмене")
ERROR_DEF(FC_CONFIG, -27, INVERTER, "FC", Alarm, "Некорректно настроен способ управления приводом")
ERROR_DEF(FC_SETPOINT, -28, INVERTER, "FC", Alarm, "Прочитанная уставка привода не совпала с записанной")
ERROR_DEF(COMP_NO_START, -29, INVERTER, "COMP", Alarm, "После команды и штатного времени разгона пуск не подтверждён")
ERROR_DEF(COMP_NO_STOP, -30, INVERTER, "COMP", Alarm, "Вращение сохраняется после штатной последовательности останова")

ERROR_DEF(MODBUS_EXCEPTION, -31, MODBUS_POWER, "MODBUS", ByDevice, "В корректном ответе устройство вернуло байт исключения")
ERROR_DEF(MODBUS_EXCHANGE, -32, MODBUS_POWER, "MODBUS", ByDevice, "Отдельная операция обмена окончательно не выполнена из-за транспортного сбоя")
ERROR_DEF(MODBUS_UNAVAILABLE, -33, MODBUS_POWER, "MODBUS", ByDevice, "Требуемый интерфейс отсутствует или не инициализирован")
ERROR_DEF(MODBUS_BUSY, -34, MODBUS_POWER, "MODBUS", ByOperation, "Длительное или повторяющееся ожидание локального замка мешает обмену")
ERROR_DEF(MODBUS_UNKNOWN, -35, MODBUS_POWER, "MODBUS", ByDevice, "Не удалось классифицировать внутренний статус драйвера")
ERROR_DEF(POWER_VOLTAGE_HIGH, -36, MODBUS_POWER, "POWER", Alarm, "Напряжение выше допустимого")
ERROR_DEF(VOLTAGE_LOW, -37, MODBUS_POWER, "POWER", Warning, "Напряжение SDM ниже настроенного предела")
ERROR_DEF(POWER_HIGH, -38, MODBUS_POWER, "POWER", Alarm, "Потребляемая мощность выше допустимой")

ERROR_DEF(CONFIG, -39, SETTINGS, "CFG", Alarm, "Некорректная конфигурация. В деталях указывается устройство или параметр")
ERROR_DEF(CONFIG_NO_WORK, -40, SETTINGS, "CFG", Alarm, "При запуске не включён ни один рабочий режим")
ERROR_DEF(CONFIG_COMPRESSOR, -41, SETTINGS, "CFG", Alarm, "Компрессор отсутствует в конфигурации")
ERROR_DEF(CONFIG_PUMP, -42, SETTINGS, "CFG", Alarm, "Требуемый насос отсутствует в конфигурации")
ERROR_DEF(SETTINGS_WRITE, -43, SETTINGS, "I2C", Warning, "Ошибка записи или контрольного чтения настроек после записи")
ERROR_DEF(SETTINGS_READ, -44, SETTINGS, "I2C", RequiredAtBoot, "Не удалось прочитать настройки")
ERROR_DEF(SETTINGS_DATA, -45, SETTINGS, "CFG", RequiredAtBoot, "Некорректный заголовок, версия, длина, структура или CRC настроек")
ERROR_DEF(SETTINGS_PARTS, -46, SETTINGS, "CFG", RequiredAtBoot, "Не удалось загрузить расписание или профиль вместе с настройками")
ERROR_DEF(PROFILE_WRITE, -47, SETTINGS, "I2C", Warning, "Не удалось сохранить профиль")
ERROR_DEF(PROFILE_READ, -48, SETTINGS, "I2C", RequiredAtBoot, "Не удалось прочитать профиль")
ERROR_DEF(PROFILE_DATA, -49, SETTINGS, "CFG", RequiredAtBoot, "Некорректная длина или CRC профиля")

ERROR_DEF(MOTO_HOUR, -50, STORAGE_SYSTEM, "I2C", Warning, "Ошибка чтения или записи моточасов")
ERROR_DEF(I2C_MEMORY, -51, STORAGE_SYSTEM, "I2C", Alarm, "Тип микросхемы памяти отличается от ожидаемого")
ERROR_DEF(JOURNAL, -52, STORAGE_SYSTEM, "I2C", Warning, "Не удалось открыть, прочитать или записать журнал")
ERROR_DEF(SD, -53, STORAGE_SYSTEM, "SD", Warning, "Ошибка инициализации, чтения или записи SD-карты")
ERROR_DEF(MEMORY, -54, STORAGE_SYSTEM, "SYS", Alarm, "Недостаточно памяти для данных")
ERROR_DEF(RTOS, -55, STORAGE_SYSTEM, "SYS", Alarm, "Не удалось создать обязательную задачу или ресурс FreeRTOS")
ERROR_DEF(RESET_LOOP, -56, STORAGE_SYSTEM, "SYS", Alarm, "Автопуск отменён после серии аварийных перезагрузок")

ERROR_DEF(I2C_BUSY, -57, INFRASTRUCTURE, "I2C", Warning, "Длительная или повторяющаяся занятость мешает обмену I2C")
ERROR_DEF(NET, -58, INFRASTRUCTURE, "NET", Warning, "Сработал действующий порог повторных восстановлений W5500")

ERROR_DEF(PRESS_PROT_LOW, -59, STAGE_TWO, "PRESS", Alarm, "Рабочее давление на стороне низкого давления устойчиво ниже порога защиты")
ERROR_DEF(PRESS_PROT_HIGH, -60, STAGE_TWO, "PRESS", Alarm, "Рабочее давление на стороне высокого давления достигло порога защиты")
ERROR_DEF(EEV_SUPERHEAT_LOW, -61, STAGE_TWO, "EEV", Alarm, "Перегрев хладагента устойчиво ниже аварийного предела")
ERROR_DEF(EEV_SUPERHEAT_HIGH, -62, STAGE_TWO, "EEV", Warning, "Перегрев хладагента устойчиво выше допустимого")
ERROR_DEF(BOILER_OVERHEAT, -63, STAGE_TWO, "TEMP", Alarm, "Температура бойлера достигла отдельного порога защиты ГВС")
ERROR_DEF(MQTT, -64, STAGE_TWO, "NET", Warning, "Подтверждён устойчивый отказ отправки MQTT")
ERROR_DEF(NOTIFY, -65, STAGE_TWO, "NET", Warning, "Подтверждён отказ отправки по каналу почты или SMS")
ERROR_DEF(TIME, -66, STAGE_TWO, "TIME", Warning, "Контроллер не располагает достоверным временем")
ERROR_DEF(HEAP_LOW, -67, STAGE_TWO, "SYS", Warning, "Запас доступной памяти устойчиво ниже порога")
ERROR_DEF(IO_BUS, -68, STAGE_TWO, "SYS", Warning, "Подтверждена серия сбоев SPI/DMA")

ERROR_DEF(ABNORMAL_RESET, -69, STORAGE_SYSTEM, "SYS", LogOnly, "Аварийная перезагрузка контроллера")
