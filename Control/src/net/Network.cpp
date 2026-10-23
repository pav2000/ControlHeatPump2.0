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
#include <Dns.h>

#include "Network.h"      // объявления этого модуля: транспорт, server1, Udp
#include "../core/HeatPump.h"     // HP - настройки сети и флаги
#include "WebShared.h"    // Socket[], fABORT_SOCK
#include "../core/Rtos.h"         // _delay, SemaphoreTake/Give
#include "../util/Util.h"         // _itoa, m_snprintf



#include "../../Constant.h"                       // Вся конфигурация и константы проекта Должен быть первым !!!!
#include "utility/w5100.h"
#include "utility/socket.h"
#include <ICMPPing.h>
// ICMPPing.h заканчивается непарным "#pragma pack(1)" - без восстановления.
// Всё, что объявлено ПОСЛЕ него в этой единице трансляции, пакуется, и
// структуры получают раскладку, отличную от остальных файлов. Снимаем здесь,
// чтобы не зависеть от того, какая копия библиотеки подхватится сборкой.
#pragma pack()
// ---------------------------------------------------------------------------
// Работа с W5200 c поддержкой многопоточности и Free RTOS
// по возможности работаем через сокеты
// ---------------------------------------------------------------------------
static unsigned long connectTime[MAX_SOCK_NUM];    // время соединения сокета, здесь по всем сокетам (и служебному)
#define W5200_LINK        0x20                     // МАСКА регистра PHYSTATUS(W5200 PHY status Register) при котором считается что связь есть
#define W5500_LINK        0x01                     // МАСКА регистра PHYCFGR  (W5500 PHY Configuration Register) [R/W] [0x002E] при котором считается что связь есть
#define W5500_SPEED       0x02                     // МАСКА регистра PHYCFGR  (W5500 PHY Configuration Register) [R/W] [0x002E] определяется скорость Speed Status
#define W5500_DUPLEX      0x04                     // МАСКА регистра PHYCFGR  (W5500 PHY Configuration Register) [R/W] [0x002E] определяется дуплекс Duplex Status

// Переключатели SPI перенесены в Network.h: они always_inline, и из
// отдельной единицы трансляции их иначе не позвать - тела не видно.

// ---- Функции для первоначальной настройки сетевого чипа ----
// Получить номер версии сетевого чипа
uint8_t W5200VERSIONR()
{
  #if defined(W5100_ETHERNET_SHIELD)
    return 51;                    // В чипе w5100 нет номера версии по этому делаем условно 51
  #else
    return W5100.readVERSIONR();  // Для w5200 и 5500
  #endif
}
// Проверить состояние сетевого чипа без сброса! (бит LINK)
// show - нужен вывод в консоль или нет, возврат true- связь есть
boolean linkStatusWiznet(boolean show)
{
#if defined(W5500_ETHERNET_SHIELD) // Задание имени чипа для вывода сообщений
	uint8_t st = W5100.readPHYCFGR();
	if(show){
		if(DBG_ON(DBG_PHY)) {
			if(st & W5500_SPEED) journal.jprintf(" Speed Status: 100Mpbs\n"); else journal.jprintf(" Speed Status: 10Mpbs\n");
			if(st & W5500_DUPLEX) journal.jprintf(" Duplex Status: Full\n"); else journal.jprintf(" Duplex Status: Half\n");
			journal.jprintf(" Register PHYCFGR: 0x%02X\n", st);
		} else journal.jprintf(" %s%c[%02X] ", st & W5500_SPEED ? "100" : "10", st & W5500_DUPLEX ? 'F' : 'H', st);
	}
	if(st & W5500_LINK) return true;
	else return false;
#elif defined(W5200_ETHERNET_SHIELD)
	if (W5100.readPHYSTATUS()&W5200_LINK) return true; else return false;
#else // w5100
	return true;
#endif
}

// Сброс чипа и проверка на соединение, делается несколько попыток (бит LINK)
// show - нужен вывод в консоль или нет, возврат true- связь есть
boolean resetWiznet(boolean show)
{
    uint8_t i;
    uint16_t t;
    for (i = 0; i <  W5200_NUM_LINK; i++)  // делается несколько попыток связи до появления LINK с задержкой
    { 
     Watchdog_AliveHere();
#ifdef PIN_ETH_RES
     digitalWriteDirect(PIN_ETH_RES, LOW); // Аппаратный сброс чипа (если он завис вдруг это помогает)
     _delay(5);
     digitalWriteDirect(PIN_ETH_RES, HIGH);
#endif
     W5100.init();                                                                                                 // Программная инициализация чипа (программный сброс и программирование)
     for (t = 0; t <  W5200_TIME_LINK; t=t+50)                                                                     // Ожидание установления связи но не более W5200_TIME_LINK мсек
       {
       _delay(50);                                                                                                 
       if (linkStatusWiznet(false)) { if(show)journal.jprintf(" %s: link OK (time %d mc)\n",(char*)__FUNCTION__, t);return true;}  // link есть, едим дальше
       }
     if (show) journal.jprintf(" %s: no link\n",(char*)__FUNCTION__);   
    }
  return false;                                                                                                     // линка нет
}
// Инициализация сети
// flag true - полный вывод на консоль false - сокращённый вывод на консоль
// Проверят сетевой кабель, возврат true - OK false - проблемы, сеть не работает
const char* NetworkChipOK={" Network library setting: %s, ID chip: 0x%x\n"};
const char* NetworkChipBad={" WRONG setting library, library: %s, ID: chip 0x%x\n"};
const char* NetworkError={" $ERROR: Problem reset and setting %s\n"};
boolean initW5200(boolean flag)
{
	uint8_t i;
	boolean EthernetOK = true;   // флаг успешности инициализации
#ifdef PIN_ETH_INT
	pinMode(PIN_ETH_INT, INPUT);
#endif
#ifdef PIN_ETH_RES
	pinMode(PIN_ETH_RES, OUTPUT);
#endif
	if(flag) journal.jprintf("Network setup:");
	if(!resetWiznet(false))  // 1. Сброс и проверка провода (молча)
	{
		if(DBG_ON(DBG_PHY)) {
			journal.jprintf(" WARNING: %s no link, check ethernet cable\n", nameWiznet);
			journal.jprintf((char*) NetworkError, nameWiznet);
		} else journal.jprintf(" WARNING: %s no link\n", nameWiznet);
		return false; // дальше ехать бесполезно
	} else if(flag && DBG_ON(DBG_PHY)) journal.jprintf(" SUCCESS: %s link OK\n", nameWiznet);
	linkStatusWiznet(flag);  // вывести полученные настройки чипа

	if(flag)  // 2. Печать настроек соответствия либы и чипа (правильная настройка либы)
	{
#ifdef DEMO
		journal.jprintf(" DEMO mode!");
#endif
#if defined(W5500_ETHERNET_SHIELD) // Определение соответствия библиотеки и чипа
		if(W5200VERSIONR() == 0x04) {
			if(DBG_ON(DBG_PHY)) journal.jprintf((char*) NetworkChipOK, nameWiznet, W5200VERSIONR());
		} else {
			journal.jprintf((char*) NetworkChipBad, nameWiznet, W5200VERSIONR());
			journal.jprintf((char*) NetworkError, nameWiznet);
			return false;
		} // дальше ехать бесполезно
#elif defined(W5200_ETHERNET_SHIELD)
		if (W5200VERSIONR()==0x03) journal.jprintf((char*)NetworkChipOK,nameWiznet,W5200VERSIONR());
		else {journal.jprintf((char*)NetworkChipBad,nameWiznet,W5200VERSIONR());journal.jprintf((char*)NetworkError,nameWiznet); return false;} // дальше ехать бесполезно
#else
		if (W5200VERSIONR()==0x51) journal.jprintf((char*)NetworkChipOK,nameWiznet,W5200VERSIONR());
		else {journal.jprintf((char*)NetworkChipBad,nameWiznet,W5200VERSIONR());journal.jprintf((char*)NetworkError,nameWiznet); return false;} // дальше ехать бесполезно
#endif
	}

	//  3. Подготовить структура для потоков
	for(i = 0; i < W5200_THREAD; i++) {
		Socket[i].flags = 0x00;
		Socket[i].sock = -1;
		memset((char*) Socket[i].inBuf, 0x00, sizeof(Socket[i].inBuf));
		memset((char*) Socket[i].outBuf, 0x00, sizeof(Socket[i].outBuf));
	}

	// 4. Инициализация сетевого адаптера, установка сетевых настроек
	Watchdog_AliveHere();                          // Сбросить вачдог  DHCP при отключенном кабеле - большой таймаут
#ifdef DEMO
	Ethernet.begin((uint8_t*)defaultMAC,(IPAddress)defaultIP,(IPAddress)defaultSDNS,(IPAddress)defaultGateway,(IPAddress)defaultSubnet); // Инициализация сетевого адаптера  в демо режиме КОНСТАНТЫ
	if (IPAddress(defaultIP)!=Ethernet.localIP()) EthernetOK=false; else beginWeb(defaultPort);
#else
	if(HP.safeNetwork) {
		Ethernet.begin((uint8_t*) defaultMAC, (IPAddress) defaultIP, (IPAddress) defaultSDNS,
				(IPAddress) defaultGateway, (IPAddress) defaultSubnet); // Инициализация сетевого адаптера  в режиме safeNetwork = КОНСТАНТЫ
		if(IPAddress(defaultIP) != Ethernet.localIP()) EthernetOK = false;
		else {
			beginWeb(defaultPort);
			journal.jprintf(" Set mode safeNetwork!\n");
		}
	} else {
		if(HP.get_DHCP()) // Работаем по DHCP
		{
			journal.jprintf(" Try DHCP: ");
			Watchdog_AliveHere();
			if(Ethernet.begin((uint8_t*) HP.get_mac()) == 0) {
				journal.jprintf("Failed!\n");
				goto x_TryStaticIP;
			} else {
				journal.jprintf("OK\n");
				HP.set_ip(Ethernet.localIP());       // Получили удачно DHCP адрес - сохраняем в сетевые настройки
				HP.set_subnet(Ethernet.subnetMask());
				HP.set_sdns(Ethernet.dnsServerIP());
				HP.set_gateway(Ethernet.gatewayIP());
			}
		} else {
x_TryStaticIP:
			Watchdog_AliveHere();
			Ethernet.begin((uint8_t*) HP.get_mac(), (IPAddress) HP.get_ip(), (IPAddress) HP.get_sdns(),
					(IPAddress) HP.get_gateway(), (IPAddress) HP.get_subnet()); // Статика
			if(HP.get_ip() != Ethernet.localIP()) EthernetOK = false;
			else beginWeb(HP.get_port());
		}
	}
#endif

	pingW5200(HP.get_NoPing());  // Установка пинга флага разрешения пинга
	W5100.writeRTR(W5200_RTR);   // установка таймаута
	W5100.writeRCR(W5200_RCR);   // установка числа повторов

	if(DBG_ON(DBG_PHY)) {
	if(flag)  // 5. Печать сетевых настроек
	{
		if(EthernetOK) {
			journal.jprintf(" DHCP use: ");
			if(HP.get_DHCP()) journal.jprintf("YES\n");
			else journal.jprintf("NO\n");
			IPAddress dip;
			dip = Ethernet.localIP();
			journal.jprintf(" IP: %s\n", IPAddress2String(dip));
			dip = Ethernet.subnetMask();
			journal.jprintf(" Subnet: %s\n", IPAddress2String(dip));
			dip = Ethernet.dnsServerIP();
			journal.jprintf(" DNS: %s\n", IPAddress2String(dip));
			dip = Ethernet.gatewayIP();
			journal.jprintf(" Gateway: %s\n", IPAddress2String(dip));
		} else journal.jprintf((char*) NetworkError, nameWiznet);
	} else   // Кратко выводим сообщение в журнал
	{
		if(EthernetOK) journal.jprintf_time("Reset %s . . . \n", nameWiznet);
		else journal.jprintf((char*) NetworkError, nameWiznet);
	}
	} else {
	if(flag)  // 5. Печать сетевых настроек
	{
		if(EthernetOK) {
			IPAddress dip;
			dip = Ethernet.localIP();
			journal.jprintf("%s%s/%d ", HP.get_DHCP() ? "DHCP " : "", IPAddress2String(dip), calc_bits_in_mask(Ethernet.subnetMask()));
			dip = Ethernet.gatewayIP();
			journal.jprintf("G:%s ", IPAddress2String(dip));
			dip = Ethernet.dnsServerIP();
			journal.jprintf("DNS:%s\n", IPAddress2String(dip));
		} else journal.jprintf(" ERROR: setting %s", nameWiznet);
	} else   // Кратко выводим сообщение в журнал
	{
		if(EthernetOK) journal.jprintf_time("Reset %s Ok.\n", nameWiznet);
		else journal.jprintf(" ERROR: setting %s", nameWiznet);
	}
	}
	return EthernetOK;
}
// ---- DNS ----
// Используется системный сокет!! W5200_SOCK_SYS
// Определить IP адрес через DNS, если на входе не IP адрес.
// Переменная adr - должна быть расположена в ОЗУ!
// При не удаче возвращается 0, при удаче: 1 - IP на входе (были цифры, DNS не нужен), 2 - был запрос к DNS и адрес получен
uint8_t check_address(char *adr, IPAddress &ip)
{
	// Задача определить  IP адрес. Если на входе был также IP то он и возвращается,
	IPAddress tempIP(0, 0, 0, 0);
	DNSClient dns;
	int8_t ret = 0;
	// 1. Попытка преобразовать строку в IP (цифры, нам повезло DNS не нужен)
	if(parseIPAddress(adr, '.', tempIP)) {
		ip = tempIP;
		return 1;
	} // Удачно выходим
	// 2. Это буквы, нужен dns
	dns.begin(Ethernet.dnsServerIP());    // только запоминаем dnsServerIP ничего больше не делаем с сокетом
	ret = dns.getHostByName(adr, tempIP, W5200_SOCK_SYS); // adr должен быть в ОЗУ!
	if(ret == 1) { // Адрес получен
		if(xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
			journal.jprintf(" Resolved %s using %s as %d.%d.%d.%d\n", adr, dns.get_protocol() ? "TCP" : "UDP", tempIP[0], tempIP[1], tempIP[2], tempIP[3]);
		ip = tempIP;
		return 2;
	} else {
		if((DBG_ON(DBG_WERR)) || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
			journal.jprintf(" DNS lookup %s using %s failed! Code: %d\n", adr, dns.get_protocol() ? "TCP" : "UDP", ret);
		ip = tempIP;
		return 0;
	}
}
// ---- Вывести состояние регистров сокета ----
void ShowSockRegisters(uint8_t s)
{
 journal.jprintf("#%d", s); // номер сокета
 journal.jprintf(" MR:%02x",W5100.readSnMR(s)); 
 journal.jprintf(" CR:%02x",W5100.readSnCR(s)); 
 journal.jprintf(" IR:%02x",W5100.readSnIR(s)); 
 journal.jprintf(" SR:%02x",W5100.readSnSR(s)); 
 journal.jprintf(" PORT:%04x",W5100.readSnPORT(s)); 
 journal.jprintf(" DPORT:%04x",W5100.readSnDPORT(s));  
 #if defined(W5500_ETHERNET_SHIELD)  
 journal.jprintf(" TOS:%02x",W5100.readSnTOS(s));
 journal.jprintf(" TTL:%02x",W5100.readSnTTL(s));
 #endif
 journal.jprintf(" "); 
 
}


// Те же сведения для веб-API: JSON-массив строк {n, c: код статуса, s: имя, h: сноска 0..2, m: MAC, ip, p: порт}.
// Сноска h: 1 - переходное состояние сокета, 2 - кандидат на сброс (как <sup> в прежней строке).
struct SockState { uint8_t code; const char *name; uint8_t hint; };
static const SockState sockStates[] = {
	{ 0x00, "CLOSED", 0 }, { 0x01, "ARP", 1 }, { 0x13, "INIT", 0 }, { 0x14, "LISTEN", 0 }, { 0x15, "SYNSENT", 1 }, { 0x16, "SYNRECV", 1 },
	{ 0x17, "ESTABLISHED", 2 }, { 0x18, "FIN_WAIT", 1 }, { 0x1a, "CLOSING", 1 }, { 0x1b, "TIME_WAIT", 1 }, { 0x1c, "CLOSE_WAIT", 2 },
	{ 0x1d, "LAST_ACK", 1 }, { 0x22, "UDP", 0 }, { 0x32, "IPRAW", 0 }, { 0x42, "MACRAW", 0 }, { 0x5f, "PPPOE", 0 } };
void socketInfoApi(ApiOut *o)
{
	api_putc(o, '[');
	for(int i = 0; i < MAX_SOCK_NUM; i++) {
		uint8_t s = W5100.readSnSR(i), dmac[6], dip[4], k;
		const char *name = "unknow state"; uint8_t hint = 0;
		for(k = 0; k < sizeof(sockStates) / sizeof(sockStates[0]); k++) if(sockStates[k].code == s) { name = sockStates[k].name; hint = sockStates[k].hint; break; }
		W5100.readSnDHAR(i, dmac);
		W5100.readSnDIPR(i, dip);
		if(i) api_putc(o, ',');
		api_put(o, "{\"n\":"); api_put_int(o, i);
		api_put(o, ",\"c\":"); api_put_str(o, byteToHex(s));
		api_put(o, ",\"s\":"); api_put_str(o, name);
		api_put(o, ",\"h\":"); api_put_int(o, hint);
		api_put(o, ",\"m\":"); api_put_str(o, MAC2String(dmac));
		api_put(o, ",\"ip\":\""); for(k = 0; k < 4; k++) { api_put_int(o, dip[k]); if(k < 3) api_putc(o, '.'); }
		api_put(o, "\",\"p\":"); api_put_int(o, W5100.readSnDPORT(i));
		api_putc(o, '}');
	}
	api_putc(o, ']');
}
// ---- Сброс зависших сокетов ----
// Учитывается многопоточность, не сбрасываются сокеты которые сейчас в работе Socket[xxxx].sock
void checkSockStatus()
{
  unsigned long thisTime = xTaskGetTickCount();
  if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE) {journal.jprintf((char*)cErrorMutex,__FUNCTION__,MutexWebThreadBusy);return;} // Захват мютекса потока или ОЖИДАНИНЕ W5200_TIME_WAIT
  for (uint8_t i = 0; i < MAX_SOCK_NUM; i++) {        // По всем сокетам!!
        // Не сбрасывать сокеты которые используется в потоке ОБЯЗАТЕЛЬНО!!
        #if    W5200_THREAD < 2
         if (Socket[0].sock==i)  continue;   
        #elif  W5200_THREAD < 3
          if((Socket[0].sock==i)||(Socket[1].sock==i))  continue;    
        #elif  W5200_THREAD < 4
          if((Socket[0].sock==i)||(Socket[1].sock==i)||(Socket[2].sock==i))  continue;   
        #else
          if((Socket[0].sock==i)||(Socket[1].sock==i)||(Socket[2].sock==i)||(Socket[3].sock==i))  continue;   
        #endif
    uint8_t s = W5100.readSnSR(i);                                          // Прочитать статус сокета
    if((s == SnSR::ESTABLISHED) || (s == SnSR::CLOSE_WAIT) /*|| (s == 0x22)*/ ) { // если он "кандидат"
        if(thisTime - connectTime[i] > HP.time_socketRes()*1000UL) {        // Время пришло
          journal.jprintf("%s : Socket frozen: %d\n",NowTimeToStr(),i); 
          W5100.execCmdSn(i, Sock_CLOSE);
          W5100.writeSnIR(i, 0xFF);
          HP.add_socketRes();                                               // добавить счетчик
        }
    } // if((s == 0x17) || (s == 0x1C))
    else connectTime[i] = thisTime;                                         // Обновить время если статус не кандидат
  } // for
  SemaphoreGive(xWebThreadSemaphore);                                      // Отдать мютекс
}
// ---- Послать один пакет!!! ----
// Послать данные TCP (максимальный размер данных W5200_MAX_LEN. больше обрезается), при ожидании освобождения буфера отдает управление Free RTOS
// Для разделения доступа к spi используется мютекс  (SemaphoreHandle_t xSPI)
// возвращает число посланых байт (0-ничего не послано)
// НЕ РАБОТАЕТ с КОНСТАНТАМИ!! их предварительно надо скопировать в ОЗУ т.к. используется DMA
// Если pause=0 то ждем подтверждения ACK пакета если pause>0 то просто ждем нужное значение (pause) мсек - работа без подтверждения
// SEM-HOLD: xWebThreadSemaphore - одалживает замок вызывающего и возвращает его ему же
uint16_t sendPacketRTOS(uint8_t thread, const uint8_t * buf, uint16_t len, uint16_t pause)
{
	uint8_t status = 0;
	uint16_t ret = 0;
	uint16_t freesize = 0;

	if(GETBIT(Socket[thread].flags, fLOCK_LOST)) return 0;   // замок уже потерян раньше в этом запросе: на шину и сокет не лезем
	SPI_switchW5200();
	if(len > W5200_MAX_LEN) ret = W5200_MAX_LEN;
	else ret = len;

	if(pause == 0) // Честно ждем ack
	{
		do// Ожидание освобождения буфера
		{
			if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
				xSemaphoreGive (xWebThreadSemaphore);  //                                      // Мютекс потока отдать
				taskYIELD();
			} else delay(1);
			if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {
				SETBIT1(Socket[thread].flags, fLOCK_LOST);   // web_server по этому флагу не отдаёт замок и не трогает SPI
				journal.jprintf("Socket: %d %s\n", Socket[thread].sock, MutexWebThreadBusy);
				return 0;
			} // Захват мютекса потока или ОЖИДАНИНЕ W5200_TIME_WAIT
			freesize = W5100.getTXFreeSize(Socket[thread].sock);
			if(freesize >= ret) {
				break;
			}
			status = W5100.readSnSR(Socket[thread].sock);
			if((status != SnSR::ESTABLISHED) && (status != SnSR::CLOSE_WAIT)) {
				ret = 0;
				break;
			}
		} while(freesize < ret);
	} else  // Не ждем ACK а просто делаем задержку
	{
		SemaphoreGive (xWebThreadSemaphore);                                                             // Мютекс потока отдать
		_delay(pause);                                                            // Ждем pause мсек
		if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {
			SETBIT1(Socket[thread].flags, fLOCK_LOST);
			journal.jprintf("Socket: %d %s\n", Socket[thread].sock, MutexWebThreadBusy);
			return 0;
		} // Захват мютекса потока или ОЖИДАНИНЕ W5200_TIME_WAIT
	}

	if(GETBIT(Socket[thread].flags, fABORT_SOCK)) {
		SETBIT0(Socket[thread].flags, fABORT_SOCK);
		return 0;
	}              // Произошел сброс прерываем передачу

	// послать данные
	W5100.send_data_processing_offset(Socket[thread].sock, 0, (uint8_t *) buf, ret);
	W5100.execCmdSn(Socket[thread].sock, Sock_SEND);

	// +2008.01 bj : reduce code
	// Ожидание подтверждения отправки с таймаутом: при зависшем W5500 (SEND_OK не
	// выставлен, но и сокет не CLOSED) задача без него крутилась бы здесь бесконечно,
	// удерживая глобальный xWebThreadSemaphore, то есть web, MQTT и почта встали бы
	// насовсем. Watchdog это не ловит - его сбрасывает vServiceHP с более высоким
	// приоритетом.
	{
		uint32_t tmSend = GetTickCount();
		while((W5100.readSnIR(Socket[thread].sock) & SnIR::SEND_OK) != SnIR::SEND_OK) {
			if(W5100.readSnSR(Socket[thread].sock) == SnSR::CLOSED) {
				close(Socket[thread].sock);
				return 0;
			}
			if(GetTickCount() - tmSend > W5200_SEND_TIMEOUT) {
				journal.jprintf("WEB: send timeout %d ms, sock %d - closing\n",
				                (int) W5200_SEND_TIMEOUT, (int) Socket[thread].sock);
				close(Socket[thread].sock);
				return 0;
			}
		}
	}
	W5100.writeSnIR(Socket[thread].sock, SnIR::SEND_OK);
	return ret;
}

// Послать буфер, может быть множество пакетов!!
// Послать данные TCP (максимальный размер данных не ограничен, может отправлять несколько пакетов),
// Использует функцию sendPaketRTOS
// возвращает число посланых байт (0-ничего не послано, или ошибка передачи)
// НЕ РАБОТАЕТ с КОНСТАНТАМИ!! их предварительно надо скопировать в ОЗУ т.к. используется DMA
uint16_t sendBufferRTOS(uint8_t thread, const uint8_t * buf, uint16_t len)
{
	uint16_t i = 0;
	while(len > 0) {
		if(len > W5200_MAX_LEN) {
			if(sendPacketRTOS(thread, (byte*) buf + i, W5200_MAX_LEN, HP.get_NoAck() ? HP.get_delayAck() : 0) == 0) { // ошибка передачи
				return 0;
			}
			i += W5200_MAX_LEN;
			len -= W5200_MAX_LEN;
		} else {  // последний пакет или ошибка передачи
			if(sendPacketRTOS(thread, (byte*) buf + i, len, HP.get_NoAck() ? HP.get_delayAck() : 0) == 0) return 0;
			else {
				i += len;
				break;
			}
		}
	}
	return i;
}


// Послать один пакет КОНСТАНТ!!!
// Послать данные TCP (максимальный размер данных W5200_MAX_LEN. больше обрезается), при ожидании освобождения буфера отдает управление Free RTOS
// Для разделения доступа к spi используется мютекс  (SemaphoreHandle_t xSPI)
// возвращает число посланых байт (0-ничего не послано)
// РАБОТАЕТ С КОНСТАНТАМИ, идет предварительное копирование в буфер потока
// Без always_inline: функция копирует строку в буфер сокета и уходит в
// sendPacketRTOS, то есть в сетевую отправку - операцию миллисекундную.
// Накладные расходы вызова на её фоне неизмеримы, а встраивание стоило места.
uint16_t sendConstRTOS(uint8_t thread, const char * buf)
{
if (strlen(buf)>W5200_MAX_LEN) return 0;   // В один пакет не влезает выходим
        strcpy(Socket[thread].outBuf,buf); 
return  sendPacketRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf),0);   
}

 // Принт в сокет
uint16_t sendPrintfRTOS(uint8_t thread, const char *format, ...)             
{
  va_list ap;
  va_start(ap, format);
  m_vsnprintf((Socket[thread].outBuf), sizeof((Socket[thread].outBuf)), format, ap);
  va_end(ap);
  return sendBufferRTOS(thread, (byte*)Socket[thread].outBuf, strlen((Socket[thread].outBuf)));
}
     
// Куски либы переделанные под многопоточность, сделаны как отдельные функции
// определение - необходимости отработки сокета
// Старт прослушивания порта веб морды (подъем веб сервера), на прослушивание ставятся не все сокеты, системный остается свободным
void beginWeb(uint16_t port)
{
 for (int sock = 0; sock < W5200_SOCK_SYS; sock++)
 {
    EthernetClient client(sock);
    if (client.status() == SnSR::CLOSED) 
    {
      socket(sock, SnMR::TCP, port, 0);
      listen(sock);
      EthernetClass::_server_port[sock] = HP.get_port();
      break;
    }
  }  
  
}

// Пингование сервера
ICMPPing ping(W5200_SOCK_SYS, (uint16_t)random(0, 255));
boolean pingServer()
{
	IPAddress ip;
	if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE)  {return false;}  // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
	if(!check_address(HP.get_pingAdr(), ip)) {journal.jprintf("Wrong ping server\n"); SemaphoreGive(xWebThreadSemaphore); return false;}  // адрес не верен, или DNS не работает - ничего не делаем
 	// Адрес правильный
	ping.setTimeout(W5200_TIME_PING);                   // время между попытками пинга мсек
	Watchdog_AliveHere();                                   // Сбросить вачдог
	ICMPEchoReply echoReply = ping(ip,W5200_NUM_PING);  // адрес и число попыток
	SemaphoreGive(xWebThreadSemaphore);                 // отдать семафор
	if(DBG_ON(DBG_PING)) journal.jprintf_time("Ping[%d] ", echoReply.data.seq);
	if(echoReply.status == SUCCESS) {
		if(DBG_ON(DBG_PING)) {
			if(ping.attempts()) {
				journal.jprintf("%dms, lost: %d.\n", (int)(GetTickCount() - echoReply.data.time), ping.attempts());
			} else {
				journal.jprintf("%dms\n", (int)(GetTickCount() - echoReply.data.time));
			}
		}
		return true;
	} else {
		if(DBG_ON(DBG_PING)) journal.jprintf("%d.%d.%d.%d: FAILED - ", ip[0], ip[1], ip[2], ip[3]);   // отметка времени уже выведена выше
		else journal.jprintf_time("Ping[%d] %d.%d.%d.%d: FAILED - ", echoReply.data.seq, ip[0], ip[1], ip[2], ip[3]);
		switch (echoReply.status)
		{
		case SEND_TIMEOUT: journal.jprintf( "send timed out");  break;
		case NO_RESPONSE:  journal.jprintf( "no response");    break;
		case BAD_RESPONSE: journal.jprintf( "bad reponse");        break;
		default:           journal.jprintf( "error: %d", echoReply.status); break;
		}
		if(!HP.NO_Power) {
			journal.jprintf(", Resetting %s...\n", nameWiznet);
			Diag.num_resPing++;
			HP.sendCommand(pNETWORK);                                                // Если связь потеряна то подать команду на сброс сетевого чипа
		}
		return false;
	}
	return false;
}

// Установка флага пингования контроллера w5200 или w5500 на w5100 не проверялся
// true - пинг запрещен
// false - пинг разрешен
#define MR_BIT_PB 0x04  //MR (Mode Register) Ping Block Mode bit (0x04) If the bit is ‘1’, it blocks the response to a ping request.
void pingW5200(boolean f)
{
	uint8_t x = W5100.readMR();
	if(f) {
		SETBIT1(x, MR_BIT_PB);
		if(DBG_ON(DBG_PHY)) journal.jprintf(" Enable Ping block\n");
	} else {
		SETBIT0(x, MR_BIT_PB);
		if(DBG_ON(DBG_PHY)) journal.jprintf(" Disable Ping block\n");
	}
	W5100.writeMR(x);
}

// Запрос на сервер с ожиданием ответа, веб блокируется, вызов из MAIN_WEB_TASK
// HTTP 1.0 GET, timeout - ms
// Ответ: "str=x", Возврат: int(x). Ошибка x <= -2000000000;
// fget_value: 0 - не читать ответ, 1 - считать тело ответа в Socket[MAIN_WEB_TASK].outBuf,
//             2 - вернуть значение после '=', 3 - проверить на "Ok"
int Send_HTTP_Request(const char *server, const char *request, uint8_t fget_value)
{
	if(server == NULL || request == NULL) return -2000000004;
	if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {   // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
		return -2000000000;
	}
	EthernetClient tTCP;
	int ret = 0;
	bool locked = true;    // замок сети у нас в руках; в цикле ожидания он отпускается и может не вернуться
	const uint16_t port = 80;
	if(DBG_ON(DBG_WFULL)) journal.jprintf_time("Send request: %s%s\n", server, request);
	uint8_t *buffer = (uint8_t *) Socket[MAIN_WEB_TASK].outBuf + sizeof(Socket[MAIN_WEB_TASK].outBuf)/2;
	IPAddress ip(0, 0, 0, 0);
	if(check_address((char*)server, ip) == 0) {
		ret = -2000000002;
	} else {
		if(!tTCP.connect(ip, port, W5200_SOCK_SYS)) {
			ret = -2000000003;
		} else {
			strcpy((char*)buffer, http_get_str1);
			strcpy((char*)buffer + sizeof(http_get_str1)-1, request);
			strcat((char*)buffer + sizeof(http_get_str1)-1, http_get_str2);
			strcat((char*)buffer + sizeof(http_get_str1)-1 + sizeof(http_get_str2)-1, server);
			strcat((char*)buffer + sizeof(http_get_str1)-1 + sizeof(http_get_str2)-1, http_get_str3);
			if(tTCP.write(buffer, strlen((char*)buffer + sizeof(http_get_str1)-1 + sizeof(http_get_str2)-1 + sizeof(http_get_str3)-1) + sizeof(http_get_str1)-1 + sizeof(http_get_str2)-1 + sizeof(http_get_str3)-1) == 0) {
				ret = -2000000011;
			} else {
				ret = -2000000001;
				int timeout = HTTP_REQ_TIMEOUT / 20;
				while(timeout-- > 0) { // ожидание ответа
					SemaphoreGive(xWebThreadSemaphore);
					_delay(20);
					if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS)) == pdFALSE) { locked = false; break; } // SEM-OK: замок не вернули - дальше locked закрывает и stop(), и Give
					if(tTCP.available()) {
						ret = 0;
						break;
					}
				}
				if(ret == 0) { // Ответ получен, формат: "str=x"
					buffer = (uint8_t *) Socket[MAIN_WEB_TASK].outBuf;
					if(tTCP.read(buffer, sizeof(http_key_ok1)-1) == sizeof(http_key_ok1)-1) {
						if(memcmp(buffer, &http_key_ok1, sizeof(http_key_ok1)-1) == 0) {
							if(tTCP.read(buffer, 3 + sizeof(http_key_ok2)-1) == 3 + sizeof(http_key_ok2)-1) { // HTTP/
								if(memcmp(buffer + 3, &http_key_ok2, sizeof(http_key_ok2)-1) == 0) {	// 200 OK
									if(fget_value == 0) {
										ret = 0;
									} else {
										int datasize = tTCP.available();
										if(datasize > (int)sizeof(Socket[MAIN_WEB_TASK].outBuf) - 1) datasize = sizeof(Socket[MAIN_WEB_TASK].outBuf) - 1;   // -1: место под терминатор
										if(tTCP.read(buffer, datasize) == datasize) {
											buffer[datasize] = '\0';   // дальше strstr/strchr/%s: без терминатора они читали за буфер
											buffer = (uint8_t*)strstr((char*)buffer, WEB_HEADER_END);
											if(buffer) {
												buffer += sizeof(WEB_HEADER_END)-1;
												if(fget_value == 1)	{
													memcpy((uint8_t*)Socket[MAIN_WEB_TASK].outBuf, buffer, datasize - (buffer - (uint8_t*)Socket[MAIN_WEB_TASK].outBuf));
													ret = 0;
													if(DBG_ON(DBG_WFULL)) journal.jprintf_time("Response: %s", Socket[MAIN_WEB_TASK].outBuf);
												} else {
													if(fget_value == 2) {
														char *p = strchr((char*)buffer, '=');
														if(p != NULL) {
															ret = atoi(p + 1);
														} else ret = -2000000009;
													} else { // 3
														ret = (buffer[0] & ~0x20) == 'O' && (buffer[1] & ~0x20) == 'K'; // 'Ok'?
														if(ret == 0 && (DBG_ON(DBG_WERR) && !DBG_ON(DBG_WFULL))) journal.jprintf_time("Response: %s", buffer);
													}
													if(DBG_ON(DBG_WFULL)) journal.jprintf_time("Response: %s", buffer);
												}
											} else ret = -2000000010;
										} else ret = -2000000012;
									}
								} else {
									buffer[sizeof(http_key_ok2) + 3] = '\0';
									if(DBG_ON(DBG_WERR | DBG_WFULL)) {
										journal.jprintf(" ERR %s", buffer);
									}
									ret = -2000000008;
								}
							} else ret = -2000000007;
						} else ret = -2000000006;
					} else ret = -2000000005;
				}
			}
			if(locked) tTCP.stop();   // без замка сокет закроет checkSockStatus: SPI сейчас не наша
		}
	}
	if(locked) SemaphoreGive(xWebThreadSemaphore);   // отдавать чужой замок нельзя: assert ядра остановит контроллер
	if(DBG_ON(DBG_WFULL)) journal.jprintf(" Ret = %d\n", ret);
	else if(ret < 0 && (DBG_ON(DBG_WERR))) {
		journal.jprintf_time("Error %d send request to %s!", ret + 2000000000, server);
		switch (ret)
		{
		case -2000000002:
			journal.jprintf(" Address wrong");
			break;
		case -2000000003:
			journal.jprintf(" Connect fail");
			break;
		case -2000000011:
			journal.jprintf(" Send error");
			break;
		case -2000000010:
			journal.jprintf(" Empty response");
			break;
		case -2000000009:
			journal.jprintf(" Response: %s", buffer);
			break;
		case -2000000008:
			journal.jprintf(" ERR %s", buffer);
			break;
		}
		journal.jprintf("\n");
	}
	return ret;
}

// ---------------------------------------------------------------------------
// Преобразования IP-адресов. Железа они не касаются и просились бы в Util.cpp, но
// IPAddress - тип из Arduino, а Util.cpp намеренно свободен от Arduino и линкуется
// с тестами на хосте. Сеть - ближайшее место, где IPAddress уже свой.
//
// Печать идёт в общий буфер _buf из Util.h: только в веб-потоке или под
// замком xWebThreadSemaphore.
// ---------------------------------------------------------------------------

// Массив из четырёх байт в IPAddress
IPAddress BytesToIPAddress(uint8_t *ip)
{
	static IPAddress b;
	b[0] = ip[0]; b[1] = ip[1]; b[2] = ip[2]; b[3] = ip[3];
	return b;
}

// Разбор строки в IP-адрес. При неудаче возвращает false и адрес не меняет.
boolean parseIPAddress(const char *str, char sep, IPAddress &ip)
{
	int i, x;
	char y;
	uint8_t tmp[4];
	for(i = 0; i < 4; i++) {
		y = str[0];
		x = strtoul(str, NULL, 10);
		if(x > 255) return false;                  // байт вне диапазона
		if((x == 0) && (y != '0')) return false;   // не число вовсе
		tmp[i] = x;
		str = strchr(str, sep);
		if(str == NULL || *str == '\0') break;     // разделителей больше нет
		str++;
	}
	if(i < 4 - 1) return false;
	ip = tmp;
	return true;
}

// IP-адрес в строку
char *IPAddress2String(IPAddress &ip)
{
	*_buf = '\0';
	for(uint8_t i = 0; i < 4; i++) {
		_itoa(ip[i], _buf);
		if(i < 3) strcat(_buf, ".");
	}
	return _buf;
}
