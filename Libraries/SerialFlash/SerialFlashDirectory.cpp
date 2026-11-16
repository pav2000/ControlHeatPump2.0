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

/* On-chip SerialFlash file allocation data structures:

  uint32_t signature = 0xFA96554C;
  uint16_t maxfiles          
  uint16_t stringssize  // div by 4
  uint16_t hashes[maxfiles]
  struct {
    uint32_t file_begin
    uint32_t file_length
    uint16_t string_index  // div4
  } fileinfo[maxfiles]
  char strings[stringssize]

A 32 bit signature is stored at the beginning of the flash memory.
If 0xFFFFFFFF is seen, the entire chip should be assumed blank.
If any value other than 0xFA96554C is found, a different data format
is stored.  This could should refuse to access the flash.

The next 4 bytes store number of files and size of the strings
section, which allow the position of every other item to be found.
The string section size is the 16 bit integer times 4, which allows
up to 262140 bytes for string data.

An array of 16 bit filename hashes allows for quick linear search
for potentially matching filenames.  A hash value of 0xFFFF indicates
no file is allocated for the remainder of the array.

Following the hashes, and array of 10 byte structs give the location
and length of the file's actual data, and the offset of its filename
in the strings section.

Strings are null terminated.  The remainder of the chip is file data.
*/

namespace {
using Status = SerialFlashStatus;

constexpr uint32_t Signature = 0xFA96554C;
constexpr uint16_t DefaultSlots = 300;
constexpr uint32_t DefaultNamesSize = 12780;
constexpr uint32_t HeaderSize = 8;
constexpr uint32_t EntrySize = 10;

struct Directory {
	uint16_t slots;
	uint32_t names;
	uint32_t data;

	// pav2000 Возвращает адрес хеша записи каталога с номером index.
	uint32_t hashAddress(uint16_t index) const { return HeaderSize + index * 2u; }
	// pav2000 Возвращает адрес описателя записи каталога с номером index.
	uint32_t entryAddress(uint16_t index) const { return hashAddress(slots) + index * EntrySize; }
};

struct Entry {
	uint32_t address;
	uint32_t size;
	uint32_t name;
};

// pav2000 Читает каталог в dir и инициализирует его на чистой памяти. Возвращает статус операции.
Status readDirectory(Directory &dir)
{
	uint32_t header[2];
	if (SerialFlash.Capacity < HeaderSize) return Status::Corrupt;
	if (!SerialFlash.read(0, header, sizeof(header))) return Status::Io;
	if (header[0] == UINT32_MAX && header[1] == UINT32_MAX) {
		const uint32_t initial[] = {Signature, ((DefaultNamesSize / 4) << 16) | DefaultSlots};
		if (HeaderSize + DefaultSlots * 12u + DefaultNamesSize > SerialFlash.Capacity) return Status::Corrupt;
		if (!SerialFlash.write(0, initial, sizeof(initial)) ||
		    !SerialFlash.read(0, header, sizeof(header)) ||
		    memcmp(header, initial, sizeof(header)) != 0) return Status::Io;
	}
	if (header[0] != Signature) return Status::Corrupt;
	dir.slots = header[1];
	dir.names = dir.entryAddress(dir.slots);
	const uint32_t namesSize = (header[1] >> 16) * 4;
	dir.data = dir.names + namesSize;
	if (!dir.slots || !namesSize || dir.data > SerialFlash.Capacity) return Status::Corrupt;
	return Status::Ok;
}

// pav2000 Возвращает 16-битный хеш имени filename в диапазоне 1…65534.
uint16_t filenameHash(const char *filename)
{
	uint32_t hash = 2166136261;
	for (const char *p = filename; *p; ++p) {
		hash ^= *p;
		hash *= 16777619;
	}
	return (hash % 0xFFFEu) + 1; // 0 и 0xffff зарезервированы
}

// pav2000 Читает и проверяет запись index каталога dir, результат помещает в entry. Возвращает статус.
Status readEntry(const Directory &dir, uint16_t index, Entry &entry)
{
	if (index >= dir.slots) return Status::Corrupt;
	uint32_t raw[3]{};
	if (!SerialFlash.read(dir.entryAddress(index), raw, EntrySize)) return Status::Io;
	entry = {raw[0], raw[1], dir.names + raw[2] * 4};
	if (entry.address < dir.data || entry.address > SerialFlash.Capacity ||
	    entry.size > SerialFlash.Capacity - entry.address || entry.name >= dir.data)
		return Status::Corrupt;
	return Status::Ok;
}

// pav2000 Читает имя от addr до конца области имён dir, при наличии сверяет с expected.
// Записывает длину с нулём в length и возвращает статус чтения и совпадения.
Status scanName(const Directory &dir, uint32_t addr, const char *expected, uint32_t &length)
{
	char buf[16];
	length = 0;
	bool matches = true;
	while (addr < dir.data) {
		uint32_t count = dir.data - addr;
		if (count > sizeof(buf)) count = sizeof(buf);
		if (!SerialFlash.read(addr, buf, count)) return Status::Io;
		for (uint32_t i = 0; i < count; ++i) {
			++length;
			if (expected && matches && *expected++ != buf[i]) matches = false;
			if (buf[i] == 0) return matches ? Status::Ok : Status::NotFound;
		}
		addr += count;
	}
	return Status::Corrupt;
}

// pav2000 Ищет filename в каталоге dir и заполняет index и entry при совпадении.
// Возвращает статус. При отсутствии имени или совпадения index указывает на свободный слот или конец.
Status findEntry(const Directory &dir, const char *filename, uint16_t &index, Entry &entry)
{
	const uint16_t hash = filename ? filenameHash(filename) : 0;
	uint16_t hashes[8];
	index = 0;
	while (index < dir.slots) {
		uint16_t count = dir.slots - index;
		if (count > 8) count = 8;
		if (!SerialFlash.read(dir.hashAddress(index), hashes, count * sizeof(hashes[0]))) return Status::Io;
		for (uint16_t i = 0; i < count; ++i, ++index) {
			if (hashes[i] == UINT16_MAX) return Status::NotFound;
			if (!filename || hashes[i] != hash) continue;
			Status status = readEntry(dir, index, entry);
			if (status != Status::Ok) return status;
			uint32_t length;
			status = scanName(dir, entry.name, filename, length);
			if (status != Status::NotFound) return status;
		}
	}
	return Status::NotFound;
}

// pav2000 Находит начало свободных областей данных и имён для слота index каталога dir.
// Пишет адреса в address и name и возвращает статус.
Status nextSpace(const Directory &dir, uint16_t index, uint32_t &address, uint32_t &name)
{
	if (index >= dir.slots) return Status::Full;
	address = dir.data;
	name = dir.names;
	if (!index) return Status::Ok;
	Entry previous;
	Status status = readEntry(dir, index - 1, previous);
	if (status != Status::Ok) return status;
	uint32_t length;
	status = scanName(dir, previous.name, nullptr, length);
	if (status != Status::Ok) return status;
	address = previous.address + previous.size;
	name = (previous.name + length + 3) & ~3u;
	return Status::Ok;
}

// pav2000 Округляет value вверх до кратного align. Возвращает false при переполнении, иначе true.
bool alignUp(uint32_t &value, uint32_t align)
{
	const uint32_t rem = value % align;
	if (!rem) return true;
	const uint32_t pad = align - rem;
	if (pad > UINT32_MAX - value) return false;
	value += pad;
	return true;
}

// pav2000 Проверяет, что length байтов от address равны 0xFF. Возвращает статус чтения и проверки.
Status checkErased(uint32_t address, uint32_t length)
{
	uint8_t data[16];
	while (length) {
		const uint32_t count = length < sizeof(data) ? length : sizeof(data);
		if (!SerialFlash.read(address, data, count)) return Status::Io;
		for (uint32_t i = 0; i < count; ++i)
			if (data[i] != 0xff) return Status::Corrupt;
		length -= count;
		address += count;
	}
	return Status::Ok;
}

// pav2000 Сохраняет status в out, если он передан. Возвращает true для Status::Ok.
bool report(SerialFlashStatus *out, Status status)
{
	if (out) *out = status;
	return status == Status::Ok;
}
} // namespace

// pav2000 Ищет файл filename и возвращает его дескриптор или пустой файл. Записывает статус в out, если он передан.
SerialFlashFile SerialFlashChip::open(const char *filename, SerialFlashStatus *out)
{
	SerialFlashFile file;
	Directory dir;
	Entry entry;
	uint16_t index;
	Status status = filename && *filename ? readDirectory(dir) : Status::Invalid;
	if (status == Status::Ok) status = findEntry(dir, filename, index, entry);
	if (report(out, status)) {
		file.address = entry.address;
		file.length = entry.size;
		file.dirindex = index;
	}
	return file;
}

// pav2000 Возвращает true, если файл filename найден и его запись в каталоге читается.
bool SerialFlashChip::exists(const char *filename)
{
	return (bool)open(filename);
}

// pav2000 Удаляет запись файла filename из каталога. Возвращает true при успехе.
bool SerialFlashChip::remove(const char *filename)
{
	SerialFlashFile file = open(filename);
	return remove(file);
}

// pav2000 Удаляет запись file из каталога и сбрасывает дескриптор. Возвращает true при успехе.
bool SerialFlashChip::remove(SerialFlashFile &file)
{
	if (!file) return false;
	Directory dir;
	Entry entry;
	if (readDirectory(dir) != Status::Ok ||
	    readEntry(dir, file.dirindex, entry) != Status::Ok ||
	    entry.address != file.address || entry.size != file.length) return false;

	const uint32_t addr = dir.hashAddress(file.dirindex);
	uint16_t hash;
	if (!SerialFlash.read(addr, &hash, sizeof(hash)) || hash == UINT16_MAX) return false;
	const uint16_t deleted = 0;
	if (hash && (!SerialFlash.write(addr, &deleted, sizeof(deleted)) ||
	             !SerialFlash.read(addr, &hash, sizeof(hash)) || hash != deleted)) return false;
	file.close();
	file.address = file.length = file.offset = 0;
	return true;
}

// pav2000 Возвращает размер свободной области данных или 0. Записывает статус в out, если он передан.
uint32_t SerialFlashChip::free_size(SerialFlashStatus *out)
{
	Directory dir;
	Entry entry;
	uint16_t index;
	uint32_t address, name;
	Status status = readDirectory(dir);
	if (status == Status::Ok) {
		status = findEntry(dir, nullptr, index, entry);
		if (status == Status::NotFound) status = nextSpace(dir, index, address, name);
	}
	if (status == Status::Ok &&
	    (name >= dir.data || dir.data - name < 2 || !alignUp(address, 256) || address > Capacity))
		status = Status::NoSpace;
	return report(out, status) ? Capacity - address : 0;
}

// pav2000 Создаёт файл filename с длиной length и выравниванием align, при 0 выравнивает адрес на 256 байтов.
// Возвращает код SerialFlashStatus.
int8_t SerialFlashChip::create(const char *filename, uint32_t length, uint32_t align)
{
	if (!filename || !*filename) return (int8_t)Status::Invalid;
	Directory dir;
	Status status = readDirectory(dir);
	if (status != Status::Ok) return (int8_t)status;

	uint16_t index;
	Entry entry;
	status = findEntry(dir, filename, index, entry);
	if (status == Status::Ok) return (int8_t)Status::Exists;
	if (status != Status::NotFound) return (int8_t)status;

	uint32_t address, name;
	status = nextSpace(dir, index, address, name);
	if (status != Status::Ok) return (int8_t)status;
	const size_t nameLength = strlen(filename);
	if (name >= dir.data || nameLength >= dir.data - name ||
	    !alignUp(address, align ? align : 256) || address > Capacity ||
	    (align && !alignUp(length, align)) || length > Capacity - address) return (int8_t)Status::NoSpace;

	const uint32_t entryAddress = dir.entryAddress(index);
	status = checkErased(entryAddress, EntrySize);
	if (status != Status::Ok) return (int8_t)status;
	status = checkErased(name, nameLength + 1);
	if (status != Status::Ok) return (int8_t)status;

	const uint32_t raw[] = {address, length, (name - dir.names) / 4};
	const uint16_t hash = filenameHash(filename);
	if (!SerialFlash.write(name, filename, nameLength + 1) ||
	    !SerialFlash.write(entryAddress, raw, EntrySize)) return (int8_t)Status::Io;

	uint32_t stored[3]{};
	uint32_t storedLength;
	if (!SerialFlash.read(entryAddress, stored, EntrySize) ||
	    memcmp(raw, stored, EntrySize) != 0 ||
	    scanName(dir, name, filename, storedLength) != Status::Ok) return (int8_t)Status::Io;

	// Хеш публикуется последним. Прерванная запись имени/описателя
	// не станет видимым файлом и не будет перезаписана поверх битов NOR.
	uint16_t storedHash;
	const uint32_t hashAddress = dir.hashAddress(index);
	if (!SerialFlash.write(hashAddress, &hash, sizeof(hash)) ||
	    !SerialFlash.read(hashAddress, &storedHash, sizeof(storedHash)) || storedHash != hash)
		return (int8_t)Status::Io;
	return (int8_t)Status::Ok;
}

// pav2000 Возвращает true для следующего файла, записывая имя в filename ёмкостью capacity и размер в filesize.
// При наличии out записывает статус, в том числе NotFound в конце каталога.
bool SerialFlashChip::readdir(char *filename, uint32_t capacity, uint32_t &filesize, SerialFlashStatus *out)
{
	if (!filename || !capacity) return report(out, Status::Invalid);
	filename[0] = 0;
	Directory dir;
	Status status = readDirectory(dir);
	if (status != Status::Ok) return report(out, status);
	for (uint16_t index = dirindex; index < dir.slots; ++index) {
		uint16_t hash;
		if (!SerialFlash.read(dir.hashAddress(index), &hash, sizeof(hash))) return report(out, Status::Io);
		if (!hash) continue; // удалённый слот
		if (hash == UINT16_MAX) break;
		Entry entry;
		status = readEntry(dir, index, entry);
		if (status != Status::Ok) return report(out, status);
		uint32_t length;
		status = scanName(dir, entry.name, nullptr, length);
		if (status != Status::Ok) return report(out, status);
		const uint32_t count = length < capacity ? length - 1 : capacity - 1;
		if (count && !SerialFlash.read(entry.name, filename, count)) {
			filename[0] = 0;
			return report(out, Status::Io);
		}
		filename[count] = 0;
		filesize = entry.size;
		dirindex = index + 1;
		return report(out, Status::Ok);
	}
	return report(out, Status::NotFound);
}

// pav2000 Стирает блоки, занятые выровненным файлом. Возвращает true после завершения всех операций.
bool SerialFlashFile::erase()
{
	const uint32_t block = SerialFlash.blockSize();
	if (!address || address % block || length % block) return false;
	for (uint32_t i = 0; i < length; i += block)
		if (!SerialFlash.eraseBlock(address + i)) return false;
	return SerialFlash.wait();
}
