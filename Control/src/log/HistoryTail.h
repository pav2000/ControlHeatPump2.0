#pragma once
#include <stdint.h>

// Остаток буфера отправляется только после нормального окончания чтения.
// При ошибке SD/DMA или сети недостоверный хвост не должен становиться
// последним успешным пакетом.
inline bool HistoryTailShouldSend(uint32_t bytes, bool failed)
{
	return bytes != 0 && !failed;
}
