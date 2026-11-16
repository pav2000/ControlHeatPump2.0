#include "utility/w5100.h"
#include "utility/socket.h"

extern "C" {
  #include "string.h"
}

#include "Arduino.h"
#include "Ethernet.h"
#include "EthernetClient.h"
#include "EthernetServer.h"
#include "Dns.h"

#define ETHERNET_CONNECT_TIMEOUT 2000  // ms

#include "FreeRTOS_ARM.h"
#define RTOS_delay(ms) { if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) vTaskDelay(ms/portTICK_PERIOD_MS); else delay(ms); }

uint16_t EthernetClient::_srcport = 1024;

EthernetClient::EthernetClient() : _sock(MAX_SOCK_NUM) {
}

// pav2000 Привязывает клиент к сокету sock; недопустимый номер обозначает отсутствие соединения.
EthernetClient::EthernetClient(uint8_t sock) : _sock(sock < MAX_SOCK_NUM ? sock : MAX_SOCK_NUM) {
}

int EthernetClient::connect(const char* host, uint16_t port) {
  // Look up the host first
  int ret = 0;
  _offset = 0;
  DNSClient dns;
  IPAddress remote_addr;
  dns.begin(Ethernet.dnsServerIP());
  ret = dns.getHostByName(host, remote_addr);

  if (ret == 1) {
    return connect(remote_addr, port);
  } else {
    return ret;
  }
}

// pav2000 Выбирает свободный сокет и подключается к ip:port. Возвращает 1 при успехе, иначе 0.
int EthernetClient::connect(IPAddress ip, uint16_t port)
{
  if (_sock != MAX_SOCK_NUM) return 0;
  for (SOCKET sock = 0; sock < MAX_SOCK_NUM; ++sock) {
    uint8_t status;
    if (W5100.sockUsable(sock) && W5100.readSnSR(sock, status) &&
        (status == SnSR::CLOSED || status == SnSR::FIN_WAIT || status == SnSR::CLOSE_WAIT))
      return connect(ip, port, sock);
  }
  return 0;
}

// Открыть клиента по NAME DNS на конкретный сокет, сокет перед этим сбрасывается
int EthernetClient::connect(const char *host, uint16_t port,uint8_t sock)// pav2000
{
  // Look up the host first
  int ret = 0;
  DNSClient dns;
  IPAddress remote_addr;
  dns.begin(Ethernet.dnsServerIP());
  ret = dns.getHostByName(host, remote_addr,sock); // Запрос через конкретный сокет
  if (ret == 1) { return connect(remote_addr, port,sock); }
  else { return ret; }
}

// pav2000 Переоткрывает сокет sock и подключается к ip:port. Возвращает 1 при установлении TCP-соединения, иначе 0.
int EthernetClient::connect(IPAddress ip, uint16_t port, uint8_t sock)
{
  _offset = 0;
  _bufferFailed = false;
  if (++_srcport == 0) _srcport = 1024;
  if (socket(sock, SnMR::TCP, _srcport, 0) && ::connect(sock, rawIPAddress(ip), port)) {
    const uint32_t started = millis();
    do {
      uint8_t status;
      if (!W5100.readSnSR(sock, status) || status == SnSR::CLOSED) break;
      if (status == SnSR::ESTABLISHED) {
        _sock = sock;
        return 1;
      }
      RTOS_delay(1);
    } while (millis() - started <= ETHERNET_CONNECT_TIMEOUT);
  }
  close(sock);
  _sock = MAX_SOCK_NUM;
  return 0;
}

size_t EthernetClient::write(uint8_t b) {
  return write(&b, 1);
}

// pav2000 Отправляет size байтов из buf и возвращает число подтверждённых байтов.
// При buf == nullptr и size == 0 отправляет накопленный буфер и возвращает 1 при успехе, иначе 0.
size_t EthernetClient::write(const uint8_t *buf, size_t size)
{
  const uint16_t buffered = _offset;
  const bool failed = _bufferFailed;
  _offset = 0;
  _bufferFailed = false;
  size_t sent = 0;
  if (size) {
    sent = send(_sock, buf, size > UINT16_MAX ? UINT16_MAX : size);
    if (sent != size) setWriteError();
  } else if (!buf) {
    // DNS и HTTP-время используют write(nullptr, 0) для отправки буфера.
    sent = !failed && buffered && sendBuffered(_sock, buffered) ? 1 : 0;
    if (!sent) setWriteError();
  }
  return sent;
}

// pav2000 Копирует до size байтов из buffer в буфер TX без отправки. Возвращает число скопированных байтов.
size_t EthernetClient::write_buffer(const uint8_t *buffer, size_t size)
{
  if (_bufferFailed || !size) return 0;
  const uint16_t count = bufferData(_sock, _offset, buffer, size > UINT16_MAX ? UINT16_MAX : size);
  _offset += count;
  if (count != size) {
    _bufferFailed = true;
    setWriteError();
  }
  return count;
}

// pav2000 Копирует до size байтов из buffer в памяти программы в буфер TX. Возвращает число скопированных байтов.
size_t EthernetClient::write_buffer_flash(const uint8_t *buffer, size_t size)
{
  return write_buffer(buffer, size); // SpiBus использует опрос для памяти программы
}

// pav2000 Добавляет byte в буфер TX. Возвращает 1 при успехе, иначе 0.
size_t EthernetClient::write_buffer(uint8_t byte)
{
  return write_buffer(&byte, 1);
}

// pav2000 Возвращает число доступных байтов в RX текущего сокета или 0 при сбое.
int EthernetClient::available()
{
  return W5100.getRXReceivedSize(_sock);
}

// pav2000 Извлекает один байт из RX, возвращает его значение или -1 при отсутствии данных или сбое.
int EthernetClient::read()
{
  uint8_t byte;
  return read(&byte, 1) == 1 ? byte : -1;
}

// pav2000 Читает до size байтов в buf. Возвращает число байтов или отрицательный код состояния RX.
int EthernetClient::read(uint8_t *buf, size_t size)
{
  return recv(_sock, buf, size > INT16_MAX ? INT16_MAX : size);
}

// pav2000 Возвращает следующий байт RX без извлечения или -1 при отсутствии данных или сбое.
int EthernetClient::peek()
{
  uint8_t byte;
  return ::peek(_sock, &byte) ? byte : -1;
}

void EthernetClient::flush() {
  ::flush(_sock);
}

void EthernetClient::stop() {
  if (_sock == MAX_SOCK_NUM)
    return;

  // attempt to close the connection gracefully (send a FIN to other side)
  disconnect(_sock);
  unsigned long start = millis();

  // wait a second for the connection to close
  while (status() != SnSR::CLOSED && millis() - start < 1000)
    RTOS_delay(1);

  // if it hasn't closed, close it forcefully
  if (status() != SnSR::CLOSED)
    close(_sock);

  EthernetClass::_server_port[_sock] = 0;
  _sock = MAX_SOCK_NUM;
}

void EthernetClient::stopBegin() {
  if (_sock == MAX_SOCK_NUM)
    return;
  disconnect(_sock);                  // FIN клиенту; чип закроет сокет сам, когда придёт ответ
}

void EthernetClient::stopEnd() {
  if (_sock == MAX_SOCK_NUM)
    return;
  if (status() != SnSR::CLOSED)
    close(_sock);
  EthernetClass::_server_port[_sock] = 0;
  _sock = MAX_SOCK_NUM;
}

// pav2000 Возвращает 1, если текущий сокет ещё активен или в CLOSE_WAIT остались данные, иначе 0.
uint8_t EthernetClient::connected() {
  if (_sock == MAX_SOCK_NUM) return 0;
  
//  uint8_t s = status();              // pav2000
//  return !(s == SnSR::LISTEN || s == SnSR::CLOSED || s == SnSR::FIN_WAIT || /
//    (s == SnSR::CLOSE_WAIT && !available()));
  uint8_t status;
  return W5100.readSnSR(_sock, status) &&
         !(status == SnSR::LISTEN || status == SnSR::CLOSED || status == SnSR::FIN_WAIT ||
           (status == SnSR::CLOSE_WAIT && !W5100.getRXReceivedSize(_sock)));
}

uint8_t EthernetClient::status() {
  if (_sock == MAX_SOCK_NUM) return SnSR::CLOSED;
  return W5100.readSnSR(_sock);
}

// the next function allows us to use the client returned by
// EthernetServer::available() as the condition in an if-statement.

EthernetClient::operator bool() {
  return _sock != MAX_SOCK_NUM;
}

bool EthernetClient::operator==(const EthernetClient& rhs) {
  return _sock == rhs._sock && _sock != MAX_SOCK_NUM && rhs._sock != MAX_SOCK_NUM;
}

// Возврат объема сободного места в буфере  на запись
int EthernetClient::get_freeTX() {
    if (_sock != MAX_SOCK_NUM)
        return W5100.getTXFreeSize(_sock);
   return 0;
}

// pav2000 Возвращает подтверждённое число байтов в RX текущего сокета или -1 при сбое.
int EthernetClient::get_ReceivedSizeRX()
{
  uint16_t count;
  return W5100.getRXReceivedSize(_sock, count) ? count : -1;
}
