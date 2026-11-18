#include "utility/socket.h"
#include <string.h>

static uint16_t local_port;

// pav2000 Переоткрывает сокет s с протоколом protocol, портом port и флагами flag.
// При port == 0 выбирает локальный порт. Возвращает 1 при подтверждении состояния, иначе 0.
uint8_t socket(SOCKET s, uint8_t protocol, uint16_t port, uint8_t flag)
{
  if (!W5100.sockUsable(s)) return 0;
  uint8_t expected = 0;
  switch (protocol) {
    case SnMR::TCP: expected = SnSR::INIT; break;
    case SnMR::UDP: expected = SnSR::UDP; break;
    case SnMR::IPRAW: expected = SnSR::IPRAW; break;
    case SnMR::MACRAW: expected = SnSR::MACRAW; break;
    case SnMR::PPPOE: expected = SnSR::PPPOE; break;
    default: return 0;
  }
  if (!port) port = ++local_port;
  uint8_t status;
  if (W5100.execCmdSn(s, Sock_CLOSE) && W5100.writeSnIR(s, 0xFF) &&
      W5100.writeSnMR(s, protocol | flag) && W5100.writeSnPORT(s, port) &&
      W5100.execCmdSn(s, Sock_OPEN) && W5100.readSnSR(s, status) && status == expected)
    return 1;
  close(s);
  return 0;
}

// pav2000 Закрывает сокет s и сбрасывает его прерывания.
void close(SOCKET s)
{
  W5100.execCmdSn(s, Sock_CLOSE);
  W5100.writeSnIR(s, 0xFF);
}

// pav2000 Переводит TCP-сокет s из INIT в прослушивание. Возвращает 1 при выполнении команды, иначе 0.
uint8_t listen(SOCKET s)
{
  uint8_t status;
  return W5100.readSnSR(s, status) && status == SnSR::INIT && W5100.execCmdSn(s, Sock_LISTEN);
}

// pav2000 Записывает адрес IPv4 addr и порт port назначения для s. Возвращает true при успехе.
static bool setDestination(SOCKET s, uint8_t *addr, uint16_t port)
{
  return addr && port && (addr[0] || addr[1] || addr[2] || addr[3]) &&
         W5100.writeSnDIPR(s, addr) == 4 && W5100.writeSnDPORT(s, port);
}

// pav2000 Запускает TCP-подключение сокета s к addr:port. Возвращает 1 при выполнении команды, иначе 0.
uint8_t connect(SOCKET s, uint8_t *addr, uint16_t port)
{
  if (!addr || (addr[0] == 0xFF && addr[1] == 0xFF && addr[2] == 0xFF && addr[3] == 0xFF)) return 0;
  const uint32_t started = millis();
  uint8_t command;
  do {
    if (!W5100.readSnCR(s, command)) return 0;
    if (!command) return setDestination(s, addr, port) && W5100.execCmdSn(s, Sock_CONNECT);
  } while (millis() - started <= W5500_EXEC_CMD_TIMEOUT);
  return 0;
}

// pav2000 Подаёт команду завершения TCP-соединения для сокета s.
void disconnect(SOCKET s)
{
  W5100.execCmdSn(s, Sock_DISCON);
}

// pav2000 Публикует len подготовленных байтов TX сокета s и запускает отправку.
// Возвращает true после подтверждения SEND_OK.
bool sendBuffered(SOCKET s, uint16_t len)
{
  if (!W5100.commitTx(s, len)) return false;
  if (W5100.startSend(s)) {
    const uint32_t started = millis();
    do {
      uint8_t irq, status;
      if (!W5100.readSnIR(s, irq) || (irq & SnIR::TIMEOUT)) break;
      if (irq & SnIR::SEND_OK) {
        if (W5100.writeSnIR(s, SnIR::SEND_OK)) return true;
        break;
      }
      if (!W5100.readSnSR(s, status) || status == SnSR::CLOSED) break;
    } while (millis() - started <= W5500_EXEC_CMD_TIMEOUT);
  }
  close(s);
  return false;
}

// pav2000 Отправляет до len байтов из buf по TCP-сокету s в пределах буфера TX.
// Возвращает число подтверждённых байтов или 0 при сбое.
uint16_t send(SOCKET s, const uint8_t *buf, uint16_t len)
{
  if (!len || !buf) return 0;
  if (len > W5100.SSIZE) len = W5100.SSIZE;
  const uint32_t started = millis();
  do {
    uint8_t status;
    uint16_t free;
    if (!W5100.readSnSR(s, status) || (status != SnSR::ESTABLISHED && status != SnSR::CLOSE_WAIT) ||
        !W5100.getTXFreeSize(s, free)) return 0;
    if (free >= len)
      return W5100.bufferTx(s, 0, buf, len) && sendBuffered(s, len) ? len : 0;
  } while (millis() - started <= W5500_EXEC_CMD_TIMEOUT);
  return 0;
}

// pav2000 Читает из s до len байтов в buf. Возвращает число байтов, SOCKET_RX_EMPTY при ожидании
// или SOCKET_RX_ERROR при сбое. При отсутствии данных в закрытом сокете возвращает 0.
int16_t recv(SOCKET s, uint8_t *buf, int16_t len)
{
  if (!len) return 0;
  uint16_t count;
  if (len < 0 || !buf || !W5100.getRXReceivedSize(s, count)) return SOCKET_RX_ERROR;
  if (!count) {
    uint8_t status;
    if (!W5100.readSnSR(s, status)) return SOCKET_RX_ERROR;
    return status == SnSR::LISTEN || status == SnSR::CLOSED || status == SnSR::CLOSE_WAIT ? 0 : SOCKET_RX_EMPTY;
  }
  if (count > uint16_t(len)) count = len;
  return W5100.readRx(s, buf, count) ? count : SOCKET_RX_ERROR;
}

// pav2000 Копирует следующий байт RX сокета s в buf без извлечения. Возвращает 1 при успехе, иначе 0.
uint16_t peek(SOCKET s, uint8_t *buf)
{
  uint16_t count;
  return buf && W5100.getRXReceivedSize(s, count) && count && W5100.readRx(s, buf, 1, true);
}

// pav2000 Извлекает и отбрасывает len байтов из RX сокета s. Возвращает true при успехе.
bool discardReceive(SOCKET s, uint16_t len)
{
  uint16_t count, ptr;
  return !len || (W5100.getRXReceivedSize(s, count) && count >= len &&
                  W5100.readSnRX_RD(s, ptr) && W5100.consumeRx(s, ptr, len));
}

// pav2000 Копирует до len байтов из buf в свободную область TX сокета s со смещением offset.
// Возвращает число скопированных байтов. Указатель записи остаётся прежним.
uint16_t bufferData(SOCKET s, uint16_t offset, const uint8_t *buf, uint16_t len)
{
  uint16_t free;
  if (!len || !W5100.getTXFreeSize(s, free) || offset >= free) return 0;
  if (len > free - offset) len = free - offset;
  return W5100.bufferTx(s, offset, buf, len) ? len : 0;
}

// pav2000 Готовит сокет s к отправке UDP на addr:port. Возвращает 1 при успехе, иначе 0.
int startUDP(SOCKET s, uint8_t *addr, uint16_t port)
{
  uint8_t status;
  return W5100.readSnSR(s, status) && status == SnSR::UDP && setDestination(s, addr, port);
}

// pav2000 Отправляет len подготовленных байтов из UDP-сокета s. Возвращает 1 при подтверждении, иначе 0.
int sendUDP(SOCKET s, uint16_t len)
{
  uint8_t status;
  return W5100.readSnSR(s, status) && status == SnSR::UDP && sendBuffered(s, len);
}

// pav2000 Отправляет len байтов из buf через сокет s на addr:port. Возвращает len при успехе, иначе 0.
uint16_t sendto(SOCKET s, const uint8_t *buf, uint16_t len, uint8_t *addr, uint16_t port)
{
  if (!len || !setDestination(s, addr, port)) return 0;
  return bufferData(s, 0, buf, len) == len && sendBuffered(s, len) ? len : 0;
}

// pav2000 Отправляет len байтов из buf по сокету s с настроенным адресом группы. Возвращает len при успехе, иначе 0.
uint16_t igmpsend(SOCKET s, const uint8_t *buf, uint16_t len)
{
  if (!len) return 0;
  return bufferData(s, 0, buf, len) == len && sendBuffered(s, len) ? len : 0;
}

struct DatagramHeader {
  uint8_t bytes[8]; // Заголовок пакета в формате RX-буфера W5500.
  uint16_t ptr;     // Позиция заголовка по указателю чтения RX.
  uint16_t size;    // Длина полезной нагрузки в байтах.
  uint8_t length;   // Длина заголовка для выбранного режима сокета.
};

// pav2000 Читает заголовок датаграммы сокета s в header согласно режиму mode.
// Возвращает true, если датаграмма доступна целиком. Указатель RX остаётся прежним.
static bool readDatagramHeader(SOCKET s, uint8_t mode, DatagramHeader &header)
{
  switch (mode) {
    case SnMR::UDP: header.length = 8; break;
    case SnMR::IPRAW: header.length = 6; break;
    case SnMR::MACRAW: header.length = 2; break;
    default: return false;
  }

  uint16_t available;
  if (!W5100.getRXReceivedSize(s, available) || available < header.length ||
      !W5100.readSnRX_RD(s, header.ptr) || !W5100.read_data(s, header.ptr, header.bytes, header.length))
    return false;
  header.size = (uint16_t(header.bytes[header.length - 2]) << 8) | header.bytes[header.length - 1];
  if (mode == SnMR::MACRAW) {
    if (header.size < header.length) return false;
    header.size -= header.length;
  }
  return header.size <= available - header.length;
}

// pav2000 Извлекает UDP-заголовок из сокета s и записывает адрес в addr и порт в port.
// Возвращает длину полезных данных или SOCKET_RX_ERROR.
int16_t recvUdpHeader(SOCKET s, uint8_t *addr, uint16_t &port)
{
  uint8_t status;
  DatagramHeader header;
  if (!addr || !W5100.readSnSR(s, status) || status != SnSR::UDP ||
      !readDatagramHeader(s, SnMR::UDP, header) || !W5100.consumeRx(s, header.ptr, header.length))
    return SOCKET_RX_ERROR;
  memcpy(addr, header.bytes, 4);
  port = (uint16_t(header.bytes[4]) << 8) | header.bytes[5];
  return header.size;
}

// pav2000 Извлекает датаграмму из s, копирует до len байтов в buf и при наличии заполняет addr и port.
// Возвращает число скопированных байтов или 0, если чтение не удалось.
uint16_t recvfrom(SOCKET s, uint8_t *buf, uint16_t len, uint8_t *addr, uint16_t *port)
{
  uint8_t mode;
  if (!buf || !len || !W5100.readSnMR(s, mode)) return 0;
  mode &= 0x07;
  DatagramHeader header;
  if (!readDatagramHeader(s, mode, header)) return 0;

  const uint16_t copied = header.size < len ? header.size : len;
  if (!W5100.read_data(s, header.ptr + header.length, buf, copied) ||
      !W5100.consumeRx(s, header.ptr, header.length + header.size)) return 0;
  if (addr && mode != SnMR::MACRAW) memcpy(addr, header.bytes, 4);
  if (port && mode == SnMR::UDP) *port = (uint16_t(header.bytes[4]) << 8) | header.bytes[5];
  return copied;
}

// pav2000 Сохраняет совместимость API. Аргумент сокета не используется: отправка уже ждёт подтверждения.
void flush(SOCKET) {}
