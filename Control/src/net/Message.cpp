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
// Класс для уведомлений
#include "Message.h"
#include "../core/HeatPump.h"     // HP, счётчики ошибок
#include "../log/Journal.h"      // journal
#include "../hw/Hardware.h"     // Modbus, IPAddress2String, ResetCause
#include "Network.h"      // sendPacketRTOS, check_address, замки SPI
#include "../core/Rtos.h"         // _delay, семафоры
#include "../util/Util.h"         // _itoa/_ftoa, base64, calc_crc16
#include "../util/DateTime.h"     // NowTimeToStr, TimeIntervalToStr
#include "../log/Statistics.h"   // Stats для тела письма
#include "WebShared.h"    // Socket[] — буферы письма живут в outBuf
#include "ParamRegistry.h" // таблица параметров веб-API (внизу файла)
#include <stddef.h>
void get_mailState(char *buf, uint16_t buf_size);   // определена ниже в этом файле
#define JOURNAL(...) if(DBG_ON(DBG_WFULL)) journal.jprintf(__VA_ARGS__)

// Инициализация
// Уведомления по умолчанию настройка на gmail без шифрования
// параметр - номер потока сервера в котором запускается отправка уведомлений
void Message::initMessage(uint8_t web_task)
{
  // инициализации рабочих буферов MQTT
  tempBuf=Socket[web_task].outBuf+0;
  tempBuf[0]=0x00; // Стереть строку
  retMail=Socket[web_task].outBuf+LEN_TEMPBUF;
  retMail[0]=0x00;
  retSMS=Socket[web_task].outBuf+LEN_TEMPBUF+LEN_RETMAIL;
  retSMS[0]=0x00;
  retTest[0]=0x00;
  mailBuf=Socket[web_task].outBuf+MAILBUF_OFFSET;
  mailBuf[0]=0x00;
  txBuf=Socket[web_task].outBuf+MAILBUF_OFFSET+LEN_MAILBUF;
  txLen=0;
  smtpCaps=0;
  smtpCode=0;
  smtpSemLost=false;
  sendCh=0;
  sendRetry=0;
  sendRetryTime=0;
  // Инициализация переменных
  IPAddress zeroIP(0, 0, 0, 0);
  dnsUpadateSMS = false;                                                           // Флаг необходимости обновления через dns IP адреса для смс
  dnsUpadateSMTP = false;                                                          // Флаг необходимости обновления через dns IP адреса для smtp
  NotifyQueue_Init(&nq);                                                           // отправлять нечего
  NotifyRecent_Init(&recent);

  SETBIT0(messageSetting.flags, fMail );                                           // флаг уведомления скидывать на почту
  SETBIT1(messageSetting.flags, fMailAUTH);                                        // флаг необходимости авторизации на почтовом сервере
  SETBIT1(messageSetting.flags, fMailInfo);                                        // флаг необходимости добавления в письмо информации о состоянии ТН
  SETBIT0(messageSetting.flags, fSMS);                                             // флаг уведомления скидывать на СМС
  SETBIT1(messageSetting.flags, fMessageReset);                                    // флаг уведомления Сброс
  SETBIT1(messageSetting.flags, fMessageError);                                    // флаг уведомления Ошибка
  SETBIT1(messageSetting.flags, fMessageLife);                                     // флаг уведомления Сигнал жизни
  SETBIT1(messageSetting.flags, fMessageTemp);                                     // флаг уведомления Достижение граничной температуры
  SETBIT1(messageSetting.flags, fMessageSD);                                       // флаг уведомления "Проблемы с sd картой"
  SETBIT1(messageSetting.flags, fMessageWarning);                                  // флаг уведомления "Прочие уведомления"


  strcpy(messageSetting.smtp_server, "smtp-devices.yandex.ru");                    // Адрес сервера без SSL/TSL
  messageSetting.smtp_serverIP = zeroIP;                                           // сделать адрес 0.0.0.0
  messageSetting.smtp_port = 25;                                                   // Адрес порта сервера
  strcpy(messageSetting.smtp_login, "login");                                      // логин сервера если включена авторизация
  strcpy(messageSetting.smtp_password, "password");                                // пароль сервера если включена авторизация
  strcpy(messageSetting.smtp_MailTo, "MK@home");                                   // адрес отправителя
  strcpy(messageSetting.smtp_RCPTTo, "");                                          // адрес получателя

  messageSetting.sms_service = pSMS_RU;                                            // Сервис отправки смс по умолчанию sms.ru
  messageSetting.sms_serviceIP = zeroIP;                                           // сделать адрес 0.0.0.0
  strcpy(messageSetting.sms_phone, "1234567890");                                  // телефон куда отправляется смс
  strcpy(messageSetting.sms_p1, "api_id");                                         // первый параметр для отправки смс
  strcpy(messageSetting.sms_p2, "none");                                           // второй параметр для отправки смс

  messageSetting.mTIN = 1000;                                                      // Критическая температура в доме (если меньше то генерится уведомление)
  messageSetting.mTBOILER = 500;                                                   // Критическая температура бойлера (если меньше то генерится уведомление)
  messageSetting.mTCOMP = 7000;                                                    // Критическая температура компрессора (если больше то генерится уведомление)

  // установка содержания уведомлений по умолчанию
  WorkFlags = 0;
}

// Адрес сервиса SMS - один на все места: DNS, Host запроса, журнал (Н-22)
static const char *sms_host(uint8_t service)
{
	switch(service) {
	case pSMSC_RU: return ADR_SMSC_RU;
	case pSMSC_UA: return ADR_SMSC_UA;
	case pSMSCLUB: return ADR_SMSCLUB;
	default:       return ADR_SMS_RU;
	}
}

// Обновление IP адресов серверов через dns
// Возврат - флаг использования дополнительного сокета и времени (нужен для разгрузки 0 потока сервера)
// возвращает true обновление не было (можно нагружать), false - прошло обновление или ошибка (нагружать не надо до следующего цикла)
boolean Message::dnsUpdate() // запускается 0 потоке вебсервера
{
	boolean ret = true; // по умолчанию преобразование не было
	if(xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
		dnsUpadateSMTP = dnsUpadateSMS = true;
		Watchdog_AliveHere();
	}
	if(dnsUpadateSMTP) //надо обновлятся
	{
		if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) return false; // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
		check_address(messageSetting.smtp_server, messageSetting.smtp_serverIP);   // Получить адрес IP через DNS При не удаче возвращается 0, при удаче: 1 - IP на входе (были цифры, DNS не нужен), 2 - был запрос к DNS и адрес получен
		dnsUpadateSMTP = false;
		if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) SemaphoreGive(xWebThreadSemaphore);
		ret = false;  // вызов обновления был
	}
	if(dnsUpadateSMS) //надо обновлятся
	{
		if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
			if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) return false; // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
		} else Watchdog_AliveHere();
		strcpy(tempBuf, sms_host(messageSetting.sms_service));   // check_address нужна строка в ОЗУ
		check_address(tempBuf, messageSetting.sms_serviceIP);// При не удаче возвращается 0, при удаче: 1 - IP на входе (были цифры, DNS не нужен), 2 - был запрос к DNS и адрес получен
		dnsUpadateSMS = false;
		if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) SemaphoreGive(xWebThreadSemaphore);
		ret = false;  // вызов обновления был
	}
	return ret;
}

// Есть ли в строке управляющие символы.
// Веб-сервер вызывает urldecode() ДО разбора команд, поэтому "%0D%0A" в запросе превращается
// в настоящие CR/LF. Попав в адрес, они разорвут команду SMTP надвое ("RCPT TO:<a>" + чужая
// команда), а в заголовках письма пустая строка оборвет их досрочно и остаток уйдет в тело.
static boolean mail_has_ctrl(const char *s)
{
	for(; *s; s++) if((uint8_t)*s < 0x20 || (uint8_t)*s == 0x7F) return true;
	return false;
}

// Получить параметр Уведомления по имени var, результат ДОБАВЛЯЕТСЯ в строку ret
char* Message::get_messageSetting(char *var, char *ret)
{
	if(strcmp(var, mess_MAIL) == 0) {
		if(GETBIT(messageSetting.flags, fMail)) return strcat(ret, (char*) cOne);
		else return strcat(ret, (char*) cZero);
	} else if(strcmp(var, mess_MAIL_AUTH) == 0) {
		if(GETBIT(messageSetting.flags, fMailAUTH)) return strcat(ret, (char*) cOne);
		else return strcat(ret, (char*) cZero);
	} else if(strcmp(var, mess_MAIL_INFO) == 0) {
		if(GETBIT(messageSetting.flags, fMailInfo)) return strcat(ret, (char*) cOne);
		else return strcat(ret, (char*) cZero);
	} else if(strcmp(var, mess_SMS) == 0) {
		if(GETBIT(messageSetting.flags, fSMS)) return strcat(ret, (char*) cOne);
		else return strcat(ret, (char*) cZero);
	} else if(strcmp(var, mess_MESS_RESET) == 0) {
		if(GETBIT(messageSetting.flags, fMessageReset)) return strcat(ret, (char*) cOne);
		else return strcat(ret, (char*) cZero);
	} else if(strcmp(var, mess_MESS_ERROR) == 0) {
		if(GETBIT(messageSetting.flags, fMessageError)) return strcat(ret, (char*) cOne);
		else return strcat(ret, (char*) cZero);
	} else if(strcmp(var, mess_MESS_LIFE) == 0) {
		if(GETBIT(messageSetting.flags, fMessageLife)) return strcat(ret, (char*) cOne);
		else return strcat(ret, (char*) cZero);
	} else if(strcmp(var, mess_MESS_TEMP) == 0) {
		if(GETBIT(messageSetting.flags, fMessageTemp)) return strcat(ret, (char*) cOne);
		else return strcat(ret, (char*) cZero);
	} else if(strcmp(var, mess_MESS_SD) == 0) {
		if(GETBIT(messageSetting.flags, fMessageSD)) return strcat(ret, (char*) cOne);
		else return strcat(ret, (char*) cZero);
	} else if(strcmp(var, mess_MESS_WARNING) == 0) {
		if(GETBIT(messageSetting.flags, fMessageWarning)) return strcat(ret, (char*) cOne);
		else return strcat(ret, (char*) cZero);
	} else if(strcmp(var, mess_SMTP_SERVER) == 0) {
		return strcat(ret, messageSetting.smtp_server);
	} else if(strcmp(var, mess_SMTP_IP) == 0) {
		return strcat(ret, IPAddress2String(messageSetting.smtp_serverIP));
	} else if(strcmp(var, mess_SMTP_PORT) == 0) {
		return _itoa(messageSetting.smtp_port, ret);
	} else if(strcmp(var, mess_SMTP_LOGIN) == 0) {
		return strcat(ret, messageSetting.smtp_login);
	} else if(strcmp(var, mess_SMTP_PASS) == 0) {
		return strcat(ret, messageSetting.smtp_password);
	} else if(strcmp(var, mess_SMTP_MAILTO) == 0) {
		return strcat(ret, messageSetting.smtp_MailTo);
	} else if(strcmp(var, mess_SMTP_RCPTTO) == 0) {
		return strcat(ret, messageSetting.smtp_RCPTTo);
	} else if(strcmp(var, mess_SMS_SERVICE) == 0) {
		return web_fill_tag_select(ret, SMS_SERVICE_WEB_SELECT, messageSetting.sms_service);
	} else if(strcmp(var, mess_SMS_IP) == 0) {
		return strcat(ret, IPAddress2String(messageSetting.sms_serviceIP));
	} else if(strcmp(var, mess_SMS_PHONE) == 0) {
		return strcat(ret, messageSetting.sms_phone);
	} else if(strcmp(var, mess_SMS_P1) == 0) { // первый параметр для отправки смс
		return strcat(ret, messageSetting.sms_p1);
	} else if(strcmp(var, mess_SMS_P2) == 0) { // второй параметр для отправки смс
		return strcat(ret, messageSetting.sms_p2);
	} else if(strcmp(var, mess_SMS_NAMEP1) == 0) {
		switch(messageSetting.sms_service)  // описание первого параметра для отправки смс
		{
		case pSMS_RU:
			return strcat(ret, (char*) "API ID");
			break;
		default:
			return strcat(ret, (char*) "Login");
			break; // Этого не должно быть, но если будет то установить по умолчанию
		}
	} else if(strcmp(var, mess_SMS_NAMEP2) == 0) {
		switch(messageSetting.sms_service)   // описание второго параметра для отправки смс
		{
		case pSMS_RU:
			return strcat(ret, (char*) "none");
			break;
		default:
			return strcat(ret, (char*) "Password");
			break; // Этого не должно быть, но если будет то установить по умолчанию
		}
	} else if(strcmp(var, mess_MESS_TIN) == 0) {
		_dtoa(ret, messageSetting.mTIN, 2);
		return ret;
	} else if(strcmp(var, mess_MESS_TBOILER) == 0) {
		_dtoa(ret, messageSetting.mTBOILER, 2);
		return ret;
	} else if(strcmp(var, mess_MESS_TCOMP) == 0) {
		_dtoa(ret, messageSetting.mTCOMP, 2);
		return ret;
	} else if(strcmp(var, mess_MAIL_RET) == 0) {
		if(busy()) return strcat(ret, (char*) "wait response");                // В зависимости готов ответ или нет
		else return strcat(ret, retTest);
	} else if(strcmp(var, mess_SMS_RET) == 0) {
		if(busy()) return strcat(ret, (char*) "wait response");                // В зависимости готов ответ или нет
		else return strcat(ret, retTest);
	} else return strcat(ret, (char*) cInvalid);

}
// Записать настройки в eeprom i2c на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
int32_t Message::save(int32_t adr)
{

  if (writeEEPROM_I2C(adr, (byte*)&messageSetting, sizeof(messageSetting))) {
    set_Error(ERR_SAVE_EEPROM, (char*)nameHeatPump);
    return ERR_SAVE_EEPROM;
  }  adr = adr + sizeof(messageSetting);       // записать параметры Уведомлений
  return adr;
}
// Считать настройки из eeprom i2c на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
int32_t Message::load(int32_t adr)
{
  if (readEEPROM_I2C(adr, (byte*)&messageSetting, sizeof(messageSetting))) {
    set_Error(ERR_LOAD_EEPROM, (char*)nameHeatPump);
    return ERR_LOAD_EEPROM;
  }  adr = adr + sizeof(messageSetting);       // прочитать параметры Уведомлений
  return adr;
}
// Считать настройки из буфера на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
int32_t Message::loadFromBuf(int32_t adr, byte *buf)
{
  memcpy((byte*)&messageSetting, buf + adr, sizeof(messageSetting)); adr = adr + sizeof(messageSetting);
  return adr;
}
// Рассчитать контрольную сумму для данных на входе входная сумма на выходе новая
uint16_t Message::get_crc16(uint16_t crc)
{
  crc = calc_crc16((unsigned char*)&messageSetting, sizeof(messageSetting), crc); // CRC16 настройки уведомлений
  return crc;
}

// ---- ТРАНСПОРТ SMTP ----
// Все команды и все строки письма завершаются SMTP_CRLF. Общая константа проекта cStrEnd ("\n")
// здесь неприменима: RFC 5321 требует именно CR+LF, а после закрытия CVE-2023-51764 (SMTP smuggling)
// современные серверы на одиночном переводе строки не распознают конец письма и рвут соединение.

// Скопировать ответ сервера в retMail с гарантированным завершающим нулем
void Message::setRetMail(const char *s)
{
	strncpy(retMail, s, LEN_RETMAIL - 1);
	retMail[LEN_RETMAIL - 1] = '\0';   // strncpy не терминирует строку, если источник не короче лимита
}

// Собрать ответ на тестовое уведомление: <prefix><значение параметра var>\nОтвет: <answer>
// Сборка идёт с контролем границ: цепочка strcpy/strcat при длинном ответе сервера
// вылезла бы за retTest[LEN_RETTEST] и затёрла бы соседние поля класса.
void Message::setRetTest(const char *prefix, const char *var, const char *answer)
{
	uint16_t n;
	m_snprintf(retTest, sizeof(retTest), "%s", prefix);
	get_messageSetting((char*) var, retTest);   // адрес получателя / номер телефона ДОБАВЛЯЕТСЯ в строку
	retTest[sizeof(retTest) - 1] = '\0';
	n = strlen(retTest);
	m_snprintf(retTest + n, sizeof(retTest) - n, "\nОтвет: %s", answer);
}

// Сбросить накопленный исходящий поток в сокет.
// Письмо копится в txBuf и уходит крупными кусками: EthernetClient::write() сразу выполняет
// Sock_SEND с активным ожиданием SEND_OK, поэтому построчная отправка - это десятки блокирующих
// операций и десятки TCP-пакетов на одно письмо.
// Пишем кусками не больше SMTP_MAX_SEND: send() в библиотеке обрезает запись по W5100.SSIZE (2048)
// и молча теряет хвост, а EthernetClient::write() все равно возвращает полный размер.
void Message::smtpFlush()
{
	// SEM-BORROW: xWebThreadSemaphore - вызывается из sendMail, замок сети там взят
	uint16_t pos = 0, chunk;
	while(pos < txLen) {
		chunk = txLen - pos;
		if(chunk > SMTP_MAX_SEND) chunk = SMTP_MAX_SEND;
		if(clientMessage.write((const uint8_t*) txBuf + pos, chunk) == 0) break;  // соединение потеряно
		pos += chunk;
	}
	txLen = 0;
}

// Вывести одну строку тела письма. ЕДИНСТВЕННАЯ точка вывода тела, отсюда все гарантии:
//  - удвоение ведущей точки (RFC 5321 4.5.2), иначе такая строка оборвет письмо;
//  - разрезание строк длиннее SMTP_MAX_LINE (предел RFC - 998 байт без CRLF), по границе utf-8;
//  - завершение ровно CR+LF, благодаря чему завершающая точка всегда дает CRLF.CRLF
void Message::smtpWriteBody(const char *line)
{
	if(line == NULL) line = "";
	smtpWriteBodyLen(line, strlen(line));
}

// То же, но длина задана явно: строка может быть куском большего буфера и не завершаться нулем.
// Входной буфер НЕ изменяется, поэтому допустимы и строковые константы.
void Message::smtpWriteBodyLen(const char *line, uint16_t len)
{
	uint16_t part;
	if(line == NULL) { line = ""; len = 0; }
	do {
		part = len;
		if(part > SMTP_MAX_LINE) {
			part = SMTP_MAX_LINE;
			while(part > 1 && (line[part] & 0xC0) == 0x80) part--;  // не рвать многобайтный символ utf-8
		}
		if(txLen + part + 3 > LEN_TXBUF) smtpFlush();              // +3: удвоенная точка и CRLF
		if(part && line[0] == '.') txBuf[txLen++] = '.';
		memcpy(txBuf + txLen, line, part); txLen += part;
		txBuf[txLen++] = '\r'; txBuf[txLen++] = '\n';
		if(txLen >= SMTP_FLUSH_AT) smtpFlush();
		line += part; len -= part;
	} while(len);
}

// Разобрать готовый многострочный блок (разделители "\n" или "\r\n") и вывести его построчно.
// Нужен для вывода get_Counts(), который формирует весь текст сразу в буфере.
void Message::smtpWriteBlock(const char *block)
{
	const char *p = block, *e;
	uint16_t len;
	while(*p) {
		e = strchr(p, '\n');
		if(e == NULL) { smtpWriteBodyLen(p, strlen(p)); break; }
		len = e - p;
		if(len && *(e - 1) == '\r') len--;      // отбросить CR перед LF
		smtpWriteBodyLen(p, len);
		p = e + 1;
	}
}

// Прочитать одну строку ответа сервера (до LF), хвостовой CR срезается.
// На время паузы семафор отдается, иначе на секундных таймаутах встанет весь веб-сервер.
// Опрос сначала частый (реле в локальной сети отвечает за единицы мсек), потом редкий -
// иначе каждый обмен стоил бы полного шага ожидания, а их за сеанс около десятка.
boolean Message::smtpReadLine(char *buf, uint16_t size, uint32_t timeout)
{
	// SEM-BORROW: xWebThreadSemaphore - замок сети принадлежит вызывающему (sendMail), на время ожидания строки он отпускается и берётся обратно
	uint16_t pos = 0, poll = 0;
	uint32_t startTick = GetTickCount();
	for(;;) {
		while(clientMessage.available()) {
			int c = clientMessage.read();
			if(c < 0) break;
			if(c == '\n') {
				if(pos && buf[pos - 1] == '\r') pos--;
				buf[pos] = '\0';
				return true;
			}
			if(pos < size - 1) buf[pos++] = (char) c;   // слишком длинную строку молча обрезаем
		}
		if(!clientMessage.connected()) {                // сервер закрыл сессию, данных больше не будет
			buf[pos] = '\0';
			return false;
		}
		if(GetTickCount() - startTick > timeout) {
			buf[pos] = '\0';
			return false;
		}
		SemaphoreGive(xWebThreadSemaphore);             // отдать семафор для обработки других задач
		_delay(poll < SMTP_POLL_FAST_N ? SMTP_POLL_FAST : SMTP_POLL_SLOW);
		if(poll < SMTP_POLL_FAST_N) poll++;
		if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {
			// Семафор обратно не получен. Он сейчас у другой задачи, поэтому НИ трогать сокет,
			// НИ отдавать семафор нельзя: он бинарный, и лишний Give освободил бы SPI из-под владельца.
			smtpSemLost = true;
			buf[pos] = '\0';
			return false;
		}
	}
}

// Прочитать ответ сервера ЦЕЛИКОМ, в том числе многострочный (RFC 5321 4.2.1):
// признак продолжения - дефис в 4-й позиции, у последней строки там пробел.
// Без этого остаток ответа (например, на EHLO) достался бы следующей команде и диалог разъехался бы.
// Код ответа кладется в smtpCode, первая строка - в retMail.
// Попутно из ответа на EHLO взводятся биты возможностей сервера в smtpCaps.
boolean Message::smtpReadReply(uint32_t timeout)
{
	boolean first = true;
	smtpCode = 0;
	for(;;) {
		if(!smtpReadLine(tempBuf, LEN_TEMPBUF, timeout)) {
			if(first) setRetMail("No answer from server");
			return false;
		}
		JOURNAL("< %s\n", tempBuf);
		if(strlen(tempBuf) < 3 || tempBuf[0] < '2' || tempBuf[0] > '5'
		   || tempBuf[1] < '0' || tempBuf[1] > '9' || tempBuf[2] < '0' || tempBuf[2] > '9') {
			setRetMail("Bad reply format");
			return false;
		}
		smtpCode = (tempBuf[0] - '0') * 100 + (tempBuf[1] - '0') * 10 + (tempBuf[2] - '0');
		if(first) { setRetMail(tempBuf); first = false; }
		else if(strlen(tempBuf) > 4) {
			// Пояснение к отказу серверы часто дают во второй строке - дописываем, пока влезает.
			// Строка короче 5 символов содержательного текста после кода не несет.
			uint16_t rl = strlen(retMail);
			if(rl < LEN_RETMAIL - 2) {
				retMail[rl++] = ' ';
				strncpy(retMail + rl, tempBuf + 4, LEN_RETMAIL - 1 - rl);
				retMail[LEN_RETMAIL - 1] = '\0';
			}
		}
		// Разбор объявленных расширений: строки вида "250-AUTH PLAIN LOGIN", "250-8BITMIME"
		if(strstr(tempBuf + 3, "8BITMIME")) SETBIT1(smtpCaps, fSMTP_8BITMIME);
		char *a = strstr(tempBuf + 3, "AUTH");
		if(a) {
			if(strstr(a, "PLAIN")) SETBIT1(smtpCaps, fSMTP_AUTH_PLAIN);
			if(strstr(a, "LOGIN")) SETBIT1(smtpCaps, fSMTP_AUTH_LOGIN);
		}
		if(strlen(tempBuf) == 3 || tempBuf[3] == ' ') break;   // последняя строка ответа
		if(tempBuf[3] != '-') { setRetMail("Bad reply format"); return false; }
	}
	return smtpCode < 400;
}

// Послать команду серверу и разобрать ответ. true - код ответа меньше 400.
boolean Message::smtpCmd(const char *cmd, uint32_t timeout)
{
	JOURNAL("> %s\n", cmd);
	return smtpCmdSecret(cmd, timeout);
}

// То же, но текст команды в журнал НЕ пишется: через нее уходят логин и пароль в base64,
// а журнал доступен через веб-морду.
boolean Message::smtpCmdSecret(const char *cmd, uint32_t timeout)
{
	// SEM-BORROW: xWebThreadSemaphore - вызывается из sendMail, замок сети там взят
	if(!clientMessage.connected()) {
		setRetMail("Connection lost");
		smtpCode = 0;
		return false;
	}
	smtpFlush();                                  // команда не должна перемешаться с остатком тела
	uint16_t len = strlen(cmd);
	if(len > LEN_TXBUF - 3) len = LEN_TXBUF - 3;
	memcpy(txBuf, cmd, len);
	txBuf[len] = '\r'; txBuf[len + 1] = '\n';
	txLen = len + 2;
	smtpFlush();
	return smtpReadReply(timeout);
}

// Установить уведомление (сформировать для отправки но НЕ ОТПРАВЛЯТЬ)
// Проверяется необходимость отправки уведомления в зависимости от установленных флагов
// p1 - температура для pMESSAGE_TEMP, код ошибки для pMESSAGE_ERROR (по нему узнаётся повтор)
// true - сообщение принято (или запрещено), false - сообщение отвергнуто т.к оно уже посылалось (дубль) или внутренняя ошибка
boolean Message::setMessage(MESSAGE ms, char *c, int p1) // может запускаться из любого потока!!
{
  // Проверка на необходимость посылки сообщения: ни почта, ни SMS не разрешены - слать некуда
  if (!((GETBIT(messageSetting.flags, fMail)) || (GETBIT(messageSetting.flags, fSMS)))) return true;	// посылать ненадо
  // Проверка необходимости отправки уведомления
  if (((GETBIT(messageSetting.flags, fMessageReset)) == 0) && (ms == pMESSAGE_RESET))       return true; // Попытка отправить не разрешенное сообщение, выходим без ошибок
  if (((GETBIT(messageSetting.flags, fMessageError)) == 0) && (ms == pMESSAGE_ERROR))       return true;
  if (((GETBIT(messageSetting.flags, fMessageLife)) == 0) && (ms == pMESSAGE_LIFE))         return true;
  if (((GETBIT(messageSetting.flags, fMessageTemp)) == 0) && (ms == pMESSAGE_TEMP))         return true;
  if (((GETBIT(messageSetting.flags, fMessageSD)) == 0) && (ms == pMESSAGE_SD))             return true;
  if (((GETBIT(messageSetting.flags, fMessageWarning)) == 0) && (ms == pMESSAGE_WARNING))   return true;

  // Решение и запись - под запретом прерываний: функцию зовут задачи разных приоритетов (set_Error из ReadSensor, Web0 при
  // этом отправляет). Копируется не больше 200 байт - микросекунды. Место отправляемого (nq.cur) очередь не выдаёт.
  // Тестовые сообщения и сигнал жизни можно посылать многократно подряд, остальные - не чаще REPEAT_TIME.
  const uint32_t now = rtcSAM3X8.unixtime();
  const bool repeatable = (ms == pMESSAGE_TESTMAIL) || (ms == pMESSAGE_TESTSMS) || (ms == pMESSAGE_LIFE);
  const uint8_t important = (ms != pMESSAGE_WARNING) && (ms != pMESSAGE_LIFE);   // предупреждения и сигнал жизни вытесняются первыми
  const uint32_t key = Notify_Key(ms, ms == pMESSAGE_ERROR ? (uint16_t) p1 : calc_crc16((unsigned char*) c, strlen(c)));
  type_messageData *d = NULL;
  uint8_t evicted = 0;
  bool dup;
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  dup = !repeatable && Notify_Seen(&recent, key, now, REPEAT_TIME);
  if (!dup) {
    int8_t s = NotifyQueue_Push(&nq, important, &evicted);
    if (s >= 0) {
      d = &pend[s];
      d->ms = ms;
      strncpy(d->data, c, sizeof(d->data) - 1);
      d->data[sizeof(d->data) - 1] = 0;
      // в сообщение pMESSAGE_TEMP добавить значение температуры
      if (ms == pMESSAGE_TEMP && strlen(d->data) < sizeof(d->data) - 16) {
        strcat(d->data, " t=");
        _dtoa(d->data, p1, 2);
      }
      d->p1 = p1;
      d->number = 0;
      Notify_Remember(&recent, key, now);
      if (!busy() && NotifyQueue_Next(&nq) >= 0) startCurrent();   // отправлять было нечего: уходит сразу
    }
  }
  const bool queued = d && &cur() != d;
  __set_PRIMASK(primask);

  if (dup) return false;
  if (!d) { journal.jprintf("MSG dropped (queue full): #%d: %s\n", ms, c); return false; }
  JOURNAL("MSG: #%d: %s%s\n", ms, c, !queued ? "" : evicted ? " (queued, older dropped)" : " (queued)");
  return true;
}

// Новое отправляемое: какие каналы пройти и повторы с нуля. Зовётся под запретом прерываний.
void Message::startCurrent()
{
  sendCh = 0;
  if (GETBIT(messageSetting.flags, fMail) && cur().ms != pMESSAGE_TESTSMS) sendCh |= NCH_MAIL;
  if (GETBIT(messageSetting.flags, fSMS) && cur().ms != pMESSAGE_TESTMAIL) sendCh |= NCH_SMS;
  sendRetry = 0;
  sendRetryTime = 0;
}

// Отправка закончена: место освобождается и следующее ожидающее становится отправляемым - одним шагом,
// иначе уведомление, пришедшее между ними из другой задачи, затиралось (С-15). Зовётся из sendMessage (Web0).
void Message::finishCurrent()
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  NotifyQueue_Done(&nq);
  if (NotifyQueue_Next(&nq) >= 0) startCurrent();
  __set_PRIMASK(primask);
}

// Установить (сформировать) тестовое письмо, отправка sendMessage();
boolean Message::setTestMail() // может запускаться из любого потока!!
{
  if(!GETBIT(messageSetting.flags, fMail)) { // setMessage молча отбросил бы письмо: объясняем причину в журнале и в ответе на тест
    m_snprintf(retTest, sizeof(retTest), "Письмо не отправлено: выключена опция «Посылать уведомления по почте».");
    journal.jprintf("Test mail not sent: mail notifications are disabled\n");
    return true;
  }
  return setMessage(pMESSAGE_TESTMAIL, (char*)"Тестовое уведомление, для проверки почты", 0);
}

//Установить (сформировать) тестовое СМС, отправка sendMessage();
boolean Message::setTestSMS() // может запускаться из любого потока!!
{
  if(!GETBIT(messageSetting.flags, fSMS)) {
    m_snprintf(retTest, sizeof(retTest), "SMS не отправлено: выключена опция «Посылать уведомления по SMS».");
    journal.jprintf("Test SMS not sent: SMS notifications are disabled\n");
    return true;
  }
  return setMessage(pMESSAGE_TESTSMS, (char*)"Тестовое уведомление, для проверки SMS", 0);
}

// Послать уведомление согласно выбранных настроек сформированное setMessage
// Каналы проходятся по маске sendCh: прошедший (или с постоянной ошибкой) снимается, с временной ошибкой ждёт повтора.
// true - уведомление отправлено или его не было false - при отправке произошли ошибки
boolean Message::sendMessage()  // запуск из 0 потока
{
  uint16_t i;

  if (!busy()) return true;                              // Отправлять нечего выходим
  if (sendRetry && rtcSAM3X8.unixtime() < sendRetryTime) return true;  // время повторной попытки еще не пришло
  strcpy(retTest, "Ничего не отправлено. Проверьте флаг разрешения отправки сообщений.");
  if (HP.get_uptime() < cDELAY_START_MESSAGE) {
    // ждать не будем, ибо зачем? _delay(200);  // Прошло мало времени после старта возможно инет еще не поднят
    return true;
  }
  clearBuf(); // очистка рабочих буферов
  const bool test = cur().ms == pMESSAGE_TESTMAIL || cur().ms == pMESSAGE_TESTSMS;   // итог теста нужен сразу: без повторов
  if (sendCh & NCH_MAIL)
  {
    boolean ok = sendMail(); // Формирование ответа
    for (i = 0; i < strlen(retMail); i++) if (retMail[i] == '=') retMail[i] = ':'; // замена знака = на : т.к. это запрещенный знак в запросах
    setRetTest(ok ? "Тестовое письмо отправлено на " : "Тестовое письмо НЕ отправлено на ", mess_SMTP_RCPTTO, retMail);
    // Ошибка с кодом 4xx или обрыв связи (smtpCode == 0) - временная, при 5xx нужна правка настроек
    sendCh = Notify_AfterTry(sendCh, NCH_MAIL, Notify_TryResult(ok, smtpCode < 500, test));
  }

  if ((!(GETBIT(messageSetting.flags, fMail))) && (cur().ms == pMESSAGE_TESTMAIL)) // Почта не разрешена а отправляется тестовое письмо
  {
    strcpy(retTest, "Письмо не отправлено. Проверьте флаг разрешения отправки почты.");
  }

  if (sendCh & NCH_SMS)
  {
    NOTIFY_TRY r;
    switch (messageSetting.sms_service)
    {
      case pSMS_RU:  r = sendSMS();     break;
      case pSMSC_RU:
      case pSMSC_UA: r = sendSMSC();    break;
      case pSMSCLUB: r = sendSMSCLUB(); break;
      default:                                   // некорректная настройка сервиса: повторять бессмысленно
        strcpy(retSMS, "Invalid SMS service");
        journal.jprintf("%s: %d\n", retSMS, (int)messageSetting.sms_service);
        r = NTRY_FAIL;
        break;
    }
    setRetTest(r == NTRY_OK ? "Тестовое SMS отправлено на номер " : "Тестовое SMS НЕ отправлено на номер ", mess_SMS_PHONE, retSMS);
    sendCh = Notify_AfterTry(sendCh, NCH_SMS, Notify_TryResult(r == NTRY_OK, r == NTRY_RETRY, test));
  }

  if ((!(GETBIT(messageSetting.flags, fSMS))) && (cur().ms == pMESSAGE_TESTSMS)) // SMS не разрешена а отправляется тестовое SMS
    strcpy(retTest, "SMS не отправлено. Проверьте флаг разрешения отправки SMS.");

  if (!Notify_Finished(sendCh, &sendRetry, MAIL_RETRY_MAX)) {   // остался канал с временной ошибкой
    sendRetryTime = rtcSAM3X8.unixtime() + MAIL_RETRY_DELAY;
    journal.jprintf("Notify retry %d/%d in %d sec:%s%s\n", sendRetry, MAIL_RETRY_MAX, MAIL_RETRY_DELAY,
                    (sendCh & NCH_MAIL) ? " mail" : "", (sendCh & NCH_SMS) ? " SMS" : "");
    return false;
  }
  finishCurrent();
  return true;
}

// ---- Отправить почту ----
// Диалог ведется строго по RFC 5321/5322: EHLO с адресным литералом, конверт без пробела после
// двоеточия, тело завершается CRLF.CRLF, 8-битное тело объявляется через MIME и BODY=8BITMIME.
// Благодаря этому письмо принимает любой стандартный сервер и послабления на стороне реле
// (smtpd_forbid_bare_newline, отказ от reject_non_fqdn_helo_hostname) не нужны.
boolean Message::sendMail()
{
	// ВСЕ переменные объявляются здесь: ниже используется переход на единую точку выхода smtp_error
	static const char dowNames[] = "SunMonTueWedThuFriSat";
	static const char monNames[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
	static uint16_t msgIdCnt = 0;
	const char *stage = "connect";       // на каком этапе оборвался сеанс, попадает в журнал и в retMail
	const char *type;                    // тип уведомления для темы письма
	const char *dom;                     // домен отправителя для Message-ID
	IPAddress ip;                        // собственный адрес контроллера, для приветствия EHLO
	IPAddress oldIP;                     // адрес сервера из кеша, чтобы заметить его смену
	IPAddress zeroIP(0, 0, 0, 0);
	boolean connected, nonAscii;
	uint16_t i;
	int n;
	uint32_t ut;
	char dowName[4], monName[4];

	setRetMail("");                      // Обнулить ошибку
	txLen = 0;
	smtpCaps = 0;
	smtpCode = 0;
	smtpSemLost = false;

	// Адреса могли попасть в структуру из EEPROM в обход проверок веб-морды, а управляющий
	// символ в адресе разорвал бы команду SMTP надвое или оборвал заголовки письма
	if(mail_has_ctrl(messageSetting.smtp_MailTo) || mail_has_ctrl(messageSetting.smtp_RCPTTo)) {
		setRetMail("Control char in mail address, check settings");
		smtpCode = 500;                  // постоянная ошибка: повторять бессмысленно
		if(!GETBIT(WorkFlags, fWF_MessageSendError)) journal.jprintf("Error send mail: %s\n", retMail);
		SETBIT1(WorkFlags, fWF_MessageSendError);
		return false;
	}

	// Подготовка
	if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {
		return false; // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
	}

	// 1. Соединение. Быстрый путь - по адресу из кеша, который заполняет dnsUpdate():
	//    connect() по имени каждый раз делает полный запрос к DNS на том же сокете.
	//    Кеш обновляется только при старте и при смене имени сервера в настройках, поэтому
	//    при неудаче имя ОБЯЗАТЕЛЬНО перепроверяется через DNS: иначе после переезда сервера
	//    на другой адрес контроллер молчал бы до перезагрузки.
	connected = false;
	oldIP = messageSetting.smtp_serverIP;
	if(!(oldIP == zeroIP)) connected = clientMessage.connect(oldIP, messageSetting.smtp_port, W5200_SOCK_SYS);
	if(!connected) {
		check_address(messageSetting.smtp_server, messageSetting.smtp_serverIP); // 0 - не удалось, 1 - на входе был IP, 2 - получен через DNS
		if(!(messageSetting.smtp_serverIP == zeroIP) && !(messageSetting.smtp_serverIP == oldIP)) {
			if(!(oldIP == zeroIP)) journal.jprintf("SMTP server address changed\n");
			connected = clientMessage.connect(messageSetting.smtp_serverIP, messageSetting.smtp_port, W5200_SOCK_SYS);
		}
	}
	if(!connected) {
		if(!GETBIT(WorkFlags, fWF_MessageSendError)) journal.jprintf("Connect failed: %s port: %d\n", messageSetting.smtp_server, messageSetting.smtp_port);
		setRetMail("No connect");
		SETBIT1(WorkFlags, fWF_MessageSendError);
		SemaphoreGive (xWebThreadSemaphore);
		return false;
	}
	JOURNAL("Connect server: %s port: %d\n", messageSetting.smtp_server, messageSetting.smtp_port);

	// 2. Приветствие сервера
	stage = "greeting";
	if(!smtpReadReply(SMTP_TO_GREETING) || smtpCode != 220) goto smtp_error;

	// 3. Представление. EHLO нужен ВСЕГДА, даже без авторизации: только из его ответа известны
	//    поддерживаемые механизмы AUTH и признак 8BITMIME. Имя - адресный литерал в скобках,
	//    RFC 5321 4.1.1.1 требует FQDN либо литерал, прежнее "host" не является ни тем ни другим.
	stage = "EHLO";
	smtpCaps = 0;                        // расширения берем только из ответа на EHLO, текст приветствия не в счет
	ip = Ethernet.localIP();             // именно фактический адрес, а не сохраненный в настройках
	if(ip == zeroIP) ip = HP.get_ip();
	m_snprintf(tempBuf, LEN_TEMPBUF, "EHLO [%d.%d.%d.%d]", ip[0], ip[1], ip[2], ip[3]);
	if(!smtpCmd(tempBuf, SMTP_TO_COMMAND)) {
		if(smtpCode < 500) goto smtp_error;                    // обрыв связи или временная ошибка
		smtpCaps = 0;                                          // сервер не знает ESMTP - откат на HELO
		stage = "HELO";
		m_snprintf(tempBuf, LEN_TEMPBUF, "HELO [%d.%d.%d.%d]", ip[0], ip[1], ip[2], ip[3]);
		if(!smtpCmd(tempBuf, SMTP_TO_COMMAND)) goto smtp_error;
	}

	// 4. Авторизация. Механизм выбирается по ответу EHLO: PLAIN укладывается в один обмен вместо трех,
	//    но предлагают его не все серверы. Защиты ни один из них не дает - base64 это не шифрование.
	if(GETBIT(messageSetting.flags, fMailAUTH))
	{
		stage = "AUTH";
		if(GETBIT(smtpCaps, fSMTP_AUTH_PLAIN)) {
			// AUTH PLAIN: <NUL>логин<NUL>пароль в base64, RFC 4616. Собираем вручную - в строке есть нули
			n = 0;
			mailBuf[n++] = '\0';
			i = strlen(messageSetting.smtp_login);  memcpy(mailBuf + n, messageSetting.smtp_login, i);    n += i;
			mailBuf[n++] = '\0';
			i = strlen(messageSetting.smtp_password); memcpy(mailBuf + n, messageSetting.smtp_password, i); n += i;
			strcpy(tempBuf, "AUTH PLAIN ");
			base64_encode(tempBuf + strlen(tempBuf), mailBuf, n);
			memset(mailBuf, 0, n);        // не оставлять пароль открытым текстом в общем буфере веб-сервера
			JOURNAL("> AUTH PLAIN <hidden>\n");
			if(!smtpCmdSecret(tempBuf, SMTP_TO_COMMAND)) goto smtp_error;
		} else if(GETBIT(smtpCaps, fSMTP_AUTH_LOGIN)) {
			if(!smtpCmd("AUTH LOGIN", SMTP_TO_COMMAND)) goto smtp_error;
			base64_encode(tempBuf, messageSetting.smtp_login, strlen(messageSetting.smtp_login));         // Послать логин
			JOURNAL("> <login hidden>\n");
			if(!smtpCmdSecret(tempBuf, SMTP_TO_COMMAND)) goto smtp_error;
			base64_encode(tempBuf, messageSetting.smtp_password, strlen(messageSetting.smtp_password));   // Послать пароль
			JOURNAL("> <password hidden>\n");
			if(!smtpCmdSecret(tempBuf, SMTP_TO_COMMAND)) goto smtp_error;
		} else {
			// Обычно так отвечает сервер, требующий STARTTLS до авторизации, а TLS контроллер не умеет
			setRetMail("Server offers no AUTH PLAIN/LOGIN");
			smtpCode = 500;                        // постоянная ошибка: повторять бессмысленно, нужна правка настроек
			goto smtp_error;
		}
	}

	// 5. Конверт. RFC 5321 4.1.1.2: пробела после двоеточия быть не должно
	stage = "MAIL FROM";
	m_snprintf(tempBuf, LEN_TEMPBUF, "MAIL FROM:<%s>%s", messageSetting.smtp_MailTo,
	           GETBIT(smtpCaps, fSMTP_8BITMIME) ? " BODY=8BITMIME" : "");  // тело в utf-8, оно 8-битное
	if(!smtpCmd(tempBuf, SMTP_TO_COMMAND)) goto smtp_error;

	stage = "RCPT TO";
	m_snprintf(tempBuf, LEN_TEMPBUF, "RCPT TO:<%s>", messageSetting.smtp_RCPTTo);
	if(!smtpCmd(tempBuf, SMTP_TO_COMMAND)) goto smtp_error;

	stage = "DATA";
	if(!smtpCmd("DATA", SMTP_TO_DATA_INIT) || smtpCode != 354) goto smtp_error;

	// 6. Заголовки письма. С этого места и до завершающей точки вывод идет ТОЛЬКО через
	//    smtpWriteBody(): он гарантирует CRLF, экранирование ведущей точки и предел длины строки.
	stage = "headers";
	// Date - свой, чтобы почтовые клиенты показывали время события, а не время получения (RFC 5322 3.3)
	ut = rtcSAM3X8.unixtime();
	memcpy(dowName, dowNames + (uint8_t)(((ut / 86400UL) + 4) % 7) * 3, 3); dowName[3] = '\0'; // 01.01.1970 - четверг
	i = rtcSAM3X8.get_months(); if(i < 1 || i > 12) i = 1;
	memcpy(monName, monNames + (i - 1) * 3, 3); monName[3] = '\0';
	m_snprintf(tempBuf, LEN_TEMPBUF, "Date: %s, %d %s %d %02d:%02d:%02d %c%02d00",
	           dowName, rtcSAM3X8.get_days(), monName, rtcSAM3X8.get_years(),
	           rtcSAM3X8.get_hours(), rtcSAM3X8.get_minutes(), rtcSAM3X8.get_seconds(),
	           TIME_ZONE < 0 ? '-' : '+', TIME_ZONE < 0 ? -(TIME_ZONE) : TIME_ZONE);
	smtpWriteBody(tempBuf);
	// Message-ID - без него почтовые клиенты не связывают ответы в цепочку (RFC 5322 3.6.4)
	dom = strchr(messageSetting.smtp_MailTo, '@');
	m_snprintf(tempBuf, LEN_TEMPBUF, "Message-ID: <%u.%u@%s>", (unsigned int)(ut), ++msgIdCnt, dom ? dom + 1 : "heatpump.local");
	smtpWriteBody(tempBuf);
	m_snprintf(tempBuf, LEN_TEMPBUF, "From: <%s>", messageSetting.smtp_MailTo);
	smtpWriteBody(tempBuf);
	m_snprintf(tempBuf, LEN_TEMPBUF, "To: <%s>", messageSetting.smtp_RCPTTo);
	smtpWriteBody(tempBuf);
	// Тема письма
	switch((int) cur().ms)   // Заголовок уведомления - добавляем тип уведомления
	{
	case pMESSAGE_NONE:		type = "";			break;                     // Нет уведомлений
	case pMESSAGE_TESTMAIL:	type = "TESTMAIL";	break;                     // Тестовое почта
	case pMESSAGE_TESTSMS:	type = "TESTSMS";	break;                     // Тестовое смс
	case pMESSAGE_RESET:	type = "RESET";		break;                     // Уведомление Сброс
	case pMESSAGE_ERROR:	type = "ERROR";		break;                     // Уведомление Ошибка
	case pMESSAGE_LIFE:		type = "LIFE";		break;                     // Уведомление Сигнал жизни
	case pMESSAGE_TEMP:		type = "TEMP";		break;                     // Уведомление Достижение граничной температуры
	case pMESSAGE_SD:		type = "ERROR SD";	break;                     // Уведомление "Проблемы с sd картой"
	case pMESSAGE_WARNING:	type = "WARNING";	break;                     // Уведомление "Прочие уведомления"
	default:
		setRetMail("Unknown message type");
		smtpCode = 500;                    // постоянная ошибка: повторять бессмысленно
		goto smtp_error;
	}
	// Тема ограничена 150 байтами: после base64 она вырастет на треть и должна уместиться в tempBuf
	if(*type) n = m_snprintf(mailBuf, 151, "Controller %s - %s", nameHeatPump, type);
	else      n = m_snprintf(mailBuf, 151, "Controller %s", nameHeatPump);   // без висящего дефиса
	// Если обрезка попала внутрь многобайтного символа utf-8 - отбросить незаконченную последовательность
	i = n;
	while(i > 0 && (mailBuf[i - 1] & 0xC0) == 0x80) i--;    // хвостовые байты-продолжения
	if(i > 0 && (mailBuf[i - 1] & 0x80)) {                  // перед ними ведущий байт последовательности
		i--;                                                // длина: 2 при 110xxxxx, 3 при 1110xxxx, иначе 4
		if(n - i < ((mailBuf[i] & 0xE0) == 0xC0 ? 2 : (mailBuf[i] & 0xF0) == 0xE0 ? 3 : 4)) {
			n = i; mailBuf[n] = '\0';                       // последовательность не поместилась целиком
		}
	}
	nonAscii = false;                       // заголовки по RFC 5322 семибитные, кириллицу надо кодировать
	for(i = 0; mailBuf[i]; i++) if(mailBuf[i] & 0x80) { nonAscii = true; break; }
	if(nonAscii) {                          // RFC 2047: =?UTF-8?B?<base64>?=
		strcpy(tempBuf, "Subject: =?UTF-8?B?");
		base64_encode(tempBuf + strlen(tempBuf), mailBuf, n);
		strcat(tempBuf, "?=");
	} else m_snprintf(tempBuf, LEN_TEMPBUF, "Subject: %s", mailBuf);
	smtpWriteBody(tempBuf);
	smtpWriteBody("MIME-Version: 1.0");
	smtpWriteBody("Content-Type: text/plain; charset=utf-8");
	smtpWriteBody("Content-Transfer-Encoding: 8bit");   // тело содержит кириллицу в utf-8, а не us-ascii

	// 7. ПУСТАЯ СТРОКА - обязательный разделитель заголовков и тела (RFC 5322 2.1).
	//    Без нее тело приклеивается к последнему заголовку и получатель видит пустое письмо.
	smtpWriteBody("");

	// 8. Текст сообщения
	m_snprintf(tempBuf, LEN_TEMPBUF, "--- %s ver.%s ---", SendMessageTitle, VERSION);
	smtpWriteBody(tempBuf);
	m_snprintf(tempBuf, LEN_TEMPBUF, "Конфигурация: %s (%s)", CONFIG_NAME, CONFIG_NOTE);
	smtpWriteBody(tempBuf);
	m_snprintf(tempBuf, LEN_TEMPBUF, "Создание уведомления: %s %s", NowDateToStr(), NowTimeToStr());
	smtpWriteBody(tempBuf);
	smtpWriteBlock(cur().data);        // само уведомление, буфер не изменяется (его ждет еще sendSMS)

	// 9. Дополнительная информация если требуется добавляется в уведомление
	if(GETBIT(messageSetting.flags, fMailInfo)) get_mailState(mailBuf, LEN_MAILBUF);

	// 10. Завершение письма. Тело гарантированно закончилось на CRLF, поэтому точка дает CRLF.CRLF
	stage = "end of DATA";
	smtpFlush();
	if(!smtpCmd(".", SMTP_TO_DATA_END)) goto smtp_error;
	// retMail уже содержит ответ сервера ("250 ... queued as ..."), записанный в smtpReadReply()

	stage = "QUIT";
	m_snprintf(mailBuf, LEN_MAILBUF, "%s", retMail);   // сохранить ответ: его перезапишет ответ на QUIT
	smtpCmd("QUIT", SMTP_TO_QUIT);                     // результат не важен, письмо уже принято
	setRetMail(mailBuf);
	SETBIT0(WorkFlags, fWF_MessageSendError);
	// Письмо уже принято сервером, поэтому неудача QUIT ничего не меняет - возвращаем успех.
	// Но если на QUIT потерян семафор, сокет трогать нельзя и отдавать семафор нельзя тоже.
	if(smtpSemLost) {
		journal.jprintf("Mail sent, but network mutex lost\n");
		return true;
	}
	clientMessage.stop();
	JOURNAL("OK disconnected\n");
	SemaphoreGive (xWebThreadSemaphore);
	return true;

smtp_error:
	// Единая точка выхода по ошибке. Сначала штатный QUIT - тогда сервер запишет в свой журнал
	// внятную причину вместо "lost connection", по которой потом трудно понять, что произошло.
	// Текст ошибки формируем ДО QUIT: ответ на него перезапишет retMail.
	if(smtpCode >= 400)   // 4xx - временная (повтор имеет смысл), 5xx - постоянная (нужна правка настроек)
		m_snprintf(mailBuf, LEN_MAILBUF, "%s error on %s: %s", smtpCode < 500 ? "Temporary" : "Permanent", stage, retMail);
	else
		m_snprintf(mailBuf, LEN_MAILBUF, "Failed on %s: %s", stage, strlen(retMail) ? retMail : "no answer");
	if(!GETBIT(WorkFlags, fWF_MessageSendError)) journal.jprintf("Error send mail: %s\n", mailBuf);
	txLen = 0;                             // недописанное тело в сокет не отправляем
	SETBIT1(WorkFlags, fWF_MessageSendError);
	// Семафор сети потерян: он сейчас у другой задачи, поэтому ни сокет трогать, ни отдавать
	// семафор нельзя - он бинарный, и лишний Give освободил бы SPI из-под владельца.
	// Сокет не течет: следующий connect() на этот же сокет начинает с Sock_CLOSE.
	if(smtpSemLost) {
		journal.jprintf("Send mail aborted: network mutex lost\n");
		setRetMail(mailBuf);
		return false;
	}
	n = smtpCode;                          // ответ на QUIT затрет код ошибки, а по нему sendMessage
	if(clientMessage.connected()) smtpCmd("QUIT", SMTP_TO_QUIT);   // решает, имеет ли смысл повтор
	smtpCode = n;
	setRetMail(mailBuf);                   // ставим ПОСЛЕ QUIT: его ответ перезаписал бы retMail
	if(!smtpSemLost) {                     // семафор мог потеряться уже на самом QUIT
		clientMessage.stop();
		SemaphoreGive (xWebThreadSemaphore);
	} else journal.jprintf("Send mail aborted: network mutex lost\n");
	return false;
}

// ---- SMS: общая часть трёх сервисов ----
// Пометка типа уведомления в начале текста SMS; NULL - тип неизвестен
static const char *sms_type_tag(MESSAGE ms)
{
	switch((int) ms) {
	case pMESSAGE_NONE:     return "";
	case pMESSAGE_TESTMAIL: return "TESTMAIL+";
	case pMESSAGE_TESTSMS:  return "TESTSMS+";
	case pMESSAGE_RESET:    return "RESET+";
	case pMESSAGE_ERROR:    return "ERROR+";
	case pMESSAGE_LIFE:     return "LIFE+";
	case pMESSAGE_TEMP:     return "TEMP+";
	case pMESSAGE_SD:       return "ERROR+SD+";
	case pMESSAGE_WARNING:  return "WARNING+";
	default:                return NULL;
	}
}

// Транспорт: адрес сервиса, соединение, запрос из mailBuf, ожидание ответа. Тело ответа (без заголовков HTTP) кладётся
// в mailBuf, его начало - в retSMS для морды. NTRY_RETRY - до сервиса не достучались, запрос не ушёл: повтор безопасен.
// NTRY_FAIL - запрос ушёл, а ответа нет: повтор мог бы прислать платный дубль. NTRY_OK - ответ получен, смысл его решает отправитель.
NOTIFY_TRY Message::smsExchange(const char *host)
{
	IPAddress zeroIP(0, 0, 0, 0);
	if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) return NTRY_RETRY;
	if(messageSetting.sms_serviceIP == zeroIP) {
		strcpy(retSMS, host);                                  // check_address нужна строка в ОЗУ
		if(!check_address(retSMS, messageSetting.sms_serviceIP)) {
			strcat(retSMS, " DNS lookup failed!");
			SemaphoreGive(xWebThreadSemaphore);
			return NTRY_RETRY;
		}
	}
	if(!clientMessage.connect(messageSetting.sms_serviceIP, 80, W5200_SOCK_SYS)) {
		if(!GETBIT(WorkFlags, fWF_SMSSendError)) journal.jprintf("Connect failed: %s port: %d\n", host, 80);
		strcpy(retSMS, "No connect "); strcat(retSMS, host);
		clientMessage.stop();
		SETBIT1(WorkFlags, fWF_SMSSendError);
		SemaphoreGive(xWebThreadSemaphore);
		return NTRY_RETRY;
	}
	JOURNAL("Connect server: %s port: %d\n", host, 80);
	clientMessage.write(mailBuf, strlen(mailBuf));

	for(uint8_t count = 0; !clientMessage.available(); count++) {     // ожидание ответа 5 сек
		if(count >= 25) {
			strcpy(retSMS, "SMS server not responding!");
			if(!GETBIT(WorkFlags, fWF_SMSSendError)) journal.jprintf("%s\n", retSMS);
			SETBIT1(WorkFlags, fWF_SMSSendError);
			clientMessage.stop();
			SemaphoreGive(xWebThreadSemaphore);
			return NTRY_FAIL;
		}
		SemaphoreGive(xWebThreadSemaphore);
		_delay(200);
		if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) return NTRY_FAIL;   // замок потерян: сокет не трогаем
	}
	uint16_t n = 0;
	while(clientMessage.available() && n < LEN_MAILBUF - 1) {
		int got = clientMessage.read((uint8_t*) mailBuf + n, LEN_MAILBUF - 1 - n);
		if(got <= 0) break;
		n += got;
	}
	mailBuf[n] = 0;
	clientMessage.stop();
	SemaphoreGive(xWebThreadSemaphore);
	// Ответ на запрос HTTP/1.0 начинается с заголовков: ответ сервиса - только в теле
	const char *body = http_body(mailBuf);
	strncpy(retSMS, body, LEN_RETSMS - 1);
	retSMS[LEN_RETSMS - 1] = 0;
	str_replace(retSMS, '=', ':');                             // морда обрезает ответ по '='
	if(body != mailBuf) memmove(mailBuf, body, strlen(body) + 1);
	return NTRY_OK;
}

// Итог по ответу сервиса
NOTIFY_TRY Message::smsVerdict(boolean ok)
{
	if(!ok) {
		if(!GETBIT(WorkFlags, fWF_SMSSendError)) journal.jprintf("Error send SMS: %s\n", retSMS);
		SETBIT1(WorkFlags, fWF_SMSSendError);
		return NTRY_FAIL;
	}
	JOURNAL("server return: %s \n", retSMS);
	SETBIT0(WorkFlags, fWF_SMSSendError);
	return NTRY_OK;
}

// ---- Отправить SMS sms.ru ----
NOTIFY_TRY Message::sendSMS()
{
	const char *tag = sms_type_tag(cur().ms);
	if(!tag) { strcpy(retSMS, "ERROR TYPE"); return NTRY_FAIL; }
	strcpy(mailBuf, "GET /sms/send?api_id=");                                 // Начало; адрес сервера уходит в заголовке Host
	url_encode_append(mailBuf, LEN_MAILBUF, messageSetting.sms_p1);       // id
	strcat(mailBuf, "&to=");
	url_encode_append(mailBuf, LEN_MAILBUF, messageSetting.sms_phone);    // номер
	strcat(mailBuf, "&text=");
	strcat(mailBuf, SendSMSTitle);
	strcat(mailBuf, "+");
	strcat(mailBuf, tag);
	url_encode_append(mailBuf, LEN_MAILBUF, cur().data);                  // содержимое
	if(!http_request_tail(mailBuf, LEN_MAILBUF, sms_host(pSMS_RU))) { strcpy(retSMS, "SMS request too long"); return NTRY_FAIL; }
	NOTIFY_TRY r = smsExchange(sms_host(pSMS_RU));
	return r == NTRY_OK ? smsVerdict(true) : r;               // любой ответ sms.ru считается отправкой, как и прежде
}

// ---- Отправить SMS smsc.ru, smsc.ua ----
NOTIFY_TRY Message::sendSMSC()
{
	const char *tag = sms_type_tag(cur().ms);
	if(!tag) { strcpy(retSMS, "ERROR TYPE"); return NTRY_FAIL; }
	strcpy(mailBuf, "GET /sys/send.php?login=");                              // Начало; адрес сервера уходит в заголовке Host
	url_encode_append(mailBuf, LEN_MAILBUF, messageSetting.sms_p1);       // login
	if(messageSetting.sms_p2[0] != '&') strcat(mailBuf, "&psw=");         // пароль без имени параметра; у того, кто вписал его вместе с "&psw=" - как было
	strcat(mailBuf, messageSetting.sms_p2);                                   // password
	strcat(mailBuf, "&phones=");
	url_encode_append(mailBuf, LEN_MAILBUF, messageSetting.sms_phone);    // номер
	strcat(mailBuf, "&mes=");
	strcat(mailBuf, SendSMSTitle);
	strcat(mailBuf, "+");
	strcat(mailBuf, tag);
	url_encode_append(mailBuf, LEN_MAILBUF, cur().data);                  // содержимое
	const char *host = sms_host(messageSetting.sms_service);
	if(!http_request_tail(mailBuf, LEN_MAILBUF, host)) { strcpy(retSMS, "SMS request too long"); return NTRY_FAIL; }
	NOTIFY_TRY r = smsExchange(host);
	return r == NTRY_OK ? smsVerdict(!strstr(mailBuf, "ERROR")) : r;
}

// ---- Отправить SMS smsclub.mobi ----
NOTIFY_TRY Message::sendSMSCLUB()
{
	const char *tag = sms_type_tag(cur().ms);
	if(!tag) { strcpy(retSMS, "ERROR TYPE"); return NTRY_FAIL; }
	strcpy(mailBuf, "POST /xml/ HTTP/1.1\r\nHost: ");
	strcat(mailBuf, sms_host(pSMSCLUB));
	strcat(mailBuf, "\r\nUser-Agent: HK 1.017\r\nConnection: close\r\nContent-Type: application/xml\r\nContent-Length:    \r\n\r\n");
	uint16_t headerLength = strlen(mailBuf);
	strcat(mailBuf, "<?xml version='1.0' encoding='utf-8'?>\r\n<request_sendsms>\r\n<username><![CDATA[");
	strcat(mailBuf, messageSetting.sms_p1);
	strcat(mailBuf, "]]></username>\r\n<password><![CDATA[");
	strcat(mailBuf, messageSetting.sms_p2);
	strcat(mailBuf, "]]></password>\r\n<from><![CDATA[infomir]]></from>\r\n<to><![CDATA[");
	strcat(mailBuf, messageSetting.sms_phone);
	strcat(mailBuf, "]]></to>\r\n<text><![CDATA[");
	strcat(mailBuf, SendSMSTitle);
	strcat(mailBuf, "+");
	strcat(mailBuf, tag);
	strcat(mailBuf, cur().data);                                              // содержимое
	strcat(mailBuf, "]]></text>\r\n</request_sendsms>\r\n");
	char len[6];
	itoa(strlen(mailBuf) - headerLength, len, 10);                            // длина тела - в три пробела после Content-Length:
	memcpy(mailBuf + headerLength - 7, len, strlen(len) < 3 ? strlen(len) : 3);
	NOTIFY_TRY r = smsExchange(sms_host(pSMSCLUB));
	return r == NTRY_OK ? smsVerdict(strstr(mailBuf, ">OK<") != NULL) : r;
}
// ---------------------------------------------------------------------------





// Тело письма о состоянии ТН: формируется и пишется через smtpWriteBody из этого файла.

// Получить состояние теплового насоса для отсылки по почте
// Строки формируются в buf и выводятся через HP.message.smtpWriteBody(): он ставит CRLF,
// экранирует ведущую точку и режет слишком длинные строки, как того требует RFC 5321.
// Вызывается ТОЛЬКО из Message::sendMail(), между DATA и завершающей точкой.
// buf_size должен быть не меньше LEN_MAILBUF: get_Counts() формирует около 1.3 кБ текста.
void get_mailState(char *buf, uint16_t buf_size)
{
#ifdef MAIL_STATE_WIDE_FORMAT
	uint8_t j;
	int16_t x;
#endif
	uint8_t i;

	HP.message.smtpWriteBody("");
	HP.message.smtpWriteBody(" -----  С О С Т О Я Н И Е  -----");

	HP.message.smtpWriteBody("");
	HP.message.smtpWriteBody("  1. Тепловой насос");

	strcpy(buf,"Состояние: "); strcat(buf,HP.StateToStr());
	HP.message.smtpWriteBody(buf);

	strcpy(buf,"Статус: "); HP.get_StateModworkStr(buf);
	HP.message.smtpWriteBody(buf);

	strcpy(buf,"Последняя ошибка: ");  _itoa(HP.get_errcode(),buf); strcat(buf," - "); strcat(buf,HP.get_lastErr());
	HP.message.smtpWriteBody(buf);

	strcpy(buf,"Режим работы: ");strcat(buf,HP.TestToStr());
	HP.message.smtpWriteBody(buf);

	strcpy(buf,"Последняя перезагрузка: "); DecodeTimeDate(HP.get_startDT(),buf);
	HP.message.smtpWriteBody(buf);

	strcpy(buf,"Время с последней перезагрузки: "); TimeIntervalToStr(HP.get_uptime(),buf);
	HP.message.smtpWriteBody(buf);

	strcpy(buf,"Причина последней перезагрузки: "); strcat(buf,ResetCause());
	HP.message.smtpWriteBody(buf);

	HP.message.smtpWriteBody("");
	HP.message.smtpWriteBody("  2. Датчики температуры");
	for(i = 0; i < TNUMBER; i++)   // Информация по  датчикам температуры
	{
		if(HP.sTemp[i].get_present())  // только присутсвующие датчики
		{
#ifdef MAIL_STATE_WIDE_FORMAT
			strcpy(buf, HP.sTemp[i].get_name());
			if((x=8-strlen(HP.sTemp[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(buf," "); }
			strcat(buf,"["); strcat(buf,addressToHex(HP.sTemp[i].get_address())); strcat(buf,"] ");
			strcat(buf, HP.sTemp[i].get_note());
			strcat(buf, ": ");
#else
			strcpy(buf, HP.sTemp[i].get_note());
			strcat(buf, " (");
			strcat(buf, HP.sTemp[i].get_name());
			strcat(buf, "): ");
#endif
			_dtoa(buf, HP.sTemp[i].get_Temp(), 2);
			if(HP.sTemp[i].get_lastErr() != OK) {
				strcat(buf, " error:");
				_itoa(HP.sTemp[i].get_lastErr(), buf);
			}
			HP.message.smtpWriteBody(buf);
		}  //   if (HP.sTemp[i].get_present())
	}
	HP.message.smtpWriteBody("");
	HP.message.smtpWriteBody("  3. Аналоговые датчики");
	for(i=0;i<ANUMBER;i++)   // Информация по  аналоговым датчикам
	{
		if (HP.sADC[i].get_present()) // только присутсвующие датчики
		{
#ifdef MAIL_STATE_WIDE_FORMAT
			strcpy(buf,HP.sADC[i].get_name());
			if((x=8-strlen(HP.sADC[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(buf," "); }
			strcat(buf,HP.sADC[i].get_note());  strcat(buf,": ");
#else
			strcpy(buf, HP.sADC[i].get_note());
			strcat(buf, " (");
			strcat(buf, HP.sADC[i].get_name());
			strcat(buf, "): ");
#endif
			_dtoa(buf, HP.sADC[i].get_Value(), 2);
			if (HP.sADC[i].get_lastErr()!=OK ) { strcat(buf," error:"); _itoa(HP.sADC[i].get_lastErr(),buf); }
			HP.message.smtpWriteBody(buf);
		}
	}

	HP.message.smtpWriteBody("");
	HP.message.smtpWriteBody("  4. Датчики 'сухой контакт'");
	for(i=0;i<INUMBER;i++)
	{
		if (HP.sInput[i].get_present()) // только присутсвующие датчики
		{
#ifdef MAIL_STATE_WIDE_FORMAT
			strcpy(buf,HP.sInput[i].get_name()); if((x=8-strlen(HP.sInput[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(buf," "); }
			strcat(buf,HP.sInput[i].get_note());  strcat(buf,": ");
#else
			strcpy(buf, HP.sInput[i].get_note());
			strcat(buf, " (");
			strcat(buf, HP.sInput[i].get_name());
			strcat(buf, "): ");
#endif
			_itoa(HP.sInput[i].get_Input(),buf);
			strcat(buf," alarm:"); _itoa(HP.sInput[i].get_alarmInput(),buf);
			HP.message.smtpWriteBody(buf);
		}
	}

	HP.message.smtpWriteBody("");
	HP.message.smtpWriteBody("  5. Реле");
	for(i=0;i<RNUMBER;i++)
	{
		if (HP.dRelay[i].get_present()) // только присутсвующие датчики
		{
#ifdef MAIL_STATE_WIDE_FORMAT
			strcpy(buf,HP.dRelay[i].get_name()); if((x=8-strlen(HP.dRelay[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(buf," "); }
			strcat(buf,HP.dRelay[i].get_note());  strcat(buf,": ");
#else
			strcpy(buf, HP.dRelay[i].get_note());
			strcat(buf, " (");
			strcat(buf, HP.dRelay[i].get_name());
			strcat(buf, "): ");
#endif
			_itoa(HP.dRelay[i].get_Relay(),buf);
			HP.message.smtpWriteBody(buf);
		}
	}

	HP.message.smtpWriteBody("");
#ifdef EEV_DEF
	strcpy(buf,"  6. ЭРВ - ");  strcat(buf,HP.dEEV.get_note());
	HP.message.smtpWriteBody(buf);
	strcpy(buf,"Минимальное положение (шаги): ");  _itoa(HP.dEEV.get_minEEV(),buf);
	HP.message.smtpWriteBody(buf);
	strcpy(buf,"Полное открыте (шаги):");  _itoa(HP.dEEV.get_maxEEV(),buf);
	HP.message.smtpWriteBody(buf);
	strcpy(buf,"Правило управления ЭРВ: ");
	HP.dEEV.get_ruleEEVtext(buf);
	HP.message.smtpWriteBody(buf);
	strcpy(buf,"Текущее положение: ");
#ifdef EEV_PREFER_PERCENT
	_dtoa(buf, HP.dEEV.calc_percent(HP.dEEV.get_EEV()), 2);
#else
	_itoa(HP.dEEV.get_EEV(),buf);
#endif
	HP.message.smtpWriteBody(buf);
	strcpy(buf,"Текущий перегрев (градусы): ");  _dtoa(buf, HP.dEEV.get_Overheat(), 2);
	HP.message.smtpWriteBody(buf);
#else
	HP.message.smtpWriteBody("  6. ЭРВ - EEV absent");
#endif

	HP.message.smtpWriteBody("");
	strcpy(buf,"  7. Инвертор ");strcat(buf,HP.dFC.get_name());
	HP.message.smtpWriteBody(buf);
	if(HP.dFC.get_present()) {
		strcpy(buf,"Текущее состояние инвертора: "); _itoa(HP.dFC.read_stateFC(),buf);
		HP.message.smtpWriteBody(buf);
		strcpy(buf,"Целевая частота [Гц]: ");    _ftoa(buf,(float)HP.dFC.get_target()/100.0,2);
		HP.message.smtpWriteBody(buf);
		strcpy(buf,"Текущая частота [Гц]: ");    _ftoa(buf,(float)HP.dFC.get_frequency()/100.0,2);
		HP.message.smtpWriteBody(buf);
		strcpy(buf,"Текущая мощность [кВт]: ");  _ftoa(buf,(float)HP.dFC.get_power()/1000.0,3);
		HP.message.smtpWriteBody(buf);
		strcpy(buf,"Tемпература радиатора [°С]: ");  _ftoa(buf,(float)HP.dFC.read_tempFC()/10.0,2);
		HP.message.smtpWriteBody(buf);
#ifdef FC_ANALOG_CONTROL // Аналоговое управление
		strcpy(buf,"ЦАП дискреты: ");            _itoa(HP.dFC.get_DAC(),buf);
		HP.message.smtpWriteBody(buf);
#endif
	} else {
		HP.message.smtpWriteBody("FC absent");
	}

	HP.message.smtpWriteBody("");
	HP.message.smtpWriteBody("  8. Электросчетчик");
#ifdef USE_ELECTROMETER_SDM
	strcpy(buf,"Текущее входное напряжение [В]: ");                          HP.dSDM.get_paramSDM((char*)sdm_VOLTAGE,buf);    HP.message.smtpWriteBody(buf);
	strcpy(buf,"Текущий потребляемый ток ТН [А]: ");                         HP.dSDM.get_paramSDM((char*)sdm_CURRENT,buf);    HP.message.smtpWriteBody(buf);
	strcpy(buf,"Текущая потребляемая активная мощность ТН [Вт]: ");          HP.dSDM.get_paramSDM((char*)sdm_ACPOWER,buf);    HP.message.smtpWriteBody(buf);
	strcpy(buf,"Коэффициент мощности: ");                                    HP.dSDM.get_paramSDM((char*)sdm_POW_FACTOR,buf); HP.message.smtpWriteBody(buf);
	strcpy(buf,"Угол фазы (градусы): ");                                     HP.dSDM.get_paramSDM((char*)sdm_PHASE,buf);      HP.message.smtpWriteBody(buf);
	strcpy(buf,"Суммарная активная энергия [кВт*ч]: ");                      HP.dSDM.get_paramSDM((char*)sdm_ACENERGY,buf);   HP.message.smtpWriteBody(buf);
	strcpy(buf,"Cостояние связи со счетчиком: ");                            HP.dSDM.get_paramSDM((char*)sdm_LINK,buf);       HP.message.smtpWriteBody(buf);
#else
	HP.message.smtpWriteBody("SDM absent");
#endif

	HP.message.smtpWriteBody("");
	HP.message.smtpWriteBody("  9. Частотные датчики потока");
	for(i=0;i<FNUMBER;i++)
	{
		if (HP.sFrequency[i].get_present())
		{
#ifdef MAIL_STATE_WIDE_FORMAT
			strcpy(buf,HP.sFrequency[i].get_name()); if((x=8-strlen(HP.sFrequency[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(buf," "); }
			strcat(buf,HP.sFrequency[i].get_note());  strcat(buf,": ");
#else
			strcpy(buf, HP.sFrequency[i].get_note());
			strcat(buf, " (");
			strcat(buf, HP.sFrequency[i].get_name());
			strcat(buf, "): ");
#endif
			_dtoa(buf, HP.sFrequency[i].get_Value(), 3);
			HP.message.smtpWriteBody(buf);
		}
	}
	// счетчики. get_Counts() формирует около 1.3 кБ - именно поэтому сюда передается mailBuf,
	// а не 256-байтный tempBuf (переполнение затирало бы retMail и retSMS)
	if(buf_size >= LEN_MAILBUF) {
		get_Counts(buf);
		HP.message.smtpWriteBlock(buf);
	}
}

// ---------------------------------------------------------------------------
// Реестр параметров веб-API: уведомления (почта и SMS). Правила - как у oHP.
// Во всех строках запрещены управляющие символы (PF_NC): они разорвали бы
// команду SMTP или заголовки письма (см. mail_has_ctrl выше). Пароль SMTP читает
// только admin. Тестовые отправки - действия test.mail и test.sms.
// ---------------------------------------------------------------------------
enum { MS_DNS_SMTP = 1, MS_DNS_SMS, MS_IP_SMTP, MS_IP_SMS, MS_NAMEP1, MS_NAMEP2, MS_RET };
static_assert(sizeof(SMS_SERVICE) == 1, "SMS_SERVICE хранится одним байтом (CHECK_SAVE_SIZE type_messageHP = 276)");
#define MSO(f) offsetof(type_messageHP, f)
static const char *const apiLabSms[] = { "sms.ru", "smsc.ru", "smsc.ua", "smsclub.mobi", 0 };
static const ApiParam apiMessTab[] = {
	PN_BIT(mess_MAIL,         MSO(flags), fMail,           PT_B16, 0, 0, 0, 0),
	PN_BIT(mess_MAIL_AUTH,    MSO(flags), fMailAUTH,       PT_B16, 0, 0, 0, 0),
	PN_BIT(mess_MAIL_INFO,    MSO(flags), fMailInfo,       PT_B16, 0, 0, 0, 0),
	PN_BIT(mess_SMS,          MSO(flags), fSMS,            PT_B16, 0, 0, 0, 0),
	PN_BIT(mess_MESS_RESET,   MSO(flags), fMessageReset,   PT_B16, 0, 0, 0, 0),
	PN_BIT(mess_MESS_ERROR,   MSO(flags), fMessageError,   PT_B16, 0, 0, 0, 0),
	PN_BIT(mess_MESS_LIFE,    MSO(flags), fMessageLife,    PT_B16, 0, 0, 0, 0),
	PN_BIT(mess_MESS_TEMP,    MSO(flags), fMessageTemp,    PT_B16, 0, 0, 0, 0),
	PN_BIT(mess_MESS_SD,      MSO(flags), fMessageSD,      PT_B16, 0, 0, 0, 0),
	PN_BIT(mess_MESS_WARNING, MSO(flags), fMessageWarning, PT_B16, 0, 0, 0, 0),
	PN_STR(mess_SMTP_SERVER,  MSO(smtp_server),   32, PF_NE | PF_NC, MS_DNS_SMTP),
	{ mess_SMTP_IP, 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_IP, MS_IP_SMTP, 0 },
	PN_U16(mess_SMTP_PORT, 2, 65533, 1, MSO(smtp_port), 0),
	PN_STR(mess_SMTP_LOGIN,   MSO(smtp_login),    32, PF_NE | PF_NC, 0),
	PN_STR(mess_SMTP_PASS,    MSO(smtp_password), 16, PF_NE | PF_NC | PF_AR, 0),
	PN_STR(mess_SMTP_MAILTO,  MSO(smtp_MailTo),   32, PF_NE | PF_NC, 0),
	PN_STR(mess_SMTP_RCPTTO,  MSO(smtp_RCPTTo),   32, PF_NE | PF_NC, 0),
	{ mess_SMS_SERVICE, apiLabSms, 0, 3, (uint16_t) MSO(sms_service), 1, PT_E8, 0, 0, MS_DNS_SMS, 0 },   // смена сервиса: IP получить заново
	{ mess_SMS_IP, 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_IP, MS_IP_SMS, 0 },
	PN_STR(mess_SMS_PHONE,    MSO(sms_phone), 16, PF_NE | PF_NC, 0),
	PN_STR(mess_SMS_P1,       MSO(sms_p1),    40, PF_NE | PF_NC, 0),
	PN_STR(mess_SMS_P2,       MSO(sms_p2),    40, PF_NE | PF_NC, 0),
	{ mess_SMS_NAMEP1, 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, MS_NAMEP1, 0 },     // подпись первого параметра зависит от сервиса
	{ mess_SMS_NAMEP2, 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, MS_NAMEP2, 0 },
	PN_I16(mess_MESS_TIN,     0,    30,  100, MSO(mTIN),    0),
	PN_I16(mess_MESS_TBOILER, 0,    70,  100, MSO(mTBOILER), 0),
	PN_I16(mess_MESS_TCOMP,   0,    80,  100, MSO(mTCOMP),  0),
	{ mess_MAIL_RET, 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, MS_RET, 0 },            // ответ на тестовое письмо / SMS (единый буфер)
	{ mess_SMS_RET,  0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TEXT, MS_RET, 0 },
};
static void *api_base_mess(uint8_t) { return HP.message.get_save_addr(); }
static int api_get_mess(uint8_t hook, uint8_t, ApiOut *o)
{
	type_messageHP *d = (type_messageHP *) HP.message.get_save_addr();
	switch(hook) {
	case MS_IP_SMTP: api_put_str(o, IPAddress2String(d->smtp_serverIP)); break;
	case MS_IP_SMS:  api_put_str(o, IPAddress2String(d->sms_serviceIP)); break;
	case MS_NAMEP1:  api_put_str(o, d->sms_service == pSMS_RU ? "API ID" : "Login"); break;
	case MS_NAMEP2:  api_put_str(o, d->sms_service == pSMS_RU ? "none" : "Password"); break;
	case MS_RET:     api_put_str(o, HP.message.api_ret()); break;
	default: return AE_FAIL;
	}
	return AE_OK;
}
static void api_after_mess(uint8_t hook)
{
	if(hook == MS_DNS_SMTP) HP.message.api_dns_smtp();
	else if(hook == MS_DNS_SMS) HP.message.api_dns_sms();
}
static const ApiOps apiOpsMess = { api_base_mess, api_get_mess, 0, api_after_mess, 0, 0, 0, 0 };
extern const ApiGroup ApiGrpMessage;
const ApiGroup ApiGrpMessage = { "message", apiMessTab, sizeof(apiMessTab) / sizeof(apiMessTab[0]), &apiOpsMess };
