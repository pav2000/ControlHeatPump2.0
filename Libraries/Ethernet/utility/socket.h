#ifndef	_SOCKET_H_
#define	_SOCKET_H_

#include "utility/w5100.h"

// Сокет не содержит данных для чтения.
constexpr int16_t SOCKET_RX_EMPTY = -1;
// Ошибка чтения сокета или непригодный заголовок пакета.
constexpr int16_t SOCKET_RX_ERROR = -2;

uint8_t socket(SOCKET s, uint8_t protocol, uint16_t port, uint8_t flag);
void close(SOCKET s);
uint8_t connect(SOCKET s, uint8_t *addr, uint16_t port);
void disconnect(SOCKET s);
uint8_t listen(SOCKET s);
uint16_t send(SOCKET s, const uint8_t *buf, uint16_t len);
int16_t recv(SOCKET s, uint8_t *buf, int16_t len);
uint16_t peek(SOCKET s, uint8_t *buf);
uint16_t sendto(SOCKET s, const uint8_t *buf, uint16_t len, uint8_t *addr, uint16_t port);
uint16_t recvfrom(SOCKET s, uint8_t *buf, uint16_t len, uint8_t *addr, uint16_t *port);
void flush(SOCKET s);
uint16_t igmpsend(SOCKET s, const uint8_t *buf, uint16_t len);

int16_t recvUdpHeader(SOCKET s, uint8_t *addr, uint16_t &port);
bool discardReceive(SOCKET s, uint16_t len);
int startUDP(SOCKET s, uint8_t *addr, uint16_t port);
uint16_t bufferData(SOCKET s, uint16_t offset, const uint8_t *buf, uint16_t len);
bool sendBuffered(SOCKET s, uint16_t len);
int sendUDP(SOCKET s, uint16_t len);

#endif
