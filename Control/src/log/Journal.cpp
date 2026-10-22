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

//  описание вспомогательных Классов данных, предназначенных для получения информации о ТН
/*
 * Системный журнал: отдельная единица трансляции.
 *
 * Заголовок Journal.h тянет Constant.h, а вместе с ним Config.h - поэтому
 * условные дефайны разрешаются здесь так же, как в главной единице трансляции.
 * Это ровно та ловушка, что ломала Freon, DateTime и stepOne;
 * проверено сверкой таблицы символов, а не рассуждением.
 *
 */
#include "Journal.h"
#include "../hw/Hardware.h"     // readEEPROM_I2C / writeEEPROM_I2C, eepromI2C
#include "../core/Rtos.h"         // _delay, замки шин
#include "../core/Watchdog.h"     // Watchdog_AliveHere
#include "../net/WebShared.h"    // Socket[] - журнал одалживает выходной буфер
#include "mini-printf.h"  // m_vsnprintf


#define bufI2C Socket[0].outBuf

// ---------------------------------------------------------------------------
// ---- Класс системный журнал пишет в консоль и в память ----
//  Журнал лежит в микросхеме I2C, какая именно - выбирается в Config.h
// ---------------------------------------------------------------------------
// Инициализация
void Journal::Init()
{
	err = OK;
	// Отложенная запись: до старта шедулера _post пишет напрямую, но состояние
	// должно быть готово с самого начала.
	JournalDedup_Reset(&dedup);
	JournalRing_Init(&ring, ringbuf, sizeof(ringbuf));
	lastPostTick = 0;
	lostReported = 0;

	uint8_t eepStatus = 0;
	uint16_t i;
	char *ptr;

	if((eepStatus = eepromI2C.begin(I2C_SPEED) != 0))  // Инициализация памяти
	{
		SerialDbg.println("$ERROR - open I2C journal, check I2C chip!");   // ошибка открытия чипа
		err = ERR_OPEN_I2C_JOURNAL;
		return;
	}

	if(checkREADY() == false) // Проверка наличия журнал
	{
		SerialDbg.print("I2C journal not found! ");
		Format();
		return;
	}

	bufferTail = bufferHead = -1;
	full = 0;
	for(i = 0; i < JOURNAL_LEN / W5200_MAX_LEN; i++)   // поиск журнала начала и конца, для ускорения читаем по W5200_MAX_LEN байт
	{
		Watchdog_AliveHere();
		SerialDbg.print(".");
		if(readEEPROM_I2C(I2C_JOURNAL_START + i * W5200_MAX_LEN, (byte*) &bufI2C,W5200_MAX_LEN)) {
			err=ERR_READ_I2C_JOURNAL;
			SerialDbg.print(errorReadI2C);
			break;
		}
		if((ptr = (char*) memchr(bufI2C, I2C_JOURNAL_HEAD, W5200_MAX_LEN)) != NULL) {
			bufferHead=i*W5200_MAX_LEN+(ptr-bufI2C);
		}
		if((ptr = (char*) memchr(bufI2C, I2C_JOURNAL_TAIL ,W5200_MAX_LEN)) != NULL) {
			bufferTail=i*W5200_MAX_LEN+(ptr-bufI2C);
		}
		if((bufferTail >= 0) && (bufferHead >= 0)) break;
	}
	if(bufferTail < bufferHead) full = 1;                   // Буфер полный
	jprintf("\nSTART ---\nFound I2C journal: size %d bytes, head=0x%x, tail=0x%x\n", JOURNAL_LEN, (unsigned int)(bufferHead), (unsigned int)(bufferTail));
}

  
// Записать признак "форматирования" журнала - журналом можно пользоваться
void Journal::writeREADY()
{  
    uint16_t  w=I2C_JOURNAL_READY; 
    if (writeEEPROM_I2C(I2C_JOURNAL_START-2, (byte*)&w,sizeof(w))) 
       { err=ERR_WRITE_I2C_JOURNAL; 
         SerialDbg.println(errorWriteI2C);
        }
}
// Проверить наличие журнала
boolean Journal::checkREADY()
{  
    uint16_t  w=0x0; 
    if (readEEPROM_I2C(I2C_JOURNAL_START-2, (byte*)&w,sizeof(w))) 
       { err=ERR_READ_I2C_JOURNAL; 
         SerialDbg.print(errorReadI2C);
        }
    if (w!=I2C_JOURNAL_READY) return false; else return true;
}

// Форматирование журнала (инициализация I2C памяти уже проведена), sizeof(buf)=W5200_MAX_LEN
void Journal::Format(void)
{
	err = OK;
	printf("Formating I2C journal ");
	uint8_t buf[] = { I2C_JOURNAL_HEAD, I2C_JOURNAL_TAIL };
	if(writeEEPROM_I2C(I2C_JOURNAL_START, (uint8_t*)buf, sizeof(buf))) {
		err = ERR_WRITE_I2C_JOURNAL;
		SerialDbg.println(errorWriteI2C);
	}
	full = 0;                   // Буфер не полный
	bufferHead = 0;
	bufferTail = 1;
	if(err == OK) {
		writeREADY();                 // было форматирование
		jprintf("\nFormat I2C journal (size %d bytes) - Ok\n", JOURNAL_LEN);
	}
}
    
// Печать только в консоль
void Journal::printf(const char *format, ...)             
{
	if(!Is_otg_vbus_high()) return;
	va_list ap;
	va_start(ap, format);
	m_vsnprintf(pbuf, PRINTF_BUF, format, ap);
	va_end(ap);
	SerialDbg.print(pbuf);
}

// Печать в консоль и журнал возвращает число записанных байт
void Journal::jprintf(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	m_vsnprintf(pbuf, PRINTF_BUF, format, ap);
	va_end(ap);
	if(Is_otg_vbus_high()) SerialDbg.print(pbuf);
	_post(pbuf, pbuf);          // вся строка - и тело для сравнения повторов
}

// +Time, далее печать в консоль и журнал
void Journal::jprintf_time(const char *format, ...)
{
	NowTimeToStr(pbuf);
	pbuf[8] = ' ';
	va_list ap;
	va_start(ap, format);
	m_vsnprintf(pbuf + 9, PRINTF_BUF - 9, format, ap);
	va_end(ap);
	if(Is_otg_vbus_high()) SerialDbg.print(pbuf);
	_post(pbuf, pbuf + 9);      // тело без отметки времени "HH:MM:SS "
}   

// +DateTime, далее печать в консоль и журнал
void Journal::jprintf_date(const char *format, ...)
{
	NowDateToStr(pbuf);
	pbuf[10] = ' ';
	NowTimeToStr(pbuf + 11);
	pbuf[19] = ' ';
	va_list ap;
	va_start(ap, format);
	m_vsnprintf(pbuf + 20, PRINTF_BUF - 20, format, ap);
	va_end(ap);
	if(Is_otg_vbus_high()) SerialDbg.print(pbuf);
	_post(pbuf, pbuf + 20);     // тело без отметки "DD.MM.YYYY HH:MM:SS "
}

// отдать журнал в сеть клиенту  Возвращает число записанных байт
int32_t Journal::send_Data(uint8_t thread)
{
	// Свежие строки лежат в кольце и в EEPROM ещё не попали. Перед отдачей
	// журнала кольцо выгружается целиком, иначе последние записи - как раз
	// самые интересные при разборе аварии - в файл не попадут.
	FlushAll();
	int32_t num, len, sum = 0;
	num = bufferHead + 1;                     // Начинаем с начала журнала, num позиция в буфере пропуская символ начала
	for(uint16_t i = 0; i < (JOURNAL_LEN / W5200_MAX_LEN + 1); i++) // Передаем пакетами по W5200_MAX_LEN байт, может быть два неполных пакета!!
	{
		__asm__ volatile ("" ::: "memory");
		if((num > bufferTail))                                        // Текущая позиция больше хвоста (начало передачи)
		{
			if(JOURNAL_LEN - num >= W5200_MAX_LEN) len = W5200_MAX_LEN;	else len = JOURNAL_LEN - num;   // Контроль достижения границы буфера
		} else {                                                        // Текущая позиция меньше хвоста (конец передачи)
			if(bufferTail - num >= W5200_MAX_LEN) len = W5200_MAX_LEN; else len = bufferTail - num;     // Контроль достижения хвоста журнала
		}
		if(readEEPROM_I2C(I2C_JOURNAL_START + num, (byte*) Socket[thread].outBuf, len))         // чтение из памяти
		{
			err = ERR_READ_I2C_JOURNAL;
			SerialDbg.print(errorReadI2C);
			return 0;
		}
		if(sendPacketRTOS(thread, (byte*) Socket[thread].outBuf, len, 0) == 0) return 0;        // передать пакет, при ошибке выйти
		_delay(2);
		sum = sum + len;                                                                        // сколько байт передано
		if(sum >= available()) break;                                                           // Все передано уходим
		num = num + len;                                                                        // Указатель на переданные данные
		if(num >= JOURNAL_LEN) num = 0;                                                         // переходим на начало
	}  // for
	return sum;
}

// Возвращает размер журнала
int32_t Journal::available(void)
{ 
    if (full) return JOURNAL_LEN - 2; else return bufferTail-1;
}    
                 
// чтобы print работал для это класса
size_t Journal::write (uint8_t c)
  {
  SerialDbg.print(char(c));
  return 1;   // one byte output
  }  // end of myOutputtingClass::write
         
// Записать строку в журнал

// Сообщение о подавленных повторах. Текст сообщения включается в запись:
// один счётчик без контекста бесполезен, если исходная строка ушла наверх.
void Journal::_putRepeats(uint16_t repeats, const char *msg)
{
	char rbuf[JRN_DEDUP_CMP + 40];
	char body[JRN_DEDUP_CMP];
	m_snprintf(body, sizeof(body), "%s", msg);
	// у тела обычно есть свой перевод строки - убираем, чтобы не рвать запись
	for(char *p = body; *p; p++) if(*p == '\n' || *p == '\r') { *p = 0; break; }
	int n = m_snprintf(rbuf, sizeof(rbuf), "  ^ повторялось %u раз: %s\n",
	                   (unsigned) repeats, body);
	if(n > 0) JournalRing_Put(&ring, rbuf, (uint16_t) n);
}

// Положить строку в кольцо отложенной записи. Обоснование - в JournalBuf.h.
//   line - строка целиком (с отметкой времени, если она есть)
//   body - та её часть, по которой определяется повтор (без отметки времени)
//
// До старта шедулера писать отложенно нельзя: выгружать кольцо некому,
// а журнал загрузки нужен целиком - пишем сразу.
void Journal::_post(char *line, const char *body)
{
	if(xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) { _write(line); return; }

	uint16_t repeats = 0;
	const char *repeated = NULL;
	JournalAction act = JournalDedup_Check(&dedup, body, &repeats, &repeated);
	if(act == JRN_SUPPRESS) return;
	if(act == JRN_EMIT_REPEATS) _putRepeats(repeats, repeated);
	JournalRing_Put(&ring, line, (uint16_t) m_strlen(line));
	lastPostTick = GetTickCount();
}

// Выгрузить кольцо целиком. Вызывается перед чтением журнала.
void Journal::FlushAll(void)
{
	if(xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return;
	uint8_t chunk[JOURNAL_DRAIN_CHUNK + 2];  // +NUL и +байт HEAD, который _write дописывает за данными
	// Предел на случай, если кто-то пишет быстрее, чем мы выгружаем
	for(uint16_t guard = 0; guard < JOURNAL_RING_SIZE / JOURNAL_DRAIN_CHUNK + 4; guard++) {
		uint16_t n = JournalRing_Get(&ring, chunk, JOURNAL_DRAIN_CHUNK);
		if(!n) break;
		chunk[n] = 0;
		_write((char*) chunk);
	}
}

// Выгрузить пачку из кольца в EEPROM. Вызывается раз в секунду из vServiceHP.
// Мьютекс журнала на время записи НЕ удерживается: запись в EEPROM занимает
// около 5 мс и берёт xI2CSemaphore, а задачи в это время должны иметь
// возможность класть строки в кольцо.
void Journal::Drain(void)
{
	// Потери при переполнении кольца показываем один раз на каждое новое значение
	if(ring.lost != lostReported) {
		lostReported = ring.lost;
		char lbuf[56];
		int n = m_snprintf(lbuf, sizeof(lbuf), "  ^ журнал: потеряно строк %u\n", (unsigned) lostReported);
		if(n > 0) JournalRing_Put(&ring, lbuf, (uint16_t) n);
	}

	if(JournalRing_Used(&ring) == 0) {
		// Ничего не пришло - выдать накопленные повторы, чтобы они не висели
		// в счётчике неопределённо долго.
		if(GetTickCount() - lastPostTick > JOURNAL_REPEAT_TIMEOUT) {
			uint16_t repeats;
			const char *repeated = NULL;
			// Имя повторявшегося сообщения отдаёт сам Flush: иначе счётчик
			// без контекста бесполезен. Из dedup.last его брать нельзя -
			// см. комментарий к JournalDedup_Check.
			if(JournalDedup_Flush(&dedup, &repeats, &repeated)) _putRepeats(repeats, repeated);
			lastPostTick = GetTickCount();
		}
		return;
	}

	// Несколько страниц за вызов: одна страница в секунду - слишком медленно,
	// при всплеске записей кольцо переполнится. Между записями мьютекс шины
	// отпускается, поэтому задачи с высоким приоритетом не ждут всю пачку.
	uint8_t chunk[JOURNAL_DRAIN_CHUNK + 2];  // +NUL и +байт HEAD, который _write дописывает за данными
	for(uint8_t k = 0; k < JOURNAL_DRAIN_CHUNKS; k++) {
		uint16_t n = JournalRing_Get(&ring, chunk, JOURNAL_DRAIN_CHUNK);
		if(!n) break;
		chunk[n] = 0;
		_write((char*) chunk);
	}
}

void Journal::_write(char *dataPtr)
{
	int32_t numBytes;
	if(dataPtr == NULL || (numBytes = strlen(dataPtr)) == 0) return;  // Записывать нечего
	if(numBytes > PRINTF_BUF) numBytes = PRINTF_BUF; // Ограничиваем размер
	// Запись в I2C память
	if(SemaphoreTake(xI2CSemaphore, I2C_TIME_WAIT / portTICK_PERIOD_MS) == pdFALSE) {  // Если шедулер запущен то захватываем семафор
		journal.printf((char*) cErrorMutex, __FUNCTION__, MutexI2CBusy);
		return;
	}
	__asm__ volatile ("" ::: "memory");
	dataPtr[numBytes] = I2C_JOURNAL_TAIL;
	if(full) dataPtr[numBytes + 1] = I2C_JOURNAL_HEAD;
	if(bufferTail + numBytes + 2 > JOURNAL_LEN) { //  Запись в два приема если число записываемых бит больше чем место от конца очереди до конца буфера ( помним про символ начала)
		int32_t n;
		if(eepromI2C.write(I2C_JOURNAL_START + bufferTail, (byte*) dataPtr, n = JOURNAL_LEN - bufferTail)) {
				if(err != ERR_WRITE_I2C_JOURNAL) SerialDbg.print(errorWriteI2C);
			err = ERR_WRITE_I2C_JOURNAL;
		} else {
			dataPtr += n;
			numBytes -= n;
			full = 1;
			if(eepromI2C.write(I2C_JOURNAL_START, (byte*) dataPtr, numBytes + 2)) {
				err = ERR_WRITE_I2C_JOURNAL;
					SerialDbg.print(errorWriteI2C);
			} else {
				bufferTail = numBytes >= 0 ? numBytes : JOURNAL_LEN - 1;
				bufferHead = bufferTail + 1;
				err = OK;
			}
		}
	} else {  // Запись в один прием
		if(eepromI2C.write(I2C_JOURNAL_START + bufferTail, (byte*) dataPtr, numBytes + 1 + full)) {
				if(err != ERR_WRITE_I2C_JOURNAL) SerialDbg.print(errorWriteI2C);
			err = ERR_WRITE_I2C_JOURNAL;
		} else {
			bufferTail += numBytes;
			if(full) bufferHead = bufferTail + 1;
		}
	}
	SemaphoreGive(xI2CSemaphore);
}

    
