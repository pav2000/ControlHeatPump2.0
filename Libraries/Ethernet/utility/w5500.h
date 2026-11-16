/*
* Copyright (c) 2010 by WIZnet <support@wiznet.co.kr>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of either the GNU General Public License version 2
 * or the GNU Lesser General Public License version 2.1, both as
 * published by the Free Software Foundation.
 */

#ifndef	W5500_H_INCLUDED
#define	W5500_H_INCLUDED

#include "NetLayout.h"   // раскладка буферов передачи сокетов (NL_MODE_*)

/*
class MR {
public:
  static const uint8_t RST   = 0x80;
  static const uint8_t PB    = 0x10;
  static const uint8_t PPPOE = 0x08;
  static const uint8_t LB    = 0x04;
  static const uint8_t AI    = 0x02;
  static const uint8_t IND   = 0x01;
};
*/
#define MR_RST                       0x80
#define MR_PB                        0x10
#define MR_FARP                      0x02
/*
class IR {
public:
  static const uint8_t CONFLICT = 0x80;
  static const uint8_t UNREACH  = 0x40;
  static const uint8_t PPPoE    = 0x20;
  static const uint8_t SOCK0    = 0x01;
  static const uint8_t SOCK1    = 0x02;
  static const uint8_t SOCK2    = 0x04;
  static const uint8_t SOCK3    = 0x08;
  static inline uint8_t SOCK(SOCKET ch) { return (0x01 << ch); };
};
*/

class SnMR {
public:
  static const uint8_t CLOSE  = 0x00;
  static const uint8_t TCP    = 0x01;
  static const uint8_t UDP    = 0x02;
  static const uint8_t IPRAW  = 0x03;
  static const uint8_t MACRAW = 0x04;
  static const uint8_t PPPOE  = 0x05;
  static const uint8_t ND     = 0x20;
  static const uint8_t MULTI  = 0x80;
};

enum SockCMD {
  Sock_OPEN      = 0x01,
  Sock_LISTEN    = 0x02,
  Sock_CONNECT   = 0x04,
  Sock_DISCON    = 0x08,
  Sock_CLOSE     = 0x10,
  Sock_SEND      = 0x20,
  Sock_SEND_MAC  = 0x21,
  Sock_SEND_KEEP = 0x22,
  Sock_RECV      = 0x40
};

/*class SnCmd {
public:
  static const uint8_t OPEN      = 0x01;
  static const uint8_t LISTEN    = 0x02;
  static const uint8_t CONNECT   = 0x04;
  static const uint8_t DISCON    = 0x08;
  static const uint8_t CLOSE     = 0x10;
  static const uint8_t SEND      = 0x20;
  static const uint8_t SEND_MAC  = 0x21;
  static const uint8_t SEND_KEEP = 0x22;
  static const uint8_t RECV      = 0x40;
};
*/

class SnIR {
public:
  static const uint8_t SEND_OK = 0x10;
  static const uint8_t TIMEOUT = 0x08;
  static const uint8_t RECV    = 0x04;
  static const uint8_t DISCON  = 0x02;
  static const uint8_t CON     = 0x01;
};

class SnSR {
public:
  static const uint8_t CLOSED      = 0x00;
  static const uint8_t INIT        = 0x13;
  static const uint8_t LISTEN      = 0x14;
  static const uint8_t SYNSENT     = 0x15;
  static const uint8_t SYNRECV     = 0x16;
  static const uint8_t ESTABLISHED = 0x17;
  static const uint8_t FIN_WAIT    = 0x18;
  static const uint8_t CLOSING     = 0x1A;
  static const uint8_t TIME_WAIT   = 0x1B;
  static const uint8_t CLOSE_WAIT  = 0x1C;
  static const uint8_t LAST_ACK    = 0x1D;
  static const uint8_t UDP         = 0x22;
  static const uint8_t IPRAW       = 0x32;
  static const uint8_t MACRAW      = 0x42;
  static const uint8_t PPPOE       = 0x5F;
};

class IPPROTO {
public:
  static const uint8_t IP   = 0;
  static const uint8_t ICMP = 1;
  static const uint8_t IGMP = 2;
  static const uint8_t GGP  = 3;
  static const uint8_t TCP  = 6;
  static const uint8_t PUP  = 12;
  static const uint8_t UDP  = 17;
  static const uint8_t IDP  = 22;
  static const uint8_t ND   = 77;
  static const uint8_t RAW  = 255;
};

class W5500Class {

public:
  void init();
  // Раскладка буферов передачи (NetLayout.h: NL_MODE_OLD / NL_MODE_FAST0). Режим только запоминается, в чип он пишется в init() после каждого
  // сброса: init() зовут и resetWiznet, и каждый Ethernet.begin, раскладка должна переживать все вызовы.
  void setLayoutMode(uint8_t mode) { layoutMode = mode; }
  bool sockUsable(uint8_t s) { return NetLayout_Usable(layoutMode, s); }   // сокеты с нулевым буфером не используются (веб, клиенты, UDP)
  uint16_t txCapacity(SOCKET s) const;
  static const uint16_t RX_CAPACITY = 2048; // init() выделяет 2 КиБ на сокет
  bool read_data(SOCKET s, uint16_t ptr, uint8_t *dst, uint16_t len);
  bool readRx(SOCKET s, uint8_t *dst, uint16_t len, bool peek = false);
  bool consumeRx(SOCKET s, uint16_t ptr, uint16_t len);
  bool bufferTx(SOCKET s, uint16_t offset, const uint8_t *data, uint16_t len);
  bool commitTx(SOCKET s, uint16_t len);
  bool startSend(SOCKET s);

  inline void setGatewayIp(uint8_t *_addr);
  inline void getGatewayIp(uint8_t *_addr);

  inline void setSubnetMask(uint8_t *_addr);
  inline void getSubnetMask(uint8_t *_addr);

  inline void setMACAddress(uint8_t * addr);
  inline void getMACAddress(uint8_t * addr);

  inline void setIPAddress(uint8_t * addr);
  inline void getIPAddress(uint8_t * addr);

  inline void setRetransmissionTime(uint16_t timeout);
  inline void setRetransmissionCount(uint8_t _retry);

  inline void setPHYCFGR(uint8_t _val);
  inline uint8_t getPHYCFGR();

  bool execCmdSn(SOCKET s, SockCMD _cmd);
  
  bool getTXFreeSize(SOCKET s, uint16_t &size);
  bool getRXReceivedSize(SOCKET s, uint16_t &size);
  // pav2000 Возвращает свободное место TX сокета s в байтах или 0 при сбое.
  uint16_t getTXFreeSize(SOCKET s) { uint16_t size = 0; return getTXFreeSize(s, size) ? size : 0; }
  // pav2000 Возвращает число доступных байтов RX сокета s или 0 при сбое.
  uint16_t getRXReceivedSize(SOCKET s) { uint16_t size = 0; return getRXReceivedSize(s, size) ? size : 0; }
  

  // W5500 Registers
  // ---------------
private:
  static uint8_t  write(uint16_t _addr, uint8_t _cb, uint8_t _data);
  static uint16_t write(uint16_t _addr, uint8_t _cb, const uint8_t *buf, uint16_t len);
  // pav2000 Читает байт по адресу addr с управляющим байтом cb. Возвращает значение или 0 при сбое.
  static uint8_t read(uint16_t addr, uint8_t cb) { uint8_t value = 0; return readByte(addr, cb, value) ? value : 0; }
  static bool readByte(uint16_t addr, uint8_t cb, uint8_t &value);
  static uint16_t read(uint16_t _addr, uint8_t _cb, uint8_t *buf, uint16_t len);
  
#define __GP_REGISTER8(name, address)             \
  static inline void write##name(uint8_t _data) { \
    write(address, 0x04, _data);                  \
  }                                               \
  static inline uint8_t read##name() {            \
    return read(address, 0x00);                   \
  }
#define __GP_REGISTER16(name, address)            \
  static void write##name(uint16_t _data) {       \
    write(address,  0x04, _data >> 8);            \
    write(address+1, 0x04, _data & 0xFF);         \
  }                                               \
  static uint16_t read##name() {                  \
    uint16_t res = read(address, 0x00);           \
    res = (res << 8) + read(address + 1, 0x00);   \
    return res;                                   \
  }
#define __GP_REGISTER_N(name, address, size)      \
  static uint16_t write##name(uint8_t *_buff) {   \
    return write(address, 0x04, _buff, size);     \
  }                                               \
  static uint16_t read##name(uint8_t *_buff) {    \
    return read(address, 0x00, _buff, size);      \
  }

public:
  __GP_REGISTER8 (MR,     0x0000);    // Mode
  __GP_REGISTER_N(GAR,    0x0001, 4); // Gateway IP address
  __GP_REGISTER_N(SUBR,   0x0005, 4); // Subnet mask address
  __GP_REGISTER_N(SHAR,   0x0009, 6); // Source MAC address
  __GP_REGISTER_N(SIPR,   0x000F, 4); // Source IP address
  __GP_REGISTER8 (IR,     0x0015);    // Interrupt
  __GP_REGISTER8 (IMR,    0x0016);    // Interrupt Mask
  __GP_REGISTER16(RTR,    0x0019);    // Timeout address
  __GP_REGISTER8 (RCR,    0x001B);    // Retry count
  __GP_REGISTER_N(UIPR,   0x0028, 4); // Unreachable IP address in UDP mode
  __GP_REGISTER16(UPORT,  0x002C);    // Unreachable Port address in UDP mode
  __GP_REGISTER8 (PHYCFGR,0x002E);    // PHY Configuration register, default value: 0b 1011 1xxx
  __GP_REGISTER8 (VERSIONR,0x0039);   // Chip version(VERSIONR) pav2000
  __GP_REGISTER8 (PHYSTATUS,0x002E);  // PHY Status(PSTATUS) //pav2000


#undef __GP_REGISTER8
#undef __GP_REGISTER16
#undef __GP_REGISTER_N

  // W5500 Socket registers
  // ----------------------
private:
  static bool stableSize(SOCKET s, uint16_t address, uint16_t limit, uint16_t &size);
  static inline uint16_t readSn(SOCKET s, uint16_t addr, uint8_t *buf, uint16_t len);
  static inline uint16_t writeSn(SOCKET s, uint16_t addr, const uint8_t *buf, uint16_t len);

// pav2000 Создаёт функции для регистра name по address. Чтение и запись принимают сокет и байт,
// проверяемые варианты возвращают статус обмена, упрощённое чтение возвращает байт или 0.
#define __SOCKET_REGISTER8(name, address)                              \
  static bool write##name(SOCKET s, uint8_t value) {                   \
    return s < SOCKETS && write(address, 0x0C + (s << 5), value);      \
  }                                                                   \
  static bool read##name(SOCKET s, uint8_t &value) {                   \
    return s < SOCKETS && readByte(address, 0x08 + (s << 5), value);   \
  }                                                                   \
  static uint8_t read##name(SOCKET s) {                               \
    uint8_t value = 0; return read##name(s, value) ? value : 0;             \
  }

// pav2000 Создаёт функции для 16-битного регистра name по address. Чтение и запись принимают сокет и значение,
// проверяемые варианты возвращают статус обмена, упрощённое чтение возвращает значение или 0.
#define __SOCKET_REGISTER16(name, address)                            \
  static bool write##name(SOCKET s, uint16_t value) {                  \
    const uint8_t bytes[] = {uint8_t(value >> 8), uint8_t(value)};      \
    return writeSn(s, address, bytes, sizeof(bytes)) == sizeof(bytes); \
  }                                                                   \
  static bool read##name(SOCKET s, uint16_t &value) {                  \
    uint8_t bytes[2];                                                 \
    if(readSn(s, address, bytes, sizeof(bytes)) != sizeof(bytes)) return false; \
    value = (uint16_t(bytes[0]) << 8) | bytes[1];                       \
    return true;                                                      \
  }                                                                   \
  static uint16_t read##name(SOCKET s) {                              \
    uint16_t value = 0; return read##name(s, value) ? value : 0;            \
  }
// pav2000 Создаёт функции чтения и записи массива регистра name по address.
// Они принимают сокет и буфер на size байтов и возвращают размер успешного обмена.
#define __SOCKET_REGISTER_N(name, address, size)             \
  static uint16_t write##name(SOCKET _s, uint8_t *_buff) {   \
    return writeSn(_s, address, _buff, size);                \
  }                                                          \
  static uint16_t read##name(SOCKET _s, uint8_t *_buff) {    \
    return readSn(_s, address, _buff, size);                 \
  }
  
public:
  __SOCKET_REGISTER8(SnMR,        0x0000)        // Mode
  __SOCKET_REGISTER8(SnCR,        0x0001)        // Command
  __SOCKET_REGISTER8(SnIR,        0x0002)        // Interrupt
  __SOCKET_REGISTER8(SnSR,        0x0003)        // Status
  __SOCKET_REGISTER16(SnPORT,     0x0004)        // Source Port
  __SOCKET_REGISTER_N(SnDHAR,     0x0006, 6)     // Destination Hardw Addr
  __SOCKET_REGISTER_N(SnDIPR,     0x000C, 4)     // Destination IP Addr
  __SOCKET_REGISTER16(SnDPORT,    0x0010)        // Destination Port
  __SOCKET_REGISTER16(SnMSSR,     0x0012)        // Max Segment Size
  __SOCKET_REGISTER8(SnPROTO,     0x0014)        // Protocol in IP RAW Mode
  __SOCKET_REGISTER8(SnTOS,       0x0015)        // IP TOS
  __SOCKET_REGISTER8(SnTTL,       0x0016)        // IP TTL
  __SOCKET_REGISTER16(SnTX_FSR,   0x0020)        // TX Free Size
  __SOCKET_REGISTER16(SnTX_RD,    0x0022)        // TX Read Pointer
  __SOCKET_REGISTER16(SnTX_WR,    0x0024)        // TX Write Pointer
  __SOCKET_REGISTER16(SnRX_RSR,   0x0026)        // RX Free Size
  __SOCKET_REGISTER16(SnRX_RD,    0x0028)        // RX Read Pointer
  __SOCKET_REGISTER16(SnRX_WR,    0x002A)        // RX Write Pointer (supported?)
  __SOCKET_REGISTER8(SnIMR,       0x002C)        // Socket Interrupt Mask Register pav2000
  __SOCKET_REGISTER16(SnFRAG,     0x002D)        // Socket Fragment Register pav2000
  __SOCKET_REGISTER8(SnKPALVTR,   0x002F)        // Socket Keep Alive Time Register pav2000
  
#undef __SOCKET_REGISTER8
#undef __SOCKET_REGISTER16
#undef __SOCKET_REGISTER_N


private:
  static const uint8_t  RST = 7; // Reset BIT
  static const int SOCKETS = 8;

public:
  static const uint16_t SSIZE = 2048; // Max Tx buffer size
  uint8_t layoutMode;   // NL_MODE_*, 0 - прежняя раскладка (глобальный объект инициализируется нулями)
private:
#if defined(ARDUINO_ARCH_AVR)
#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega1284P__)
  inline static void initSS()    { DDRB  |=  _BV(4); };
  inline static void setSS()     { PORTB &= ~_BV(4); };
  inline static void resetSS()   { PORTB |=  _BV(4); };
#elif defined(__AVR_ATmega32U4__)
  inline static void initSS()    { DDRB  |=  _BV(6); };
  inline static void setSS()     { PORTB &= ~_BV(6); };
  inline static void resetSS()   { PORTB |=  _BV(6); }; 
#elif defined(__AVR_AT90USB1286__) || defined(__AVR_AT90USB646__) || defined(__AVR_AT90USB162__)
  inline static void initSS()    { DDRB  |=  _BV(0); };
  inline static void setSS()     { PORTB &= ~_BV(0); };
  inline static void resetSS()   { PORTB |=  _BV(0); }; 
#elif defined(REL_GR_KURUMI) || defined(REL_GR_KURUMI_PROTOTYPE)
  inline static void initSS()    { pinMode(SS, OUTPUT); \
                                   digitalWrite(SS, HIGH); };
  inline static void setSS()     { digitalWrite(SS, LOW); };
  inline static void resetSS()   { digitalWrite(SS, HIGH); };
#else
  inline static void initSS()    { DDRB  |=  _BV(2); };
  inline static void setSS()     { PORTB &= ~_BV(2); };
  inline static void resetSS()   { PORTB |=  _BV(2); };
#endif
#endif // ARDUINO_ARCH_AVR
};

extern W5500Class W5100;

// pav2000 Читает len байтов из регистров сокета s по адресу addr в buf. Возвращает len при успехе, иначе 0.
uint16_t W5500Class::readSn(SOCKET s, uint16_t addr, uint8_t *buf, uint16_t len) {
  return s < SOCKETS ? read(addr, 0x08 + (s << 5), buf, len) : 0;
}

// pav2000 Записывает len байтов из buf в регистры сокета s по адресу addr. Возвращает len при успехе, иначе 0.
uint16_t W5500Class::writeSn(SOCKET s, uint16_t addr, const uint8_t *buf, uint16_t len) {
  return s < SOCKETS ? write(addr, 0x0C + (s << 5), buf, len) : 0;
}

void W5500Class::getGatewayIp(uint8_t *_addr) {
  readGAR(_addr);
}

void W5500Class::setGatewayIp(uint8_t *_addr) {
  writeGAR(_addr);
}

void W5500Class::getSubnetMask(uint8_t *_addr) {
  readSUBR(_addr);
}

void W5500Class::setSubnetMask(uint8_t *_addr) {
  writeSUBR(_addr);
}

void W5500Class::getMACAddress(uint8_t *_addr) {
  readSHAR(_addr);
}

void W5500Class::setMACAddress(uint8_t *_addr) {
  writeSHAR(_addr);
}

void W5500Class::getIPAddress(uint8_t *_addr) {
  readSIPR(_addr);
}

void W5500Class::setIPAddress(uint8_t *_addr) {
  writeSIPR(_addr);
}

void W5500Class::setRetransmissionTime(uint16_t _timeout) {
  writeRTR(_timeout);
}

void W5500Class::setRetransmissionCount(uint8_t _retry) {
  writeRCR(_retry);
}

void W5500Class::setPHYCFGR(uint8_t _val) {
  writePHYCFGR(_val);
}

uint8_t W5500Class::getPHYCFGR() {
//  readPHYCFGR();
  return read(0x002E, 0x00);
}

#endif
