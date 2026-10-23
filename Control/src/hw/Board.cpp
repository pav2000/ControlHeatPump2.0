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
 * Плата и её постоянная память: сколько осталось ОЗУ, температура кристалла,
 * сторож питания, программный сброс и его причина, опознание чипа,
 * инициализация SD и SPI-флеша, доступ к I2C EEPROM и часам DS3232, запись
 * настроек в EEPROM с подсчётом контрольной суммы на лету.
 *
 * Отдельная единица трансляции. Чистая
 * часть этого кода (числа, строки, шестнадцатеричный вывод) уже живёт в
 * Util.cpp; здесь осталось то, что без железа не существует: регистры SAM,
 * шина I2C под замком, карта памяти, журнал.
 *
 * Hardware.h тянет Constant.h, а с ним Config.h, поэтому условия
 * конфигурации разрешаются так же, как в главной единице трансляции.
 * Проверено сверкой размеров функций.
 */
#include "Hardware.h"     // объявления этого модуля, eepromI2C, rtcI2C
#include "../core/HeatPump.h"     // HP - опрос при инициализации карты
#include "../core/Rtos.h"         // xI2CSemaphore, SemaphoreTake/Give, Watchdog_AliveHere
#include "../log/Statistics.h"   // card - карта памяти, cid_t, SD_SCK_MHZ
#include <SerialFlash.h>  // микросхема flash на шине SPI

// Сколько осталось свободной памяти. Считается по трём границам: конец
// статических данных, вершина кучи и текущий указатель стека.
#include <malloc.h>
extern char _end;
extern "C" char *sbrk(int i);
void freeRamShow()
{
  char *ramstart = (char *) 0x20070000;
  char *ramend = (char *) 0x20088000;
  char *heapend = sbrk(0);
  register char * stack_ptr asm( "sp" );
  struct mallinfo mi = mallinfo();
 
  journal.jprintf("Ram used (bytes):\n");
  journal.jprintf("  dynamic: %d\n",mi.uordblks); 
  journal.jprintf("  static:  %d\n",&_end - ramstart); 
  journal.jprintf("  stack:   %d\n",ramend - stack_ptr); 
  journal.jprintf("Estimation free Ram: %d\n",stack_ptr - heapend + mi.fordblks);
 
}

int freeRam()
{
  char *heapend = sbrk(0);
  register char * stack_ptr asm( "sp" );
  struct mallinfo mi = mallinfo();
 return stack_ptr - heapend + mi.fordblks;
}

// ---- Чтение температуры чипа SAM3X ----
//http://arduino.cc/forum/index.php/topic,129013.0.html
#define TEMP_TRANS       (float)(SAM3X_ADC_REF/4096.0)
#define TEMP_OFFSET      0.80
#define TEMP_FACTOR      0.00265
#define TEMP_FIXTEMP     27.0
float temp_DUE() 
{
	adc_enable_channel(ADC, ADC_TEMPERATURE_SENSOR);                                                 // Enable the corresponding channel
	adc_enable_ts(ADC);                                                                              // Enable the temperature sensor
	Watchdog_AliveHere();
	delayMicroseconds(100);
	adc_start(ADC);                                                                                  // Start the ADC
	uint32_t timeout = 1000000;
	while(!(ADC->ADC_ISR & (1 << ADC_TEMPERATURE_SENSOR)) && --timeout) ;   						// Wait for end of conversion
	uint32_t ulValue = (unsigned int)*(ADC->ADC_CDR + ADC_TEMPERATURE_SENSOR);                              		 // Read the value
	adc_disable_ts(ADC);
	adc_disable_channel(ADC, ADC_TEMPERATURE_SENSOR);                                                // Disable the corresponding channel
	return TEMP_FIXTEMP + (ulValue * TEMP_TRANS - TEMP_OFFSET) / TEMP_FACTOR;
}

// ---- Включение монитора питания ----
void SupplyMonitorON(uint32_t voltage)
{
	startSupcStatusReg = SUPC->SUPC_SR;                        // запомнить состояние при старте
	journal.jprintf("Supply Controller Register [SUPC_SR]: 0x%08x\n", (unsigned int)(startSupcStatusReg));

	SUPC->SUPC_SMMR |= voltage | SUPC_SMMR_SMRSTEN_ENABLE | SUPC_SMMR_SMSMPL_CSM;   // RESET если напряжение просело, контроль 1/32768 сек
	SUPC->SUPC_MR |= SUPC_MR_KEY(SUPC_KEY_VALUE) | SUPC_MR_BODDIS_ENABLE; // Включение контроля (это лишнее при сбросе это включено)
	journal.jprintf("Supply monitor ON, voltage: %.1fV\n", (float) voltage / 10 + 1.9);
}

// Программный сброс контроллера
void Software_Reset() {
  const int RSTC_KEY = 0xA5;
  RSTC->RSTC_CR = RSTC_CR_KEY(RSTC_KEY) | RSTC_CR_PROCRST | RSTC_CR_PERRST;
  while (true);
}

// ---------------------------------------------------------------------------
// Своя обработка провала assert().
//
// ЗАЧЕМ. В прошивке ровно один assert - внутри атмеловского rtc.c (проверки аргументов
// rtc_set_hour_mode и соседних). Своего кода он не касается, но тянет цепочку
//   libsam(rtc.o) -> __assert_func -> libc.a(lib_a-assert.o) -> fiprintf
//                 -> _vfiprintf_r (3156 байт) + __sprint_r (120)
// - около 3.3 КБ flash в КАЖДОЙ сборке ради сообщения в stderr, который на этой плате никуда не
// подключён (провал assert выглядел бы как молчаливое зависание).
//
// Своё определение отменяет подстановку из libc.a: компоновщик берёт член архива, только если
// символ остался неразрешённым, и ни assert.o, ни форматировщик newlib в прошивку не попадают.
// Правило tools/ci.sh следит, чтобы _vfiprintf_r не вернулся: любой новый вызов printf-семейства
// из libc утащит его обратно незаметно.
extern "C" void __assert_func(const char *file, int line, const char *func, const char *expr)
	__attribute__((noreturn));

extern "C" void __assert_func(const char *file, int line, const char *func, const char *expr)
{
	// Короткими строками: pbuf журнала PRINTF_BUF байт, а путь к файлу длинный.
	journal.jprintf("$ERROR assert failed: %s\n", expr ? expr : "?");
	journal.jprintf(" %s:%d, %s\n", file ? file : "?", line, func ? func : "?");
	journal.jprintf(" Reset in 2 s\n");
	delay(2000);                       // дать журналу дописаться в I2C и в консоль
	Software_Reset();
	while(1) ;                         // Software_Reset не возвращается, это для noreturn
}

// Получить причину последнего сброса контроллера, wdt_addr=1 - адрес, где сработал watchdog
char* ResetCause(void)
{
	uint32_t resetCause = rstc_get_reset_cause(RSTC);
	switch ( resetCause ) {
		case RSTC_GENERAL_RESET:  strcpy(_buf, "General");  break;
		case RSTC_BACKUP_RESET:   strcpy(_buf, "Backup");   break;
		case RSTC_WATCHDOG_RESET: strcpy(_buf, "Watchdog"); break;
		case RSTC_SOFTWARE_RESET: strcpy(_buf, "Software"); break;
		case RSTC_USER_RESET:     strcpy(_buf, "User");     break;
		default:                  strcpy(_buf, "Unknown");  break;
	}
	if((resetCause = lastErrorFreeRtosCode & 0xFF)) { // FreeRTOS error
		strcpy(_buf, "RTOS ");
		switch(resetCause) {
			case 1: strcat(_buf, "CONFIG");  break;
			case 2: strcat(_buf, "MALLOC");  break;
			case 3: strcpy(_buf, "MEM "); strncat(_buf, (char *)&GPBR->SYS_GPBR[1], 10);  break;
			case 4: strcat(_buf, "HARD FAULT");  break;
			case 5: strcat(_buf, "BUS FAULT");  break;
			case 6: strcat(_buf, "USAGE FAULT");  break;
			case 7: strcat(_buf, "TASK #");  break;
		}
	}
	return _buf;
}

#define CHIPiD_BASE_REG_ADDR 0x400E0940
#define CHIPiD_CIDR_OFFSET 0x0
#define CHIPiD_EXID_OFFSET 0x4
#define CHIPiD_CIDR   (*((volatile unsigned long *) (CHIPiD_BASE_REG_ADDR + CHIPiD_CIDR_OFFSET)))
#define CHIPiD_EXID   (*((volatile unsigned long *) (CHIPiD_BASE_REG_ADDR + CHIPiD_EXID_OFFSET)))
void showID() 
{
uint32_t test = 0x0;
test = CHIPiD_CIDR;
journal.jprintf("Chip ID EXID: %d\n",(int)(test)); 
}


// ---------------------------------------------------------------------------
// Определение уникального номера чипа пишет в строку
enum {
	FLASH_RC_OK = 0,        //!< Operation OK
	FLASH_RC_YES = 1,       //!< Yes
	FLASH_RC_NO = 0,        //!< No
	FLASH_RC_ERROR = 0x10,  //!< General error
	FLASH_RC_INVALID,       //!< Invalid argument input
	FLASH_RC_NOT_SUPPORT = 0xFFFFFFFF    //!< Operation is not supported
};
#define EFC_ACCESS_MODE_128       0
#define FLASH_ACCESS_MODE_128    EFC_ACCESS_MODE_128
#define _EFC0_                   (0x400E0A00U)   // Возможно переопределение имен
#define EFC                      ((Efc*)EFC0)
void getIDchip(char *outstr)
{
	uint32_t unique_id[4];

	portDISABLE_INTERRUPTS();
	efc_init(EFC, FLASH_ACCESS_MODE_128, 4);
	if(efc_perform_read_sequence(EFC, EFC_FCMD_STUI, EFC_FCMD_SPUI, unique_id, 4) != FLASH_RC_OK) {
		unique_id[0] = 0;
	}
	portENABLE_INTERRUPTS();
	if(unique_id[0] == 0) {
		strcat(outstr, "FLASH_RC_ERROR");
	} else {
		for(uint8_t i = 0; i < 4; i++) {
			outstr += strlen(outstr);
			itoa(unique_id[i], outstr, 16);
			if(i < 3) strcat(outstr, "-");
		}
	}
}


// Инициализация СД карты
// возврат true - если успешно, false - карты нет работаем без нее
uint8_t initSD(void)
{
	boolean success = false;   // флаг успешности инициализации
	journal.jprintf("Init SD card: ");
#ifdef NO_SD_CONTROL                // Если реализован механизм проверки наличия карты в слоте (выключатель в слоте карты)
	pinMode(PIN_NO_SD_CARD, INPUT);     // ++ CD Программирование проверки наличия карты
	if (digitalReadDirect(PIN_NO_SD_CARD)) {journal.jprintf("slot empty!\n"); return false;}
#endif
	// 1. Инициалазация карты
	if(!card.begin(PIN_SPI_CS_SD, SD_SCK_MHZ(SD_CLOCK))) {
		journal.jprintf("Error %d,%d!\n", card.cardErrorCode(), (int)(card.cardErrorData()));
	} else success = true;  // Карта инициализирована с первого раза

	if(success)  // Запоминаем результаты
		journal.jprintf("OK\n");
	else {
		HP.message.setMessage(pMESSAGE_SD, (char*) "Ошибка инициализации SD карты", 0);    // сформировать уведомление
		SPI_switchW5200();
		return false;
	}

	// 2. Вывод инфо о карте
	cid_t cid;
	if(!card.card()->readCID(&cid)) {
		journal.jprintf((char*) "$ERROR - readCID SD card failed!\n");
		HP.message.setMessage(pMESSAGE_SD, (char*) "Ошибка чтения информации о карте (readCID)", 0); // сформировать уведомление
		SPI_switchW5200();
		return 0;
	}
	// вывод информации о карте
	journal.jprintf((char*) "SD card info\n");
	journal.jprintf((char*) " Manufacturer ID: 0x%x\n", int(cid.mid));
	journal.jprintf((char*) " OEM ID: %c%c\n", cid.oid[0], cid.oid[1]);
	journal.jprintf((char*) " Serial number: 0x%x\n", int(cid.psn));
	journal.jprintf((char*) " Volume is FAT%d\n", int(card.vol()->fatType()));
	journal.jprintf((char*) " blocksPerCluster: %d\n", int(card.vol()->blocksPerCluster()));
	journal.jprintf((char*) " clusterCount: %d\n", (int)(card.vol()->clusterCount()));
	journal.jprintf((char*) " freeSpace: %.2f Mb\n", (float) 0.000512 * card.vol()->freeClusterCount() * card.vol()->blocksPerCluster());

	// 3. Проверка наличия индексного файла
	if(!card.exists(INDEX_FILE)) {
		journal.jprintf((char*) " ERROR - Can't find %s file on SD card!\n", INDEX_FILE);
		HP.message.setMessage(pMESSAGE_SD, (char*) "Файл индекса не найден на SD карте", 0); // сформировать уведомление
		SPI_switchW5200();
		return 1;
	} // стартовый файл не найден
	journal.jprintf((char*) " Found %s file\n", INDEX_FILE);

	SPI_switchW5200();
	return 2;
}

// Инициализация SPI диска и проверка наличия веб морды (файл индекс), параметр true - вывод инфо в журнал false молча проверяем состояние
// Возврат true - если морда есть на карте (готово к использованию), false - диск не работает или нет морды (использовать нельзя)
uint8_t initSpiDisk(boolean show)
{
#ifdef SPI_FLASH
	unsigned char id[8];
	if(!SerialFlash.begin(PIN_SPI_CS_FLASH)) {
		if(show) journal.jprintf(" SPI flash not found!\n");
		return false;
	} else {
		if(show) {
			SerialFlash.readID(id);
			journal.jprintf(" Manufacturer ID: 0x%02X\n", id[0]);
			journal.jprintf(" Memory type: 0x%02X\n", id[1]);
			journal.jprintf(" Chip size(0x%02X): %d bytes\n", id[2], (int)(SerialFlash.Capacity));
			journal.jprintf(" Free: %d bytes\n", (int)(SerialFlash.free_size()));
			SerialFlash.readSerialNumber(id);
			journal.jprintf(" Serial number: 0x%02x%02x%02x%02x%02x%02x%02x%02x\n", id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
		}
		if (HP.get_WebStoreOnSPIFlash()) { // проверка наличия файла INDEX_FILE, если стоит флаг загрузки из флеша в настройках
			if (!SerialFlash.exists((char*)INDEX_FILE)) { // файл не найден морды во флеше нет
				HP.set_WebStoreOnSPIFlash(false);//  сбрасываем в оперативке (но не на флеше) флаг загрузки из флеш диска
				if(show) journal.jprintf((char*) " ERROR - Can't find %s file on SPI flash!\n", INDEX_FILE);
				HP.message.setMessage(pMESSAGE_SD, (char*) "Файл индекса не найден на SPI флеш диске", 0);// сформировать уведомление
				return 1;
			} else if(show) journal.jprintf((char*) " Found %s file\n", INDEX_FILE);	// файл найден
		} // if HP.get_fSPIFlash()

		return 2;
	}
#endif
}


// ---------------------------------------------------------------------------
// Функции работы с шиной I2C под free RTOS   На шине висят 3 устройства и шину надо делить
// Запись в eeprom, 0 - успешно
byte writeEEPROM_I2C(unsigned long addr, byte *values, unsigned int nBytes)
{
	if(SemaphoreTake(xI2CSemaphore, I2C_TIME_WAIT / portTICK_PERIOD_MS) == pdFALSE) {  // Если шедулер запущен то захватываем семафор
		journal.printf((char*) cErrorMutex, __FUNCTION__, MutexI2CBusy);
		return 7;
	}
	uint32_t _ret = eepromI2C.write(addr, values, nBytes);
	SemaphoreGive(xI2CSemaphore);
	return _ret;
}
// Чтение в eeprom, 0 - успешно
byte readEEPROM_I2C(unsigned long addr, byte *values, unsigned int nBytes)
{
	if(SemaphoreTake(xI2CSemaphore, I2C_TIME_WAIT / portTICK_PERIOD_MS) == pdFALSE) {  // Если шедулер запущен то захватываем семафор
		journal.printf((char*) cErrorMutex, __FUNCTION__, MutexI2CBusy);
		return 7;
	}
	uint32_t _ret = eepromI2C.read(addr, values, nBytes);
	SemaphoreGive(xI2CSemaphore);
	return _ret;
}
// ---- ЧАСЫ  есть проблема - работают на прямую с i2c не через wire ----
// Часы на I2C   Чтение температуры
int16_t getTemp_RtcI2C()
{
	if(SemaphoreTake(xI2CSemaphore, I2C_TIME_WAIT / portTICK_PERIOD_MS) == pdFALSE) {  // Если шедулер запущен то захватываем семафор
		journal.printf((char*) cErrorMutex, __FUNCTION__, MutexI2CBusy);
		return ERROR_TEMPERATURE;
	}
	int16_t rtc_temp = rtcI2C.temperature();
	SemaphoreGive(xI2CSemaphore);
	return rtc_temp;
}

// Часы на I2C   Чтение времени
tmElements_t ret_getTime_RtcI2C;
tmElements_t *getTime_RtcI2C()
{
	if(SemaphoreTake(xI2CSemaphore, I2C_TIME_WAIT / portTICK_PERIOD_MS) == pdFALSE) {  // Если шедулер запущен то захватываем семафор
		journal.printf("getTime_RtcI2C %s", MutexI2CBusy);
		ret_getTime_RtcI2C.Year = 0;
	} else {
		if(rtcI2C.read(ret_getTime_RtcI2C)) ret_getTime_RtcI2C.Year = 0;
		SemaphoreGive(xI2CSemaphore);
	}
	return &ret_getTime_RtcI2C;
}

// Часы на I2C   Установка времени
void setTime_RtcI2C(uint8_t hour, uint8_t min, uint8_t sec)
{
	if(SemaphoreTake(xI2CSemaphore, I2C_TIME_WAIT / portTICK_PERIOD_MS) == pdFALSE) {  // Если шедулер запущен то захватываем семафор
		journal.printf((char*) cErrorMutex, __FUNCTION__, MutexI2CBusy);
		return;
	}
	rtcI2C.setTime(hour, min, sec);
	SemaphoreGive(xI2CSemaphore);
}

// Часы на I2C   Установка даты
void setDate_RtcI2C(uint8_t date, uint8_t mon, uint16_t year)
{
	if(SemaphoreTake(xI2CSemaphore, I2C_TIME_WAIT / portTICK_PERIOD_MS) == pdFALSE) {  // Если шедулер запущен то захватываем семафор
		journal.printf((char*) cErrorMutex, __FUNCTION__, MutexI2CBusy);
		return;
	}
	rtcI2C.setDate(date, mon, year);
	SemaphoreGive(xI2CSemaphore);
}


// Сохраняет структуру настроек: <длина><структура>, считает CRC, возвращает ошибку или OK
int8_t save_struct(uint32_t &addr_to, uint8_t *addr_from, uint16_t size, uint16_t &crc)
{
	if(size < 128) { // Меньше 128 байт, длина 1 байт, первый бит = 0
		size <<= 1;
		if(writeEEPROM_I2C(addr_to++, (uint8_t *)&size, 1)) {
			set_Error(ERR_SAVE_EEPROM, (char*)nameHeatPump);
			return ERR_SAVE_EEPROM;
		}
		crc = _crc16(crc, size);
	} else { // длина 2 байта, первый бит = 1
		size = (size << 1) | 1;
		if(writeEEPROM_I2C(addr_to, (uint8_t *)&size, 2)) {
			set_Error(ERR_SAVE_EEPROM, (char*)nameHeatPump);
			return ERR_SAVE_EEPROM;
		}
		addr_to += 2;
		crc = _crc16(crc, size & 0xFF);
		crc = _crc16(crc, size >> 8);
	}
	size >>= 1;
	if(writeEEPROM_I2C(addr_to, addr_from, size)) {
		set_Error(ERR_SAVE_EEPROM, (char*)nameHeatPump);
		return ERR_SAVE_EEPROM;
	}
	addr_to += size;
	while(size--) crc = _crc16(crc, *addr_from++);
	return OK;
}

// Запись в каркасе НОВОГО формата: [тег][длина][данные]. Описание каркаса -
// в Util.h. Контрольная сумма считается по тем же байтам, что уходят в
// EEPROM, включая заголовок: иначе перестановка записей не изменила бы сумму.
#define SAVE_REC_ATTEMPTS  3      // сколько раз переписывать запись, если структуру меняли во время записи
int8_t save_rec(uint32_t &addr_to, uint16_t tag, const uint8_t *addr_from, uint16_t size, uint16_t &crc)
{
	uint8_t hdr[REC_HDR_SIZE];
	rec_hdr_put(hdr, tag, size);
	if(writeEEPROM_I2C(addr_to, hdr, sizeof(hdr))) {
		set_Error(ERR_SAVE_EEPROM, (char*)nameHeatPump);
		return ERR_SAVE_EEPROM;
	}
	for(uint8_t i = 0; i < sizeof(hdr); i++) crc = _crc16(crc, hdr[i]);
	addr_to += sizeof(hdr);
	if(size) {
		// Данные пишутся из ЖИВОЙ структуры, которую могут менять другие задачи, а CRC обязана совпасть с
		// записанными байтами: иначе контрольное чтение даст ERR_SETTINGS_VERIFY. Запись с повтором при изменении.
		struct W { static int put(void *a, const uint8_t *d, uint16_t n) { return writeEEPROM_I2C(*(uint32_t *) a, (uint8_t *) d, n); } };
		uint32_t at = addr_to;
		int8_t r = Record_WriteStable(W::put, &at, addr_from, size, &crc, SAVE_REC_ATTEMPTS);
		if(r == 1) {
			set_Error(ERR_SAVE_EEPROM, (char*)nameHeatPump);
			return ERR_SAVE_EEPROM;
		} else if(r == 2) {
			journal.jprintf("Save record %d: data kept changing, gave up\n", (int) tag);
			set_Error(ERR_SETTINGS_VERIFY, (char*)nameHeatPump);
			return ERR_SETTINGS_VERIFY;
		}
		addr_to += size;
	}
	return OK;
}

int8_t save_2bytes(uint32_t &addr_to, uint16_t data, uint16_t &crc)
{
	if(writeEEPROM_I2C(addr_to, (uint8_t *)&data, 2)) {
		set_Error(ERR_SAVE_EEPROM, (char*)nameHeatPump);
		return ERR_SAVE_EEPROM;
	}
	addr_to += 2;
	crc = _crc16(crc, data & 0xFF);
	crc = _crc16(crc, data >> 8);
	return OK;
}
