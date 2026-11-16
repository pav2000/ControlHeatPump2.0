/*
 * Copyright (c) 2010 by WIZnet <support@wiznet.co.kr>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of either the GNU General Public License version 2
 * or the GNU Lesser General Public License version 2.1, both as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <string.h>
#include "utility/w5100.h"
#include <SpiBus.h>         // общая шина SPI0 с DMA (Libraries/SpiBus)

#define W5500_REG_READ_TIMEOUT 50

#if defined(W5500_ETHERNET_SHIELD)
// W5500 controller instance
W5500Class W5100;

void W5500Class::init(void)
{
    delay(300);

#if 0
  SPI.begin(SPI_CS);
  SPI.setClockDivider(SPI_CS, 3);
  SPI.setDataMode(SPI_CS, SPI_MODE0);
#endif
	spiBusInit();
	spiBusSetDevice(SPIDEV_NET, SS, SPI_RATE);     // SS = 10: CS сетевого чипа

  writeMR(1<<RST);

    uint8_t tx[NL_SOCKETS];
    NetLayout_Fill(layoutMode, tx);           // размеры передачи по сокетам, КБ (регистр Sn_TXBUF_SIZE принимает 0/1/2/4/8/16 как число КБ)
    for (int i=0; i<MAX_SOCK_NUM; i++) {
        uint8_t cntl_byte = (0x0C + (i<<5));
        write( 0x1E, cntl_byte, 2); //0x1E - Sn_RXBUF_SIZE
        write( 0x1F, cntl_byte, tx[i]); //0x1F - Sn_TXBUF_SIZE
    }
}

// pav2000 Возвращает ёмкость TX сокета s в байтах или 0 для недопустимого сокета.
uint16_t W5500Class::txCapacity(SOCKET s) const
{
  if (s >= SOCKETS) return 0;
  uint8_t tx[NL_SOCKETS];
  NetLayout_Fill(layoutMode, tx);
  return uint16_t(tx[s]) * 1024;
}

// pav2000 Читает регистр address сокета s до двух совпадающих значений в пределах limit.
// Записывает результат в size и возвращает true при успехе.
bool W5500Class::stableSize(SOCKET s, uint16_t address, uint16_t limit, uint16_t &size)
{
  const uint32_t started = millis();
  do {
    uint8_t first[2], second[2];
    if (readSn(s, address, first, sizeof(first)) != sizeof(first) ||
        readSn(s, address, second, sizeof(second)) != sizeof(second)) return false;
    if (first[0] == second[0] && first[1] == second[1]) {
      const uint16_t value = (uint16_t(first[0]) << 8) | first[1];
      if (value > limit) return false;
      size = value;
      return true;
    }
  } while (millis() - started <= W5500_REG_READ_TIMEOUT);
  return false;
}

// pav2000 Записывает подтверждённое свободное место TX сокета s в size. Возвращает true при успехе.
bool W5500Class::getTXFreeSize(SOCKET s, uint16_t &size)
{
  return stableSize(s, 0x0020, txCapacity(s), size);
}

// pav2000 Записывает подтверждённое число байтов RX сокета s в size. Возвращает true при успехе.
bool W5500Class::getRXReceivedSize(SOCKET s, uint16_t &size)
{
  return stableSize(s, 0x0026, RX_CAPACITY, size);
}

// pav2000 Копирует len байтов из data в TX сокета s со смещением offset от указателя записи.
// Возвращает true при успехе. Указатель записи остаётся прежним.
bool W5500Class::bufferTx(SOCKET s, uint16_t offset, const uint8_t *data, uint16_t len)
{
  const uint16_t capacity = txCapacity(s);
  if (!capacity || offset > capacity || len > capacity - offset || (len && !data)) return false;
  if (!len) return true;
  uint16_t ptr;
  return readSnTX_WR(s, ptr) && write(ptr + offset, 0x14 + (s << 5), data, len) == len;
}

// pav2000 Продвигает указатель записи TX сокета s на len байтов. Возвращает true при успехе.
bool W5500Class::commitTx(SOCKET s, uint16_t len)
{
  const uint16_t capacity = txCapacity(s);
  if (!capacity || len > capacity) return false;
  if (!len) return true;
  uint16_t ptr;
  if (!readSnTX_WR(s, ptr)) return false;
  if (writeSnTX_WR(s, ptr + len)) return true;
  // Один из байтов указателя мог измениться.
  execCmdSn(s, Sock_CLOSE);
  return false;
}

// pav2000 Продвигает указатель RX сокета s с ptr на len байтов и освобождает принятые данные.
// Возвращает true при успехе.
bool W5500Class::consumeRx(SOCKET s, uint16_t ptr, uint16_t len)
{
  if (writeSnRX_RD(s, ptr + len) && execCmdSn(s, Sock_RECV)) return true;
  execCmdSn(s, Sock_CLOSE);
  return false;
}

// pav2000 Читает len байтов RX сокета s в dst. При peek сохраняет указатель RX. Возвращает true при успехе.
bool W5500Class::readRx(SOCKET s, uint8_t *dst, uint16_t len, bool peek)
{
  uint16_t ptr;
  return readSnRX_RD(s, ptr) && read_data(s, ptr, dst, len) &&
         (peek || consumeRx(s, ptr, len));
}

// pav2000 Копирует len байтов из RX сокета s по адресу ptr в dst. Возвращает true при успехе.
bool W5500Class::read_data(SOCKET s, uint16_t ptr, uint8_t *dst, uint16_t len)
{
  return s < SOCKETS && len <= RX_CAPACITY && (!len || (dst && read(ptr, 0x18 + (s << 5), dst, len) == len));
}

// pav2000 Передаёт заголовок W5500 с адресом addr и байтом control. Возвращает true при успешном обмене SPI.
static bool transferHeader(uint16_t addr, uint8_t control)
{
  return spiBusXfer(addr >> 8, nullptr) == SPIBUS_OK &&
         spiBusXfer(addr, nullptr) == SPIBUS_OK &&
         spiBusXfer(control, nullptr) == SPIBUS_OK;
}

// pav2000 Записывает байт data в addr с байтом управления cb. Возвращает 1 при успехе, иначе 0.
uint8_t W5500Class::write(uint16_t addr, uint8_t cb, uint8_t data)
{
  spiBusSelect(SPIDEV_NET);
  const bool ok = transferHeader(addr, cb) && spiBusXfer(data, nullptr) == SPIBUS_OK;
  spiBusDeselect(SPIDEV_NET);
  return ok;
}

// pav2000 Записывает len байтов из buf в addr с байтом управления cb. Возвращает len при успехе, иначе 0.
uint16_t W5500Class::write(uint16_t addr, uint8_t cb, const uint8_t *buf, uint16_t len)
{
  spiBusSelect(SPIDEV_NET);
  const bool ok = transferHeader(addr, cb) && spiBusSend(buf, len) == SPIBUS_OK;
  spiBusDeselect(SPIDEV_NET);
  return ok ? len : 0;
}

// pav2000 Читает байт value из addr с байтом управления cb. Возвращает true при успехе.
bool W5500Class::readByte(uint16_t addr, uint8_t cb, uint8_t &value)
{
  spiBusSelect(SPIDEV_NET);
  const bool ok = transferHeader(addr, cb) && spiBusXfer(0xFF, &value) == SPIBUS_OK;
  spiBusDeselect(SPIDEV_NET);
  return ok;
}

// pav2000 Читает len байтов из addr в buf с байтом управления cb. Возвращает len при успехе, иначе 0.
uint16_t W5500Class::read(uint16_t addr, uint8_t cb, uint8_t *buf, uint16_t len)
{
  spiBusSelect(SPIDEV_NET);
  const bool ok = transferHeader(addr, cb) && spiBusRec(buf, len) == SPIBUS_OK;
  spiBusDeselect(SPIDEV_NET);
  return ok ? len : 0;
}

// pav2000 Передаёт команду cmd сокету s и ждёт очистки регистра команд. Возвращает true при успехе.
bool W5500Class::execCmdSn(SOCKET s, SockCMD cmd)
{
  if (!writeSnCR(s, cmd)) return false;
  const uint32_t started = millis();
  uint8_t pending;
  do {
    if (!readSnCR(s, pending)) return false;
    if (!pending) return true;
  } while (millis() - started <= W5500_EXEC_CMD_TIMEOUT);
  return false;
}

// pav2000 Сбрасывает старые признаки отправки и запускает SEND для сокета s. Возвращает true при приёме команды.
bool W5500Class::startSend(SOCKET s)
{
  return writeSnIR(s, SnIR::SEND_OK | SnIR::TIMEOUT) && execCmdSn(s, Sock_SEND);
}
#endif
