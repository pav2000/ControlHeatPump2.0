/**
 * Copyright (c) 2011-2018 Bill Greiman
 * This file is part of the SdFat library for SD memory cards.
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "SdSpiDriver.h"
#if defined(__SAM3X8E__) || defined(__SAM3X8H__)
// Контроллер ТН: SD-карта на общей шине SPI0 (Libraries/SpiBus). Прежняя копия кода DMA (почти дословно как в драйвере
// W5500) и перенастройка SPI0 на каждый блок (SPI.beginTransaction, программный сброс, MR, CSR) заменены шиной: канал
// под SD ставит spiBusConfig/spiBusSelect, DMA и счётчики его ошибок общие с сетью.
//------------------------------------------------------------------------------
void SdSpiAltDriver::begin(uint8_t csPin) {
  m_csPin = csPin;
  spiBusInit();                               // SPI0 и DMAC; заодно возвращает режим, если SPI.begin() флеш оставил PS=1
  spiBusSetDevice(SPIDEV_SD, csPin, 255);      // до setSpiSettings - самая низкая частота (329 кГц)
}
//------------------------------------------------------------------------------
// Делитель берётся из SPISettings ядра: поля там закрыты, а считает их ядро своей формулой (div = F_CPU/(clock+1)+1).
// beginTransaction() пишет результат в канал 3 - оттуда его и читаем, поэтому частоты SD ровно прежние: SD_SCK_MHZ(30)
// -> SCBR 3 (28 МГц), инициализация SD_SCK_HZ(250000) -> SCBR 255 (329 кГц). Канал потом перезапишет spiBusConfig().
// Зовётся только при инициализации карты (SdSpiCard::begin), не на каждом блоке.
void SdSpiAltDriver::setSpiSettings(SPISettings spiSettings) {
  SPI.beginTransaction(spiSettings);
  const uint8_t scbr = (SPI0->SPI_CSR[3] & SPI_CSR_SCBR_Msk) >> SPI_CSR_SCBR_Pos;
  SPI.endTransaction();
  spiBusSetClock(SPIDEV_SD, scbr);
}
//------------------------------------------------------------------------------
void SdSpiAltDriver::activate() {
  spiBusConfig(SPIDEV_SD);                    // канал под SD: 74 такта инициализации идут при поднятом CS
}
//------------------------------------------------------------------------------
void SdSpiAltDriver::deactivate() {
}
//------------------------------------------------------------------------------
/** SPI receive a byte */
uint8_t SdSpiAltDriver::receive() {
  return spiBusXfer(0XFF);
}
//------------------------------------------------------------------------------
/** SPI receive multiple bytes */
uint8_t SdSpiAltDriver::receive(uint8_t* buf, size_t n) {
  return spiBusRec(buf, n);                   // 0 - OK, 1 - переполнение, 2 - таймаут DMA (как прежде)
}
//------------------------------------------------------------------------------
/** SPI send a byte */
void SdSpiAltDriver::send(uint8_t b) {
  spiBusXfer(b);
}
//------------------------------------------------------------------------------
void SdSpiAltDriver::send(const uint8_t* buf , size_t n) {
  spiBusSend(buf, n);
}
#endif  // defined(__SAM3X8E__) || defined(__SAM3X8H__)
