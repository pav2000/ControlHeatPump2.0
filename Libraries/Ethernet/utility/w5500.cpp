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

    for (int i=0; i<MAX_SOCK_NUM; i++) {
        uint8_t cntl_byte = (0x0C + (i<<5));
        write( 0x1E, cntl_byte, 2); //0x1E - Sn_RXBUF_SIZE
        write( 0x1F, cntl_byte, 2); //0x1F - Sn_TXBUF_SIZE
    }
}

uint16_t W5500Class::getTXFreeSize(SOCKET s)
{
  uint16_t val=0, val1=0;
  do {
    val1 = readSnTX_FSR(s);
    if (val1 != 0)
      val = readSnTX_FSR(s);
  }
  while (val != val1);
  return val;
}

uint16_t W5500Class::getRXReceivedSize(SOCKET s)
{
  uint16_t val=0,val1=0;
  do {
    val1 = readSnRX_RSR(s);
    if (val1 != 0)
      val = readSnRX_RSR(s);
  }
  while (val != val1);
  return val;
}

void W5500Class::send_data_processing(SOCKET s, const uint8_t *data, uint16_t len)
{
  // This is same as having no offset in a call to send_data_processing_offset
  send_data_processing_offset(s, 0, data, len);
}

void W5500Class::send_data_processing_offset(SOCKET s, uint16_t data_offset, const uint8_t *data, uint16_t len)
{
	if(len == 0) return;
    uint16_t ptr = readSnTX_WR(s);
    uint8_t cntl_byte = (0x14+(s<<5));
    ptr += data_offset;
    write(ptr, cntl_byte, data, len);   // передача пакета
    ptr += len;
    writeSnTX_WR(s, ptr);
}

void W5500Class::recv_data_processing(SOCKET s, uint8_t *data, uint16_t len, uint8_t peek)
{
    uint16_t ptr;
    ptr = readSnRX_RD(s);

    read_data(s, ptr, data, len);
    if (!peek)
    {
        ptr += len;
        writeSnRX_RD(s, ptr);
    }
}

void W5500Class::read_data(SOCKET s, volatile uint16_t src, volatile uint8_t *dst, uint16_t len)
{
    uint8_t cntl_byte = (0x18+(s<<5));
    read((uint16_t)src , cntl_byte, (uint8_t *)dst, len);
}

uint8_t W5500Class::write(uint16_t _addr, uint8_t _cb, uint8_t _data)
{
    spiBusSelect(SPIDEV_NET);
    spiBusXfer(_addr >> 8);
    spiBusXfer(_addr & 0xFF);
    spiBusXfer(_cb);
    spiBusXfer(_data);
    spiBusDeselect(SPIDEV_NET);
    return 1;
}
// Передать пакет, есть отличия от 5200 чипа - длина передаваемых данных не передается
uint16_t W5500Class::write(uint16_t _addr, uint8_t _cb, const uint8_t *_buf, uint16_t _len)
{
    spiBusSelect(SPIDEV_NET);
    spiBusXfer(_addr >> 8);
    spiBusXfer(_addr & 0xFF);
    spiBusXfer(_cb);
   // spiSend((0x80 | ((_len & 0x7F00) >> 8)));     // для w5500 длина не передается
   // spiSend(_len & 0x00FF);
    spiBusSend(_buf,_len);
    spiBusDeselect(SPIDEV_NET);
  return _len;
}

uint8_t W5500Class::read(uint16_t _addr, uint8_t _cb)
{
    spiBusSelect(SPIDEV_NET);
    spiBusXfer(_addr >> 8);
    spiBusXfer(_addr & 0xFF);
    spiBusXfer(_cb);
    uint8_t _data = spiBusXfer(0xFF);
    spiBusDeselect(SPIDEV_NET);
  return _data;
}

uint16_t W5500Class::read(uint16_t _addr, uint8_t _cb, uint8_t *_buf, uint16_t _len)
{
    spiBusSelect(SPIDEV_NET);
    spiBusXfer(_addr >> 8);
    spiBusXfer(_addr & 0xFF);
    spiBusXfer( _cb);
  //  spiSend((0x00 | ((_len & 0x7F00) >> 8)));  // для w5500 длина не передается
  //  spiSend(_len & 0x00FF);
    spiBusRec(_buf,_len);
    spiBusDeselect(SPIDEV_NET);
  return _len;
}

void W5500Class::execCmdSn(SOCKET s, SockCMD _cmd) {
    writeSnCR(s, _cmd);     // Send command to socket
//    uint32_t t = GetTickCount();
    while(readSnCR(s)) ; //if(GetTickCount() - t > W5500_EXEC_CMD_TIMEOUT) break;   // Wait for command to complete
}
#endif
