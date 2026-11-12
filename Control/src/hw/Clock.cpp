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
// ---- Работа со временем ----
#include "../../Constant.h"                       // Вся конфигурация и константы проекта Должен быть первым !!!!
#include <rtc_clock.h>                      // Работа со встроенными часами
#include "stdlib.h"
#include "Clock.h"
#include "../util/DateTime.h"     // форматирование, к которому сводится вывод
#include "../core/HeatPump.h"     // HP: адрес NTP-сервера, флаг обновления, uptime
#include "../log/Journal.h"      // journal
#include "Hardware.h"     // getTime_RtcI2C, rtcSAM3X8
#include "../net/Network.h"      // check_address, Udp, сокет W5200_SOCK_SYS
#include "../core/Rtos.h"         // семафор сети, Watchdog_AliveHere
#include "../core/ServiceWarnings.h"

static ServiceClockTrust clockTrust; // Подтверждение источника и контроль хода внутренних часов.

// Подтверждает установку часов по исправному RTC или принятому сетевому времени.
void ClockConfirmTime()
{
	const uint32_t now = Uptime_s();
	const uint32_t irq = irqSave();
	clockTrust.Confirm(now);
	irqRestore(irq);
}

// Учитывает введённое поле date и сохраняет подтверждённые часы во внешний RTC.
void ClockManualTimeSet(bool date)
{
	const uint32_t now = Uptime_s();
	const uint32_t irq = irqSave();
	const bool trusted = clockTrust.Manual(date, now);
	irqRestore(irq);
	if(trusted) saveTime_RtcI2C();
}

// Возвращает достоверность и подтверждает, что встроенные часы идут, в момент now.
bool ClockTimeTrusted(uint32_t now)
{
	uint16_t year;
	uint8_t month, day, hour, minute, second;
	const uint32_t irq = irqSave();
	rtcSAM3X8.get_date(nullptr, &day, &month, &year);
	rtcSAM3X8.get_time(&hour, &minute, &second);
	const bool valid = !(RTC->RTC_VER & (RTC_VER_NVTIM | RTC_VER_NVCAL)) &&
	                   ServiceDateValid(day, month, year) && hour < 24 && minute < 60 && second < 60;
	const uint32_t wall = valid ? rtcSAM3X8.unixtime() : 0;
	const bool trusted = clockTrust.Trusted(now, wall, valid);
	irqRestore(irq);
	return trusted;
}

// Установить внутренние часы. Интервалы защит и задержек считаются по времени работы (Uptime_s), от скачка часов
// не зависят и сдвига не требуют (раздел 6.5) - поправка только пишется в журнал. При старте часы ещё не выставлены,
// и поправка в десятки лет журналу не нужна.
static void setClockLogged(uint32_t secs)
{
	uint32_t oldTime = rtcSAM3X8.unixtime();
	rtcSAM3X8.set_clock(secs, TIME_ZONE);
	if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
		int32_t dTime = (int32_t)(rtcSAM3X8.unixtime() - oldTime);
		if(dTime) journal.jprintf(" Clock corrected by %d s\n", (int) dTime);
	}
}


// Объявление функций 
// ---- NTP ----
#define NTP_PACKET_SIZE 48                  // временная отметка NTP находится в первых 48 байтах сообщения
static byte packetBuffer[NTP_PACKET_SIZE+1];       // буфер, в котором будут храниться входящие и исходящие пакеты

// Установка времени системы (сначала читаем из i2c часов)
// Возвращает код ошибки
int8_t set_time(void)
{
	const uint32_t t = TimeToUnixTime(getTime_RtcI2C());
	if(t) journal.jprintf(" I2C RTC DS3232: %s\n", DecodeTimeDate(t, (char*) packetBuffer));
	journal.jprintf(" Init SAM3X8E RTC\n");
	rtcSAM3X8.init();                             // Запуск внутренних часов
	if(!(HP.get_updateNTP() && set_time_NTP())) { // Обновить время по NTP
		if(t) {
			rtcSAM3X8.set_clock(t);                // Установить внутренние часы по i2c
			ClockConfirmTime();
			{ char d_[NOW_DATE_LEN], t_[NOW_TIME_LEN]; journal.jprintf(" Time updated from I2C RTC: %s %s\n", NowDateToStr(d_), NowTimeToStr(t_)); }
		} else {
			journal.jprintf("Error read I2C RTC\n");
		}
	}
	return OK;
}

// Итог последней синхронизации времени (см. get_NTP_result в Clock.h)
static uint8_t  ntpResultSeq = 0;
static uint8_t  ntpResultCode = 0;
static char     ntpResultTime[9];
static void ntp_done(uint8_t code)
{
	ntpResultSeq++;
	ntpResultCode = code;
	NowTimeToStr(ntpResultTime);
}
char* get_NTP_result(char *ret)
{
	if(ntpResultSeq == 0) return ret;
	_itoa(ntpResultSeq, ret);
	strcat(ret, ";");
	_itoa(ntpResultCode, ret);
	strcat(ret, ";");
	return strcat(ret, ntpResultTime);
}

#ifdef HTTP_TIME_REQUEST
// Формат HTTP 1.0 GET запроса: "http://server:port/curr_time.csv"
// Ответ: "UTC time sec;"
static EthernetClient tTCP; // For get time
static char NTP_buffer[20];

// True - OK
boolean set_time_NTP(void)
{
	unsigned long secs = 0;
	int8_t flag = 0;
	IPAddress ip(0, 0, 0, 0);
	// Если запущен шедулер то захватываем семафор
	if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {
		ntp_done(NTP_RES_BUSY);
		return false;
	}  // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
	journal.jprintf("Update time from: %s\n", HP.get_serverNTP());
	if(check_address(HP.get_serverNTP(), ip) == 0) {
		SemaphoreGive(xWebThreadSemaphore);
		ntp_done(NTP_RES_DNS);
		return false;
	}  // DNS - ошибка выходим
	char *p = strchr(HP.get_serverNTP(), ':');
	uint16_t port = 80;
	if(p != NULL) port = atoi(p + 1);

	// 2. Посылка пакета
	for(uint8_t i = 0; i < NTP_REPEAT; i++)                                       // Делам 5 попыток получить время
	{
		Watchdog_AliveHere();                                            // Сбросить вачдог
		journal.jprintf(" Send request, wait...");
		flag = tTCP.connect(ip, port, W5200_SOCK_SYS);
		if(!flag) {
			journal.jprintf(" connect fail\n");
		} else {
			tTCP.write_buffer_flash((uint8_t *) &http_get_str1, sizeof(http_get_str1)-1);
			tTCP.write_buffer_flash((uint8_t *) HTTP_TIME_REQ, sizeof(HTTP_TIME_REQ)-1);
			tTCP.write_buffer_flash((uint8_t *) &http_get_str2, sizeof(http_get_str2)-1);
			tTCP.write_buffer((uint8_t *) HP.get_serverNTP(), strlen(HP.get_serverNTP()));
			tTCP.write_buffer_flash((uint8_t *) &http_get_str3, sizeof(http_get_str3)-1);
			if(tTCP.write((const uint8_t *)NULL, (size_t)0) == 0) {
				journal.jprintf(" send error\n");
			} else {
				uint8_t wait = 20;
				bool locked = true;      // замок сети у нас; в цикле он отпускается и может не вернуться
				flag = 0;
				while(wait--) { // ожидание ответа
					SemaphoreGive(xWebThreadSemaphore);
					_delay(100);
					if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE) { locked = false; break; } // SEM-OK: ниже сразу выход без замка и без обращения к сокету
					if(tTCP.available()) {
						flag = 1;
						break;
					}
				}
				if(!locked) {            // замок не вернули: шина SPI чужая, сокет закроет checkSockStatus, отдавать нечего
					ntp_done(NTP_RES_BUSY);
					return false;  // SEM-OK: замок потерян, отдавать нечего
				}
				if(flag > 0) { // Ответ получен, формат: "<время UTC>;"
					if(tTCP.read((uint8_t *)&NTP_buffer, sizeof(http_key_ok1)-1) == sizeof(http_key_ok1)-1) {
						if(memcmp(&NTP_buffer, &http_key_ok1, sizeof(http_key_ok1)-1) == 0) {
							if(tTCP.read((uint8_t *)&NTP_buffer, 3 + sizeof(http_key_ok2)-1) == 3 + sizeof(http_key_ok2)-1) { // HTTP/
								if(memcmp((uint8_t *)&NTP_buffer + 3, &http_key_ok2, sizeof(http_key_ok2)-1) == 0) {	// 200 OK
									flag = -6;
									while(tTCP.available()) {
										if(tTCP.read() == '\r' && tTCP.read() == '\n' && tTCP.read() == '\r' && tTCP.read() == '\n') { // тело
											memset(&NTP_buffer, 0, sizeof(NTP_buffer));
											tTCP.read((uint8_t *)&NTP_buffer, sizeof(NTP_buffer));
											uint32_t hs = 0;
											if(Http_ParseTime(NTP_buffer, sizeof(NTP_buffer), &hs) == NTP_PKT_OK) { secs = hs; flag = 1; }
											else flag = -7;           // нет «;», не число или год вне 2024...2099
											break;
										}
									}
								} else flag = -5;
							} else flag = -4;
						} else flag = -3;
					} else flag = -2;
				} else flag = -1;
			}
			tTCP.stop();
			if(flag > 0) break; else journal.jprintf(" Error %d\n", flag);
		}
	}
	if(flag > 0 && !Ntp_JumpOk(rtcSAM3X8.unixtime(), secs + TIME_ZONE * 3600UL)) {   // как у NTP: скачок больше суток от случайного ответа календарь не трогает
		journal.jprintf("$NTP: time jump %d s rejected (limit %d s), set the clock manually\n", (int)(secs + TIME_ZONE * 3600UL - rtcSAM3X8.unixtime()), (int) NTP_MAX_JUMP_S);
		flag = -8;
	}
	if(flag > 0) {  // Обновление времени если оно получено
		unsigned long lt = rtcSAM3X8.unixtime();
		setClockLogged(secs);
		// обновились, можно и часы i2c обновить
		ClockConfirmTime();
		saveTime_RtcI2C();
		{ char d_[NOW_DATE_LEN], t_[NOW_TIME_LEN]; journal.jprintf("OK\n Set time from server: %s %s", NowDateToStr(d_), NowTimeToStr(t_)); }
		journal.jprintf(", was: %02lu:%02lu:%02lu\n", lt % 86400L / 3600, lt % 3600 / 60, lt % 60);
	}
	SemaphoreGive(xWebThreadSemaphore);
	// flag: 1 - получено, 0 - не соединились, -1 - нет ответа, -2...-8 - ответ неверный (разбор заголовка и тела; -8 - скачок часов больше суток)
	ntp_done(flag > 0 ? NTP_RES_OK : flag < -1 ? NTP_RES_BADANSWER : NTP_RES_NOANSWER);
	return flag > 0;
}

#else

// send an NTP request to the time server at the given address
// true если нет ошибок
static boolean sendNTPpacket(IPAddress &ip)
{
	// SEM-BORROW: xWebThreadSemaphore - вызывается из set_time_NTP, замок сети там взят
	memset(packetBuffer, 0, NTP_PACKET_SIZE);
	// Initialize values needed to form NTP request
	// (see URL above for details on the packets)
	packetBuffer[0] = 0b11100011;   // LI, Version, Mode
	packetBuffer[1] = 0;            // Stratum, or type of clock
	packetBuffer[2] = 6;            // Polling Interval
	packetBuffer[3] = 0xEC;         // Peer Clock Precision
	// 8 bytes of zero for Root Delay & Root Dispersion
	packetBuffer[12] = 49;
	packetBuffer[13] = 0x4E;
	packetBuffer[14] = 49;
	packetBuffer[15] = 52;
	// all NTP fields have been given values, now
	// you can send a packet requesting a timestamp:
	if(Udp.beginPacket(ip, NTP_PORT, W5200_SOCK_SYS) == 1) {
		Udp.write(packetBuffer, NTP_PACKET_SIZE);
		if(Udp.endPacket() != 1) {
			journal.jprintf("Send packet NTP error\n");
			return false;
		}
	} else {
		journal.jprintf("Socket error\n");
		return false;
	}
	return true;
}

// Функция обновления времени по ntp вызывается из задачи или кнопкой. true - если время обновлено
// Запрос времени от NTP сервера, возвращает время как long
boolean set_time_NTP(void)
{
	uint32_t secs = 0;
	boolean flag = false;
	uint8_t bad = NTP_PKT_OK;                                    // последняя причина отказа пакета
	IPAddress ip(0, 0, 0, 0);

	journal.jprintf_time("Update time from NTP server: %s\n", HP.get_serverNTP());
	//1. Установить адрес  не забываем работаем через один сокет, операции строго последовательные,иначе настройки сбиваются
	Watchdog_AliveHere();                                        // Сбросить вачдог  при ошибке долго ждем

	// Если запущен шедулер то захватываем семафор
	if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {
		ntp_done(NTP_RES_BUSY);
		return false;
	}  // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
	// DNS запрос для определения адреса

	if(check_address(HP.get_serverNTP(), ip) == 0) {
		SemaphoreGive(xWebThreadSemaphore);
		ntp_done(NTP_RES_DNS);
		return false;
	}  // DNS - ошибка выходим

	// 2. Посылка пакета
	if(!Udp.begin(NTP_LOCAL_PORT, W5200_SOCK_SYS)) {
		journal.jprintf(" UDP fail\n");
		SemaphoreGive(xWebThreadSemaphore);
		ntp_done(NTP_RES_SOCKET);
		return false;
	}
	for(uint8_t i = 0; i < NTP_REPEAT; i++)                                       // Делам 5 попыток получить время
	{
		Watchdog_AliveHere();                                            // Сбросить вачдог
		journal.jprintf(" Send packet NTP, wait . . .\n");
		flag = sendNTPpacket(ip);
		_delay(NTP_REPEAT_TIME);                                             // Ждем, чтобы увидеть, доступен ли ответ:
		if(flag) {
			flag = false;
			if(Udp.parsePacket()) {
				// Пакет получен: читаем и проверяем. Принимается только настоящий ответ сервера (длина, mode, stratum, LI, отметка,
				// правдоподобный год), см. Ntp_Parse. Чужой пакет не прерывает опрос: ждём следующей попытки.
				memset(packetBuffer, 0, NTP_PACKET_SIZE);
				int got = Udp.read(packetBuffer, NTP_PACKET_SIZE);   // считываем содержимое пакета в буфер
				bad = Ntp_Parse(packetBuffer, got, &secs);
				if(bad == NTP_PKT_OK) flag = true;                   // Время получили
				else journal.jprintf(" NTP bad packet (%d bytes, reason %d)\n", got, (int) bad);
			}
		}
		if(flag) break;                                      // ответ получен выходим
	} // for
	Udp.stop();
	// Внутренние часы хранят МЕСТНОЕ время (set_clock добавляет пояс), а secs - UTC: сравниваем в одной шкале.
	const uint32_t secsLocal = secs + TIME_ZONE * 3600UL;
	if(flag && !Ntp_JumpOk(rtcSAM3X8.unixtime(), secsLocal)) {   // правдоподобный пакет, но скачок часов слишком велик: календарь ТН не трогаем
		journal.jprintf("$NTP: time jump %d s rejected (limit %d s), set the clock manually\n", (int)(secsLocal - rtcSAM3X8.unixtime()), (int) NTP_MAX_JUMP_S);
		flag = false;
		bad = NTP_PKT_RANGE;
	}
	if(flag) {  // Обновление времени если оно получено
		setClockLogged(secs);                   // обновить внутренние часы
		// обновились, можно и часы i2c обновить
		ClockConfirmTime();
		saveTime_RtcI2C();
		journal.jprintf_date("Set time from NTP server");
	} else {
		journal.jprintf_date("ERROR update time from NTP server!");
	}
	SemaphoreGive(xWebThreadSemaphore);
	ntp_done(flag ? NTP_RES_OK : bad != NTP_PKT_OK ? NTP_RES_BADANSWER : NTP_RES_NOANSWER);

	return flag;
}

#endif // HTTP_TIME_REQUEST

// Выбор формата uptime. Обязан быть здесь, а не в DateTime.cpp: SENSOR_IP
// приходит из Config.h, которого модуль не видит, и такой #ifdef там молча
// выбрал бы не ту ветку (ровно так ломался Freon на R4WAY). Форматы не
// взаимозаменяемы: парсер удалённого датчика разбирает только свой.
char* TimeIntervalToStr(uint32_t idt, char *ret, uint8_t fSec)
{
#ifdef SENSOR_IP
	(void) fSec;                         // у формата удалённого датчика секунды всегда
	return TimeIntervalToStrRemote(idt, ret);
#else
	return TimeIntervalToStrCompact(idt, ret, fSec);
#endif
}

//  Получить текущее время (XX:XX:XX) в buf (NOW_TIME_LEN байт)
char* NowTimeToStr(char *buf)
{
	uint32_t tm = RTC->RTC_TIMR;
	return FormatTimeHMS(buf, SAM_RTC_HOUR(tm), SAM_RTC_MIN(tm), SAM_RTC_SEC(tm));
}

//  Получить текущую дату (XX.XX.XXXX) в buf (NOW_DATE_LEN байт)
char* NowDateToStr(char *buf)
{
	uint32_t dt = RTC->RTC_CALR;
	return FormatDateDMY(buf, SAM_RTC_DAYS(dt), SAM_RTC_MONTH(dt), SAM_RTC_YEARS(dt));
}


// Перевод формата времени Time в формат Unix (секунды с 1970 года)
#define SEC_1970_TO_2000      946684800
static  const uint8_t dim[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
unsigned long TimeToUnixTime(tmElements_t *t) //[V]*
{
	if(!ServiceDateValid(t->Day, t->Month, t->Year) || t->Hour > 23 ||
	   t->Minute > 59 || t->Second > 59) return 0;
	uint16_t dc;
	dc = t->Day;
	for(uint8_t i = 0; i < (t->Month - 1); i++)	dc += dim[i];
	if((t->Month > 2) && (((t->Year - 2000) % 4) == 0)) ++dc;
	dc = dc + (365 * (t->Year - 2000)) + (((t->Year - 2000) + 3) / 4) - 1;
	return ((((((dc * 24L) + t->Hour) * 60) + t->Minute) * 60) + t->Second) + SEC_1970_TO_2000;
}
