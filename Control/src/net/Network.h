/*
 * Copyright (c) 2016-2026 by Pavel Panfilov <firstlast2007@gmail.com>  telegram @pav20000
 * &                       by Vadim Kulakov vad7@yahoo.com, vad711
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
 * Сетевой транспорт: то, чем пользуются остальные модули.
 *
 * Функции определены в Network.cpp. Переключатели SPI always_inline: из отдельного
 * модуля их иначе нельзя было бы позвать, компоновщик не нашёл бы тела. Поэтому они
 * здесь целиком, как в GpioDirect.h: тело видно каждому, кто включил заголовок, и
 * инлайн сохраняется.
 *
 * Шину SPI делят три устройства - W5500, SD-карта и flash, - и переключение
 * между ними делается вручную перед каждым обращением.
 */
#ifndef Network_h
#define Network_h

#include "../../Constant.h"
#include "../hw/GpioDirect.h"
#include "mini-printf.h"   // MINI_PRINTF_FMT - проверка форматов компилятором
// Если mini-printf.h взят из копии БЕЗ этого макроса (например из sketchbook,
// который не выровняли), собираем без проверки форматов - но собираем. Иначе
// IDE выдаёт три десятка ошибок на ровном месте, что уже случилось.
// Полная проверка включается, когда копии библиотеки совпадают; tools/build.sh
// предупреждает о расхождении, но IDE его не запускает.
#ifndef MINI_PRINTF_FMT
#define MINI_PRINTF_FMT(n, m)
#endif
#include <Ethernet.h>

// Транспорт. Определены в Network.cpp.
// UDP-сокет для NTP. Определён в Control.ino.
extern EthernetUDP Udp;
// Слушающий сокет веб-сервера. Создан в Control.ino.
extern EthernetServer server1;

// Имя микросхемы сети, зависит от конфигурации. Определено в Control.ino;
// объявление обязано быть видно ДО определения, иначе const на уровне
// пространства имён даст внутреннее связывание и модули его не увидят.
extern const char nameWiznet[];

uint16_t sendPacketRTOS(uint8_t thread, const uint8_t *buf, uint16_t len, uint16_t pause);
uint16_t sendConstRTOS(uint8_t thread, const char *buf);
uint16_t sendBufferRTOS(uint8_t thread, const uint8_t *buf, uint16_t len);
uint16_t sendPrintfRTOS(uint8_t thread, const char *format, ...) MINI_PRINTF_FMT(2, 3);
void     beginWeb(uint16_t port);         // поднять слушающий сокет веб-сервера
struct ApiOut;
void     socketInfoApi(ApiOut *o);        // то же JSON-массивом для /api (sys.sockets)
boolean  initW5200(boolean flag);
void     pingW5200(boolean f);            // разрешить/запретить ответ на ping
boolean  linkStatusWiznet(boolean show);
uint8_t  check_address(char *adr, IPAddress &ip);

// Преобразования IP-адресов. Определены в Network.cpp; печать идёт в общий
// буфер _buf из Util.h, поэтому те же правила замка.
IPAddress BytesToIPAddress(uint8_t *ip);
boolean   parseIPAddress(const char *str, char sep, IPAddress &ip);
char     *IPAddress2String(IPAddress &ip);
void     ShowSockRegisters(uint8_t s);
void     checkSockStatus();               // сторож зависших сокетов, зовётся из Control.ino
boolean  pingServer();                    // проверка связи пингом, там же

// ---- SPI переключение между устройствами ----
// Функции переключения SPI между тремя! устройствами (активный уровень низкий)
 __attribute__((always_inline)) inline void SPI_switchW5200()   // Переключение на сеть
{
  
  digitalWriteDirect(PIN_SPI_CS_SD,HIGH);  
  #ifdef SPI_FLASH
  digitalWriteDirect(PIN_SPI_CS_FLASH,HIGH);  
  #endif
  digitalWriteDirect(PIN_SPI_CS_W5XXX,LOW);
   }

 __attribute__((always_inline)) inline void SPI_switchSD()     // Переключение на карту памяти
{

  #ifdef SPI_FLASH
  digitalWriteDirect(PIN_SPI_CS_FLASH,HIGH);  
  #endif
  digitalWriteDirect(PIN_SPI_CS_W5XXX,HIGH);  
  digitalWriteDirect(PIN_SPI_CS_SD,LOW);
 
  }
#ifdef SPI_FLASH
 __attribute__((always_inline)) inline void SPI_switchFlash()  // переключение на флеш память
{
  digitalWriteDirect(PIN_SPI_CS_SD,HIGH);  
  digitalWriteDirect(PIN_SPI_CS_W5XXX,HIGH);
  digitalWriteDirect(PIN_SPI_CS_FLASH,LOW);
 
   }
#endif
 __attribute__((always_inline)) inline void SPI_switchAllOFF()  // Все выключить
{
  digitalWriteDirect(PIN_SPI_CS_SD,HIGH);  
  digitalWriteDirect(PIN_SPI_CS_W5XXX,HIGH);
  #ifdef SPI_FLASH
  digitalWriteDirect(PIN_SPI_CS_FLASH,HIGH);
  #endif
}

#endif
