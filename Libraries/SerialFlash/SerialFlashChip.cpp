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
#include <SpiBus.h>      // контроллер ТН: флеш на общей шине SPI0 с W5500 и SD (Libraries/SpiBus)

// Обмен с чипом - через общую шину вместо библиотеки SPI ядра. Ядро к любой настройке добавляло паузу DLYBCT 381 нс после
// каждого байта и гоняло данные циклом процессора (~520 нс на байт). Теперь spiBusSelect() ставит канал под флеш
// (42 МГц, без пауз) и опускает CS, данные чтения идут по DMA (190 нс на байт), страница записи - тоже.
#define CSASSERT()  spiBusSelect(SPIDEV_FLASH)
#define CSRELEASE() spiBusDeselect(SPIDEV_FLASH)
#define FLASH_SCBR  2      // 84/2 = 42 МГц: у W25Q32 предел Fast Read 104/133 МГц, у прежнего чтения 03h - 50 МГц
#define FLASH_READ_TRIES 3 // попыток чтения при сбое DMA

static inline void xfer16(uint16_t w) { spiBusXfer(w >> 8); spiBusXfer(w); }

uint16_t SerialFlashChip::dirindex = 0;
uint8_t SerialFlashChip::flags = 0;
uint8_t SerialFlashChip::busy = 0;
uint32_t SerialFlashChip::Capacity = 0;

#define FLAG_32BIT_ADDR		0x01	// larger than 16 MByte address
#define FLAG_STATUS_CMD70	0x02	// requires special busy flag check
#define FLAG_DIFF_SUSPEND	0x04	// uses 2 different suspend commands
#define FLAG_MULTI_DIE		0x08	// multiple die, don't read cross 32M barrier
#define FLAG_256K_BLOCKS	0x10	// has 256K erase blocks
#define FLAG_DIE_MASK		0xC0	// top 2 bits count during multi-die erase

void SerialFlashChip::wait(void)
{
	uint32_t status;
	//Serial.print("wait-");
	while (1) {
		CSASSERT();
		if (flags & FLAG_STATUS_CMD70) {
			// some Micron chips require this different
			// command to detect program and erase completion
			spiBusXfer(0x70);
			status = spiBusXfer(0);
			CSRELEASE();
			//Serial.printf("b=%02x.", status & 0xFF);
			if ((status & 0x80)) break;
		} else {
			// all others work by simply reading the status reg
			spiBusXfer(0x05);
			status = spiBusXfer(0);
			CSRELEASE();
			//Serial.printf("b=%02x.", status & 0xFF);
			if (!(status & 1)) break;
		}
	}
	busy = 0;
	//Serial.println();
}

// Заголовок чтения Fast Read 0Bh: команда, адрес (3 байта или 4 в режиме 32-битных адресов), пустой байт. Прежняя
// команда 03h у W25Q32 ограничена 50 МГц (у части марок 33 МГц), 0Bh - 104/133 МГц; цена - один байт на вызов.
uint8_t SerialFlashChip::readHeader(uint8_t *h, uint32_t addr, bool addr32)
{
	uint8_t n = 0;
	h[n++] = 0x0B;
	if (addr32) h[n++] = addr >> 24;
	h[n++] = addr >> 16;
	h[n++] = addr >> 8;
	h[n++] = addr;
	h[n++] = 0;
	return n;
}

void SerialFlashChip::read(uint32_t addr, void *buf, uint32_t len)
{
	uint8_t *p = (uint8_t *)buf;
	uint8_t b, f, status, cmd;

	f = flags;                 // буфер больше не обнуляется: его целиком перезаписывает чтение (memset 6 КБ стоил 20-40 мкс)
	b = busy;
	if (b) {
		// read status register ... chip may no longer be busy
		CSASSERT();
		if (flags & FLAG_STATUS_CMD70) {
			spiBusXfer(0x70);
			status = spiBusXfer(0);
			if ((status & 0x80)) b = 0;
		} else {
			spiBusXfer(0x05);
			status = spiBusXfer(0);
			if (!(status & 1)) b = 0;
		}
		CSRELEASE();
		if (b == 0) {
			// chip is no longer busy :-)
			busy = 0;
		} else if (b < 3) {
			// TODO: this may not work on Spansion chips
			// which apparently have 2 different suspend
			// commands, for program vs erase
			CSASSERT();
			spiBusXfer(0x06); // write enable (Micron req'd)
			CSRELEASE();
			delayMicroseconds(1);
			cmd = 0x75; //Suspend program/erase for almost all chips
			// but Spansion just has to be different for program suspend!
			if ((f & FLAG_DIFF_SUSPEND) && (b == 1)) cmd = 0x85;
			CSASSERT();
			spiBusXfer(cmd); // Suspend command
			CSRELEASE();
			if (f & FLAG_STATUS_CMD70) {
				// Micron chips don't actually suspend until flags read
				CSASSERT();
				spiBusXfer(0x70);
				do {
					status = spiBusXfer(0);
				} while (!(status & 0x80));
				CSRELEASE();
			} else {
				CSASSERT();
				spiBusXfer(0x05);
				do {
					status = spiBusXfer(0);
				} while ((status & 0x01));
				CSRELEASE();
			}
		} else {
			// chip is busy with an operation that can not suspend
			wait();			// ждать конца (стирание всего чипа - до десятков секунд)
			b = 0;
		}
	}
	do {
		uint32_t rdlen = len;
		if (f & FLAG_MULTI_DIE) {
			if ((addr & 0xFE000000) != ((addr + len - 1) & 0xFE000000)) {
				rdlen = 0x2000000 - (addr & 0x1FFFFFF);
			}
		}
		uint8_t h[6];
		const uint8_t hl = readHeader(h, addr, f & FLAG_32BIT_ADDR);
		// Сбой DMA (переполнение приёмника или таймаут) оставляет в буфере мусор, а он ушёл бы клиенту как файл морды.
		// Чтение побочных эффектов не имеет - повторяем; каждый сбой виден в счётчиках spiBusStat и журнале ($SPI DMA).
		for (uint8_t t = 1; ; t++) {
			CSASSERT();
			spiBusSend(h, hl);         // заголовок короче порога DMA - опросом
			const uint8_t e = spiBusRec(p, rdlen);   // данные: DMA порциями по 2048 (короткие - опросом)
			CSRELEASE();
			if (e == SPIBUS_OK || t >= FLASH_READ_TRIES) break;
		}
		p += rdlen;
		addr += rdlen;
		len -= rdlen;
	} while (len > 0);
	if (b) {
		CSASSERT();
		spiBusXfer(0x06); // write enable (Micron req'd)
		CSRELEASE();
		delayMicroseconds(1);
		cmd = 0x7A;
		if ((f & FLAG_DIFF_SUSPEND) && (b == 1)) cmd = 0x8A;
		CSASSERT();
		spiBusXfer(cmd); // Resume program/erase
		CSRELEASE();
	}
}

void SerialFlashChip::write(uint32_t addr, const void *buf, uint32_t len)
{
	const uint8_t *p = (const uint8_t *)buf;
	uint32_t max, pagelen;

	 //Serial.printf("WR: addr %08X, len %d\n", addr, len);
	do {
		if (busy) wait();
		CSASSERT();
		// write enable command
		spiBusXfer(0x06);
		CSRELEASE();
		max = 256 - (addr & 0xFF);
		pagelen = (len <= max) ? len : max;
		 //Serial.printf("WR: addr %08X, pagelen %d\n", addr, pagelen);
		delayMicroseconds(1); // TODO: reduce this, but prefer safety first
		CSASSERT();
		if (flags & FLAG_32BIT_ADDR) {
			spiBusXfer(0x02); // program page command
			xfer16(addr >> 16);
			xfer16(addr);
		} else {
			xfer16(0x0200 | ((addr >> 16) & 255));
			xfer16(addr);
		}
		addr += pagelen;
		len -= pagelen;
		spiBusSend(p, pagelen);    // страница: DMA (короткие и константы во флеш МК - опросом)
		p += pagelen;
		CSRELEASE();
		busy = 4;
	} while (len > 0);
}

void SerialFlashChip::eraseAll()
{
	if (busy) wait();
	uint8_t id[5];
	readID(id);
	//Serial.printf("ID: %02X %02X %02X\n", id[0], id[1], id[2]);
	if (id[0] == 0x20 && id[2] >= 0x20 && id[2] <= 0x22) {
		// Micron's multi-die chips require special die erase commands
		//  N25Q512A	20 BA 20  2 dies  32 Mbyte/die   65 nm transitors
		//  N25Q00AA	20 BA 21  4 dies  32 Mbyte/die   65 nm transitors
		//  MT25QL02GC	20 BA 22  2 dies  128 Mbyte/die  45 nm transitors
		uint8_t die_count = 2;
		if (id[2] == 0x21) die_count = 4;
		uint8_t die_index = flags >> 6;
		 //Serial.printf("Micron die erase %d\n", die_index);
		flags &= 0x3F;
		if (die_index >= die_count) return; // all dies erased :-)
		uint8_t die_size = 2;  // in 16 Mbyte units
		if (id[2] == 0x22) die_size = 8;
		CSASSERT();
		spiBusXfer(0x06); // write enable command
		CSRELEASE();
		 delayMicroseconds(1);
		CSASSERT();
		// die erase command
		spiBusXfer(0xC4);
		xfer16((die_index * die_size) << 8);
		xfer16(0x0000);
		CSRELEASE();
		 //Serial.printf("Micron erase begin\n");
		flags |= (die_index + 1) << 6;
	} else {
		// All other chips support the bulk erase command
		CSASSERT();
		// write enable command
		spiBusXfer(0x06);
		CSRELEASE();
		 delayMicroseconds(1);
		CSASSERT();
		// bulk erase command
		spiBusXfer(0xC7);
		CSRELEASE();
	}
	busy = 3;
}

void SerialFlashChip::eraseBlock(uint32_t addr)
{
	uint8_t f = flags;
	if (busy) wait();
	CSASSERT();
	spiBusXfer(0x06); // write enable command
	CSRELEASE();
	 delayMicroseconds(1);
	CSASSERT();
	if (f & FLAG_32BIT_ADDR) {
		spiBusXfer(0xD8);
		xfer16(addr >> 16);
		xfer16(addr);
	} else {
		xfer16(0xD800 | ((addr >> 16) & 255));
		xfer16(addr);
	}
	CSRELEASE();
	busy = 2;
}


bool SerialFlashChip::ready()
{
	uint32_t status;
	if (!busy) return true;
	CSASSERT();
	if (flags & FLAG_STATUS_CMD70) {
		// some Micron chips require this different
		// command to detect program and erase completion
		spiBusXfer(0x70);
		status = spiBusXfer(0);
		CSRELEASE();
		//Serial.printf("ready=%02x\n", status & 0xFF);
		if ((status & 0x80) == 0) return false;
	} else {
		// all others work by simply reading the status reg
		spiBusXfer(0x05);
		status = spiBusXfer(0);
		CSRELEASE();
		//Serial.printf("ready=%02x\n", status & 0xFF);
		if ((status & 1)) return false;
	}
	busy = 0;
	if (flags & 0xC0) {
		// continue a multi-die erase
		eraseAll();
		return false;
	}
	return true;
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

bool SerialFlashChip::begin(uint8_t pin)
{
	uint8_t id[5];
	uint8_t f;

	spiBusInit();                                   // SPI0 и DMAC (без SD в конфигурации шину до сети никто не поднимет)
	spiBusSetDevice(SPIDEV_FLASH, pin, FLASH_SCBR); // CS - выход, поднят
	readID(id);
	if ((id[0]==0 && id[1]==0 && id[2]==0) || (id[0]==255 && id[1]==255 && id[2]==255)) {
		return false;
	}
	f = 0;
	Capacity = capacity(id);
	if (Capacity > 16777216) {
		// more than 16 Mbyte requires 32 bit addresses
		f |= FLAG_32BIT_ADDR;
		if (id[0] == ID0_SPANSION) {
			// spansion uses MSB of bank register
			CSASSERT();
			xfer16(0x1780); // bank register write
			CSRELEASE();
		} else {
			// micron & winbond & macronix use command
			CSASSERT();
			spiBusXfer(0x06); // write enable
			CSRELEASE();
			delayMicroseconds(1);
			CSASSERT();
			spiBusXfer(0xB7); // enter 4 byte addr mode
			CSRELEASE();
		}
		if (id[0] == ID0_MICRON) f |= FLAG_MULTI_DIE;
	}
	if (id[0] == ID0_SPANSION) {
		// Spansion has separate suspend commands
		f |= FLAG_DIFF_SUSPEND;
		if (!id[4]) {
			// Spansion chips with id[4] == 0 use 256K sectors
			f |= FLAG_256K_BLOCKS;
		}
	}
	if (id[0] == ID0_MICRON) {
		// Micron requires busy checks with a different command
		f |= FLAG_STATUS_CMD70; // TODO: all or just multi-die chips?
	}
	flags = f;
	readID(id);
	return true;
}

// chips tested: https://github.com/PaulStoffregen/SerialFlash/pull/12#issuecomment-169596992
//
void SerialFlashChip::sleep()
{
	if (busy) wait();
	CSASSERT();
	spiBusXfer(0xB9); // Deep power down command
	CSRELEASE();
}

void SerialFlashChip::wakeup()
{
	CSASSERT();
	spiBusXfer(0xAB); // Wake up from deep power down command
	CSRELEASE();
}

void SerialFlashChip::readID(uint8_t *buf)
{
	if (busy) wait();
	CSASSERT();
	spiBusXfer(0x9F);
	buf[0] = spiBusXfer(0); // manufacturer ID
	buf[1] = spiBusXfer(0); // memory type
	buf[2] = spiBusXfer(0); // capacity
	if (buf[0] == ID0_SPANSION) {
		buf[3] = spiBusXfer(0); // ID-CFI
		buf[4] = spiBusXfer(0); // sector size
	}
	CSRELEASE();
	//Serial.printf("ID: %02X %02X %02X\n", buf[0], buf[1], buf[2]);
}

void SerialFlashChip::readSerialNumber(uint8_t *buf) //needs room for 8 bytes
{
	if (busy) wait();
	CSASSERT();
	spiBusXfer(0x4B);			
	xfer16(0);	
	xfer16(0);
	for (int i=0; i<8; i++) {		
		buf[i] = spiBusXfer(0);
	}
	CSRELEASE();
//	Serial.printf("Serial Number: %02X %02X %02X %02X %02X %02X %02X %02X\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
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
