/*
 *  Udp.cpp: Library to send/receive UDP packets with the Arduino ethernet shield.
 *  This version only offers minimal wrapping of socket.c/socket.h
 *  Drop Udp.h/.cpp into the Ethernet library directory at hardware/libraries/Ethernet/ 
 *
 * MIT License:
 * Copyright (c) 2008 Bjoern Hartmann
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * bjoern@cs.stanford.edu 12/30/2008
 */

#include "utility/w5100.h"
#include "utility/socket.h"
#include "Ethernet.h"
#include "Udp.h"
#include "Dns.h"

/* Constructor */
EthernetUDP::EthernetUDP() : _sock(MAX_SOCK_NUM) {}

// pav2000 Открывает свободный сокет UDP на порту port. Возвращает 1 при успехе, иначе 0.
uint8_t EthernetUDP::begin(uint16_t port)
{
  if (_sock != MAX_SOCK_NUM) return 0;
  for (uint8_t s = 0; s < MAX_SOCK_NUM; ++s) {
    uint8_t status;
    if (W5100.sockUsable(s) && W5100.readSnSR(s, status) &&
        (status == SnSR::CLOSED || status == SnSR::FIN_WAIT)) return begin(port, s);
  }
  return 0;
}

// pav2000 Переоткрывает UDP-сокет sock на порту port. Возвращает 1 при успехе, иначе 0.
uint8_t EthernetUDP::begin(uint16_t port, uint8_t sock)
{
  if (_sock != sock) stop();
  _sock = MAX_SOCK_NUM;
  _remaining = _offset = 0;
  _packetReady = false;
  if (!socket(sock, SnMR::UDP, port, 0)) return 0;
  _port = port;
  _sock = sock;
  return 1;
}

// pav2000 Возвращает число непрочитанных байтов текущей датаграммы.
int EthernetUDP::available()
{
  return _remaining;
}

// pav2000 Закрывает текущий сокет и сбрасывает состояние приёма и отправки.
void EthernetUDP::stop()
{
  if (_sock != MAX_SOCK_NUM) {
    close(_sock);
    EthernetClass::_server_port[_sock] = 0;
  }
  _sock = MAX_SOCK_NUM;
  _remaining = _offset = 0;
  _packetReady = false;
}

// pav2000 Готовит UDP-пакет для host:port после запроса DNS. Возвращает 1 при успехе, 0 при сбое или код DNS.
int EthernetUDP::beginPacket(const char *host, uint16_t port)
{
  _packetReady = false;
  // Look up the host first
  int ret = 0;
  DNSClient dns;
  IPAddress remote_addr;

  dns.begin(Ethernet.dnsServerIP());
  ret = dns.getHostByName(host, remote_addr);
  if (ret == 1) {
    return beginPacket(remote_addr, port);
  } else {
    return ret;
  }
}

// pav2000 Готовит текущий сокет к отправке UDP-пакета на ip:port. Возвращает 1 при успехе, иначе 0.
int EthernetUDP::beginPacket(IPAddress ip, uint16_t port)
{
  _offset = 0;
  _packetReady = startUDP(_sock, rawIPAddress(ip), port);
  return _packetReady;
}

// pav2000 Открывает сокет sock и готовит UDP-пакет для ip:port. Возвращает 1 при успехе, иначе 0.
int EthernetUDP::beginPacket(IPAddress ip, uint16_t port, uint8_t sock)
{
  return begin(_port, sock) && beginPacket(ip, port);
}

// pav2000 Использует sock для DNS и подготовки UDP-пакета к host:port. Возвращает 1 при успехе, 0 при сбое или код DNS.
int EthernetUDP::beginPacket(const char *host, uint16_t port, uint8_t sock)
{
  _packetReady = false;
  if (_sock == sock) {
    _sock = MAX_SOCK_NUM; // DNS закрывает этот сокет
    _remaining = 0;
  }
  DNSClient dns;
  IPAddress remote;
  dns.begin(Ethernet.dnsServerIP());
  const int result = dns.getHostByName(host, remote, sock);
  return result == 1 ? beginPacket(remote, port, sock) : result;
}

// pav2000 Отправляет подготовленный UDP-пакет. Возвращает 1 при подтверждении отправки, иначе 0.
int EthernetUDP::endPacket()
{
  const bool ready = _packetReady;
  _packetReady = false;
  return ready && sendUDP(_sock, _offset);
}

size_t EthernetUDP::write(uint8_t byte)
{
  return write(&byte, 1);
}

// pav2000 Добавляет до size байтов из buffer в UDP-пакет. Возвращает число записанных байтов и помечает неполный пакет.
size_t EthernetUDP::write(const uint8_t *buffer, size_t size)
{
  if (!_packetReady || !size) return 0;
  const uint16_t count = bufferData(_sock, _offset, buffer, size > UINT16_MAX ? UINT16_MAX : size);
  _offset += count;
  if (count != size) {
    _packetReady = false;
    setWriteError();
  }
  return count;
}

// pav2000 Отбрасывает остаток прошлого пакета и читает заголовок следующего.
// Возвращает длину данных или 0, если пакет пуст, недоступен или чтение не удалось.
int EthernetUDP::parsePacket()
{
  flush();
  if (_remaining || _sock == MAX_SOCK_NUM) return 0;

  uint8_t address[4];
  uint16_t port;
  const int16_t size = recvUdpHeader(_sock, address, port);
  if (size < 0) return 0;
  _remoteIP = address;
  _remotePort = port;
  _remaining = size;
  return size;
}

// pav2000 Извлекает и возвращает один байт текущей датаграммы или -1 при отсутствии данных или сбое.
int EthernetUDP::read()
{
  uint8_t byte;
  return read(&byte, 1) == 1 ? byte : -1;
}

// pav2000 Извлекает до len байтов текущей датаграммы в buffer. Возвращает число байтов или -1 при отсутствии чтения.
int EthernetUDP::read(unsigned char *buffer, size_t len)
{
  if (!_remaining || !len) return -1;
  const int got = recv(_sock, buffer, len < _remaining ? len : _remaining);
  if (got <= 0) return -1;
  _remaining -= got;
  return got;
}

// pav2000 Возвращает следующий байт текущей датаграммы без извлечения или -1 при отсутствии данных или сбое.
int EthernetUDP::peek()
{
  uint8_t byte;
  return _remaining && ::peek(_sock, &byte) ? byte : -1;
}

// pav2000 Отбрасывает остаток текущей датаграммы. При сбое сохраняет счётчик для повторной попытки.
void EthernetUDP::flush()
{
  uint8_t status;
  if (_remaining && W5100.readSnSR(_sock, status) && status == SnSR::UDP &&
      discardReceive(_sock, _remaining)) _remaining = 0;
}
