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
// Отправка телеметрии: народный мониторинг, MQTT, ThingSpeak.

#include "../../Constant.h"                       // Вся конфигурация и константы проекта Должен быть первым !!!!
#include "utility/w5100.h"
#include "utility/socket.h"
#include <ICMPPing.h>
// ICMPPing.h заканчивается непарным "#pragma pack(1)" - без восстановления.
// Всё, что объявлено ПОСЛЕ него в этой единице трансляции, пакуется, и
// структуры получают раскладку, отличную от остальных файлов. Снимаем здесь,
// чтобы не зависеть от того, какая копия библиотеки подхватится сборкой.
#pragma pack()
// ВНИМАНИЕ: включения обязаны стоять ДО #ifdef MQTT ниже. Если убрать их
// внутрь, при выключенном MQTT единица трансляции станет пустой, а при
// включённом - компилятор не увидит объявлений. На этом уже ломался MQTT.cpp.
#include "MQTT.h"         // clientMQTT, w5200_MQTT, объявления этих функций
#include "../core/HeatPump.h"     // HP: настройки, датчики, счётчики
#include "../log/Journal.h"      // journal
#include "../hw/Hardware.h"     // Modbus, IPAddress2String
#include "Network.h"      // sendPacketRTOS, check_address, замки SPI
#include "../core/Rtos.h"         // _delay, семафоры, Watchdog_AliveHere
#include "../util/Util.h"         // _itoa/_ftoa/_dtoa
#include "../util/DateTime.h"     // NowTimeToStr
#include "../log/Statistics.h"   // Stats для набора величин
#include "WebShared.h"    // Socket[]

// ---- M Q T T ----
#ifdef MQTT    // признак использования MQTT
static const char MQTTpublish[]={">> %s "};
static const char MQTTPublishOK[]={"OK\n"};
static const char MQTTDebugStr[]={" %s %s,"};  // вывод информации при отладке

// Посылка данных на народный мониторинг
// debug  true - выводить в консоль информацию о посылаемых данных false - нет вывода
boolean sendNarodMon(boolean debug)
{
 uint8_t i;
     if (memcmp(defaultMAC,HP.get_mac(),sizeof(defaultMAC))==0) {journal.jprintf("sendNarodMon ignore: Wrong MAC address, change MAC from default.\n"); return false;}
  
     HP.clMQTT.clearBuf();   // очистить рабочие буфера
     if(debug || DBG_ON(DBG_MON)) journal.jprintf((char*)MQTTpublish,HP.clMQTT.get_narodMon_server());
     strcpy(HP.clMQTT.root,"");  // Формирование строки корня, куда потом пишутся топики
     HP.clMQTT.get_paramMQTT((char*)mqtt_LOGIN_NARMON,HP.clMQTT.root);
     strcat(HP.clMQTT.root,"/");
     HP.clMQTT.get_paramMQTT((char*)mqtt_ID_NARMON,HP.clMQTT.root);  
     strcat(HP.clMQTT.root,"/");
     
     // посылка отдельных топиков
     strcpy(HP.clMQTT.topic,HP.clMQTT.root);
     strcat(HP.clMQTT.topic,HP.sTemp[TOUT].get_name());
     ftoa(HP.clMQTT.temp,(float)HP.sTemp[TOUT].get_Temp()/100.0,1);
     if (HP.clMQTT.sendTopic(true,debug,false)) {if (debug) journal.jprintf((char*)MQTTDebugStr, HP.clMQTT.topic,HP.clMQTT.temp);} else return false; 
     strcpy(HP.clMQTT.topic,HP.clMQTT.root);
     strcat(HP.clMQTT.topic,HP.sTemp[TIN].get_name());
     ftoa(HP.clMQTT.temp,(float)HP.sTemp[TIN].get_Temp()/100.0,1);
     if (HP.clMQTT.sendTopic(true,debug,false)) {if (debug) journal.jprintf((char*)MQTTDebugStr, HP.clMQTT.topic,HP.clMQTT.temp);} else return false; 
     
     strcpy(HP.clMQTT.topic,HP.clMQTT.root);
     strcat(HP.clMQTT.topic,HP.sTemp[TBOILER].get_name());
     ftoa(HP.clMQTT.temp,(float)HP.sTemp[TBOILER].get_Temp()/100.0,1);
     if (HP.clMQTT.sendTopic(true,debug,false)) {if (debug) journal.jprintf((char*)MQTTDebugStr, HP.clMQTT.topic,HP.clMQTT.temp);} else return false; 

     strcpy(HP.clMQTT.topic,HP.clMQTT.root);
     if(HP.dFC.get_present())
         {
          strcat(HP.clMQTT.topic,"FC");
          ftoa(HP.clMQTT.temp,(float)HP.dFC.get_frequency()/100.0,2);
          if (HP.clMQTT.sendTopic(true,debug,false)) {if (debug) journal.jprintf((char*)MQTTDebugStr, HP.clMQTT.topic,HP.clMQTT.temp);} else return false; 
         }
     else
         {
         strcat(HP.clMQTT.topic,HP.dRelay[RCOMP].get_name());
         if (HP.dRelay[RCOMP].get_Relay()){strcpy(HP.clMQTT.temp,(char*)cOne); if (HP.clMQTT.sendTopic(true,debug,false)) {if (debug) journal.jprintf(" %s 1\n", HP.clMQTT.topic);}  else return false;  }    
         else                             {strcpy(HP.clMQTT.temp,(char*)cZero); if (HP.clMQTT.sendTopic(true,debug,false)) {if (debug) journal.jprintf(" %s 0\n", HP.clMQTT.topic);}  else return false;  } 
         } 
         
         strcpy(HP.clMQTT.topic,HP.clMQTT.root);
         strcat(HP.clMQTT.topic,"ERROR");
         itoa(HP.get_errcode(),HP.clMQTT.temp,10);
         if (HP.clMQTT.sendTopic(true,debug,true)) {if (debug) journal.jprintf((char*)MQTTDebugStr, HP.clMQTT.topic,HP.clMQTT.temp);} else return false; 
         
        if (debug) journal.jprintf(cStrEnd);   
         
      // Послать расширенный набор данных TCOMP OWERHEAT мощность выходная коп полный, положение ЭРВ, два давления,
      if (HP.clMQTT.get_NarodMonBig())                
         {
         _delay(100);// пауза перед отправкой следующего пакета - разгружаем сервер и балансируем загрузку у себя
         if (debug) journal.jprintf("Additional data:");  
         strcpy(HP.clMQTT.topic,HP.clMQTT.root);
         strcat(HP.clMQTT.topic,HP.sTemp[TCOMP].get_name());
         ftoa(HP.clMQTT.temp,(float)HP.sTemp[TCOMP].get_Temp()/100.0,1);
         if (HP.clMQTT.sendTopic(true,debug,false)) {if (debug) journal.jprintf((char*)MQTTDebugStr, HP.clMQTT.topic,HP.clMQTT.temp);} else return false;    

         strcpy(HP.clMQTT.topic,HP.clMQTT.root);
         #ifdef EEV_DEF
         strcat(HP.clMQTT.topic,"OWERHEAT");
         ftoa(HP.clMQTT.temp,(float)HP.dEEV.get_Overheat()/100.0,1);
         #else
          strcat(HP.clMQTT.topic,HP.sTemp[TEVAOUTG].get_name());
          if(HP.sTemp[TEVAOUTG].get_present()) ftoa(HP.clMQTT.temp,(float)HP.sTemp[TEVAOUTG].get_Temp()/100.0,1);
          else   strcat(topic,"skip1 0");
         #endif
         if (HP.clMQTT.sendTopic(true,debug,false)) {if (debug) journal.jprintf((char*)MQTTDebugStr, HP.clMQTT.topic,HP.clMQTT.temp);} else return false; 

         for(i=0;i<ANUMBER;i++)
             if(HP.sADC[i].get_present()) 
             {
                 strcpy(HP.clMQTT.topic,HP.clMQTT.root);
                 strcat(HP.clMQTT.topic,HP.sADC[i].get_name());
                 ftoa(HP.clMQTT.temp,(float)HP.sADC[i].get_Value()/100.0,2);
                 if (HP.clMQTT.sendTopic(true,debug,false)) {if (debug) journal.jprintf((char*)MQTTDebugStr, HP.clMQTT.topic,HP.clMQTT.temp);} else return false;      
             }
         
             strcpy(HP.clMQTT.topic,HP.clMQTT.root);
           #ifdef EEV_DEF   
             strcat(HP.clMQTT.topic,HP.dEEV.get_name());
             itoa(HP.dEEV.get_EEV(),HP.clMQTT.temp,10);
           #else
             strcat(HP.clMQTT.topic,HP.sTemp[TEVAING].get_name());
             if(HP.sTemp[TEVAING].get_present()) ftoa(HP.clMQTT.temp,(float)HP.sTemp[TEVAING].get_Temp()/100.0,1);
             else   strcat(topic,"skip2 0");
           #endif  
             if (HP.clMQTT.sendTopic(true,debug,false)) {if (debug) journal.jprintf((char*)MQTTDebugStr, HP.clMQTT.topic,HP.clMQTT.temp);} else return false; 
            
           strcpy(HP.clMQTT.topic,HP.clMQTT.root);
           strcat(HP.clMQTT.topic,"powerCO");
           ftoa(HP.clMQTT.temp,HP.powerOUT,1);
           if (HP.clMQTT.sendTopic(true,debug,true)) {if (debug) journal.jprintf((char*)MQTTDebugStr, HP.clMQTT.topic,HP.clMQTT.temp);} else return false; 
           if (debug) journal.jprintf(cStrEnd);   
         }//  if (HP.clMQTT.get_NarodMonBig())
         if (!debug && DBG_ON(DBG_MON)) journal.jprintf((char*)MQTTPublishOK);
           
  return true;
}

// Посылка данных на народный мониторинг без использования MQTT потребовалось в декабре 2022 года.
/*Передача показаний по протоколам TCP/UDP на narodmon.ru:8283
Если Ваш прибор допускает только ввод IP адреса или у Вас проблемы с DNS, то используйте любой IP из списка nslookup narodmon.ru до которого у Вас наименьший ping, но помните, что IP-адрес сервера может сменится, а DNS-имя - нет. TCP также можно использовать для массовой загрузки показаний на сервер "задним числом" путем передачи в одном пакете данных показаний датчика в разные моменты времени, но не более 4КБ на пакет для TCP и 512 байт для UDP. Предпочтительно отправлять показания всех датчиков в одном пакете данных для минимизации издержек на инициализацию соединения с сервером и импорт показаний датчиков.
Ответ сервера при успешной отправке будет OK (или текст ошибки или команда на исполнение), что рекомендуется использовать в качестве подтверждения получения данных, а при отсутствии ответа повторить отправку когда восстановится связь с сервером. При сообщениях об ошибках НЕ ПОВТОРЯТЬ!
Конец строки ответа сервера завершается спецсимволом \n код 10(0A). Для оперативного отслеживания состояния логических входов и выходов рекомендуется в ответ на получение команды от сервера возвращать новое состояние логических датчиков в уже открытый ранее сокет согласно протоколу, но только в случае изменений дабы не получить ошибку малого интервала показаний.
При использовании протокола UDP ответа сервера не последует.
Формат пакета данных:
#MAC[#NAME]\n
#mac1#value1[#time1][#name1]\n
...
#macN#valueN[#timeN][#nameN]\n
##
*/

// Дописать в пакет народного мониторинга строку "#метрика#значение\n" с контролем границы буфера.
// p - текущий конец пакета (строка всегда закрыта нулем), end - первый байт ЗА буфером.
// Возврат: новый конец пакета или NULL, если место кончилось. NULL на входе прозрачно возвращается
// обратно - это позволяет не проверять результат после каждого вызова, достаточно одной проверки в конце.
static char* nm_add(char *p, char *end, const char *metric, const char *value)
{
	uint16_t lm, lv;
	if(p == NULL) return NULL;
	lm = strlen(metric); lv = strlen(value);
	if(p + lm + lv + 3 >= end) return NULL;      // '#' + метрика + '#' + значение + '\n' + завершающий ноль, он тоже должен влезть
	*p++ = '#';
	memcpy(p, metric, lm); p += lm;
	*p++ = '#';
	memcpy(p, value, lv);  p += lv;
	*p++ = '\n';
	*p = '\0';
	return p;
}

// Прочитать строку ответа сервера до LF, хвостовой CR срезается, слишком длинная строка молча обрезается.
// На время паузы семафор ОТДАЕТСЯ, иначе на секундных таймаутах встанет весь веб сервер.
// Опрос сначала частый (сервер отвечает быстро), потом редкий - иначе ожидание всегда стоило бы полного шага.
// Возврат: >0 - длина строки, 0 - ответа нет (таймаут или сервер закрыл сессию),
//          -1 - семафор потерян: вызывающему трогать сокет и делать SemaphoreGive НЕЛЬЗЯ.
static int16_t nm_read_line(EthernetClient &c, char *buf, uint16_t size, uint32_t timeout)
{
	// SEM-BORROW: xWebThreadSemaphore - замок сети принадлежит вызывающему (sendNarodMon2022), на время ожидания строки он отпускается и берётся обратно
	uint16_t pos = 0, poll = 0;
	uint32_t startTick = GetTickCount();
	buf[0] = '\0';
	for(;;) {
		while(c.available()) {
			int ch = c.read();
			if(ch < 0) break;
			if(ch == '\n') {
				if(pos && buf[pos - 1] == '\r') pos--;
				buf[pos] = '\0';
				return pos;
			}
			if(pos < size - 1) buf[pos++] = (char) ch;
		}
		if(!c.connected()) { buf[pos] = '\0'; return pos; }   // сервер закрыл сессию, данных больше не будет
		if(GetTickCount() - startTick > timeout) { buf[pos] = '\0'; return pos; }
		SemaphoreGive(xWebThreadSemaphore);                   // отдать семафор для обработки других задач
		_delay(poll < NARMON_POLL_FAST_N ? NARMON_POLL_FAST : NARMON_POLL_SLOW);
		if(poll < NARMON_POLL_FAST_N) poll++;
		if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {
			// Семафор обратно не получен. Он сейчас у другой задачи, поэтому НИ трогать сокет,
			// НИ отдавать семафор нельзя: он бинарный, и лишний Give освободил бы SPI из-под владельца.
			buf[pos] = '\0';
			return -1;
		}
	}
}

// Посылка данных на народный мониторинг по TCP протоколу.
// debug  true - выводить в консоль информацию о посылаемых данных false - нет вывода
// Возврат: true  - повторять НЕ надо: пакет принят сервером, либо сервер ответил ошибкой протокола
//                  ("При сообщениях об ошибках НЕ ПОВТОРЯТЬ!"), либо отправка невозможна в принципе;
//          false - пакет НЕ доставлен (нет линка, нет соединения, нет ответа) - имеет смысл повторить.
boolean sendNarodMon2022(boolean debug)
{
	uint8_t i;
	int flag = 0;
	int16_t len;
	boolean ret;
	char val[16];                                    // временный буфер под одно значение
	char *p, *end, *answer;
	IPAddress zeroIP(0, 0, 0, 0), oldIP, newIP;
	EthernetClient tTCP;

	if(memcmp(defaultMAC, HP.get_mac(), sizeof(defaultMAC)) == 0) {
		journal.jprintf("sendNarodMon2022 ignore: Wrong MAC address, change MAC from default.\n");
		return true;                                 // Пока MAC не сменят, повторять бессмысленно
	}

	// Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
	if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) return false;
	if(!linkStatusWiznet(false)) {                   // Нет связи выходим
		journal.jprintf((char*)"sendNarodMon2022: no link.\n");
		SemaphoreGive(xWebThreadSemaphore);
		return false;
	}

	// Пакет собирается ЦЕЛИКОМ в рабочем буфере root - это Socket[MAIN_WEB_TASK].outBuf (6 кБ),
	// поэтому отправка обязана идти из потока MAIN_WEB_TASK, там буфер гарантированно свободен.
	// Под пакет отведено NARMON_PACKET_MAX, сразу за ним лежит буфер ответа сервера.
	// Области topic/temp при этом перекрываются - в этой функции они не используются.
	HP.clMQTT.clearBuf();
	p      = HP.clMQTT.root;
	end    = HP.clMQTT.root + NARMON_PACKET_MAX;
	answer = HP.clMQTT.root + NARMON_PACKET_MAX;
	*p = '\0';

	// 1я строка: #MAC[#Имя прибора]. MAC служит идентификатором прибора в проекте.
	p = nm_add(p, end, HP.get_netMAC(), HP.clMQTT.get_narodMon_id());
	// Логин владельца отдаем метрикой OWNER - сервер один раз использует его для автоматической
	// привязки ничейного прибора к учетной записи. Дефолтный логин не отправляем.
	if((HP.clMQTT.get_narodMon_login()[0] != '\0') && (strcmp(HP.clMQTT.get_narodMon_login(), DEFAULT_LOGIN_NARMON) != 0))
		p = nm_add(p, end, "OWNER", HP.clMQTT.get_narodMon_login());

	// Температуры отдаем только с установленных датчиков: get_Temp() у отсутствующего возвращает 0,
	// и на сервере завелся бы датчик с вечными 0.0 градуса.
	if(HP.sTemp[TOUT].get_present())    { ftoa(val, (float)HP.sTemp[TOUT].get_Temp()    / 100.0, 1); p = nm_add(p, end, HP.sTemp[TOUT].get_name(), val);    }
	if(HP.sTemp[TIN].get_present())     { ftoa(val, (float)HP.sTemp[TIN].get_Temp()     / 100.0, 1); p = nm_add(p, end, HP.sTemp[TIN].get_name(), val);     }
	if(HP.sTemp[TBOILER].get_present()) { ftoa(val, (float)HP.sTemp[TBOILER].get_Temp() / 100.0, 1); p = nm_add(p, end, HP.sTemp[TBOILER].get_name(), val); }

	if(HP.dFC.get_present()) {                       // Инвертор - отдаем частоту
		ftoa(val, (float)HP.dFC.get_frequency() / 100.0, 2);
		p = nm_add(p, end, "FC", val);
	} else {                                         // Старт-стоп - отдаем состояние компрессора
		p = nm_add(p, end, HP.dRelay[RCOMP].get_name(), HP.dRelay[RCOMP].get_Relay() ? cOne : cZero);
	}

	itoa(HP.get_errcode(), val, 10);
	p = nm_add(p, end, "ERROR", val);

	// Расширенный набор: нагнетание, перегрев, давления, положение ЭРВ, выходная мощность
	if(HP.clMQTT.get_NarodMonBig())
	{
		if(HP.sTemp[TCOMP].get_present()) { ftoa(val, (float)HP.sTemp[TCOMP].get_Temp() / 100.0, 1); p = nm_add(p, end, HP.sTemp[TCOMP].get_name(), val); }
	#ifdef EEV_DEF
		// ИМЯ МЕТРИКИ OWERHEAT НЕ МЕНЯТЬ! Опечатка историческая, но переименование заведет
		// на сервере НОВЫЙ датчик и обнулит всю накопленную историю перегрева.
		ftoa(val, (float)HP.dEEV.get_Overheat() / 100.0, 1);
		p = nm_add(p, end, "OWERHEAT", val);
	#endif
		for(i = 0; i < ANUMBER; i++)                 // давления
			if(HP.sADC[i].get_present()) { ftoa(val, (float)HP.sADC[i].get_Value() / 100.0, 2); p = nm_add(p, end, HP.sADC[i].get_name(), val); }
	#ifdef EEV_DEF
		itoa(HP.dEEV.get_EEV(), val, 10);            // Положение ЭРВ
		p = nm_add(p, end, HP.dEEV.get_name(), val);
	#endif
		ftoa(val, HP.powerOUT, 1);                   // Мощность системы отопления
		p = nm_add(p, end, "powerCO", val);
	}

	// Признак окончания пакета - именно "##" без перевода строки, как в описании протокола
	if(p != NULL) {
		if(p + 3 > end) p = NULL;
		else { *p++ = '#'; *p++ = '#'; *p = '\0'; }
	}
	if(p == NULL) {                                  // Обрезанный пакет слать нельзя: ##_MISSED_AT_THE_END
		journal.jprintf("sendNarodMon2022: packet overflow, increase NARMON_PACKET_MAX\n");
		SemaphoreGive(xWebThreadSemaphore);
		return true;
	}

	if(debug) {                                      // Пакет печатаем построчно: в PRINTF_BUF он целиком не влезет
		char *s = HP.clMQTT.root, *e;
		journal.jprintf("sendNarodMon2022 packet:\n");
		while(*s) {
			e = strchr(s, '\n');
			if(e) *e = '\0';
			journal.jprintf(" %s\n", s);
			if(e == NULL) break;
			*e = '\n';
			s = e + 1;
		}
	}

	// Соединение. Быстрый путь - по адресу из кеша, который заполняет dnsUpdate() (при старте и при
	// смене имени сервера в настройках). При неудаче имя ОБЯЗАТЕЛЬНО перепроверяется через DNS:
	// иначе после переезда сервера на другой адрес контроллер молчал бы до перезагрузки.
	HP.clMQTT.recoverNARMON();                       // Если прошлая отправка была неудачной - реанимируем связь
	oldIP = HP.clMQTT.get_narodMon_serverIP();
	if(!(oldIP == zeroIP)) flag = tTCP.connect(oldIP, HP.clMQTT.get_narodMon_port(), W5200_SOCK_SYS);
	if(!flag) {
		check_address(HP.clMQTT.get_narodMon_server(), newIP); // 0 - не удалось, 1 - на входе был IP, 2 - получен через DNS
		if(!(newIP == zeroIP) && !(newIP == oldIP)) {
			HP.clMQTT.set_narodMon_serverIP(newIP);
			if(!(oldIP == zeroIP)) journal.jprintf((char*)ChangeIP, HP.clMQTT.get_narodMon_server());
			flag = tTCP.connect(newIP, HP.clMQTT.get_narodMon_port(), W5200_SOCK_SYS);
		}
	}
	if(!flag) {
		journal.jprintf("sendNarodMon2022: connect fail\n");
		HP.clMQTT.updateErrMQTT(true);               // После NARMON_NUM_ERR_SLOW неудач подряд период станет редким, но отправка не прекратится
		HP.clMQTT.nextStateNARMON();                 // Следующая ступень реанимации - на следующей попытке
		SemaphoreGive(xWebThreadSemaphore);
		return false;
	}

	if(tTCP.write((const uint8_t *)HP.clMQTT.root, strlen(HP.clMQTT.root)) == 0) {
		journal.jprintf("sendNarodMon2022: send error\n");
		tTCP.stop();                                 // Сокет закрыть обязательно, иначе останется занятым
		HP.clMQTT.updateErrMQTT(true);
		HP.clMQTT.nextStateNARMON();
		SemaphoreGive(xWebThreadSemaphore);
		return false;
	}
	if(debug) journal.jprintf("sendNarodMon2022: send OK\n");

	// Ответ сервера: OK при успешной отправке, иначе текст ошибки протокола или команда на исполнение
	len = nm_read_line(tTCP, answer, NARMON_ANSWER_MAX, NARMON_TO_ANSWER);
	if(len < 0) {
		// Семафор потерян (он сейчас у другой задачи) - сокет трогать и семафор отдавать НЕЛЬЗЯ.
		// Пакет при этом уже ушел в сеть и сервер его почти наверняка принял, поэтому повторять
		// НЕ надо: повтор через минуту дал бы INTERVAL...<5m и риск бана прибора.
		journal.jprintf("sendNarodMon2022: mutex lost, answer not read\n");
		return true;
	}
	tTCP.stop();
	bool answer_ok = len > 0 && answer[0] == 'O' && answer[1] == 'K';
	// Успешный ответ пишется только по флагу MON (или в ручном режиме debug); сбои и ответы с ошибкой - всегда.
	if(!answer_ok || debug || DBG_ON(DBG_MON)) journal.jprintf_time((char*)MQTTpublish, HP.clMQTT.get_narodMon_server());
	if(len == 0) {                                   // Ответа нет - протокол предписывает повторить отправку
		journal.jprintf("no answer\n");
		HP.clMQTT.updateErrMQTT(true);
		HP.clMQTT.nextStateNARMON();
		ret = false;
	} else if(answer_ok) {                           // Показания приняты
		if(debug || DBG_ON(DBG_MON)) journal.jprintf("%s\n", answer);
		HP.clMQTT.resetErrMQTT(true);
		HP.clMQTT.updateState(true, pMQTT_OK);
		ret = true;
	} else {                                         // Ошибка протокола: связь исправна, но данные не приняты.
		journal.jprintf("ERROR: %s\n", answer);      // ПОВТОРЯТЬ НЕЛЬЗЯ, надо исправлять настройки прибора
		HP.clMQTT.updateErrMQTT(true);
		HP.clMQTT.updateState(true, pMQTT_OK);       // Реанимировать связь не нужно, она работает
		ret = true;
	}
	SemaphoreGive(xWebThreadSemaphore);
	return ret;
}  // Конец отсылки данных на народный мониторинг


// Посылка данных на брокер MQTT
// debug  true - выводить в консоль информацию о посылаемых данных false - нет вывода
// ---------------------------------------------------------------------------
// Что отправлять по MQTT: одна строка таблицы - один топик.
//
// Таблица вместо цепочки одинаковых блоков: строки различаются только именем топика,
// источником значения и числом знаков после запятой; добавление величины - одна строка.
//
// ИМЕНА ТОПИКОВ - ВНЕШНИЙ КОНТРАКТ: на них настроены дашборды пользователей.
// Поэтому они берутся из таблиц Config.h и
// nameEEV, - а НЕ из отображаемых имён STATS_OBJ. Те на русском и служат
// подписями на графиках; свести их в одну таблицу, как предлагал план, значило
// бы переименовать топики у всех пользователей. Эталон списка лежит в
// tests/data/mqtt-topics-2.71.txt.

enum MQTT_GROUP : uint8_t { MQG_BASE, MQG_BIG, MQG_SDM, MQG_FC, MQG_COP, MQG_COUNT };

enum MQTT_SRC : uint8_t {
	MQS_Temp,       // sTemp[number].get_Temp(), сотые градуса
	MQS_ADC,        // sADC[number].get_Value(), сотые; number == MQS_ALL - все, что есть
	MQS_FCfreq,     // dFC.get_frequency(), сотые
	MQS_FCcurrent,  // dFC.get_current(), сотые
	MQS_FCparam,    // dFC.get_paramFC(param)
	MQS_SDMparam,   // dSDM.get_paramSDM(param)
	MQS_Relay,      // dRelay[number].get_Relay() как "1" или "0"
	MQS_Error,      // get_errcode()
	MQS_Overheat,   // dEEV.get_Overheat(), сотые
	MQS_EEVpos,     // dEEV.get_EEV(), шаги
	MQS_PowerOUT,   // powerOUT
	MQS_COPfull,    // fullCOP, сотые
	MQS_COP,        // COP, сотые
};
#define MQS_ALL 0xFF

// Условие отправки сверх включённости самой группы
enum MQTT_COND : uint8_t {
	MQC_Always,     // безусловно
	MQC_Present,    // датчик есть в текущей конфигурации
	MQC_FC,         // инвертор есть
	MQC_NoFC,       // инвертора нет
};

struct Mqtt_item {
	// NULL - имя берётся у самого объекта через get_name(). Написать здесь
	// TempSetup[TOUT].name было заманчиво, но такая ссылка не является константой на
	// этапе компоновки, и вся таблица уезжает из flash в SRAM - 345 байт. Та же ловушка
	// описана у HistorySetup в Config.h.
	const char *topic;      // имя топика, NULL - спросить у объекта
	const char *param;      // имя параметра для get_paramFC/get_paramSDM, иначе NULL
	uint8_t group;          // MQTT_GROUP
	uint8_t src;            // MQTT_SRC
	uint8_t number;         // индекс датчика или реле
	uint8_t decimals;       // знаков после запятой, 0xFF - целое
	uint8_t cond;           // MQTT_COND
	// 1 - эта группа закрывает связь после себя. Стоит на последней строке
	// группы, но ГДЕ именно связь закроется, решает не это поле, а
	// mqtt_close_here(): строка с признаком может не пройти своё условие.
	uint8_t last;
};
#define MQS_INT 0xFF

static const Mqtt_item MqttSetup[] = {
	// ---- базовый набор, шлётся всегда ----
	{ NULL, /* TOUT */          NULL, MQG_BASE, MQS_Temp,      TOUT,     1, MQC_Always,  0 },
	{ NULL, /* TIN */           NULL, MQG_BASE, MQS_Temp,      TIN,      1, MQC_Always,  0 },
	{ NULL, /* TBOILER */       NULL, MQG_BASE, MQS_Temp,      TBOILER,  1, MQC_Always,  0 },
	{ "FC",                     NULL, MQG_BASE, MQS_FCfreq,    0,        1, MQC_FC,      0 },
	{ NULL, /* RCOMP */         NULL, MQG_BASE, MQS_Relay,     RCOMP, MQS_INT, MQC_NoFC, 0 },
	{ "ERROR",                  NULL, MQG_BASE, MQS_Error,     0,  MQS_INT, MQC_Always,  1 },

	// ---- расширенный набор, get_MqttBig() ----
	{ NULL, /* TCOMP */         NULL, MQG_BIG,  MQS_Temp,      TCOMP,    1, MQC_Present, 0 },
	{ NULL, /* TCONING */       NULL, MQG_BIG,  MQS_Temp,      TCONING,  1, MQC_Present, 0 },
	{ NULL, /* TCONOUTG */      NULL, MQG_BIG,  MQS_Temp,      TCONOUTG, 1, MQC_Present, 0 },
	{ NULL, /* TEVAOUTG */      NULL, MQG_BIG,  MQS_Temp,      TEVAOUTG, 1, MQC_Present, 0 },
	{ NULL, /* TEVAING */       NULL, MQG_BIG,  MQS_Temp,      TEVAING,  1, MQC_Present, 0 },
	{ "OVERHEAT",               NULL, MQG_BIG,  MQS_Overheat,  0,        1, MQC_Always,  0 },
// Перебор всех аналоговых датчиков, а не перечисление: новый датчик давления попадёт
// в отправку сам, без правки таблицы.
	{ NULL,                     NULL, MQG_BIG,  MQS_ADC,       MQS_ALL,  2, MQC_Present, 0 },
#ifdef EEV_DEF
	{ NULL, /* EEV */           NULL, MQG_BIG,  MQS_EEVpos,    0,  MQS_INT, MQC_Always,  0 },
#endif
	{ "powerCO",                NULL, MQG_BIG,  MQS_PowerOUT,  0,        1, MQC_Always,  1 },

#ifdef USE_ELECTROMETER_SDM
	// ---- электросчётчик, get_MqttSDM120() ----
	{ "ACPOWER",   sdm_ACPOWER, MQG_SDM,  MQS_SDMparam,  0,  MQS_INT, MQC_Always,  0 },
	{ "CURRENT",   sdm_CURRENT, MQG_SDM,  MQS_SDMparam,  0,  MQS_INT, MQC_Always,  0 },
	{ "VOLTAGE",   sdm_VOLTAGE, MQG_SDM,  MQS_SDMparam,  0,  MQS_INT, MQC_Always,  1 },
#endif

	// ---- инвертор, get_MqttFC() и наличие инвертора ----
	{ "powerFC",   fc_cPOWER,   MQG_FC,   MQS_FCparam,   0,  MQS_INT, MQC_Always,  0 },
	{ "freqFC",    fc_cFC,      MQG_FC,   MQS_FCparam,   0,  MQS_INT, MQC_Always,  0 },
	{ "currentFC",              NULL, MQG_FC,   MQS_FCcurrent, 0,        1, MQC_Always,  1 },

	// ---- COP, get_MqttCOP() ----
#ifdef USE_ELECTROMETER_SDM
	{ "fullCOP",                NULL, MQG_COP,  MQS_COPfull,   0,        2, MQC_Always,  0 },
#endif
	// Признак последнего стоит здесь, а отправляется этот топик только при
	// наличии инвертора. Без него группа уходит одним fullCOP с
	// link_close = false, и соединение остаётся открытым до следующей пачки.
	// Поведение исходного кода; записано в задачи после рефакторинга.
	{ "COP",                    NULL, MQG_COP,  MQS_COP,       0,        2, MQC_FC,      1 },
};
#define MQTT_ITEMS (sizeof(MqttSetup) / sizeof(MqttSetup[0]))

// Заголовки пачек в отладочном выводе, по одному на группу
static const char *MqttGroupTitle[MQG_COUNT] = {
	NULL, "Additional data:", "SDM120 data:", "FC data:", "COP data:"
};

// Имя топика: литерал из таблицы либо имя самого объекта
static const char *mqtt_topic_name(const Mqtt_item &it, uint8_t n)
{
	if(it.topic) return it.topic;
	switch(it.src) {
	case MQS_Temp:   return HP.sTemp[n].get_name();
	case MQS_ADC:    return HP.sADC[n].get_name();
	case MQS_Relay:  return HP.dRelay[n].get_name();
#ifdef EEV_DEF
	case MQS_EEVpos: return HP.dEEV.get_name();
#endif
	}
	return "";
}

// Состояние значения пункта кладётся в clMQTT.temp
static void mqtt_value(const Mqtt_item &it, uint8_t n)
{
	char *t = HP.clMQTT.temp;
	switch(it.src) {
	case MQS_Temp:      ftoa(t, (float)HP.sTemp[n].get_Temp() / 100.0, it.decimals); break;
	case MQS_ADC:       ftoa(t, (float)HP.sADC[n].get_Value() / 100.0, it.decimals); break;
	case MQS_FCfreq:    ftoa(t, (float)HP.dFC.get_frequency() / 100.0, it.decimals); break;
	case MQS_FCcurrent: ftoa(t, (float)HP.dFC.get_current() / 100.0, it.decimals); break;
	case MQS_FCparam:   *t = '\0'; HP.dFC.get_paramFC((char*)it.param, t); break;
	case MQS_Relay:     strcpy(t, (char*)(HP.dRelay[n].get_Relay() ? cOne : cZero)); break;
	case MQS_Error:     itoa(HP.get_errcode(), t, 10); break;
	case MQS_PowerOUT:  ftoa(t, HP.powerOUT, it.decimals); break;
	case MQS_COP:       ftoa(t, (float)HP.COP / 100.0, it.decimals); break;
#ifdef USE_ELECTROMETER_SDM
	case MQS_SDMparam:  *t = '\0'; HP.dSDM.get_paramSDM((char*)it.param, t); break;
	case MQS_COPfull:   ftoa(t, (float)HP.fullCOP / 100.0, it.decimals); break;
#endif
#ifdef EEV_DEF
	case MQS_Overheat:  ftoa(t, (float)HP.dEEV.get_Overheat() / 100.0, it.decimals); break;
	case MQS_EEVpos:    itoa(HP.dEEV.get_EEV(), t, 10); break;
#else
	case MQS_Overheat:  strcpy(t, (char*)cZero); break;
#endif
	default:            *t = '\0'; break;
	}
}

// Условие отправки пункта сверх включённости его группы
static boolean mqtt_cond(const Mqtt_item &it, uint8_t n)
{
	switch(it.cond) {
	case MQC_FC:      return HP.dFC.get_present();
	case MQC_NoFC:    return !HP.dFC.get_present();
	case MQC_Present:
		if(it.src == MQS_Temp) return HP.sTemp[n].get_present();
		if(it.src == MQS_ADC)  return HP.sADC[n].get_present();
		return true;
	}
	return true;
}

// Закрывать ли связь после этого топика.
//
// ЗАЧЕМ. Признак "последний в пачке" обязан стоять на топике, который
// ОТПРАВИЛСЯ последним, а не на том, который задуман последним. В группе COP
// признак стоит на строке COP, а у неё условие MQC_FC. На релейной
// конфигурации (инвертора нет) строка COP пропускается, группа уходит одним
// fullCOP с link_close = false, и TCP-соединение с брокером остаётся открытым
// до следующей пачки или следующего цикла. Не авария, но сокет держится зря.
//
// Поэтому смотрим вперёд: если дальше в этой же группе есть хоть один пункт,
// проходящий своё условие, - связь закрывать рано. Если не осталось ни
// одного, закрываем, когда группа вообще закрывает связь (признак last хоть
// на одной её строке). Просмотр идёт по таблице из 24 строк и стоит ничто
// на фоне самой отправки.
static boolean mqtt_close_here(uint8_t i, uint8_t n)
{
	const Mqtt_item &it = MqttSetup[i];
	// Остаток текущей строки-перебора: датчики после n-го
	if(it.number == MQS_ALL)
		for(uint8_t k = n + 1; k < ANUMBER; k++) if(mqtt_cond(it, k)) return false;
	// Строки той же группы ниже по таблице
	for(uint8_t j = i + 1; j < MQTT_ITEMS; j++) {
		const Mqtt_item &nx = MqttSetup[j];
		if(nx.group != it.group) break;          // группы в таблице идут подряд
		if(nx.number == MQS_ALL) {
			for(uint8_t k = 0; k < ANUMBER; k++) if(mqtt_cond(nx, k)) return false;
		} else if(mqtt_cond(nx, nx.number)) return false;
	}
	// Дальше в группе никого. Закрывает ли эта группа связь вообще?
	for(uint8_t j = 0; j < MQTT_ITEMS; j++)
		if(MqttSetup[j].group == it.group && MqttSetup[j].last) return true;
	return false;
}

// Включена ли группа в настройках
static boolean mqtt_group_on(uint8_t g)
{
	switch(g) {
	case MQG_BIG: return HP.clMQTT.get_MqttBig();
	case MQG_SDM: return HP.clMQTT.get_MqttSDM120();
	case MQG_FC:  return HP.clMQTT.get_MqttFC() && HP.dFC.get_present();
	case MQG_COP: return HP.clMQTT.get_MqttCOP();
	}
	return true;     // MQG_BASE шлётся всегда
}

boolean sendMQTT(boolean debug)
{
	HP.clMQTT.clearBuf();                                        // очистить рабочие буфера
	strcpy(HP.clMQTT.root, "");                                  // корень, к которому приписываются топики
	if(debug || DBG_ON(DBG_MON)) journal.jprintf((char*)MQTTpublish, HP.clMQTT.get_mqtt_server());
	HP.clMQTT.get_paramMQTT((char*)mqtt_ID_MQTT, HP.clMQTT.root);
	strcat(HP.clMQTT.root, "/");

	uint8_t group = MQG_COUNT;        // ни одна пачка ещё не начата
	boolean on = false;
	for(uint8_t i = 0; i < MQTT_ITEMS; i++) {
		const Mqtt_item &it = MqttSetup[i];
		if(it.group != group) {
			if(on && debug) journal.jprintf(cStrEnd);            // конец предыдущей пачки
			group = it.group;
			on = mqtt_group_on(group);
			if(on && MqttGroupTitle[group]) {
				_delay(100);   // пауза перед следующей пачкой - разгружаем сервер и себя
				if(debug) journal.jprintf("%s", MqttGroupTitle[group]);
			}
		}
		if(!on) continue;
		// Обычный пункт - один датчик; MQS_ALL - перебрать весь массив
		uint8_t from = it.number, to = it.number;
		if(it.number == MQS_ALL) { from = 0; to = ANUMBER - 1; }
		for(uint8_t n = from; n <= to; n++) {
			if(!mqtt_cond(it, n)) continue;
			strcpy(HP.clMQTT.topic, HP.clMQTT.root);
			strcat(HP.clMQTT.topic, mqtt_topic_name(it, n));
			mqtt_value(it, n);
			// false - это брокер MQTT, а не НародМон.
			if(!HP.clMQTT.sendTopic(false, debug, mqtt_close_here(i, n))) return false;
			if(debug) journal.jprintf((char*)MQTTDebugStr, HP.clMQTT.topic, HP.clMQTT.temp);
		}
	}
	if(on && debug) journal.jprintf(cStrEnd);
	if(!debug && DBG_ON(DBG_MON)) journal.jprintf((char*)MQTTPublishOK);
	return true;
}

 
// Отослать данные на ThingSpeak отсылка идет одним пакетом
// debug  true - выводить в консоль информацию о посылаемых данных false - нет вывода
// возвращает true если удачно
// При удачной отправке Socket# 7 Sn_MR:1 Sn_CR:0 Sn_IR:7 Sn_SR:0 Sn_PORT:401 Sn_DPORT:75b Sn_TOS:0 Sn_TTL:80 Sn_IMR:ff Sn_FRAG:4000 Sn_KPALVTR:0
// При неудачной
boolean  sendThingSpeak(boolean debug)
{

     HP.clMQTT.clearBuf();   // подготовить рабочие буфера
      // Готовим данные
     strcpy(HP.clMQTT.root,"channels/");  // Название топика едино посылаем одним запросом
     HP.clMQTT.get_paramMQTT((char*)mqtt_LOGIN_MQTT,HP.clMQTT.root);
     strcat(HP.clMQTT.root,"/publish/");
     HP.clMQTT.get_paramMQTT((char*)mqtt_PASSWORD_MQTT,HP.clMQTT.root);
    
     // Формируем данные и посылаем данные  сразу на много полей
     strcpy(HP.clMQTT.topic,"field1=");
     strcat(HP.clMQTT.topic,ftoa(HP.clMQTT.temp,(float)HP.sTemp[TOUT].get_Temp()/100.0,1));
 
     strcat(HP.clMQTT.topic,"&field2=");
     strcat(HP.clMQTT.topic,ftoa(HP.clMQTT.temp,(float)HP.sTemp[TIN].get_Temp()/100.0,1));
 
     strcat(HP.clMQTT.topic,"&field3=");
     strcat(HP.clMQTT.topic,ftoa(HP.clMQTT.temp,(float)HP.sTemp[TBOILER].get_Temp()/100.0,1));

     strcat(HP.clMQTT.topic,"&field4=");
     if(HP.dFC.get_present())
         {
          strcat(HP.clMQTT.topic,ftoa(HP.clMQTT.temp,(float)HP.dFC.get_frequency()/100.0,1));
         }
     else
         {
         if (HP.dRelay[RCOMP].get_Relay()) strcat(HP.clMQTT.topic,cOne);   
         else                              strcat(HP.clMQTT.topic,cZero);  
         }
          
     strcat(HP.clMQTT.topic,"&field5=");
     strcat(HP.clMQTT.topic,ftoa(HP.clMQTT.temp,(float)HP.sTemp[TCOMP].get_Temp()/100.0,1));
         
     strcat(HP.clMQTT.topic,"&field6=");
     #ifdef EEV_DEF 
     strcat(HP.clMQTT.topic,ftoa(HP.clMQTT.temp,(float)HP.dEEV.get_Overheat()/100.0,1));
     #else
     if(HP.sTemp[TEVAOUTG].get_present()) strcat(HP.clMQTT.topic,ftoa(HP.clMQTT.temp,(float)HP.sTemp[TEVAOUTG].get_Temp()/100.0,1));
     else   strcat(HP.clMQTT.topic,cZero);
     #endif
      
     strcat(HP.clMQTT.topic,"&field7=");
     #ifdef EEV_DEF
     _itoa(HP.dEEV.get_EEV(),HP.clMQTT.topic);
     #else  // Вместо положения ЭРВ посылаем температуру гликоля
     if(HP.sTemp[TEVAING].get_present()) strcat(HP.clMQTT.topic,ftoa(HP.clMQTT.temp,(float)HP.sTemp[TEVAING].get_Temp()/100.0,1));
     else   strcat(topic,cZero);
     #endif
     
     strcat(HP.clMQTT.topic,"&field8=");
     _itoa(HP.get_errcode(),HP.clMQTT.topic);
     strcat(HP.clMQTT.topic,(char*)"&status=MQTTPUBLISH");
     // Проверка на длины
       if((strlen(HP.clMQTT.root)>=LEN_ROOT-2)||(strlen(HP.clMQTT.topic)>LEN_TOPIC-2)) { journal.jprintf("$WARNING: Long topic or data string, is problem.\n"); return false;}
     // Отправка
     if(debug || DBG_ON(DBG_MON)) journal.jprintf((char*)MQTTpublish,HP.clMQTT.get_mqtt_server());
     
     if (HP.clMQTT.sendTopic(false,debug,true)) { if (debug) journal.jprintf(" %s %s\n", HP.clMQTT.root,HP.clMQTT.topic);else if(DBG_ON(DBG_MON)) journal.jprintf((char*)MQTTPublishOK);return true;} else return false;  
     return true;
}
  
// получить  состояние MQTT клиента в виде строки
char *get_stateMQTT()
{
    switch (w5200_MQTT.state())
        {
        case MQTT_CONNECTION_TIMEOUT      : return (char*)"MQTT_CONNECTION_TIMEOUT";       break;
        case MQTT_CONNECTION_LOST         : return (char*)"MQTT_CONNECTION_LOST";          break;
        case MQTT_CONNECT_FAILED          : return (char*)"MQTT_CONNECT_FAILED";           break;
        case MQTT_DISCONNECTED            : return (char*)"MQTT_DISCONNECTED";             break;
        case MQTT_CONNECTED               : return (char*)"MQTT_CONNECTED";                break;
        case MQTT_CONNECT_BAD_PROTOCOL    : return (char*)"MQTT_CONNECT_BAD_PROTOCOL";     break;
        case MQTT_CONNECT_BAD_CLIENT_ID   : return (char*)"MQTT_CONNECT_BAD_CLIENT_ID";    break;
        case MQTT_CONNECT_UNAVAILABLE     : return (char*)"MQTT_CONNECT_UNAVAILABLE";      break;
        case MQTT_CONNECT_BAD_NAME_OR_PASS: return (char*)"MQTT_CONNECT_BAD_NAME_OR_PASS"; break;
        case MQTT_CONNECT_UNAUTHORIZED    : return (char*)"MQTT_CONNECT_UNAUTHORIZED";     break;
        default                           : return (char*)"MQTT_UNKNOW_ERROR";             break;
        }
return (char*)cError;  
}
#endif

