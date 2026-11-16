/* SerialFlash Library - for filesystem-like access to SPI Serial Flash memory
 * https://github.com/PaulStoffregen/SerialFlash
 * Copyright (C) 2015, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this library was funded by PJRC.COM, LLC by sales of Teensy.
 * Please support PJRC's efforts to develop open source software by purchasing
 * Teensy or other genuine PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "SerialFlash.h"
#include <string.h>
#include <SpiBus.h>      // контроллер ТН: флеш на общей шине SPI0 с W5500 и SD (Libraries/SpiBus)

// Обмен с чипом - через общую шину вместо библиотеки SPI ядра. Ядро к любой настройке добавляло паузу DLYBCT 381 нс после
// каждого байта и гоняло данные циклом процессора (~520 нс на байт). Теперь spiBusSelect() ставит канал под флеш
// (42 МГц, без пауз) и опускает CS, данные чтения идут по DMA (190 нс на байт), страница записи - тоже.
#define CSASSERT()  spiBusSelect(SPIDEV_FLASH)
#define CSRELEASE() spiBusDeselect(SPIDEV_FLASH)
#define FLASH_SCBR  2      // 84/2 = 42 МГц: у W25Q32 предел Fast Read 104/133 МГц, у прежнего чтения 03h - 50 МГц
#define FLASH_READ_TRIES 3 // попыток чтения при сбое SPI
uint16_t SerialFlashChip::dirindex = 0;
uint8_t SerialFlashChip::flags = 0;
uint8_t SerialFlashChip::busy = 0;
uint32_t SerialFlashChip::Capacity = 0;

#define FLAG_32BIT_ADDR		0x01	// larger than 16 MByte address
#define FLAG_STATUS_CMD70	0x02	// requires special busy flag check
#define FLAG_DIFF_SUSPEND	0x04	// uses 2 different suspend commands
#define FLAG_MULTI_DIE		0x08	// multiple die, don't read cross 32M barrier
#define FLAG_256K_BLOCKS	0x10	// has 256K erase blocks

static uint8_t nextEraseDie, eraseDieCount, resumeCommand;
static uint32_t eraseDieSize;

// pav2000 Передаёт чипу однобайтовую команду cmd. Возвращает true при успешном обмене SPI.
static bool command(uint8_t cmd)
{
	CSASSERT();
	const bool ok = spiBusXfer(cmd, nullptr) == SPIBUS_OK;
	CSRELEASE();
	return ok;
}

// pav2000 Пишет в h команду cmd и адрес addr. При addr32 использует 4 байта адреса, иначе 3.
// Возвращает длину заголовка.
static uint8_t addressHeader(uint8_t *h, uint8_t cmd, uint32_t addr, bool addr32)
{
	uint8_t n = 0;
	h[n++] = cmd;
	if (addr32) h[n++] = addr >> 24;
	h[n++] = addr >> 16;
	h[n++] = addr >> 8;
	h[n++] = addr;
	return n;
}

// pav2000 Разрешает запись в чип. Возвращает true при успешной передаче команды.
static bool writeEnable()
{
	if (!command(0x06)) return false;
	delayMicroseconds(1);
	return true;
}

// pav2000 Читает состояние чипа командой, выбранной по flags. Возвращает статус обмена, в done пишет признак готовности.
static bool readReady(uint8_t flags, bool &done)
{
	const bool cmd70 = flags & FLAG_STATUS_CMD70;
	uint8_t status;
	CSASSERT();
	const bool ok = spiBusXfer(cmd70 ? 0x70 : 0x05, nullptr) == SPIBUS_OK &&
	                spiBusXfer(0, &status) == SPIBUS_OK;
	CSRELEASE();
	done = ok && (cmd70 ? (status & 0x80) != 0 : (status & 1) == 0);
	return ok;
}

// pav2000 Возобновляет приостановленную операцию. Возвращает true при успехе или отсутствии отложенной команды.
static bool resumeOperation()
{
	if (!resumeCommand) return true;
	if (!writeEnable() || !command(resumeCommand)) return false;
	resumeCommand = 0;
	return true;
}

// pav2000 Запускает стирание кристалла по адресу addr. Возвращает true при успешной передаче команды.
static bool startDieErase(uint32_t addr)
{
	if (!writeEnable()) return false;
	uint8_t header[5];
	const uint8_t n = addressHeader(header, 0xC4, addr, true);
	CSASSERT();
	const bool ok = spiBusSend(header, n) == SPIBUS_OK;
	CSRELEASE();
	return ok;
}

// pav2000 Проверяет текущую операцию и при необходимости запускает стирание следующего кристалла.
// Возвращает статус обмена, в done пишет признак завершения всех операций.
bool SerialFlashChip::pollReady(bool &done)
{
	done = false;
	if (!resumeOperation()) return false;
	if (busy) {
		if (!readReady(flags, done)) return false;
		if (!done) return true;
		busy = 0;
	}
	if (nextEraseDie < eraseDieCount) {
		done = false;
		busy = 3; // при отказе команды на чипе всё ещё может идти стирание
		const bool ok = startDieErase(nextEraseDie * eraseDieSize);
		if (ok) ++nextEraseDie;
		else eraseDieCount = 0;
		return ok;
	}
	done = true;
	return true;
}

// pav2000 Возвращает true, если чип завершил операции и обмен SPI прошёл успешно.
bool SerialFlashChip::ready()
{
	bool done;
	return pollReady(done) && done;
}

// pav2000 Ждёт готовности чипа до timeout мс. Возвращает true при готовности, false при сбое или тайм-ауте.
bool SerialFlashChip::wait(uint32_t timeout)
{
	const uint32_t started = millis();
	for (;;) {
		bool done;
		if (!pollReady(done)) return false;
		if (done) return true;
		if (millis() - started > timeout) return false;
	}
}

// pav2000 Формирует в h заголовок Fast Read с адресом addr и пустым байтом.
// При addr32 использует 4 байта адреса, иначе 3. Возвращает длину заголовка.
// Fast Read 0Bh допускает более высокую частоту SPI, чем 03h.
uint8_t SerialFlashChip::readHeader(uint8_t *h, uint32_t addr, bool addr32)
{
	uint8_t n = addressHeader(h, 0x0B, addr, addr32);
	h[n++] = 0;
	return n;
}

// pav2000 Читает len байтов от addr в buf, при необходимости приостанавливая текущую операцию.
// Возвращает true при успешном чтении и возобновлении операции.
bool SerialFlashChip::read(uint32_t addr, void *buf, uint32_t len)
{
	if (addr > Capacity || len > Capacity - addr || (len && !buf)) return false;
	if (!len) return true;
	if (!resumeOperation()) return false;
	const uint8_t operation = busy;
	if (operation && operation < 3) {
		bool done;
		if (!readReady(flags, done)) return false;
		if (done) busy = 0;
		else {
			if (!writeEnable()) return false;
			const bool spansionProgram = (flags & FLAG_DIFF_SUSPEND) && operation == 1;
			resumeCommand = spansionProgram ? 0x8A : 0x7A;
			bool ok = command(spansionProgram ? 0x85 : 0x75);
			const uint32_t started = millis();
			while (ok && !done) {
				ok = readReady(flags, done);
				if (!done && millis() - started > SPIBUS_DMA_TIMEOUT) break;
			}
			if (!ok || !done) {
				resumeOperation();
				return false;
			}
		}
	} else if (operation && !wait()) return false;

	uint8_t *p = (uint8_t *)buf;
	bool ok = true;
	while (len) {
		uint32_t count = len;
		if (flags & FLAG_MULTI_DIE) {
			const uint32_t remaining = 0x2000000 - (addr & 0x1FFFFFF);
			if (count > remaining) count = remaining;
		}
		uint8_t header[6];
		const uint8_t n = readHeader(header, addr, flags & FLAG_32BIT_ADDR);
		// Чтение можно повторить, в том числе при отказе передачи заголовка.
		uint8_t status = SPIBUS_TIMEOUT;
		for (uint8_t attempt = 0; attempt < FLASH_READ_TRIES && status != SPIBUS_OK; ++attempt) {
			CSASSERT();
			status = spiBusSend(header, n);
			if (status == SPIBUS_OK) status = spiBusRec(p, count);
			CSRELEASE();
		}
		if (status != SPIBUS_OK) { ok = false; break; }
		p += count;
		addr += count;
		len -= count;
	}
	return resumeOperation() && ok;
}

// pav2000 Записывает len байтов из buf по адресу addr страницами. Возвращает true после завершения всех страниц.
bool SerialFlashChip::write(uint32_t addr, const void *buf, uint32_t len)
{
	if (addr > Capacity || len > Capacity - addr || (len && !buf)) return false;
	if (!len) return true;
	const uint8_t *p = (const uint8_t *)buf;
	if (!wait()) return false;
	while (len) {
		if (!writeEnable()) return false;
		const uint32_t remaining = 256 - (addr & 0xFF);
		const uint32_t count = len < remaining ? len : remaining;
		uint8_t header[5];
		const uint8_t n = addressHeader(header, 0x02, addr, flags & FLAG_32BIT_ADDR);
		CSASSERT();
		const bool ok = spiBusSend(header, n) == SPIBUS_OK &&
		                spiBusSend(p, count) == SPIBUS_OK;
		CSRELEASE();
		busy = 4; // уже переданные байты могли попасть в страницу
		if (!ok || !wait(5000)) return false;
		addr += count;
		len -= count;
		p += count;
	}
	return true;
}

// pav2000 Запускает полное стирание чипа. Возвращает true при успешном запуске.
bool SerialFlashChip::eraseAll()
{
	uint8_t id[5];
	if (!Capacity || !readID(id) || !capacity(id)) return false;
	nextEraseDie = eraseDieCount = 0;
	if (id[0] == 0x20 && id[2] >= 0x20 && id[2] <= 0x22) {
		eraseDieCount = id[2] == 0x21 ? 4 : 2;
		eraseDieSize = Capacity / eraseDieCount;
		busy = 3;
		if (!startDieErase(0)) {
			eraseDieCount = 0;
			return false;
		}
		nextEraseDie = 1;
		return true;
	}
	if (!writeEnable()) return false;
	busy = 3;
	return command(0xC7);
}

// pav2000 Запускает стирание блока с выровненным адресом addr. Возвращает true при успешном запуске.
bool SerialFlashChip::eraseBlock(uint32_t addr)
{
	if (addr >= Capacity || blockSize() > Capacity - addr || (addr % blockSize())) return false;
	if (!wait() || !writeEnable()) return false;
	uint8_t header[5];
	const uint8_t n = addressHeader(header, 0xD8, addr, flags & FLAG_32BIT_ADDR);
	CSASSERT();
	const bool ok = spiBusSend(header, n) == SPIBUS_OK;
	CSRELEASE();
	busy = 2;
	return ok;
}

#define ID0_WINBOND	0xEF
#define ID0_SPANSION	0x01
#define ID0_MICRON	0x20
#define ID0_MACRONIX	0xC2
#define ID0_SST		0xBF

//#define FLAG_32BIT_ADDR	0x01	// larger than 16 MByte address
//#define FLAG_STATUS_CMD70	0x02	// requires special busy flag check
//#define FLAG_DIFF_SUSPEND	0x04	// uses 2 different suspend commands
//#define FLAG_256K_BLOCKS	0x10	// has 256K erase blocks

// pav2000 Настраивает SPI с выводом CS pin, определяет ёмкость и режим адресации. Возвращает true при успешной настройке.
bool SerialFlashChip::begin(uint8_t pin)
{
	spiBusInit();                                  // без SD в конфигурации шину до сети никто не поднимет
	spiBusSetDevice(SPIDEV_FLASH, pin, FLASH_SCBR);
	flags = busy = nextEraseDie = eraseDieCount = resumeCommand = 0;
	Capacity = 0;
	uint8_t id[5];
	if (!readID(id)) return false;
	const uint32_t size = capacity(id);
	if (!size) return false;
	uint8_t features = 0;
	if (size > 16777216) {
		features |= FLAG_32BIT_ADDR;
		if (id[0] == ID0_SPANSION) {
			const uint8_t bank[] = {0x17, 0x80};
			CSASSERT();
			const bool ok = spiBusSend(bank, sizeof(bank)) == SPIBUS_OK;
			CSRELEASE();
			if (!ok) return false;
		} else if (!writeEnable() || !command(0xB7)) return false;
		if (id[0] == ID0_MICRON) features |= FLAG_MULTI_DIE;
	}
	if (id[0] == ID0_SPANSION) {
		features |= FLAG_DIFF_SUSPEND;
		if (!id[4]) features |= FLAG_256K_BLOCKS;
	}
	if (id[0] == ID0_MICRON) features |= FLAG_STATUS_CMD70;
	flags = features;
	Capacity = size;
	return true;
}

// pav2000 Ждёт готовности и переводит чип в сон. Возвращает true при успешном обмене.
bool SerialFlashChip::sleep()
{
	return wait() && command(0xB9);
}

// pav2000 Посылает команду выхода из сна. Возвращает true при успешном обмене.
bool SerialFlashChip::wakeup()
{
	return command(0xAB);
}

// pav2000 Читает идентификатор чипа в buf ёмкостью 5 байтов. Возвращает true, если считано 3 байта или 5 для Spansion.
bool SerialFlashChip::readID(uint8_t *buf)
{
	if (!buf || !wait()) return false;
	uint8_t id[5]{};
	CSASSERT();
	bool ok = spiBusXfer(0x9F, nullptr) == SPIBUS_OK;
	for (uint8_t i = 0; ok && i < 3; ++i) ok = spiBusXfer(0, id + i) == SPIBUS_OK;
	if (ok && id[0] == ID0_SPANSION)
		for (uint8_t i = 3; ok && i < sizeof(id); ++i) ok = spiBusXfer(0, id + i) == SPIBUS_OK;
	CSRELEASE();
	if (ok) memcpy(buf, id, id[0] == ID0_SPANSION ? sizeof(id) : 3);
	return ok;
}

// pav2000 Читает 8 байтов серийного номера в buf. Возвращает true при успешном обмене.
bool SerialFlashChip::readSerialNumber(uint8_t *buf)
{
	if (!buf || !wait()) return false;
	const uint8_t header[] = {0x4B, 0, 0, 0, 0};
	CSASSERT();
	const bool ok = spiBusSend(header, sizeof(header)) == SPIBUS_OK &&
	                spiBusRec(buf, 8) == SPIBUS_OK;
	CSRELEASE();
	return ok;
}

uint32_t SerialFlashChip::capacity(const uint8_t *id)
{
	uint32_t n = 1048576; // unknown chips, default to 1 MByte

	if (id[2] >= 16 && id[2] <= 31) {
		n = 1ul << id[2];
	} else
	if (id[2] >= 32 && id[2] <= 37) {
		n = 1ul << (id[2] - 6);
	} else
	if ((id[0]==0 && id[1]==0 && id[2]==0) || 
		(id[0]==255 && id[1]==255 && id[2]==255)) {
		n = 0;
	}
	//Serial.printf("capacity %lu\n", n);
	return n;
}

uint32_t SerialFlashChip::blockSize()
{
	// Spansion chips >= 512 mbit use 256K sectors
	if (flags & FLAG_256K_BLOCKS) return 262144;
	// everything else seems to have 64K sectors
	return 65536;
}




/*
Chip		Uniform Sector Erase
		20/21	52	D8/DC
		-----	--	-----
W25Q64CV	4	32	64
W25Q128FV	4	32	64
S25FL127S			64
N25Q512A	4		64
N25Q00AA	4		64
S25FL512S			256
SST26VF032	4
*/



//			size	sector			busy	pgm/erase	chip
// Part			Mbyte	kbyte	ID bytes	cmd	suspend		erase
// ----			----	-----	--------	---	-------		-----
// Winbond W25Q64CV	8	64	EF 40 17
// Winbond W25Q128FV	16	64	EF 40 18	05	single		60 & C7
// Winbond W25Q256FV	32	64	EF 40 19	
// Spansion S25FL064A	8	?	01 02 16
// Spansion S25FL127S	16	64	01 20 18	05
// Spansion S25FL128P	16	64	01 20 18
// Spansion S25FL256S	32	64	01 02 19	05			60 & C7
// Spansion S25FL512S	64	256	01 02 20
// Macronix MX25L12805D 16	?	C2 20 18
// Macronix MX66L51235F	64		C2 20 1A
// Numonyx M25P128	16	?	20 20 18
// Micron M25P80	1	?	20 20 14
// Micron N25Q128A	16	64	20 BA 18
// Micron N25Q512A	64	?	20 BA 20	70	single		C4 x2
// Micron N25Q00AA	128	64	20 BA 21		single		C4 x4
// Micron MT25QL02GC	256	64	20 BA 22	70			C4 x2
// SST SST25WF010	1/8	?	BF 25 02
// SST SST25WF020	1/4	?	BF 25 03
// SST SST25WF040	1/2	?	BF 25 04
// SST SST25VF016B	1	?	BF 25 41
// SST26VF016			?	BF 26 01
// SST26VF032			?	BF 26 02
// SST25VF032		4	64	BF 25 4A
// SST26VF064		8	?	BF 26 43
// LE25U40CMC		1/2	64	62 06 13

SerialFlashChip SerialFlash;
