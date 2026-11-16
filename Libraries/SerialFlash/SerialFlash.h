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

#ifndef SerialFlash_h_
#define SerialFlash_h_

#include <Arduino.h>

class SerialFlashFile;

enum class SerialFlashStatus : int8_t {
	Ok = 0, Exists = -1, Corrupt = -2, Full = -3, NoSpace = -4, Io = -5, Invalid = -6, NotFound = -7
};

class SerialFlashChip
{
public:
	static bool begin(uint8_t pin = 6);           // контроллер ТН: обмен через общую шину SpiBus (выбор порта SPIClass убран)
	static uint32_t capacity(const uint8_t *id);
	static uint32_t blockSize();
	static bool sleep();
	static bool wakeup();
	static bool readID(uint8_t *buf);
	static bool readSerialNumber(uint8_t *buf);
	static bool read(uint32_t addr, void *buf, uint32_t len);
	static uint8_t readHeader(uint8_t *h, uint32_t addr, bool addr32);   // заголовок Fast Read в h (до 6 байт), возвращает длину
	static bool ready();
	static bool pollReady(bool &done);  // false — сбой обмена; done — операция завершена
	static bool wait(uint32_t timeout = 60000);
	static bool write(uint32_t addr, const void *buf, uint32_t len);
	static bool eraseAll();
	static bool eraseBlock(uint32_t addr);

	static SerialFlashFile open(const char *filename, SerialFlashStatus *status = nullptr);
	static int8_t create(const char *filename, uint32_t length, uint32_t align = 0);
	static int8_t createErasable(const char *filename, uint32_t length) {
		return create(filename, length, blockSize());
	}
	static bool exists(const char *filename);
	static bool remove(const char *filename);
	static bool remove(SerialFlashFile &file);
	static void opendir() { dirindex = 0; }
	static bool readdir(char *filename, uint32_t strsize, uint32_t &filesize, SerialFlashStatus *status = nullptr);
	static uint32_t free_size(SerialFlashStatus *status = nullptr);
	static uint32_t Capacity;
private:
	static uint16_t dirindex; // current position for readdir()
	static uint8_t flags;	// chip features
	static uint8_t busy;	// 0 = ready
				// 1 = suspendable program operation
				// 2 = suspendable erase operation
				// 3 = bulk erase, 4 = page program without suspend
};

extern SerialFlashChip SerialFlash;


class SerialFlashFile
{
public:
	constexpr SerialFlashFile() { }
	operator bool() {
		if (address > 0) return true;
		return false;
	}
	// pav2000 Читает до rdlen байтов в buf с текущей позиции файла. Возвращает число прочитанных байтов или 0 при сбое.
	uint32_t read(void *buf, uint32_t rdlen) {
		const uint32_t remaining = available();
		if (!address || !remaining) return 0;
		if (rdlen > remaining) rdlen = remaining;
		if (!SerialFlash.read(address + offset, buf, rdlen)) return 0;
		offset += rdlen;
		return rdlen;
	}
	// pav2000 Записывает до wrlen байтов из buf с текущей позиции файла. Возвращает число записанных байтов или 0 при сбое.
	uint32_t write(const void *buf, uint32_t wrlen) {
		const uint32_t remaining = available();
		if (!address || !remaining) return 0;
		if (wrlen > remaining) wrlen = remaining;
		if (!SerialFlash.write(address + offset, buf, wrlen)) return 0;
		offset += wrlen;
		return wrlen;
	}
	void seek(uint32_t n) {
		offset = n;
	}
	uint32_t position() {
		return offset;
	}
	uint32_t size() {
		return length;
	}
	uint32_t available() {
		if (offset >= length) return 0;
		return length - offset;
	}
	bool erase();
	void flush() {
	}
	void close() {
	}
	uint32_t getFlashAddress() {
		return address;
	}
protected:
	friend class SerialFlashChip;
	uint32_t address = 0;  // where this file's data begins in the Flash, or zero
	uint32_t length = 0;   // total length of the data in the Flash chip
	uint32_t offset = 0; // current read/write offset in the file
	uint16_t dirindex = 0;
};


#endif
