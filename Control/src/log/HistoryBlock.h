// Блок истории в ОЗУ (history_buffer): чистые операции без замков и железа, проверяются на ПК (tests/test_historyblock.cpp).
// Запись (задача сервиса) и чтение выгрузки (веб) идут из разных задач, поэтому вызывающий оборачивает каждый вызов
// запретом прерываний (irqSave/irqRestore, Rtos.h): блок 512 байт копируется за единицы микросекунд (Н-5).
#ifndef HistoryBlock_h
#define HistoryBlock_h

#include <stdint.h>
#include <string.h>

// Дописать в блок размером size с позиции *pos не больше len байт. Возвращает сколько записано; позиция сдвигается.
// Выход за size невозможен, в том числе при испорченной позиции (*pos > size - ничего не пишется).
static inline uint16_t HistBlock_Append(uint8_t *buf, uint16_t size, uint16_t *pos, const char *data, uint16_t len)
{
	if(*pos >= size) return 0;
	uint16_t n = size - *pos;
	if(n > len) n = len;
	memcpy(buf + *pos, data, n);
	*pos += n;
	return n;
}

// Скопировать блок в dst, если blk - текущий блок записи (cur != 0). false - блок уже ушёл на карту, читать его оттуда.
static inline bool HistBlock_CopyIfCurrent(const uint8_t *buf, uint16_t size, uint32_t cur, uint32_t blk, uint8_t *dst)
{
	if(cur == 0 || blk != cur) return false;
	memcpy(dst, buf, size);
	return true;
}

// Начать новый блок: обнулить буфер, позиция в начало.
static inline void HistBlock_Reset(uint8_t *buf, uint16_t size, uint16_t *pos)
{
	memset(buf, 0, size);
	*pos = 0;
}

#endif
