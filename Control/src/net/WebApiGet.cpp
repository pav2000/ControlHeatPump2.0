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
 * Веб-API контроллера: диспетчер /api (ver, meta, get, set, do, chart), группы параметров,
 * действия и узкий путь удалённых датчиков IP. Протокол - tools/api/spec.md, ядро разбора
 * и записи - ParamRegistry.cpp, таблицы остальных параметров лежат рядом с классами-владельцами.
 *
 * Разбора команд вида "GET /&команда&&" (parserGET) нет: из старых адресов живы только
 * три команды удалённых датчиков IP (sensorIpRequest), остальные получают 404.
 *
 * HeatPump.h тянет Constant.h, а с ним Config.h, поэтому условия
 * конфигурации разрешаются так же, как в главной единице трансляции.
 */
#include "../core/HeatPump.h"     // HeatPump, HP, устройства и датчики
#include "WebShared.h"    // Socket[], Watchdog_StatWeb, get_Counts
#include "Network.h"      // initW5200, sendBufferRTOS, socketInfo, nameWiznet
#include "../log/Statistics.h"   // Stats, имена файлов статистики
#include "../hw/Clock.h"        // get_NTP_result: итог последней синхронизации времени
#include "../util/Util.h"         // _itoa, ftoa, dptoa, byteToHex, addressToHex


// Системная информация для разработчика: «подпись|значение;...», разделы - строки <b>…</b>. Источник ключа sys.info.
static void sysInfoText(char *strReturn)
{
	strcat(strReturn,"Источник загрузкки web интерфейса |");
	switch (HP.get_SourceWeb())
	{
	case pMIN_WEB:   strcat(strReturn,"internal;"); break;
	case pSD_WEB:    strcat(strReturn,"SD card;"); break;
	case pFLASH_WEB: strcat(strReturn,"SPI Flash;"); break;
	default:         strcat(strReturn,"unknown;"); break;
	}

	strcat(strReturn, "Входное напряжение питания контроллера, V|");
	#ifdef VCC_CONTROL  // если разрешено чтение напряжение питания
	_ftoa(strReturn,(float)Diag.AdcVcc/K_VCC_POWER,2);strcat(strReturn,";");
	#else
	strReturn += m_snprintf(strReturn += strlen(strReturn), 256, "%sесли < %.1d - сброс;", Is_otg_vbus_high() ? "+USB, " : "", ((SUPC->SUPC_SMMR & SUPC_SMMR_SMTH_Msk) >> SUPC_SMMR_SMTH_Pos) + 19);
	#endif
	strReturn += m_snprintf(strReturn += strlen(strReturn), 256, "Режим safeNetwork (%sадрес:%d.%d.%d.%d шлюз:%d.%d.%d.%d, не спрашивать пароль)|%s;", defaultDHCP ?"DHCP, ":"",defaultIP[0],defaultIP[1],defaultIP[2],defaultIP[3],defaultGateway[0],defaultGateway[1],defaultGateway[2],defaultGateway[3],HP.safeNetwork ?cYes:cNo);

	#ifdef DRV_EEV_L9333          // Контроль за работой драйвера ЭРВ
	strcat(strReturn,"Контроль за работой драйвера ЭРВ |");
	if (digitalReadDirect(PIN_STEP_DIAG))  strcat(strReturn,"Error L9333;"); else strcat(strReturn,"Normal;");
	#endif
	m_snprintf(strReturn+strlen(strReturn), 256, "Состояние FreeRTOS при старте (task+err_code) <sup>2</sup>|0x%04X;", lastErrorFreeRtosCode);

	// Замер запаса наблюдателя задач: одной строкой худшая задача.
	// Вся таблица - отдельным запросом get_sysWDT, чтобы не раздувать
	// этот ответ: здесь он и так близок к размеру outBuf.
	{
		uint16_t wdt_pc; const char *wdt_worst = Watchdog_Worst(&wdt_pc);
		m_snprintf(strReturn += strlen(strReturn), 256,
		           "Наблюдатель задач: худший запас (get_sysWDT - все)|%s, %d%% предела;",
		           wdt_worst, wdt_pc);
	}

	startSupcStatusReg |= SUPC->SUPC_SR;                                  // Копим изменения
	m_snprintf(strReturn += strlen(strReturn), 256, "Регистры контроллера питания (SUPC) SAM3X8E [SUPC_SMMR SUPC_MR SUPC_SR]|0x%08X %08X %08X", (unsigned int)(SUPC->SUPC_SMMR), (unsigned int)(SUPC->SUPC_MR), (unsigned int)(startSupcStatusReg));  // Регистры состояния контроллера питания
	if((startSupcStatusReg|SUPC_SR_SMS)==SUPC_SR_SMS_PRESENT) strcat(strReturn," bad VDDIN!");
	strcat(strReturn,";");

	// Вывод строки статуса
	strReturn += m_snprintf(strReturn += strlen(strReturn), 256, "Строка статуса ТН <sup>4</sup>|State:%d modWork:%X[%s] fHP:%X", HP.get_State(), HP.get_modWork(), Ret_Text(HP.get_ret()), HP.flags);
	strcat(strReturn,";");

	strcat(strReturn,"<b> Времена</b>|;");
	strcat(strReturn,"Текущее время|"); DecodeTimeDate(rtcSAM3X8.unixtime(),strReturn); strcat(strReturn,";");
	strcat(strReturn,"Время последнего включения ТН|");DecodeTimeDate(HP.get_startTime(),strReturn);strcat(strReturn,";");
	strcat(strReturn,"Время текущего состояния ТН|");DecodeTimeDate(HP.get_command_completed(),strReturn);strcat(strReturn,";");
	strcat(strReturn,"Время старта компрессора|");DecodeTimeDate(HP.get_startCompressor(),strReturn);strcat(strReturn,";");
	strcat(strReturn,"Время останова компрессора|");DecodeTimeDate(HP.get_stopCompressor(),strReturn);strcat(strReturn,";");
	strcat(strReturn,"Время сохранения текущих настроек ТН|");DecodeTimeDate(HP.get_saveTime(),strReturn);strcat(strReturn,";");

	strcat(strReturn,"<b> Счетчики ошибок</b>|;");
	strcat(strReturn,"Счетчик текущего числа повторных попыток пуска ТН|");
	if(HP.get_State()==pWORK_HP) { _itoa(HP.num_repeat,strReturn);strcat(strReturn,";");} else strcat(strReturn,"0;");
	strcat(strReturn,"Счетчик \"Потеря связи с "); strcat(strReturn,nameWiznet);strcat(strReturn,"\", повторная инициализация  <sup>3</sup>|");_itoa(Diag.num_resW5200,strReturn);strcat(strReturn,";");
	strcat(strReturn,"Счетчик числа сбросов мютекса захвата шины SPI|");_itoa(Diag.num_resMutexSPI,strReturn);strcat(strReturn,";");
	strcat(strReturn,"Счетчик числа сбросов мютекса захвата шины I2C|");_itoa(Diag.num_resMutexI2C,strReturn);strcat(strReturn,";");
	strcat(strReturn,"Попыток взять замок при остановленном планировщике (должно быть 0)|");_itoa(Diag.num_semSuspended,strReturn);strcat(strReturn,";");
	#ifdef MQTT
	strcat(strReturn,"Счетчик числа повторных соединений MQTT клиента|");_itoa(Diag.num_resMQTT,strReturn);strcat(strReturn,";");
	#endif
	strcat(strReturn,"Счетчик неудачных ping|");_itoa(Diag.num_resPing,strReturn);strcat(strReturn,";");
	#ifdef USE_ELECTROMETER_SDM
	strcat(strReturn,"Счетчик числа ошибок чтения электро-счетчика|");_itoa(HP.dSDM.get_numErr(),strReturn);strcat(strReturn,";");
	#endif
	if (HP.dFC.get_present()) strcat(strReturn,"Счетчик числа ошибок чтения частотного преобразователя (RS485)|");_itoa(HP.dFC.get_numErr(),strReturn);strcat(strReturn,";");
	strcat(strReturn,"Счетчик числа ошибок чтения датчиков температуры (DS18x20)|");_itoa(HP.get_errorReadDS18B20(),strReturn);strcat(strReturn,";");

	strcat(strReturn,"<b> Глобальные счетчики (Всего за весь период)</b>|;");
	strcat(strReturn,"Время сброса счетчиков|");DecodeTimeDate(HP.get_motoHour()->D1,strReturn);strcat(strReturn,";");
	strcat(strReturn,"Часы работы ТН (час)|");_ftoa(strReturn,(float)HP.get_motoHour()->H1/60.0f,1);strcat(strReturn,";");
	strcat(strReturn,"Часы работы компрессора ТН (час)|");_ftoa(strReturn,(float)HP.get_motoHour()->C1/60.0f,1);strcat(strReturn,";");
	strcat(strReturn,"Потребленная энергия ТН (кВт*ч)|");_dtoa(strReturn, HP.get_motoHour()->E1 / 1000, 3);strcat(strReturn,";");
	#ifdef  FLOWCON
	if(HP.sTemp[TCONING].get_present() & HP.sTemp[TCONOUTG].get_present()) {
		strcat(strReturn,"Выработанная энергия ТН (кВт*ч)|");_dtoa(strReturn, HP.get_motoHour()->P1 / 1000, 3);strcat(strReturn,";"); // Если есть оборудование
	}
	#endif
	strcat(strReturn,"<b> Сезонные счетчики</b>|;");
	strcat(strReturn,"Время сброса сезонных счетчиков ТН|");DecodeTimeDate(HP.get_motoHour()->D2,strReturn);strcat(strReturn,";");
	strcat(strReturn,"Часы работы ТН за сезон (час)|");_ftoa(strReturn,(float)HP.get_motoHour()->H2/60.0f,1);strcat(strReturn,";");
	strcat(strReturn,"Часы работы компрессора ТН за сезон (час)|");_ftoa(strReturn,(float)HP.get_motoHour()->C2/60.0f,1);strcat(strReturn,";");
	strcat(strReturn,"Потребленная энергия ТН за сезон (кВт*ч)|");_dtoa(strReturn, HP.get_motoHour()->E2 / 1000, 3);strcat(strReturn,";");
	#ifdef  FLOWCON
	if(HP.sTemp[TCONING].get_present() & HP.sTemp[TCONOUTG].get_present()) {
		strcat(strReturn,"Выработанная энергия ТН за сезон (кВт*ч)|");_dtoa(strReturn, HP.get_motoHour()->P2 / 1000, 3);strcat(strReturn,";"); // Если есть оборудование
	}
	#endif

	strcat(strReturn,"<b> Статистика за день</b>|;");
	strReturn += strlen(strReturn);
	Stats.StatsWebTable(strReturn);
}

static void wc_CONST(char *strReturn)
{
		WEB_STORE_DEBUG_INFO(25);
		strcat(strReturn,"VERSION|Версия прошивки|");strcat(strReturn,VERSION);strcat(strReturn,";");
		strcat(strReturn,"__DATE__ __TIME__|Дата и время сборки прошивки|");strcat(strReturn,__DATE__);strcat(strReturn," ");strcat(strReturn,__TIME__) ;strcat(strReturn,";");
		strcat(strReturn,"CONFIG_NAME|Имя конфигурации|");strcat(strReturn,CONFIG_NAME);strcat(strReturn,";");
		strcat(strReturn,"CONFIG_NOTE|");strcat(strReturn,CONFIG_NOTE);strcat(strReturn,"|;");
		strcat(strReturn,"configCPU_CLOCK_HZ|Частота CPU (МГц)|");_itoa(configCPU_CLOCK_HZ/1000000,strReturn);strcat(strReturn,";");
		strcat(strReturn,"SD_SPI_SPEED|Частота SPI SD карты (МГц)|");_itoa(SD_CLOCK, strReturn);strcat(strReturn,";");
		strcat(strReturn,"W5200_SPI_SPEED|Частота SPI сети "); strcat(strReturn,nameWiznet);strcat(strReturn," (МГц)|");_itoa(84/W5200_SPI_SPEED, strReturn);strcat(strReturn,";");
		strcat(strReturn,"I2C_SPEED|Частота работы шины I2C (кГц)|"); _itoa(I2C_SPEED/1000,strReturn); strcat(strReturn,";");
		strcat(strReturn,"UART_SPEED|Скорость отладочного порта (бод)|");_itoa(UART_SPEED,strReturn);strcat(strReturn,";");
		strcat(strReturn,"WDT_TIME|Период сторожевого таймера, 0 - нет (сек)|");_itoa(WDT_TIME,strReturn);strcat(strReturn,";");
		strcat(strReturn,"MODBUS_PORT_NUM|Используемый порт для обмена по Modbus RTU|Serial");
		if(&MODBUS_PORT_NUM==&Serial1) strcat(strReturn,cOne);
		else if(&MODBUS_PORT_NUM==&Serial2) strcat(strReturn,"2");
		else if(&MODBUS_PORT_NUM==&Serial3) strcat(strReturn,"3");
		else strcat(strReturn,"?");
		strcat(strReturn,";");
		strcat(strReturn,"MODBUS_PORT_SPEED|Скорость обмена (бод)|");_itoa(MODBUS_PORT_SPEED,strReturn);strcat(strReturn,";");
		strcat(strReturn,"MODBUS_PORT_CONFIG|Конфигурация порта|8N1;");
		strcat(strReturn,"MODBUS_TIME_WAIT|Максимальное время ожидания освобождения порта (мсек)|");_itoa(MODBUS_TIME_WAIT,strReturn);strcat(strReturn,";");
		// Инвертор
		strcat(strReturn,"DEVICEFC|Поддержка инвертора для компрессора|");
		if (HP.dFC.get_present())
		{
			strcat(strReturn,HP.dFC.get_name());strcat(strReturn,";");
			strcat(strReturn,"FC_MODBUS_ADR|Адрес инвертора на шине Modbus RTU|");strcat(strReturn,byteToHex(FC_MODBUS_ADR));strcat(strReturn,";");
		}
		else strcat(strReturn,"Нет;");
		// NEXTION
		strcat(strReturn,"NEXTION|Использование дисплея Nextion|");
#ifdef NEXTION
		strcat(strReturn,"ON;");
#else
		strcat(strReturn,"OFF;");
#endif
		strcat(strReturn,"NEXTION_PORT|Порт подключения дисплея Nextion|Serial");
		if(&NEXTION_PORT==&Serial1) strcat(strReturn,cOne);
		else if(&NEXTION_PORT==&Serial2) strcat(strReturn,"2");
		else if(&NEXTION_PORT==&Serial3) strcat(strReturn,"3");
		else strcat(strReturn,"?");
		strcat(strReturn,";");
		strcat(strReturn,"NEXTION_PORT_SPEED|Скорость обмена (бод)|");_itoa(NEXTION_PORT_SPEED,strReturn);strcat(strReturn,";");
		strcat(strReturn,"NEXTION_UPDATE|Время обновления информации на дисплее Nextion (мсек)|");_itoa(NEXTION_UPDATE,strReturn);strcat(strReturn,";");
		strcat(strReturn,"NEXTION_READ|Время опроса дисплея Nextion (мсек)|");_itoa(NEXTION_READ,strReturn);strcat(strReturn,";");

		// Карта
		m_snprintf(strReturn + strlen(strReturn), 128, "SD_FAT_VERSION|Версия библиотеки SdFat|%s;", SD_FAT_VERSION);
		m_snprintf(strReturn + strlen(strReturn), 128, "USE_SD_CRC|SD - Использовать проверку CRC|%c;", USE_SD_CRC ? '0'+USE_SD_CRC : USE_SD_CRC_FOR_WRITE ? 'W' : '-');
		strcat(strReturn,"SD_REPEAT|SD - Число попыток чтения, при неудаче переход на работу без карты|");_itoa(SD_REPEAT,strReturn);strcat(strReturn,";");

		// W5200
		strcat(strReturn,"W5200_THREAD|Число потоков для сетевого чипа (web сервера) "); strcat(strReturn,nameWiznet);strcat(strReturn,"|");_itoa(W5200_THREAD,strReturn);strcat(strReturn,";");
		strcat(strReturn,"W5200_TIME_WAIT|Время ожидания захвата мютекса, для управления потоками (мсек)|");_itoa( W5200_TIME_WAIT,strReturn);strcat(strReturn,";");
		strcat(strReturn,"STACK_vWebX|Размер стека для задачи одного web потока "); strcat(strReturn,nameWiznet);strcat(strReturn," (х4 байта)|");_itoa(STACK_vWebX,strReturn);strcat(strReturn,";");
		strcat(strReturn,"W5200_NUM_PING|Число попыток пинга до определения потери связи |");_itoa(W5200_NUM_PING,strReturn);strcat(strReturn,";");
		strcat(strReturn,"W5200_MAX_LEN|Размер аппаратного буфера  сетевого чипа "); strcat(strReturn,nameWiznet);strcat(strReturn," (байт)|");_itoa(W5200_MAX_LEN,strReturn);strcat(strReturn,";");
		strcat(strReturn,"INDEX_FILE|Файл загружаемый по умолчанию|");strcat(strReturn,INDEX_FILE);strcat(strReturn,";");
		strcat(strReturn,"TIME_ZONE|Часовой пояс|");_itoa(TIME_ZONE,strReturn);strcat(strReturn,";");
		// FreeRTOS
		strcat(strReturn,"FREE_RTOS_ARM_VERSION|Версия библиотеки FreeRTOS|");_itoa(FREE_RTOS_ARM_VERSION,strReturn);strcat(strReturn,";");
		strcat(strReturn,"configTICK_RATE_HZ|Квант времени системы FreeRTOS (мкс)|");_itoa(configTICK_RATE_HZ,strReturn);strcat(strReturn,";");

		strcat(strReturn,"TIME_READ_SENSOR|Период опроса датчиков (мсек)|");_itoa(TIME_READ_SENSOR,strReturn);//strcat(strReturn,";");
		ADD_WEBDELIM(strReturn);  return;
}

static void wc_CONST1(char *strReturn)
{
		strcat(strReturn,"TIME_CONTROL|Период управления тепловым насосом (мсек)|");_itoa(TIME_CONTROL,strReturn);strcat(strReturn,";");
		strcat(strReturn,"TIME_EEV|Период управления ЭРВ (мсек)|");_itoa(TIME_EEV,strReturn);strcat(strReturn,";");
		strcat(strReturn,"TIME_WEB_SERVER|Период опроса web сервера "); strcat(strReturn,nameWiznet);strcat(strReturn," (мсек)|");_itoa(TIME_WEB_SERVER,strReturn);strcat(strReturn,";");
		strcat(strReturn,"TIME_COMMAND|Период разбора команд управления ТН (мсек)|");_itoa(TIME_COMMAND,strReturn);strcat(strReturn,";");
		strcat(strReturn,"TIME_I2C_UPDATE |Период синхронизации внутренних часов с I2C часами (мсек)|");_itoa(TIME_I2C_UPDATE,strReturn);strcat(strReturn,";");
		// i2c
		strcat(strReturn,"I2C_COUNT_EEPROM|Адрес внутри чипа I2C с которого пишется счетчики ТН|"); strcat(strReturn,uint16ToHex(I2C_COUNT_EEPROM)); strcat(strReturn,";");
		strcat(strReturn,"I2C_SETTING_EEPROM|Адрес внутри чипа I2C с которого пишутся настройки ТН|"); strcat(strReturn,uint16ToHex(I2C_SETTING_EEPROM)); strcat(strReturn,";");
		strcat(strReturn,"I2C_PROFILE_EEPROM|Адрес внутри чипа I2C с которого пишется профили ТН|"); strcat(strReturn,uint16ToHex(I2C_PROFILE_EEPROM)); strcat(strReturn,";");
		// Датчики
		strcat(strReturn,"P_NUMSAMLES|Число значений для усреднения показаний давления|");_itoa(P_NUMSAMLES,strReturn);strcat(strReturn,";");
		strcat(strReturn,"T_NUMSAMLES|Число значений для усреднения показаний температуры|");_itoa(T_NUMSAMLES,strReturn);strcat(strReturn,";");
		strcat(strReturn,"GAP_TEMP_VAL|Допустимая разница показаний между двумя считываниями (°C)|");_dtoa(strReturn, GAP_TEMP_VAL, 2);strcat(strReturn,";");
		strcat(strReturn,"MAX_TEMP_ERR|Максимальная систематическая ошибка датчика температуры (°C)|");_dtoa(strReturn, MAX_TEMP_ERR, 2);strcat(strReturn,";");
		// Удаленные датчики
		strcat(strReturn,"SENSOR_IP|Использование удаленных датчиков|");
#ifdef SENSOR_IP
		strcat(strReturn,"ON;");
		strcat(strReturn,"IPNUMBER|Максимальное число удаленных датчиков, нумерация начинается с 1|");_itoa(IPNUMBER,strReturn);strcat(strReturn,";");
		strcat(strReturn,"UPDATE_IP|Максимальное время между посылками данных с удаленного датчика (сек)|");_itoa(UPDATE_IP,strReturn);strcat(strReturn,";");
#else
		strcat(strReturn,"OFF;");
#endif
#ifdef VCC_CONTROL  // если разрешено чтение напряжение питания
		strcat(strReturn,"K_VCC_POWER|Коэффициент пересчета для канала контроля напряжения питания (отсчеты/В)|");
		_ftoa(strReturn,(float)K_VCC_POWER,2);strcat(strReturn,";");
#endif
		// SALMONELLA
		strcat(strReturn,"SALMONELLA_DAY|День недели когда проводится обеззараживание ГВС (1-понедельник)|");_itoa(SALMONELLA_DAY,strReturn);strcat(strReturn,";");
		strcat(strReturn,"SALMONELLA_HOUR|Час когда начинаятся обеззарживание ГВС|");_itoa(SALMONELLA_HOUR,strReturn);strcat(strReturn,";");
		strcat(strReturn,"SALMONELLA_TEMP|Целевая температура обеззараживания ГВС (°C)|");_dtoa(strReturn, SALMONELLA_TEMP, 2);strcat(strReturn,";");
		// ЭРВ
#ifdef EEV_DEF
		strcat(strReturn,"EEV_QUEUE|Длина очереди команд шагового двигателя ЭРВ|");_itoa(EEV_QUEUE,strReturn);strcat(strReturn,";");
		strcat(strReturn,"EEV_INVERT|Инвертирование направления движения ЭРВ (по выходам)|");
#ifdef EEV_INVERT
		strcat(strReturn,"ON;");
#else
		strcat(strReturn,"OFF;");
#endif
		strcat(strReturn,"EEV_PHASE|Управляющая последовательность ЭРВ (фазы движения)|");
// Часовой: если StepMotorPhase.h не включён, препроцессор посчитает ВСЕ
// имена PHASE_* нулями, сравнение ниже окажется истинным, и сборка молча
// возьмёт не ту последовательность фаз ЭРВ. Неопределённые имена равны
// друг другу - на этом их и ловим.
#if PHASE_4 == PHASE_8s
#error "PHASE_* не определены: нужен #include \"StepMotorPhase.h\" до сравнений с EEV_PHASE"
#endif
#if EEV_PHASE==PHASE_8s
		strcat(strReturn,"8 фаз, полушаг;");
#elif EEV_PHASE==PHASE_8
		strcat(strReturn,"8 фаз, шаг;");
#elif EEV_PHASE==PHASE_4
		strcat(strReturn,"4 фазы, шаг;");
#else
		strcat(strReturn,"Ошибочная;");
#endif
#endif   // EEV
#ifdef MQTT
		strcat(strReturn,"MQTT_NUM_ERR_OFF|Число ошибок отправки подряд при котором отключается сервис MQTT (флаг сбрасывается)|");_itoa(MQTT_NUM_ERR_OFF,strReturn);strcat(strReturn,";");

#endif
		strcat(strReturn,"DEMO|Режим демонстрации (эмуляция датчиков)|");
#ifdef DEMO
		strcat(strReturn,"ON;");
#else
		strcat(strReturn,"OFF;");
#endif
		strcat(strReturn,"VER_SAVE|Версия формата сохраненных данных в I2C памяти|");
		_itoa(VER_SAVE,strReturn);
		// Фактическая версия прочитанных данных может отличаться от той, которой
		// прошивка пишет: до первого сохранения в памяти лежит прежний формат.
		if(VER_SAVE != HP.Option.ver) { strcat(strReturn," (в памяти "); _itoa(HP.Option.ver, strReturn); strcat(strReturn,")"); }
		strcat(strReturn,";");
		strcat(strReturn,"STAT_FREE_RTOS|Накопление статистики FreeRTOS (отладка)|");
#ifdef STAT_FREE_RTOS
		strcat(strReturn,"ON;");
#else
		strcat(strReturn,"OFF;");
#endif
		strcat(strReturn,"CHART_POINT|Максимальное число точек графиков|");_itoa(CHART_POINT,strReturn);strcat(strReturn,";");
		strcat(strReturn,"I2C memory|Микросхема памяти и место хранения журнала|");
		strcat(strReturn,I2C_MEMORY_NAME);
		strcat(strReturn, i2c_mem_check_note());
		strcat(strReturn,";");
		strcat(strReturn,"JOURNAL_LEN|Размер кольцевого буфера системного журнала (байт)|");_itoa(JOURNAL_LEN,strReturn);//strcat(strReturn,";");
		ADD_WEBDELIM(strReturn) ;
		return;
}


// ---------------------------------------------------------------------------
// Удалённые датчики IP (WiFi): узкий путь вне parserGET и вне нового API.
//
// Прошивка удалённых датчиков (ESP8266) в проект не входит; формат запросов и ответов
// прежний, байт в байт (эталоны api-ref/v2.11-pre-webapi/extra/*.raw, проверка -
// tools/api/sensorip.py):
//   GET /&set_sensorIP(N:темп:rssi:vcc:счётчик)&&   ->  \x7f...=OK\x7f\x7f или E21...E24
//   GET /&get_sensorParamIP(N:ПАРАМЕТР)&&
//   GET /&get_infoESP&&
// Роли здесь не разбираются, как и раньше: пароль (если включён) проверяет
// GetRequestedHttpResource до разбора. Адрес отправителя берётся из сокета. Запрос - одна или несколько таких
// команд через "&" (датчик с двумя термометрами шлёт две set_sensorIP подряд); всё прочее с адресом "/&..." получает 404 (web_server()).
// strtok_r вместо strtok: разбор тот же, но без общего статического состояния.
// ---------------------------------------------------------------------------
#ifdef SENSOR_IP
static void sip_set(char *strReturn, char *x, int8_t sock)       // x - содержимое скобок
{
	int32_t l_i32;
	char *ptr, *sv;
	int16_t a, b, c, d;
	// разбор строки формат "номер:температура:уровень_сигнала:питание:счетчик"
	ptr = strtok_r(x, ":", &sv);
	if(ptr == NULL)               {strcat(strReturn, "E21" WEBDELIM); return;}
	if((a = atoi(ptr)) == 0)      {strcat(strReturn, "E22" WEBDELIM); return;}  // если возвращен 0 то ошибка преобразования
	if((a < 1) || (a > IPNUMBER)) {strcat(strReturn, "E23" WEBDELIM); return;}  // проверка диапазона номеров датчиков

	ptr = strtok_r(NULL, ":", &sv);
	if(ptr == NULL)               {strcat(strReturn, "E21" WEBDELIM); return;}
	b = 100 * my_atof(ptr);
	if((b < -4000) || (b > 12000)) {strcat(strReturn, "E24" WEBDELIM); return;}  // проверка диапазона температуры -40...+120

	ptr = strtok_r(NULL, ":", &sv);
	if(ptr == NULL)               {strcat(strReturn, "E21" WEBDELIM); return;}
	if((c = 10 * my_atof(ptr)) == 0) {strcat(strReturn, "E22" WEBDELIM); return;}  // если возвращен 0 то ошибка преобразования
	if((c < -1000) || (c > 0))    {strcat(strReturn, "E24" WEBDELIM); return;}  // проверка диапазона уровня сигнала -100...-1 дБ

	ptr = strtok_r(NULL, ":", &sv);
	if(ptr == NULL)               {strcat(strReturn, "E21" WEBDELIM); return;}
	if((d = atoi(ptr)) == 0)      {strcat(strReturn, "E22" WEBDELIM); return;}  // если возвращен 0 то ошибка преобразования
	if((d < 1000) || (d > 5000))  {strcat(strReturn, "E24" WEBDELIM); return;}  // проверка диапазона питания 1000...4000 мВ

	ptr = strtok_r(NULL, ":", &sv);
	if(ptr == NULL)               {strcat(strReturn, "E21" WEBDELIM); return;}
	l_i32 = atoi(ptr);
	if(l_i32 < 0)                 {strcat(strReturn, "E24" WEBDELIM); return;}  // счётчик не меньше 0

	// ошибок нет, данные можно использовать
	byte adr[4];
	W5100.readSnDIPR(sock, adr);
	HP.sIP[a - 1].set_DataIP(a, b, c, d, l_i32, BytesToIPAddress(adr));
	strcat(strReturn, "OK" WEBDELIM);
}

static void sip_param(char *strReturn, char *x)                  // x - "N:ПАРАМЕТР"
{
	char *ptr, *sv;
	int16_t a;
	ptr = strtok_r(x, ":", &sv);
	if(ptr == NULL)               {strcat(strReturn, "E21" WEBDELIM); return;}  // нет параметра
	if((a = atoi(ptr)) == 0)      {strcat(strReturn, "E22" WEBDELIM); return;}  // не число
	if((a < 1) || (a > IPNUMBER)) {strcat(strReturn, "E23" WEBDELIM); return;}  // проверка диапазона номеров датчиков
	ptr = strtok_r(NULL, ":", &sv);
	if(ptr == NULL)               {strcat(strReturn, "E21" WEBDELIM); return;}  // нет имени параметра
	HP.sIP[a - 1].get_sensorIP(ptr, strReturn);
	ADD_WEBDELIM(strReturn);
}

static void sip_info(char *strReturn)
{
	// TIN, TOUT, TBOILER, ВЕРСИЯ, ПАМЯТЬ, ЗАГРУЗКА, АПТАЙМ, ПЕРЕГРЕВ, ОБОРОТЫ, СОСТОЯНИЕ.
	_ftoa(strReturn, (float)HP.sTemp[TIN].get_Temp() / 100.0, 1);     strcat(strReturn, "|");
	_ftoa(strReturn, (float)HP.sTemp[TOUT].get_Temp() / 100.0, 1);    strcat(strReturn, "|");
	_ftoa(strReturn, (float)HP.sTemp[TBOILER].get_Temp() / 100.0, 1); strcat(strReturn, "|");
	strcat(strReturn, VERSION);                                       strcat(strReturn, "|");
	_itoa(freeRam() + Diag.startRAM, strReturn);                      strcat(strReturn, "|");
	_itoa(Diag.CPU_LOAD, strReturn);                                  strcat(strReturn, "|");
	TimeIntervalToStr(HP.get_uptime(), strReturn);                    strcat(strReturn, "|");
#ifdef EEV_DEF
	_ftoa(strReturn, (float)HP.dEEV.get_Overheat() / 100, 2);         strcat(strReturn, "|");
#else
	strcat(strReturn, "-;");
#endif
	if(HP.dFC.get_present()) {HP.dFC.get_paramFC((char*)fc_FC, strReturn); strcat(strReturn, "|");}  // В зависимости от наличия инвертора
	else                     {strcat(strReturn, " - ");                    strcat(strReturn, "|");}
	if(HP.get_errcode() == OK) strcat(strReturn, HP.StateToStrEN());       // Ошибок нет
	else {strcat(strReturn, "Error "); _itoa(HP.get_errcode(), strReturn);} // есть ошибки
	strcat(strReturn, "|");
	ADD_WEBDELIM(strReturn);
}
#endif

// Какая это команда датчика: 0 - не наша. Имя проверяется по началу токена (после него скобка или конец).
static uint8_t sip_kind(const char *s)
{
	if(strncmp(s, "set_sensorIP(", 13) == 0) return 1;
#ifdef SENSOR_IP
	if(strncmp(s, "get_sensorParamIP(", 18) == 0) return 2;
	if(strncmp(s, "get_infoESP", 11) == 0 && (s[11] == '&' || s[11] == 0)) return 3;
#endif
	return 0;
}

// Вызывается из web_server() для запроса "GET /&..."; заголовок ответа HEADER_ANSWER уже записан в outBuf.
// Возвращает 1, если запрос обработан здесь (ответ собран в outBuf), 0 - запрос не наш (web_server отвечает 404).
// Запрос - одна или несколько команд датчика через "&" (датчик с двумя термометрами шлёт цепочку
// "&set_sensorIP(1:...)&set_sensorIP(2:...)&&"); ответ как у прежнего разбора: разделитель, затем на каждую команду
// эхо "команда(параметры)=" и результат с разделителем, в конце ещё разделитель. Любая чужая команда в цепочке
// даёт 404 на весь запрос: сначала проверяются все токены, потом выполняются.
uint8_t sensorIpRequest(uint8_t thread, int8_t sock)
{
	char *buf = Socket[thread].inPtr;                      // "&команда(параметры)&команда...&&"
	if(buf[0] != '&') return 0;
	char *e = strstr(buf, "&&");
	if(e) e[1] = 0;                                        // обрезать после комбинации &&
	for(char *p = buf + 1; ; ) {                           // проверка по сырому тексту: имена команд ASCII, раскодирование их не меняет
		if(!sip_kind(p)) return 0;
		p = strchr(p, '&');
		if(!p || !p[1]) break;
		p++;
	}
	urldecode(buf, buf, W5200_MAX_LEN);
	char *outBuf = Socket[thread].outBuf, *sv, *str, *x, *y;
	strcat(outBuf, WEBDELIM);                              // начало ответа
	for(str = strtok_r(buf, "&", &sv); str != NULL; str = strtok_r(NULL, "&", &sv)) {
		char *strReturn = outBuf + strlen(outBuf);
		if(strReturn > outBuf + sizeof(Socket[thread].outBuf) - 250) {   // контроль длины ответа
			strcat(strReturn, "E07" WEBDELIM);
			break;
		}
		uint8_t cmd = sip_kind(str);
		if(!cmd) { strcat(strReturn, "E01" WEBDELIM); continue; }
		if(strpbrk(str, "=") == 0) {                       // тело команды повторяется в ответе с "="
			strcat(strReturn, str);
			strReturn += strlen(strReturn);
			*strReturn++ = '=';
			*strReturn = 0;
		}
		if(cmd == 3) {
#ifdef SENSOR_IP
			sip_info(strReturn);
#endif
		} else if((x = strpbrk(str, "(")) != 0 && (y = strpbrk(str, ")")) != 0) {
			y[0] = 0;
			*x++ = 0;
#ifdef SENSOR_IP
			if(cmd == 1) sip_set(strReturn, x, sock);
			else sip_param(strReturn, x);
#else
			strcat(strReturn, "E25" WEBDELIM);             // сборка без SENSOR_IP: приём данных датчика не поддерживается
#endif
		} else strcat(strReturn, "E01" WEBDELIM);
	}
	strcat(outBuf, WEBDELIM);                              // двойной знак закрытия посылки
	return 1;
}


// ---- W E B   A P I  (/api/...) ----
// Протокол - tools/api/spec.md, ядро разбора и записи - ParamRegistry.cpp, таблицы
// параметров лежат рядом с классами-владельцами (Profile.cpp ...). Здесь только то, что
// знает про устройство и транспорт: группа state, действия do, ver, сборка ответа.
//
// Работает ТОЛЬКО в Socket[thread].inBuf/outBuf, без static-буферов и strtok. Мьютекс
// xWebThreadSemaphore уже взят в web_server().
// ---------------------------------------------------------------------------
#define API_PROTO    1       // версия протокола; морда сверяет её через /api/ver
#define API_SCRATCH  256     // рабочая область ядра (разбор значений) в хвосте outBuf: чтение и meta
#define API_SCRATCH_W 2300   // для set и do: значение до 2 КБ (поток календаря расписания), запрос целиком не длиннее ~1950 байт

// ---- группа state: живые значения шапки и главной ----
enum { S_WORK = 1, S_MODE, S_ERRCODE, S_ERROR, S_TIME, S_DATE, S_UPTIME, S_CPU, S_LASTCMD, S_STATUS, S_MODEH, S_TRGHOME, S_TRGPID, S_OH, S_OH2, S_EVAPOR, S_TCTC, S_STARTDT, S_RESETCAUSE, S_TEMPRTC, S_COP, S_PWRO, S_PWRG, S_PWR220, S_OVERCOOL, S_VCC };

#define SP(nm, kind, hk) { nm, 0, 0, 0, 0, 1, PT_CALL, PF_RO, kind, hk, 0 }
#define SPN(nm, hk) { nm, 0, 0, 0, 0, 100, PT_CALL, PF_RO, MK_NUM, hk, 0 }
static const char *const apiLabModeHouse[] = { "Выключено", "Отопление", "Охлаждение", 0 };
static const ApiParam apiStateTab[] = {
	SP("work",     MK_BOOL, S_WORK),      // ТН включён
	SP("mode",     MK_TEXT, S_MODE),      // состояние текстом
	SP("errcode",  MK_INT,  S_ERRCODE),
	SP("error",    MK_TEXT, S_ERROR),
	SP("time",     MK_TEXT, S_TIME),
	SP("date",     MK_TEXT, S_DATE),
	SP("uptime",   MK_TEXT, S_UPTIME),
	SP("cpu",      MK_INT,  S_CPU),       // загрузка процессора, %
	SP("last_cmd", MK_TEXT, S_LASTCMD),   // сколько прошло с последней выполненной команды; null - команд не было
	SP("status",   MK_TEXT, S_STATUS),    // состояние алгоритма или "Error N"
	{ "mode_house", apiLabModeHouse, 0, 2, 0, 1, PT_CALL, PF_UW, MK_INT, S_MODEH, 0 },    // режим дома: выключено/отопление/охлаждение
	{ "trg_home",   0, 0, 0, 0, 100, PT_CALL, PF_UW, MK_NUM, S_TRGHOME, 0 },              // целевая температура дома; null - режим «выключено»
	{ "trg_pid",    0, 0, 0, 0, 100, PT_CALL, PF_RO, MK_NUM, S_TRGPID, 0 },               // расчётная подача для ПИД; null - алгоритм без ПИД
#ifdef EEV_DEF
	SPN("overheat",       S_OH),          // перегрев по выбранной формуле, K
	SPN("overheat_tcomp", S_OH2),         // второй перегрев (TCOMPIN - T[PEVA]); null - формула его не использует
#endif
	SPN("evapor",         S_EVAPOR),      // температура кипения по давлению; null - датчика давления нет
	SPN("tcomp_tcon",     S_TCTC),        // нагнетание минус конденсация, K
	SP("start_dt",     MK_TEXT, S_STARTDT),     // дата и время последнего пуска контроллера
	SP("reset_cause",  MK_TEXT, S_RESETCAUSE), // причина последнего сброса
	{ "temp_rtc", 0, 0, 0, 0, 100, PT_CALL, PF_RO | PF_AR, MK_NUM, S_TEMPRTC, 0 },    // температура платы (датчик часов), °C
	SPN("cop",         S_COP),         // Full COP; null - не рассчитан
	{ "power_out",  0, 0, 0, 0, 1000, PT_CALL, PF_RO, MK_NUM, S_PWRO, 0 },     // выходная мощность, кВт
	{ "power_geo",  0, 0, 0, 0, 1000, PT_CALL, PF_RO, MK_NUM, S_PWRG, 0 },     // мощность геоконтура, кВт
	{ "power_220",  0, 0, 0, 0, 1000, PT_CALL, PF_RO, MK_NUM, S_PWR220, 0 },   // потребляемая мощность, кВт; null - пропало питание (USE_UPS)
	SPN("overcool",    S_OVERCOOL),    // переохлаждение, K
#ifdef VCC_CONTROL
	{ "vcc",        0, 0, 0, 0, 100, PT_CALL, PF_RO | PF_AR, MK_NUM, S_VCC, 0 },  // входное напряжение питания, В
#endif
};

static int api_get_state(uint8_t hook, uint8_t, ApiOut *o)
{
	char t[64]; t[0] = 0;
	switch(hook) {
	case S_WORK:    api_put(o, HP.IsWorkingNow() ? "true" : "false"); break;
	case S_MODE:    api_put_str(o, HP.StateToStr()); break;
	case S_ERRCODE: api_put_int(o, HP.get_errcode()); break;
	case S_ERROR:   api_put_str(o, HP.get_lastErr()); break;
	case S_TIME:    HP.get_datetime((char*) time_TIME, t); api_put_str(o, t); break;
	case S_DATE:    HP.get_datetime((char*) time_DATE, t); api_put_str(o, t); break;
	case S_UPTIME:  TimeIntervalToStr(HP.get_uptime(), t); api_put_str(o, t); break;
	case S_CPU:     api_put_int(o, Diag.CPU_LOAD); break;
	case S_LASTCMD: {
		uint32_t c = HP.get_command_completed();
		if(!c) api_put(o, "null"); else { TimeIntervalToStr(rtcSAM3X8.unixtime() - c, t, 1); api_put_str(o, t); }
		break; }
	case S_STATUS:
		if(HP.get_errcode() == OK) HP.get_StateModworkStr(t); else { strcpy(t, "Error "); _itoa(HP.get_errcode(), t); }
		api_put_str(o, t);
		break;
	case S_MODEH:   api_put_int(o, HP.get_modeHouse()); break;
	case S_TRGHOME:
		if(HP.get_modeHouse() == pHEAT) api_put_dec(o, HP.get_targetTempHeat(), 100);
		else if(HP.get_modeHouse() == pCOOL) api_put_dec(o, HP.get_targetTempCool(), 100);
		else api_put(o, "null");
		break;
	case S_TRGPID:
		if(HP.get_modeHouse() == pOFF || HP.get_modeHouseSettings()->Rule == pHYSTERESIS) api_put(o, "null");
		else api_put_dec(o, HP.CalcTargetPID(*HP.get_modeHouseSettings()), 100);
		break;
#ifdef EEV_DEF
	case S_OH:      api_put_dec(o, HP.dEEV.get_Overheat(), 100); break;
	case S_OH2:
#ifdef TCOMPIN
	#ifdef TCONIN
		if(HP.dEEV.get_ruleEEV() != TCOMPIN_PEVA) {
	#else
		if(HP.dEEV.get_ruleEEV() != TCOMPIN_PEVA && HP.is_heating()) {
	#endif
			api_put_dec(o, HP.dEEV.OverheatTCOMP, 100);
			break;
		}
#endif
		api_put(o, "null");
		break;
#endif
	case S_EVAPOR:  if(HP.sADC[PEVA].get_present()) api_put_dec(o, HP.get_temp_evaporating(), 100); else api_put(o, "null"); break;
	case S_TCTC:    api_put_dec(o, HP.sTemp[TCOMP].get_Temp() - HP.get_temp_condensing(), 100); break;
	case S_COP:     if(HP.fullCOP != -1000) api_put_dec(o, HP.fullCOP, 100); else api_put(o, "null"); break;
	case S_PWRO:    api_put_dec(o, HP.powerOUT, 1000); break;
	case S_PWRG:    api_put_dec(o, HP.powerGEO, 1000); break;
	case S_PWR220:
#ifdef USE_UPS
		if(HP.NO_Power) { api_put(o, "null"); break; }
#endif
		api_put_dec(o, HP.power220, 1000); break;
	case S_OVERCOOL: api_put_dec(o, HP.get_overcool(), 100); break;
#ifdef VCC_CONTROL
	case S_VCC:     api_put_dec(o, (int32_t)(Diag.AdcVcc * 100 / K_VCC_POWER), 100); break;
#endif
	case S_STARTDT: DecodeTimeDate(HP.get_startDT(), t); api_put_str(o, t); break;
	case S_RESETCAUSE: api_put_str(o, ResetCause()); break;
	case S_TEMPRTC: api_put_dec(o, getTemp_RtcI2C(), 100); break;
	default: return AE_FAIL;
	}
	return AE_OK;
}
static int api_set_state(uint8_t hook, uint8_t, const char *val)
{
	if(hook == S_MODEH) {                    // перечисление уже проверено ядром, режим «охлаждение» при ONLY_HEAT отсечён маской
		HP.set_mode((MODE_HP)(int)my_atof(val));
		return AE_OK;
	}
	if(hook == S_TRGHOME) {                  // сдвиг уставки на разницу с текущей целью
		float x = my_atof(val);
		if(x == ATOF_ERROR) return AE_BAD_VALUE;
		if(HP.get_modeHouse() == pOFF) return AE_NA;
		HP.setTargetTemp(rd(x, 100) - (HP.get_modeHouse() == pCOOL ? HP.get_targetTempCool() : HP.get_targetTempHeat()));
		return AE_OK;
	}
	return AE_FAIL;
}
static uint16_t api_mask_state(uint8_t hook) { return (hook == S_MODEH && HouseOnlyHeat) ? 4 : 0; }
static const ApiOps apiOpsState = { 0, api_get_state, api_set_state, 0, api_mask_state, 0, 0, 0 };
static const ApiGroup ApiGrpState = { "state", apiStateTab, sizeof(apiStateTab) / sizeof(apiStateTab[0]), &apiOpsState };

extern const ApiGroup ApiGrpSlot, ApiGrpCal, ApiGrpChart, ApiGrpChartCfg;
int api_chart(const char *q, ApiOut *o);       // HeatPumpCharts.cpp
extern const ApiGroup ApiGrpOhp, ApiGrpNet, ApiGrpTime, ApiGrpSched, ApiGrpTemp, ApiGrpMessage, ApiGrpRelay, ApiGrpPress, ApiGrpFlow, ApiGrpInput;
#ifdef MQTT
extern const ApiGroup ApiGrpMqtt;
#endif
#ifdef SENSOR_IP
extern const ApiGroup ApiGrpIpsens;
#endif
#ifdef USE_ELECTROMETER_SDM
extern const ApiGroup ApiGrpSdm;
#endif
#ifdef EEV_DEF
extern const ApiGroup ApiGrpEev;
#endif     // у классов-владельцев

// ---- группа sys: служебные данные для страниц системы ----
enum { Y_SOCKETS = 1, Y_SOCKRES, Y_OWPIN, Y_LSTATS, Y_LHIST, Y_FREERAM, Y_INFO, Y_CONST, Y_CONST1, Y_WDT, Y_TASKS, Y_ERRORS, Y_ERRORS1, Y_LHISTOLD };
static const ApiParam apiSysTab[] = {
	{ "sockets",    0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_TABLE, Y_SOCKETS, 0 },    // состояние сокетов сетевого чипа
	{ "socket_res", 0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_INT,   Y_SOCKRES, 0 },    // сколько зависших сокетов сброшено
	{ "onewire_pin", 0, 0, 0, 0, 1, PT_CALL, PF_RO,        MK_TEXT,  Y_OWPIN,   0 },    // вывод или мастер шины OneWire
	{ "list_stats", 0, 0, 0, 0, 1, PT_CALL, PF_RO,         MK_TABLE, Y_LSTATS,  0 },    // файлы статистики на SD по годам (csv), новые первыми
	{ "list_hist",  0, 0, 0, 0, 1, PT_CALL, PF_RO,         MK_TABLE, Y_LHIST,   0 },    // то же для истории (hist2_ГГГГ: текущий формат)
	{ "list_hist_old", 0, 0, 0, 0, 1, PT_CALL, PF_RO,      MK_TABLE, Y_LHISTOLD, 0 },   // история прежнего формата (hist_ГГГГ), только скачивание
	{ "free_ram",   0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_INT,   Y_FREERAM, 0 },    // свободная память, байт
	// Таблицы для страниц «Система», «Константы», «Задачи»: текст в прежнем виде («подпись|значение;...»), разбирает морда
	{ "info",   0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_TEXT, Y_INFO,   0 },
	{ "const",  0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_TEXT, Y_CONST,  0 },
	{ "const1", 0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_TEXT, Y_CONST1, 0 },
	{ "wdt",    0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_TEXT, Y_WDT,    0 },     // запас наблюдателя задач
	{ "tasks",  0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_TEXT, Y_TASKS,  0 },     // задачи FreeRTOS, по строке на задачу
	// Описания кодов ошибок: массив строк, индекс = -код. Весь справочник (около 8 КБ) в выходной буфер не помещается,
	// поэтому два ключа по API_ERR_HALF записей: errors - коды 0...-(API_ERR_HALF-1), errors1 - остальные
	{ "errors",  0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_TABLE, Y_ERRORS,  0 },
	{ "errors1", 0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_TABLE, Y_ERRORS1, 0 },
};
#define API_ERR_HALF 47
static_assert(-ERR_ERRMAX + 1 <= 2 * API_ERR_HALF, "справочник ошибок не помещается в два ключа sys.errors/errors1: добавьте ключ");
static void api_put_errors(ApiOut *o, uint8_t part)
{
	int from = part * API_ERR_HALF, to = from + API_ERR_HALF;
	if(to > -ERR_ERRMAX + 1) to = -ERR_ERRMAX + 1;
	api_putc(o, '[');
	for(int i = from; i < to; i++) {
		if(i > from) api_putc(o, ',');
		api_put_str(o, Error_Text((int8_t) -i));
	}
	api_putc(o, ']');
}
// Список файлов статистики или истории: годы от текущего вниз, пока файл существует (имя отдаётся с расширением csv, как раньше).
// Карта годов ведёт Statistics (скан при старте + создание файлов): открытие файла SdFat в веб-задаче съедало почти весь её стек.
// Имя собирается в свободном хвосте выходного буфера, без локальных массивов и printf.
static void api_put_year_files(ApiOut *o, uint8_t what)
{
	if(o->cap - o->len < 128) { o->over = 1; return; }
	char *nm = o->p + o->cap - 48;
	api_putc(o, '[');
	bool first = true;
	for(int32_t y = rtcSAM3X8.get_years(); y > 2000 && Stats.HasYear(what, y); y--) {
		if(o->len + 64 >= o->cap - 48) { o->over = 1; break; }     // вывод дорос до рабочего хвоста
		strcpy(nm, what == ID_HISTORY_OLD ? history_file_start : what == ID_HISTORY ? history2_file_start : stats_file_start);
		char *e = nm + strlen(nm);
		e[0] = (char)('0' + y / 1000 % 10); e[1] = (char)('0' + y / 100 % 10); e[2] = (char)('0' + y / 10 % 10); e[3] = (char)('0' + y % 10); e[4] = 0;
		strcat(nm, stats_csv_file_ext);
		if(!first) api_putc(o, ',');
		first = false;
		api_put_str(o, nm);
	}
	api_putc(o, ']');
}
// Текстовая таблица в свободную часть выходного буфера и экранирование на месте. Источники старые (пишут strcat без предела),
// поэтому место проверяется заранее: самая длинная таблица (sysInfo) около 3,5 КБ.
static int api_sys_table(uint8_t hook, ApiOut *o)
{
	if(o->cap - o->len < 4600) return AE_TOO_BIG;
	char *t = o->p + o->len + 1;
	t[0] = 0;
	switch(hook) {
	case Y_INFO:   sysInfoText(t); break;
	case Y_CONST:  wc_CONST(t); break;
	case Y_CONST1: wc_CONST1(t); break;
	case Y_WDT:    Watchdog_StatWeb(t, 0); break;
#ifdef STAT_FREE_RTOS
	case Y_TASKS:  vTaskList(t); break;
#endif
	default: return AE_NA;
	}
	size_t n = strlen(t);
	while(n && t[n - 1] == *WEBDELIM) t[--n] = 0;               // старые обёртки дописывают разделитель ответа
	api_str_inplace(o);
	return AE_OK;
}
static int api_get_sys(uint8_t hook, uint8_t, ApiOut *o)
{
	if(hook == Y_SOCKETS) socketInfoApi(o);
	else if(hook == Y_SOCKRES) api_put_int(o, HP.socketRes());
	else if(hook == Y_LSTATS) api_put_year_files(o, 0);
	else if(hook == Y_LHIST) api_put_year_files(o, 1);
	else if(hook == Y_LHISTOLD) api_put_year_files(o, 2);
	else if(hook == Y_FREERAM) api_put_int(o, freeRam() + Diag.startRAM);
	else if(hook == Y_ERRORS || hook == Y_ERRORS1) api_put_errors(o, hook - Y_ERRORS);
	else if(hook >= Y_INFO && hook <= Y_TASKS) return api_sys_table(hook, o);
	else if(hook == Y_OWPIN) {
		char t[24]; t[0] = 0;
#ifdef ONEWIRE_DS2482
		strcpy(t, "I2C, DS2482(1)");
#else
		strcpy(t, "D"); _itoa((int)(PIN_ONE_WIRE_BUS), t);
#endif
		api_put_str(o, t);
	}
	else return AE_FAIL;
	return AE_OK;
}
static const ApiOps apiOpsSys = { 0, api_get_sys, 0, 0, 0, 0, 0, 0 };
static const ApiGroup ApiGrpSys = { "sys", apiSysTab, sizeof(apiSysTab) / sizeof(apiSysTab[0]), &apiOpsSys };


// ---- группа fc: частотный преобразователь (Omron MX2 или Vacon) ----
// Обе сборки отвечают на один и тот же get_paramFC/set_paramFC (его же читают телеметрия и Nextion), поэтому группа одна
// и без своих копий формул: хук ищет строку-имя в самой таблице (hook = номер строки + 1) и зовёт прежнюю функцию.
// Границы в таблице - для meta и страницы; окончательную проверку, как и раньше, делает сам класс (границы Omron и Vacon
// разные: частоты в Гц и в процентах скорости).
static const int FCB = __COUNTER__;
#define FCK(nm, fl, kind, sc, lo, hi) { nm, 0, (float)(lo), (float)(hi), 0, sc, PT_CALL, fl, kind, (uint8_t)(__COUNTER__ - FCB), 0 }
#define FCKR(nm, fl, kind, sc, b) FCK(nm, fl, kind, sc, b)    // границы одним именем: пара раскрывается до подстановки в FCK
#ifdef FC_VACON
	#define FB_UPTIME 1, 650
	#define FB_PIDSTOP 0, 100
	#define FB_DTC 0, 24.99f
	#define FB_FREQ 0, 327.67f
	#define FB_GEN 0, 327.67f
	#define FB_PIDSTEP 0, 200
	#define FB_STEP 0, 99.99f
	#define FB_DT 0, 9.99f
#else
	#define FB_UPTIME 3, 599
	#define FB_PIDSTOP 50, 100
	#define FB_DTC 1, 25
	#define FB_FREQ 20, 240
	#define FB_GEN 40, 240
	#define FB_PIDSTEP 0.01f, 5
	#define FB_STEP 0.2f, 10
	#define FB_DT 0.01f, 9.99f
#endif
static const ApiParam apiFcTab[] = {
	FCK(fc_ON_OFF,  0,     MK_BOOL, 1, 0, 0),
	FCK(fc_INFO,    PF_RO, MK_TEXT, 1, 0, 0),                 // регистры по Modbus: «код|описание|значение;...», опрос занимает секунды
	FCK(fc_NAME,    PF_RO, MK_TEXT, 1, 0, 0),
	FCK(fc_NOTE,    PF_RO, MK_TEXT, 1, 0, 0),
	FCK(fc_PIN,     PF_RO, MK_INT,  1, 0, 0),
	FCK(fc_PRESENT, PF_RO, MK_BOOL, 1, 0, 0),
	FCK(fc_STATE,   PF_RO, MK_INT,  1, 0, 0),
	FCK(fc_FC,      0,     MK_NUM,  100, 0, 0),               // целевая частота (Omron, Гц) или скорость (Vacon, %); границы - динамические
	FCK(fc_cFC,     PF_RO, MK_NUM,  100, 0, 0),
	FCK(fc_cPOWER,  PF_RO, MK_NUM,  10, 0, 0),
	FCK(fc_INFO1,   PF_RO, MK_TEXT, 1, 0, 0),
	FCK(fc_cCURRENT, PF_RO, MK_NUM, 100, 0, 0),
	FCK(fc_AUTO_RESET_FAULT, 0, MK_BOOL, 1, 0, 0),
	FCK(fc_ANALOG,  PF_RO, MK_BOOL, 1, 0, 0),
	FCK(fc_DAC,     PF_RO, MK_INT,  1, 0, 0),
	FCK(fc_LEVEL0,  0,     MK_INT,  1, 0, 4096),              // только при FC_ANALOG_CONTROL, иначе null
	FCK(fc_LEVEL100, 0,    MK_INT,  1, 0, 4096),
#ifndef FC_VACON
	FCK(fc_LEVELOFF, 0,    MK_INT,  1, 0, 4096),
#endif
	FCK(fc_BLOCK,   0,     MK_BOOL, 1, 0, 0),                 // запись 0 снимает блокировку, 1 ставит
	FCK(fc_ERROR,   PF_RO, MK_INT,  1, 0, 0),
	FCKR(fc_UPTIME, 0, MK_INT, 1, FB_UPTIME),
	FCKR(fc_PID_STOP, 0, MK_INT, 1, FB_PIDSTOP),
	FCKR(fc_DT_COMP_TEMP, 0, MK_NUM, 100, FB_DTC),
	FCKR(fc_PID_FREQ_STEP, 0, MK_NUM, 100, FB_PIDSTEP),
	FCKR(fc_START_FREQ, 0, MK_NUM, 100, FB_FREQ),
	FCKR(fc_START_FREQ_BOILER, 0, MK_NUM, 100, FB_FREQ),
	FCKR(fc_MIN_FREQ, 0, MK_NUM, 100, FB_FREQ),
	FCKR(fc_MIN_FREQ_COOL, 0, MK_NUM, 100, FB_FREQ),
	FCKR(fc_MIN_FREQ_BOILER, 0, MK_NUM, 100, FB_FREQ),
	FCKR(fc_MIN_FREQ_USER, 0, MK_NUM, 100, FB_FREQ),
	FCKR(fc_MAX_FREQ, 0, MK_NUM, 100, FB_FREQ),
	FCKR(fc_MAX_FREQ_COOL, 0, MK_NUM, 100, FB_FREQ),
	FCKR(fc_MAX_FREQ_BOILER, 0, MK_NUM, 100, FB_FREQ),
	FCKR(fc_MAX_FREQ_USER, 0, MK_NUM, 100, FB_FREQ),
	FCKR(fc_MAX_FREQ_GEN, 0, MK_NUM, 100, FB_GEN),
	FCKR(fc_STEP_FREQ, 0, MK_NUM, 100, FB_STEP),
	FCKR(fc_STEP_FREQ_BOILER, 0, MK_NUM, 100, FB_STEP),
	FCKR(fc_DT_TEMP, 0, MK_NUM, 100, FB_DT),
	FCKR(fc_DT_TEMP_BOILER, 0, MK_NUM, 100, FB_DT),
	FCK(fc_MB_ERR,  PF_RO, MK_INT,  1, 0, 0),
	FCK(fc_FC_RETOIL_FREQ, PF_RO, MK_NUM, 100, 0, 0),         // null - в сборке не задана
	FCK(fc_FC_TIME_READ, PF_RO, MK_INT, 1, 0, 0),
#ifdef FC_VACON
	FCK(fc_fFC_RetOil, 0,  MK_BOOL, 1, 0, 0),                 // возврат масла (только Vacon)
	FCK(fc_ReturnOilPeriod, 0, MK_INT, 1, 0, 32767),
	FCK(fc_ReturnOilPerDivHz, 0, MK_INT, 1, 0, 32767),
	FCK(fc_ReturnOilEEV, 0, MK_INT, 1, -32768, 32767),
#endif
};
static int api_get_fc(uint8_t hook, uint8_t, ApiOut *o)
{
	if(hook < 1 || hook > sizeof(apiFcTab) / sizeof(apiFcTab[0])) return AE_FAIL;
	const ApiParam *p = &apiFcTab[hook - 1];
	if(!strcmp(p->name, fc_INFO)) {                    // сырой текст дописывается в свободную часть выходного буфера и экранируется на месте
		if(o->cap - o->len < 3000) return AE_TOO_BIG;
		char *t = o->p + o->len + 1;
		t[0] = 0;
		HP.dFC.get_paramFC((char*) fc_INFO, t);
		api_str_inplace(o);
		return AE_OK;
	}
	char t[128]; t[0] = 0;                              // описание инвертора - русский текст до 83 байт (noteFC_NO)
	HP.dFC.get_paramFC((char*) p->name, t);
	switch(p->bit) {
	case MK_TEXT: api_put_str(o, t); break;
	case MK_BOOL: api_put(o, (t[0] == '1' || !strcmp(t, cYes)) ? "true" : "false"); break;
	default: {                                         // число; после него может стоять единица («50.00%», «12.5 Гц»); иное - null
		uint8_t n = 0, dg = 0;
		while(t[n] == '-' || t[n] == '.' || (t[n] >= '0' && t[n] <= '9')) { if(t[n] >= '0' && t[n] <= '9') dg++; n++; }
		if(dg && t[0] != '.' && t[n - 1] != '.' && (t[n] == 0 || t[n] == ' ' || t[n] == '%')) api_put_n(o, t, n); else api_put(o, "null"); }   // «-» и «E10» - не числа
	}
	return AE_OK;
}
static int api_set_fc(uint8_t hook, uint8_t, const char *val)
{
	if(hook < 1 || hook > sizeof(apiFcTab) / sizeof(apiFcTab[0])) return AE_FAIL;
	const ApiParam *p = &apiFcTab[hook - 1];
	float x;
	if(p->bit == MK_BOOL) {
		if(!strcmp(val, "1") || !strcmp(val, "true")) x = 1; else if(!strcmp(val, "0") || !strcmp(val, "false")) x = 0; else return AE_BAD_VALUE;
	} else {
		x = my_atof(val);
		if(x == ATOF_ERROR) return AE_BAD_VALUE;
	}
	return HP.dFC.set_paramFC((char*) p->name, x) ? AE_OK : AE_RANGE;
}
static const ApiOps apiOpsFc = { 0, api_get_fc, api_set_fc, 0, 0, 0, 0, 0 };
static const ApiGroup ApiGrpFc = { "fc", apiFcTab, sizeof(apiFcTab) / sizeof(apiFcTab[0]), &apiOpsFc };


// ---- группа dbg: флаги отладки (dbg.<ИМЯ>.on / .group / .note), таблица DbgTable в DebugFlags.cpp ----
// Включение действует сразу (dbgFlags), в EEPROM флаги попадают общей кнопкой «Записать». Читает и пишет только admin.
enum { D_ON = 1, D_GROUP, D_NOTE };
static const ApiParam apiDbgTab[] = {
	{ "on",    0, 0, 0, 0, 1, PT_CALL, PF_AR, MK_BOOL, D_ON, 0 },
	{ "group", 0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_TEXT, D_GROUP, 0 },
	{ "note",  0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_TEXT, D_NOTE, 0 },    // «описание [ключевые слова журнала]»
};
static uint8_t api_dbg_n(void) { return DbgCount; }
static const char *api_dbg_name(uint8_t i) { return DbgTable[i].name; }
static uint8_t api_dbg_ok(uint8_t) { return 1; }
static int api_get_dbg(uint8_t hook, uint8_t dev, ApiOut *o)
{
	if(hook == D_ON) api_put(o, (dbgFlags & DbgTable[dev].bit) ? "true" : "false");
	else if(hook == D_GROUP) api_put_str(o, DbgTable[dev].group);
	else if(hook == D_NOTE) api_put_str(o, DbgTable[dev].note);
	else return AE_FAIL;
	return AE_OK;
}
static int api_set_dbg(uint8_t hook, uint8_t dev, const char *val)
{
	if(hook != D_ON) return AE_FAIL;
	int v;
	if(!strcmp(val, "1") || !strcmp(val, "true")) v = 1; else if(!strcmp(val, "0") || !strcmp(val, "false")) v = 0; else return AE_BAD_VALUE;
	uint32_t m = dbgFlags;
	if(!Dbg_Set(&m, DbgTable[dev].name, v)) return AE_FAIL;
	dbgFlags = m;
	return AE_OK;
}
static const ApiOps apiOpsDbg = { 0, api_get_dbg, api_set_dbg, 0, 0, api_dbg_n, api_dbg_name, api_dbg_ok };
static const ApiGroup ApiGrpDbg = { "dbg", apiDbgTab, sizeof(apiDbgTab) / sizeof(apiDbgTab[0]), &apiOpsDbg };


// ---- группа ver: версия и сборка (доступно и роли user) ----
enum { V_FW = 1, V_CONFIG, V_NOTE, V_BUILD, V_WEBSRC };
static const ApiParam apiVerTab[] = {
	{ "fw",          0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, V_FW,     0 },
	{ "config",      0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, V_CONFIG, 0 },     // имя конфигурации (Config.h)
	{ "config_note", 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, V_NOTE,   0 },
	{ "build",       0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, V_BUILD,  0 },     // дата и время сборки «Oct 12 2026 14:09:55»
	{ "web_src",     0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, V_WEBSRC, 0 },     // откуда реально загружена морда: internal (аварийная), SD card, SPI Flash
};
static int api_get_ver(uint8_t hook, uint8_t, ApiOut *o)
{
	switch(hook) {
	case V_FW:     api_put_str(o, VERSION); break;
	case V_CONFIG: api_put_str(o, CONFIG_NAME); break;
	case V_NOTE:   api_put_str(o, CONFIG_NOTE); break;
	case V_BUILD:  api_put_str2(o, __DATE__ " ", __TIME__); break;
	case V_WEBSRC: api_put_str(o, HP.get_SourceWeb() == pMIN_WEB ? "internal" : HP.get_SourceWeb() == pSD_WEB ? "SD card" : HP.get_SourceWeb() == pFLASH_WEB ? "SPI Flash" : "unknown"); break;
	default: return AE_FAIL;
	}
	return AE_OK;
}
static const ApiOps apiOpsVer = { 0, api_get_ver, 0, 0, 0, 0, 0, 0 };
static const ApiGroup ApiGrpVer = { "ver", apiVerTab, sizeof(apiVerTab) / sizeof(apiVerTab[0]), &apiOpsVer };

// ---- группа test: режим тестирования (менять можно только при остановленном ТН) ----
enum { T_MODE = 1, T_REMARK };
static const char *const apiLabTestMode[] = { "NORMAL", "SAFE_TEST", "TEST", "HARD_TEST", 0 };
static const ApiParam apiTestTab[] = {
	{ "mode",   apiLabTestMode, 0, 3, 0, 1, PT_CALL, PF_OFF | PF_AR, MK_INT, T_MODE, 0 },
	{ "remark", 0, 0, 0, 0, 1, PT_CALL, PF_RO | PF_AR, MK_TEXT, T_REMARK, 0 },       // пояснение к режиму
};
static int api_get_test(uint8_t hook, uint8_t, ApiOut *o)
{
	if(hook == T_MODE) api_put_int(o, HP.get_testMode());
	else if(hook == T_REMARK) api_put_str(o, noteRemarkTest[HP.get_testMode()]);
	else return AE_FAIL;
	return AE_OK;
}
static int api_set_test(uint8_t hook, uint8_t, const char *val)
{
	if(hook != T_MODE) return AE_FAIL;
	HP.set_testMode((TEST_MODE)(int) my_atof(val));          // допустимость индекса проверило ядро
	return AE_OK;
}
static const ApiOps apiOpsTest = { 0, api_get_test, api_set_test, 0, 0, 0, 0, 0 };
static const ApiGroup ApiGrpTest = { "test", apiTestTab, sizeof(apiTestTab) / sizeof(apiTestTab[0]), &apiOpsTest };

static const ApiGroup *const apiGroups[] = { &ApiGrpState, &ApiGrpHeat, &ApiGrpCool, &ApiGrpBoiler, &ApiGrpOhp, &ApiGrpNet, &ApiGrpTime,
#ifdef MQTT
	&ApiGrpMqtt,
#endif
	&ApiGrpMessage, &ApiGrpSys, &ApiGrpProfile, &ApiGrpSlot, &ApiGrpDbg, &ApiGrpVer, &ApiGrpTest, &ApiGrpSched, &ApiGrpCal, &ApiGrpChart, &ApiGrpChartCfg, &ApiGrpTemp, &ApiGrpRelay, &ApiGrpPress, &ApiGrpFlow, &ApiGrpInput, &ApiGrpFc,
#ifdef EEV_DEF
	&ApiGrpEev,
#endif
#ifdef SENSOR_IP
	&ApiGrpIpsens,
#endif
#ifdef USE_ELECTROMETER_SDM
	&ApiGrpSdm,
#endif
};

// ---- действия do ----
static int apiAct_power_on(ApiOut *o, const char *)  { HP.sendCommand(pSTART); api_put(o, "\"ok\""); return AE_OK; }
static int apiAct_power_off(ApiOut *o, const char *) { HP.sendCommand(pSTOP);  api_put(o, "\"ok\""); return AE_OK; }
static int apiAct_save_all(ApiOut *o, const char *)
{
	int32_t r = HP.save_all();     // профили, расписание и настройки новым форматом
	if(r == ERR_I2C_BUSY) return AE_BUSY;   // другое сохранение ещё идёт
	HP.save_motoHour();
	if(r < 0) return AE_FAIL;
	api_put_int(o, r);             // число записанных байт, как отвечал set_SAVE
	return AE_OK;
}
static int apiAct_reset_settings(ApiOut *o, const char *)
{
	if(HP.get_State() != pOFF_HP) return AE_BUSY;       // при работающем ТН команда не выполняется
	journal.jprintf("$RESET All HP settings . . .\n");
	uint16_t d = 0;
	writeEEPROM_I2C(I2C_SETTING_EEPROM, (byte*)&d, sizeof(d));
	HP.sendCommand(pRESET);                             // контроллер перезагрузится с заводскими настройками
	api_put(o, "\"ok\"");
	return AE_OK;
}
static int apiAct_net_apply(ApiOut *o, const char *)
{
	journal.jprintf("Update network setting . . .\r\n");
	HP.sendCommand(pNETWORK);         // применить сетевые настройки: сброс чипа Wiznet, связь пропадёт на несколько секунд
	api_put(o, "\"ok\"");
	return AE_OK;
}
static int apiAct_test_mail(ApiOut *o, const char *)
{
	char t[40]; t[0] = 0;
	if(!HP.message.setTestMail()) return AE_FAIL;            // письмо ставится в очередь, итог - ключ message.mail_ret
	HP.message.get_messageSetting((char*) mess_SMTP_RCPTTO, t);
	api_put_str(o, t);                                       // получатель
	return AE_OK;
}
static int apiAct_test_sms(ApiOut *o, const char *)
{
	char t[40]; t[0] = 0;
	if(!HP.message.setTestSMS()) return AE_FAIL;
	HP.message.get_messageSetting((char*) mess_SMS_PHONE, t);
	api_put_str(o, t);
	return AE_OK;
}
#ifdef MQTT
static int apiAct_test_narodmon(ApiOut *o, const char *)
{
	if(!HP.clMQTT.get_NarodMonUse()) return AE_NA;           // отправка выключена
	HP.clMQTT.setTestNARMON();                               // только взводим флаг: обработчик может крутиться в любом потоке
	api_put_str(o, HP.clMQTT.get_narodMon_server());
	return AE_OK;
}
#endif
static int apiAct_reset_terr(ApiOut *o, const char *) { HP.Reset_TempErrors(); api_put(o, "\"ok\""); return AE_OK; }   // счётчики и коды ошибок датчиков температуры
// Поиск датчиков OneWire. Результат - прежняя строка "номер:тип:t:адрес;..." одним JSON-значением: страница датчиков её разбирает.
// Сканирование пишет во временную область второй половины выходного буфера, затем строка копируется в ответ.
static int apiAct_onewire_scan(ApiOut *o, const char *)
{
	if(HP.get_State() == pWORK_HP) return AE_BUSY;            // при работающем ТН не сканируется
	char *tmp = o->p + o->cap / 2;
	if(o->len >= o->cap / 2) return AE_TOO_BIG;
	tmp[0] = 0;
	HP.scan_OneWire(tmp);
	api_put_str(o, tmp);
	return AE_OK;
}
#ifdef USE_ELECTROMETER_SDM
static int apiAct_sdm_program(ApiOut *o, const char *)
{
	if(!HP.dSDM.get_present()) return AE_NA;                   // счётчика нет
	HP.dSDM.progConnect();                                     // параметры связи записаны, счётчик надо сбросить
	api_put(o, "\"ok\"");
	return AE_OK;
}
static int apiAct_sdm_link(ApiOut *o, const char *)
{
	if(!HP.dSDM.get_present()) return AE_NA;
	HP.dSDM.uplinkSDM();                                       // проверка связи, сброс флага сбоя
	api_put(o, "\"ok\"");
	return AE_OK;
}
#endif
#ifdef EEV_DEF
static int apiAct_eev_zero(ApiOut *o, const char *) { api_put_int(o, HP.dEEV.set_zero()); return AE_OK; }   // закрыть ЭРВ с запасом, ответ - код ошибки (0 - успех)
#endif
// Инвертор: действия идут через очередь команд или прямой вызов
static int apiAct_fc_program(ApiOut *o, const char *)
{
	if(!HP.dFC.get_present()) return AE_NA;
	if(HP.get_modWork() != pOFF) return AE_BUSY;                // только на выключенном ТН
	HP.sendCommand(pPROG_FC);
	api_put(o, "\"ok\"");
	return AE_OK;
}
static int apiAct_reset_fc_errors(ApiOut *o, const char *)
{
	if(!HP.dFC.get_present()) return AE_NA;
#ifndef FC_ANALOG_CONTROL
	HP.dFC.reset_errorFC();
#endif
	api_put(o, "\"ok\"");
	return AE_OK;
}
static int apiAct_reset_fc(ApiOut *o, const char *)
{
	if(!HP.dFC.get_present()) return AE_NA;
	if(!HP.dFC.reset_FC()) return AE_FAIL;
#ifdef FC_VACON
	HP.dFC.set_nominal_power();                                 // после сброса номинальная мощность читается заново
	api_put_int(o, HP.dFC.nominal_power);
#else
	api_put(o, "\"ok\"");
#endif
	return AE_OK;
}
// Профили. Параметр p - номер слота с 0 (copy: «из:в»). Те же проверки, что делали прежние loadProfile/eraseProfile/copyProfile.
static int api_slot_arg(const char *p)
{
	if(p[0] < '0' || p[0] > '9' || (p[1] && (p[1] < '0' || p[1] > '9')) || (p[1] && p[2])) return -1;   // одна-две цифры
	int n = atoi(p);
	return n < I2C_PROFIL_NUM ? n : -1;
}
static int apiAct_profile_load(ApiOut *o, const char *p)
{
	int n = api_slot_arg(p);
	if(n < 0) return AE_BAD_VALUE;
	int32_t r = HP.Prof.load(n);
	if(r < 0) return AE_FAIL;
	api_put_int(o, r);
	return AE_OK;
}
static int apiAct_profile_save(ApiOut *o, const char *p)
{
	int n = api_slot_arg(p);
	if(n < 0) return AE_BAD_VALUE;
	int16_t r = HP.Prof.save(n);
	if(r < 0) return AE_FAIL;
	api_put_int(o, r);
	return AE_OK;
}
static int apiAct_profile_erase(ApiOut *o, const char *p)
{
	int n = api_slot_arg(p);
	if(n < 0) return AE_BAD_VALUE;
	if(n == HP.Prof.get_idProfile() || HP.Schdlr.UsesProfile(n)) return AE_CONFLICT;   // текущий профиль или есть в расписании
	int32_t r = HP.Prof.erase(n);
	HP.Prof.update_list(HP.Prof.get_idProfile());
	if(r < 0) return AE_FAIL;
	api_put_int(o, r);
	return AE_OK;
}
static int apiAct_profile_copy(ApiOut *o, const char *p)
{
	if(p[0] < '0' || p[0] > '9' || p[1] != ':' || p[2] < '0' || p[2] > '9' || p[3]) return AE_BAD_VALUE;
	int8_t r = HP.Prof.copy_slot(p[0] - '0', p[2] - '0');
	if(r < 0) return AE_BAD_VALUE;
	if(r == 0) return AE_CONFLICT;                          // в текущий профиль копировать нельзя
	api_put(o, "\"ok\"");
	return AE_OK;
}
static int apiAct_profile_info(ApiOut *o, const char *p)
{
	int n = api_slot_arg(p);
	if(n < 0) return AE_BAD_VALUE;
	if(o->cap - o->len < 400) return AE_TOO_BIG;
	char *t = o->p + o->len + 1;                            // текст до ~190 байт пишется в свободную часть буфера и экранируется на месте
	t[0] = 0;
	HP.Prof.get_info(t, n);
	api_str_inplace(o);
	return AE_OK;
}
static int apiAct_dbg_default(ApiOut *o, const char *) { dbgFlags = Dbg_Defaults(); api_put(o, "\"ok\""); return AE_OK; }   // все флаги в рабочий набор
// Расписание: очистка календарей, чтение из EEPROM и запись только расписания (общее сохранение - save.all)
static int apiAct_schedule_clear(ApiOut *o, const char *) { HP.Schdlr.web_set_param((char*) "Clear", (char*) ""); api_put(o, "\"ok\""); return AE_OK; }
static int apiAct_schedule_load(ApiOut *o, const char *) { int16_t r = HP.Schdlr.load(); if(r < 0) return AE_FAIL; api_put_int(o, r); return AE_OK; }
static int apiAct_save_schedule(ApiOut *o, const char *) { int8_t r = HP.Schdlr.save(); if(r < 0) return AE_FAIL; api_put_int(o, r); return AE_OK; }
static int apiAct_chart_clear(ApiOut *o, const char *) { HP.clearChart(); api_put(o, "\"ok\""); return AE_OK; }   // сбросить ряды в ОЗУ
// Статистика и подготовка к обновлению: счётчики, статистика, история на SD
static int api_save_stats(ApiOut *o)
{
	int16_t r;
	if((r = HP.save_motoHour()) == OK)
		if((r = Stats.SaveStats(1)) == OK)
			r = Stats.SaveHistory(1);
	if(r != OK) return AE_FAIL;
	api_put_int(o, r);
	return AE_OK;
}
static int apiAct_save_stats(ApiOut *o, const char *) { return api_save_stats(o); }
static int apiAct_save_prepare_update(ApiOut *o, const char *)
{
	if(HP.is_compressor_on()) return AE_BUSY;
	if(HP.dEEV.EEV != -1) HP.dEEV.set_EEV(HP.dEEV.get_maxEEV());      // ЭРВ в максимальное положение
	return api_save_stats(o);
}
static int apiAct_reset_stats(ApiOut *o, const char *)
{
	if(HP.get_modWork() != pOFF) return AE_BUSY;                        // форматирование статистики - только на выключенном ТН
	HP.sendCommand(pSFORMAT);
	api_put(o, "\"ok\"");
	return AE_OK;
}
// Служебные действия страниц «Система», «Задачи», «Журнал», «Modbus»
static int apiAct_reset_controller(ApiOut *o, const char *) { HP.sendCommand(pRESET); api_put(o, "\"ok\""); return AE_OK; }   // сначала штатный останов ТН: до минуты
static int apiAct_reset_journal(ApiOut *o, const char *) { journal.Format(); api_put(o, "\"ok\""); return AE_OK; }
static int apiAct_reset_counters(ApiOut *o, const char *) { journal.jprintf("$RESET Season Counters. . .\n"); HP.resetCount(false); api_put(o, "\"ok\""); return AE_OK; }
static int apiAct_reset_counters_all(ApiOut *o, const char *) { journal.jprintf("$RESET All Counters. . .\n"); HP.resetCount(true); api_put(o, "\"ok\""); return AE_OK; }
static int apiAct_wdt_reset(ApiOut *o, const char *)       // обнулить замеры наблюдателя; таблицу замеров читают отдельно (sys.wdt)
{
	if(o->cap - o->len < 1024) return AE_TOO_BIG;
	char *t = o->p + o->cap - 1024;                            // таблица пишется в свободный хвост буфера и отбрасывается
	t[0] = 0;
	Watchdog_StatWeb(t, 1);
	api_put(o, "\"ok\"");
	return AE_OK;
}
static int apiAct_tasks_reset(ApiOut *o, const char *)
{
#ifdef STAT_FREE_RTOS
	vTaskResetRunTimeCounters();
	api_put(o, "\"ok\"");
	return AE_OK;
#else
	return AE_NA;
#endif
}
// Регистры Modbus: p = «устройство:тип:адрес» (чтение) или «устройство:тип:адрес=значение» (запись). Типы: w, l, i, h, f, c.
// Ответ - {"v":число} или {"err":код ошибки Modbus} (например -2: нет ответа); неверный p - bad_value.
static int api_modbus(ApiOut *o, const char *p, bool wr)
{
	char *c1 = (char *) strchr(p, ':');
	if(!c1 || c1 == p || !c1[1] || c1[2] != ':' || !c1[3]) return AE_BAD_VALUE;
	uint8_t id = (uint8_t) atoi(p);
	char type = c1[1];
	char *av = c1 + 3;
	uint16_t par = (uint16_t) atoi(av);
	const char *val = 0;
	if(wr) { val = strchr(av, '='); if(!val || !val[1]) return AE_BAD_VALUE; val++; }
	if(id == FC_MODBUS_ADR && par) par--;                      // в документации инвертора нумерация регистров с 1, а в Modbus с 0
	int i = OK;
	if(wr) {
		float x = my_atof(val);
		if(x == ATOF_ERROR) return AE_BAD_VALUE;
		if(type == 'h') i = Modbus.writeHoldingRegisters16(id, par, (int32_t) x);
		else if(type == 'f') i = Modbus.writeHoldingRegistersFloat(id, par, (int32_t) x);
		else if(type == 'c') i = Modbus.writeSingleCoil(id, par, (int) x);
		else return AE_BAD_VALUE;
		_delay(MODBUS_TIME_TRANSMISION * 10);                  // пауза перед чтением обратно
	}
	if(i == OK) {
		uint16_t w16 = 0; int32_t l32 = 0; float f = 0;
		api_put(o, "{\"v\":");
		if(type == 'w') { if((i = Modbus.readInputRegisters16(id, par, &w16)) == OK) api_put_int(o, w16); }
		else if(type == 'l') { if((i = Modbus.readInputRegisters32(id, par, (uint32_t *)&l32)) == OK) api_put_int(o, l32); }
		else if(type == 'i') { if((i = Modbus.readInputRegistersFloat(id, par, &f)) == OK) api_put_dec(o, (int32_t) (f * 100), 100); }
		else if(type == 'h') { if((i = Modbus.readHoldingRegisters16(id, par, &w16)) == OK) api_put_int(o, w16); }
		else if(type == 'f') { if((i = Modbus.readHoldingRegistersFloat(id, par, &f)) == OK) api_put_dec(o, (int32_t) (f * 100), 100); }
		else if(type == 'c') { if((i = Modbus.readCoil(id, par, (boolean *)&w16)) == OK) api_put_int(o, w16 & 1); }
		else return AE_BAD_VALUE;
		if(i == OK) { api_putc(o, '}'); return AE_OK; }
		o->len -= 5; o->p[o->len] = 0;                         // откат заготовки «{"v":»
	}
	api_put(o, "{\"err\":"); api_put_int(o, i); api_putc(o, '}');
	return AE_OK;
}
static int apiAct_modbus_read(ApiOut *o, const char *p) { return api_modbus(o, p, false); }
static int apiAct_modbus_write(ApiOut *o, const char *p) { return api_modbus(o, p, true); }
static int apiAct_ntp_update(ApiOut *o, const char *) { HP.timeNTP = 0; api_put(o, "\"ok\""); return AE_OK; }     // 0 - обновить сразу, в ближайшем проходе основного цикла
static const ApiAction apiActions[] = {
	{ "power.on",       1, apiAct_power_on },
	{ "net.apply",      0, apiAct_net_apply },
	{ "ntp.update",     0, apiAct_ntp_update },
	{ "reset.terr",     0, apiAct_reset_terr },
	{ "chart.clear",    0, apiAct_chart_clear },
	{ "reset.controller", 0, apiAct_reset_controller },
	{ "reset.journal",  0, apiAct_reset_journal },
	{ "reset.counters", 0, apiAct_reset_counters },
	{ "reset.counters_all", 0, apiAct_reset_counters_all },
	{ "wdt.reset",      0, apiAct_wdt_reset },
	{ "tasks.reset",    0, apiAct_tasks_reset },
	{ "modbus.read",    0, apiAct_modbus_read },
	{ "modbus.write",   0, apiAct_modbus_write },
	{ "dbg.default",    0, apiAct_dbg_default },
	{ "onewire.scan",   0, apiAct_onewire_scan },
#ifdef EEV_DEF
	{ "eev.zero",       0, apiAct_eev_zero },
#endif
	{ "fc.program",     0, apiAct_fc_program },
	{ "reset.fc_errors", 0, apiAct_reset_fc_errors },
	{ "reset.fc",       0, apiAct_reset_fc },
#ifdef USE_ELECTROMETER_SDM
	{ "sdm.program",    0, apiAct_sdm_program },
	{ "sdm.link",       0, apiAct_sdm_link },
#endif
	{ "test.mail",      0, apiAct_test_mail },
	{ "test.sms",       0, apiAct_test_sms },
#ifdef MQTT
	{ "test.narodmon",  0, apiAct_test_narodmon },
#endif
	{ "power.off",      1, apiAct_power_off },
	{ "profile.load",   1, apiAct_profile_load },
	{ "profile.save",   0, apiAct_profile_save },
	{ "profile.erase",  0, apiAct_profile_erase },
	{ "profile.copy",   0, apiAct_profile_copy },
	{ "profile.info",   1, apiAct_profile_info },
	{ "save.all",       0, apiAct_save_all },
	{ "save.schedule",  0, apiAct_save_schedule },
	{ "save.stats",     0, apiAct_save_stats },
	{ "save.prepare_update", 0, apiAct_save_prepare_update },
	{ "reset.stats",    0, apiAct_reset_stats },
	{ "schedule.clear", 0, apiAct_schedule_clear },
	{ "schedule.load",  0, apiAct_schedule_load },
	{ "reset.settings", 0, apiAct_reset_settings },
};

// ---- ответ ----
// Малый ответ целиком (ошибки запроса): заголовок с настоящим Content-Length.
static uint8_t api_reply_small(uint8_t thread, const char *status, const char *body)
{
	char *ob = Socket[thread].outBuf;
	strcpy(ob, "HTTP/1.1 ");
	strcat(ob, status);
	strcat(ob, HEADER_API_TAIL);
	_itoa(strlen(body), ob);
	strcat(ob, "\r\n\r\n");
	strcat(ob, body);
	return sendBufferRTOS(thread, (byte*) ob, strlen(ob)) != 0;
}

// Версия протокола, прошивки и состава сборки: по ней морда понимает, что показывать.
static void api_ver(const ApiCtx *c, ApiOut *o)
{
	api_put(o, "{\"proto\":" ); api_put_int(o, API_PROTO);
	api_put(o, ",\"fw\":\"" VERSION "\",\"sch\":\""); api_put_hex32(o, api_schema(c));
	api_put(o, c->role == ROLE_ADMIN ? "\",\"role\":\"admin\"" : "\",\"role\":\"user\"");
	api_put(o, HP.safeNetwork ? ",\"safe_net\":1" : ",\"safe_net\":0");
	api_put(o, ",\"caps\":{\"cool\":"); api_put(o, HouseOnlyHeat ? "0" : "1");
	api_put(o,
#ifdef FC_ANALOG_CONTROL
		",\"fc_analog\":1"
#else
		",\"fc_analog\":0"
#endif
#ifdef FC_VACON
		",\"fc_type\":\"vacon\""
#else
		",\"fc_type\":\"omron\""
#endif
#ifdef TCOMPIN
		",\"tcompin\":1"
#else
		",\"tcompin\":0"
#endif
#ifdef EEV_PREFER_PERCENT
		",\"eev_units\":\"pct\""
#else
		",\"eev_units\":\"steps\""
#endif
#ifdef HTTP_TIME_REQUEST
		",\"time_proto\":\"TCP\""
#else
		",\"time_proto\":\"NTP\""
#endif
#ifdef USE_ELECTROMETER_SDM
		",\"sdm\":1"
#else
		",\"sdm\":0"
#endif
#ifdef MQTT
		",\"mqtt\":1"
#else
		",\"mqtt\":0"
#endif
		);
#ifdef SENSOR_IP
	api_put(o, ",\"ipsens\":"); api_put_int(o, IPNUMBER);
#else
	api_put(o, ",\"ipsens\":0");
#endif
	api_put(o, "}}");
}

// Запрос /api/<глагол>?<параметры>. Возвращает 0, если ответ не удалось отправить.
uint8_t api_dispatch(uint8_t thread)
{
	char *ob = Socket[thread].outBuf;
	const char *path = Socket[thread].inPtr + 4;     // после "api/"
	if(strlen(Socket[thread].inPtr) > W5200_MAX_LEN - 100)
		return api_reply_small(thread, "414 URI Too Long", "{\"e\":\"too_big\"}");
	const char *q = path;
	while(*q && *q != '?' && *q != '&') q++;
	uint8_t vl = (uint8_t)(q - path);
	if(*q == '?') q++;                               // дальше - параметры (при '&' сразу хвост авторизации)

	// Заголовок с фиксированной шириной поля длины (пробелы после двоеточия допустимы), тело сразу за ним.
	strcpy(ob, "HTTP/1.1 200 OK");
	strcat(ob, HEADER_API_TAIL);
	char *num = ob + strlen(ob);
	memset(num, ' ', 6);
	strcpy(num + 6, "\r\n\r\n");
	char *body = num + 6 + 4;

	ApiCtx c;
	c.groups = apiGroups; c.ng = sizeof(apiGroups) / sizeof(apiGroups[0]);
	c.acts = apiActions;  c.na = sizeof(apiActions) / sizeof(apiActions[0]);
	c.role = GETBIT(Socket[thread].flags, fUser) ? ROLE_USER : ROLE_ADMIN;
	c.running = HP.IsWorkingNow();
	bool wr = (vl == 3 && !strncmp(path, "set", 3)) || (vl == 2 && !strncmp(path, "do", 2));
	c.scratch_cap = wr ? API_SCRATCH_W : API_SCRATCH;
	c.scratch = ob + sizeof(Socket[thread].outBuf) - c.scratch_cap;
	ApiOut o;
	api_out_init(&o, body, (uint16_t)(c.scratch - body));

	int r = -1;
	if(vl == 3 && !strncmp(path, "get", 3)) r = api_get(&c, q, &o);
	else if(vl == 3 && !strncmp(path, "set", 3)) r = api_set(&c, q, &o);
	else if(vl == 2 && !strncmp(path, "do", 2)) r = api_do(&c, q, &o);
	else if(vl == 4 && !strncmp(path, "meta", 4)) r = api_meta(&c, q, &o);
	else if(vl == 5 && !strncmp(path, "chart", 5)) r = api_chart(q, &o);
	else if(vl == 3 && !strncmp(path, "ver", 3)) { api_ver(&c, &o); r = o.over ? -2 : 0; }

	if(r == -1) return api_reply_small(thread, "400 Bad Request", "{\"e\":\"bad_request\"}");
	if(r == -2) return api_reply_small(thread, "200 OK", "{\"d\":{},\"e\":{\"*\":\"too_big\"}}");
	// длина тела в поле Content-Length, выровнено вправо
	uint16_t n = o.len;
	for(int8_t i = 5; i >= 0 && n; i--) { num[i] = (char)('0' + n % 10); n /= 10; }
	if(!o.len) num[5] = '0';
	return sendBufferRTOS(thread, (byte*) ob, (uint16_t)(body - ob) + o.len) != 0;
}
