#include "utility/w5100.h"
#include "utility/socket.h"
extern "C" {
#include "string.h"
}

#include "Ethernet.h"
#include "EthernetClient.h"
#include "EthernetServer.h"

EthernetServer::EthernetServer(uint16_t port)
{
  _port = port;
}
// pav2000 Устанавливает TCP-порт port и переоткрывает первый сокет, на котором удаётся начать прослушивание.
void EthernetServer::begin(uint16_t port)
{
  _port = port;
//  begin();
  for (SOCKET sock = 0; sock < MAX_SOCK_NUM; ++sock) {
    close(sock);
    EthernetClass::_server_port[sock] = 0;
    if (socket(sock, SnMR::TCP, _port, 0) && listen(sock)) {
      EthernetClass::_server_port[sock] = _port;
      return;
    }
  }
}

// pav2000 Запускает прослушивание настроенного TCP-порта на первом подходящем сокете.
void EthernetServer::begin()
{
  for (SOCKET sock = 0; sock < MAX_SOCK_NUM; ++sock) {
    uint8_t status;
    if (W5100.sockUsable(sock) && W5100.readSnSR(sock, status) && status == SnSR::CLOSED &&
        socket(sock, SnMR::TCP, _port, 0) && listen(sock)) {
      EthernetClass::_server_port[sock] = _port;
      return;
    }
  }
}
#ifdef FAST_LIB  // Переделка
// pav2000 Запускает прослушивание настроенного TCP-порта на закрытом сокете sock.
void EthernetServer::begin_(int sock)
{
  uint8_t status;
  if (!W5100.readSnSR(sock, status) || status != SnSR::CLOSED) return;
  if (socket(sock, SnMR::TCP, _port, 0) && listen(sock))
    EthernetClass::_server_port[sock] = _port;
  else {
    EthernetClass::_server_port[sock] = 0;
    close(sock);
  }
}
#endif
#ifdef FAST_LIB  // Переделка
void EthernetServer::accept_(int sock) {
  int listening = 0;
  EthernetClient client(sock);

  if (EthernetClass::_server_port[sock] == _port) {
    if (client.status() == SnSR::LISTEN) {
      listening = 1;
    } 
    else if (client.status() == SnSR::CLOSE_WAIT && !client.available()) {
      client.stop();
    }
  } 

  if (!listening) {
    //begin();
    begin_(sock); // added
  }
}
#else
void EthernetServer::accept()
{
  int listening = 0;

  for (int sock = 0; sock < MAX_SOCK_NUM; sock++) {
    EthernetClient client(sock);

    if (EthernetClass::_server_port[sock] == _port) {
      if (client.status() == SnSR::LISTEN) {
        listening = 1;
      } 
      else if (client.status() == SnSR::CLOSE_WAIT && !client.available()) {
        client.stop();
      }
    } 
  }

  if (!listening) {
    begin();
  }
}
#endif
#ifdef FAST_LIB  // Переделка
// pav2000 Проверяет соединение на сокете sock. Возвращает клиент с готовыми данными или пустой клиент.
EthernetClient EthernetServer::available_(int sock)
{
  if (sock < 0 || sock >= MAX_SOCK_NUM) return EthernetClient();
  uint8_t status;
  if (!W5100.readSnSR(sock, status)) return EthernetClient();
  if (status == SnSR::CLOSED) begin_(sock);
  if (EthernetClass::_server_port[sock] == _port &&
      (status == SnSR::ESTABLISHED || status == SnSR::CLOSE_WAIT)) {
    uint16_t size;
    if (W5100.getRXReceivedSize(sock, size)) {
      if (size) return EthernetClient(sock);
      if (status == SnSR::CLOSE_WAIT) {
        EthernetClient client(sock);
        client.stop();
        begin_(sock);
      }
    }
  }
  return EthernetClient();
}
#else
EthernetClient EthernetServer::available()
{
  accept();

  for (int sock = 0; sock < MAX_SOCK_NUM; sock++) {
    EthernetClient client(sock);
    if (EthernetClass::_server_port[sock] == _port &&
        (client.status() == SnSR::ESTABLISHED ||
         client.status() == SnSR::CLOSE_WAIT)) {
      if (client.available()) {
        // XXX: don't always pick the lowest numbered socket.
        return client;
      }
    }
  }

  return EthernetClient(MAX_SOCK_NUM);
}
#endif

size_t EthernetServer::write(uint8_t b) 
{
  return write(&b, 1);
}

#ifdef FAST_LIB  // Переделка
size_t EthernetServer::write(const uint8_t *buffer, size_t size) {
size_t n = 0;
for (int sock = 0; sock < MAX_SOCK_NUM; sock++) {
          accept_(sock); // added
          EthernetClient client(sock);

         if (EthernetClass::_server_port[sock] == _port &&
          client.status() == SnSR::ESTABLISHED) {
          n += client.write(buffer, size);
         }
    }
return n;
}
#else
size_t EthernetServer::write(const uint8_t *buffer, size_t size) 
{
  size_t n = 0;
  
  accept();

  for (int sock = 0; sock < MAX_SOCK_NUM; sock++) {
    EthernetClient client(sock);

    if (EthernetClass::_server_port[sock] == _port &&
      client.status() == SnSR::ESTABLISHED) {
      n += client.write(buffer, size);
    }
  }
  
  return n;
}
#endif
