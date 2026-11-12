/*
 * Copyright (c) 2016-2026 by Pavel Panfilov <firstlast2007@gmail.com>  telegram @pav20000
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

#include "ServiceWarnings.h"
#include "HeatPump.h"
#include "../hw/Clock.h"
#include <SpiBus.h>

static uint32_t spiGeneration; // Меняется при ручном обнулении отображаемых счётчиков.

// Сбрасывает отображаемые счётчики SPI и помечает снимок для нового окна.
void ServiceWarningsResetSpi()
{
	const uint32_t irq = irqSave();
	spiBusStat.dmaTimeouts = 0;
	spiBusStat.dmaOverruns = 0;
	spiBusStat.dmaLate = 0;
	++spiGeneration;
	irqRestore(irq);
}

// Возвращает согласованный снимок счётчиков и поколения для сервисной задачи.
static SpiWarningSample readSpiWarningSample()
{
	const uint32_t irq = irqSave();
	const SpiWarningSample sample{{spiBusStat.busTimeouts, spiBusStat.dmaTimeouts,
	                               spiBusStat.dmaOverruns}, spiGeneration};
	irqRestore(irq);
	return sample;
}

// Проверяет время, свободную память и SPI/DMA из одного контекста раз в секунду.
void ServiceWarningsPoll()
{
	const uint32_t now = Uptime_s();
	if(TIME_WARN_ENABLED) {
		static TimedWarning time; // Эпизод недостоверных часов.
		const bool trusted = ClockTimeTrusted(now);
		if(time.Update(now, !trusted, trusted, TIME_UNTRUSTED_CONFIRM_S, 0))
			HP.set_Warning(ErrorCatalog::kTIME, "RTC", 0, nullptr, true);
	}
	if(HEAP_LOW_WARN_ENABLED) {
		static TimedWarning heap; // Эпизод малого запаса памяти.
		const uint8_t suspended = TaskSuspendAll();
		const int32_t bytes = freeRam() + static_cast<int32_t>(Diag.startRAM);
		if(suspended) xTaskResumeAll();
		if(heap.Update(now, bytes < HEAP_LOW_BYTES, bytes >= HEAP_RECOVER_BYTES,
		               HEAP_LOW_CONFIRM_S, HEAP_RECOVER_CONFIRM_S)) {
			char detail[64];
			m_snprintf(detail, sizeof(detail), "Свободно %d байт, порог %d", (int)bytes, HEAP_LOW_BYTES);
			HP.set_Warning(ErrorCatalog::kHEAP_LOW, "RAM", 0, detail, true);
		}
	}
	if(IO_BUS_WARN_ENABLED) {
		static SpiWarning spi; // Счётчики последовательных неблагополучных окон.
		static constexpr unsigned limits[] = {IO_BUS_TIMEOUT_LIMIT, IO_DMA_TIMEOUT_LIMIT,
		                                      IO_DMA_OVERRUN_LIMIT}; // Пороги в порядке снимка SPI.
		const uint8_t reason = spi.Update(now, readSpiWarningSample(), IO_BUS_WINDOW_S, limits, IO_BAD_WINDOWS);
		if(reason) {
			char detail[64];
			m_snprintf(detail, sizeof(detail), "Повторные сбои:%s%s%s",
			           reason & 1 ? " SPI timeout" : "", reason & 2 ? " DMA timeout" : "",
			           reason & 4 ? " DMA overrun" : "");
			HP.set_Warning(ErrorCatalog::kIO_BUS, "SPI", reason, detail, true);
		}
	}
}
