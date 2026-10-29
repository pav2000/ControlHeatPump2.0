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

/*
 * Общая шина SPI0 с DMA. Контракт и пояснения - SpiBus.h.
 * Код DMA перенесён из Ethernet/utility/spi_dma.cpp без изменений по сути (почти дословная его копия лежит в
 * SdFat/src/SpiDriver/SdSpiSAM3X.cpp - до перевода SdFat на шину). Каналы DMAC 0 (передача) и 1 (приём) общие
 * у всех устройств, конфликта нет, пока обмены сериализованы замком.
 * Модуль собирается и на хосте: tests/sam/Arduino.h подставляет регистры SPI0, DMAC и PIO в ОЗУ (tests/test_spibus.cpp).
 */
#include <Arduino.h>
#include <SpiBus.h>

#define SPI_CHIP_SEL    3   // общий канал CSR всех устройств (CS программные, аппаратный выбор не используется)
#define SPI_DMAC_RX_CH  1   // канал DMAC приёма
#define SPI_DMAC_TX_CH  0   // канал DMAC передачи
#define SPI_TX_IDX      1   // номер аппаратного запроса DMAC для SPI TX
#define SPI_RX_IDX      2   // номер аппаратного запроса DMAC для SPI RX
#define SPI_SCBR_INIT   8   // делитель до настройки устройств: 10,5 МГц, годен любому из трёх
// Арбитраж шинной матрицы под DMA SPI. Приёмник SPI держит один байт: на 42 МГц DMA обязан забрать его за 190 нс
// (16 тактов), иначе следующий байт затирает его (переполнение), канал ждёт недостающий байт до таймаута, а в буфере
// мусор. Так и вышло на стенде, когда флеш начал читаться по DMA (этап 4: 3 сбоя за 4 минуты нагрузки, на этапах 1-3,
// где DMA принимал только короткие блоки W5500 и SD на 28 МГц, - ни одного). Набор значений - USE_SAM3X_BUS_MATRIX_FIX
// из SdFat (Greiman), без изменений: одиночные обращения вместо длинных пачек у системной шины процессора и PDC
// (MCFG[1], MCFG[2]) и ограничение удержания ведомых (SRAM0, SRAM1, 7-й) 16 тактами. 0 - не трогать матрицу.
#define SPIBUS_BUS_MATRIX_FIX 1

SpiBusStat spiBusStat;

struct SpiBusDev {
	Pio     *port;   // порт вывода CS
	uint32_t mask;   // бит вывода CS
	uint32_t csr;    // значение CSR[3] для устройства
};
static SpiBusDev spiDev[SPIDEV_NUM];

uint32_t spiBusCsr(uint8_t scbr)
{
	return SPI_CSR_SCBR(scbr) | SPI_CSR_NCPHA;     // режим 0 (CPOL 0, NCPHA 1), 8 бит; DLYBCT/DLYBS 0 - без пауз
}

bool spiBusUseDma(const void *buf, size_t n)
{
	return n >= SPIBUS_DMA_MIN && (uintptr_t) buf >= SPIBUS_SRAM_START;
}

static inline void dmac_disable() { DMAC->DMAC_EN &= (~DMAC_EN_ENABLE); }
static inline void dmac_enable() { DMAC->DMAC_EN = DMAC_EN_ENABLE; }
static inline void dmac_channel_disable(uint32_t ch) { DMAC->DMAC_CHDR = DMAC_CHDR_DIS0 << ch; }
static inline void dmac_channel_enable(uint32_t ch) { DMAC->DMAC_CHER = DMAC_CHER_ENA0 << ch; }
static inline bool dmac_channel_transfer_done(uint32_t ch) { return (DMAC->DMAC_CHSR & (DMAC_CHSR_ENA0 << ch)) ? false : true; }

// Запуск приёма SPI -> память
static void spiDmaRX(uint8_t *dst, uint16_t count)
{
	dmac_channel_disable(SPI_DMAC_RX_CH);
	DMAC->DMAC_CH_NUM[SPI_DMAC_RX_CH].DMAC_SADDR = (uint32_t) (uintptr_t) &SPI0->SPI_RDR;
	DMAC->DMAC_CH_NUM[SPI_DMAC_RX_CH].DMAC_DADDR = (uint32_t) (uintptr_t) dst;
	DMAC->DMAC_CH_NUM[SPI_DMAC_RX_CH].DMAC_DSCR = 0;
	DMAC->DMAC_CH_NUM[SPI_DMAC_RX_CH].DMAC_CTRLA = count | DMAC_CTRLA_SRC_WIDTH_BYTE | DMAC_CTRLA_DST_WIDTH_BYTE;
	DMAC->DMAC_CH_NUM[SPI_DMAC_RX_CH].DMAC_CTRLB = DMAC_CTRLB_SRC_DSCR | DMAC_CTRLB_DST_DSCR | DMAC_CTRLB_FC_PER2MEM_DMA_FC
	                                             | DMAC_CTRLB_SRC_INCR_FIXED | DMAC_CTRLB_DST_INCR_INCREMENTING;
	DMAC->DMAC_CH_NUM[SPI_DMAC_RX_CH].DMAC_CFG = DMAC_CFG_SRC_PER(SPI_RX_IDX) | DMAC_CFG_SRC_H2SEL | DMAC_CFG_SOD | DMAC_CFG_FIFOCFG_ASAP_CFG;
	dmac_channel_enable(SPI_DMAC_RX_CH);
}

// Запуск передачи память -> SPI; src = 0 - передавать 0xFF (тактирование приёма)
static void spiDmaTX(const uint8_t *src, uint16_t count)
{
	static uint8_t ff = 0xFF;
	uint32_t src_incr = DMAC_CTRLB_SRC_INCR_INCREMENTING;
	if(!src) {
		src = &ff;
		src_incr = DMAC_CTRLB_SRC_INCR_FIXED;
	}
	dmac_channel_disable(SPI_DMAC_TX_CH);
	DMAC->DMAC_CH_NUM[SPI_DMAC_TX_CH].DMAC_SADDR = (uint32_t) (uintptr_t) src;
	DMAC->DMAC_CH_NUM[SPI_DMAC_TX_CH].DMAC_DADDR = (uint32_t) (uintptr_t) &SPI0->SPI_TDR;
	DMAC->DMAC_CH_NUM[SPI_DMAC_TX_CH].DMAC_DSCR = 0;
	DMAC->DMAC_CH_NUM[SPI_DMAC_TX_CH].DMAC_CTRLA = count | DMAC_CTRLA_SRC_WIDTH_BYTE | DMAC_CTRLA_DST_WIDTH_BYTE;
	DMAC->DMAC_CH_NUM[SPI_DMAC_TX_CH].DMAC_CTRLB = DMAC_CTRLB_SRC_DSCR | DMAC_CTRLB_DST_DSCR | DMAC_CTRLB_FC_MEM2PER_DMA_FC
	                                             | src_incr | DMAC_CTRLB_DST_INCR_FIXED;
	DMAC->DMAC_CH_NUM[SPI_DMAC_TX_CH].DMAC_CFG = DMAC_CFG_DST_PER(SPI_TX_IDX) | DMAC_CFG_DST_H2SEL | DMAC_CFG_SOD | DMAC_CFG_FIFOCFG_ALAP_CFG;
	dmac_channel_enable(SPI_DMAC_TX_CH);
}

// Бывшие spiBegin() и spiInit() драйвера W5500. Сброс SPI0 затрагивает всех, поэтому зовётся под замком (W5100.init()).
void spiBusInit()
{
	PIO_Configure(g_APinDescription[PIN_SPI_MOSI].pPort, g_APinDescription[PIN_SPI_MOSI].ulPinType,
	              g_APinDescription[PIN_SPI_MOSI].ulPin, g_APinDescription[PIN_SPI_MOSI].ulPinConfiguration);
	PIO_Configure(g_APinDescription[PIN_SPI_MISO].pPort, g_APinDescription[PIN_SPI_MISO].ulPinType,
	              g_APinDescription[PIN_SPI_MISO].ulPin, g_APinDescription[PIN_SPI_MISO].ulPinConfiguration);
	PIO_Configure(g_APinDescription[PIN_SPI_SCK].pPort, g_APinDescription[PIN_SPI_SCK].ulPinType,
	              g_APinDescription[PIN_SPI_SCK].ulPin, g_APinDescription[PIN_SPI_SCK].ulPinConfiguration);
	pmc_enable_periph_clk(ID_SPI0);
	pmc_enable_periph_clk(ID_DMAC);
	dmac_disable();
	DMAC->DMAC_GCFG = DMAC_GCFG_ARB_CFG_FIXED;
	dmac_enable();
#if SPIBUS_BUS_MATRIX_FIX
	MATRIX->MATRIX_WPMR = 0x4d415400;          // "MAT" + WPEN 0: снять защиту записи регистров матрицы
	MATRIX->MATRIX_MCFG[1] = 1;
	MATRIX->MATRIX_MCFG[2] = 1;
	MATRIX->MATRIX_SCFG[0] = 0x01000010;
	MATRIX->MATRIX_SCFG[1] = 0x01000010;
	MATRIX->MATRIX_SCFG[7] = 0x01000010;
#endif
	Spi *pSpi = SPI0;
	pSpi->SPI_CR = SPI_CR_SPIDIS;
	pSpi->SPI_CR = SPI_CR_SWRST;
	pSpi->SPI_MR = SPI_PCS(SPI_CHIP_SEL) | SPI_MR_MODFDIS | SPI_MR_MSTR;    // ведущий, постоянный выбор канала 3, без контроля режима
	pSpi->SPI_CSR[SPI_CHIP_SEL] = spiBusCsr(SPI_SCBR_INIT);                 // рабочую настройку ставит spiBusSelect()
	pSpi->SPI_CR = SPI_CR_SPIEN;
}

void spiBusSetDevice(SpiDev d, uint8_t csPin, uint8_t scbr)
{
	spiDev[d].port = g_APinDescription[csPin].pPort;
	spiDev[d].mask = g_APinDescription[csPin].ulPin;
	spiDev[d].csr = spiBusCsr(scbr);
	pinMode(csPin, OUTPUT);
	spiDev[d].port->PIO_SODR = spiDev[d].mask;
}

void spiBusSetClock(SpiDev d, uint8_t scbr)
{
	spiDev[d].csr = spiBusCsr(scbr);
}

void spiBusConfig(SpiDev d)
{
	SPI0->SPI_CSR[SPI_CHIP_SEL] = spiDev[d].csr;
}

void spiBusSelect(SpiDev d)
{
	SPI0->SPI_CSR[SPI_CHIP_SEL] = spiDev[d].csr;   // сперва режим устройства, потом CS: первый такт уже на его частоте
	spiDev[d].port->PIO_CODR = spiDev[d].mask;
}

void spiBusDeselect(SpiDev d)
{
	spiDev[d].port->PIO_SODR = spiDev[d].mask;
}

void spiBusDeselectAll()
{
	for(uint8_t d = 0; d < SPIDEV_NUM; d++) if(spiDev[d].port) spiDev[d].port->PIO_SODR = spiDev[d].mask;
}

uint8_t spiBusScbr(SpiDev d)
{
	return spiDev[d].port ? (spiDev[d].csr & SPI_CSR_SCBR_Msk) >> SPI_CSR_SCBR_Pos : 0;
}

uint8_t spiBusXfer(uint8_t b)
{
	Spi *pSpi = SPI0;
	pSpi->SPI_TDR = b;
	while((pSpi->SPI_SR & SPI_SR_RDRF) == 0) {}
	return pSpi->SPI_RDR;
}

void spiBusSend(const uint8_t *buf, size_t n)
{
	Spi *pSpi = SPI0;
	if(spiBusUseDma(buf, n)) {
		while(n) {
			const uint16_t k = n > SPIBUS_DMA_MAX ? SPIBUS_DMA_MAX : n;
			spiDmaTX(buf, k);
			while(!dmac_channel_transfer_done(SPI_DMAC_TX_CH)) {}
			buf += k;
			n -= k;
		}
	} else {   // короткий блок или константа во флеш-памяти МК (DMAC её не читает)
		while((pSpi->SPI_SR & SPI_SR_TXEMPTY) == 0) {}
		for(size_t i = 0; i < n; i++) {
			pSpi->SPI_TDR = buf[i];
			while((pSpi->SPI_SR & SPI_SR_TDRE) == 0) {}
		}
	}
	while((pSpi->SPI_SR & SPI_SR_TXEMPTY) == 0) {}
	(void) pSpi->SPI_RDR;    // приёмник оставить пустым: следующий обмен читает только своё
}

uint8_t spiBusRec(uint8_t *buf, size_t n)
{
	Spi *pSpi = SPI0;
	if(!spiBusUseDma(buf, n)) {
		for(size_t i = 0; i < n; i++) {
			pSpi->SPI_TDR = 0xFF;
			while((pSpi->SPI_SR & SPI_SR_RDRF) == 0) {}
			buf[i] = pSpi->SPI_RDR;
		}
		return SPIBUS_OK;
	}
	uint8_t rtn = SPIBUS_OK;
	(void) pSpi->SPI_SR;     // чтение статуса сбрасывает флаг переполнения от прошлых обменов
	while(n) {
		const uint16_t k = n > SPIBUS_DMA_MAX ? SPIBUS_DMA_MAX : n;
		spiDmaRX(buf, k);
		spiDmaTX(0, k);
		const uint32_t m = millis();
		while(!dmac_channel_transfer_done(SPI_DMAC_RX_CH)) {
			if(millis() - m > SPIBUS_DMA_TIMEOUT) {
				// Поток могли вытеснить между проверкой канала и чтением времени дольше таймаута, а DMA за это время
				// закончил: без перепроверки засчитывался ложный таймаут при целых данных (так было и в spi_dma.cpp;
				// на стенде - таймаут без переполнения под нагрузкой). Такие случаи считаются отдельно.
				if(dmac_channel_transfer_done(SPI_DMAC_RX_CH)) { spiBusStat.dmaLate++; break; }
				dmac_channel_disable(SPI_DMAC_RX_CH);
				dmac_channel_disable(SPI_DMAC_TX_CH);
				rtn = SPIBUS_TIMEOUT;
				spiBusStat.dmaTimeouts++;
				break;
			}
		}
		if(rtn) break;       // дальше читать нет смысла: данные уже испорчены
		buf += k;
		n -= k;
	}
	if(pSpi->SPI_SR & SPI_SR_OVRES) { rtn |= SPIBUS_OVERRUN; spiBusStat.dmaOverruns++; }
	return rtn;
}
