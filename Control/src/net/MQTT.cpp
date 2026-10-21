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

// ---------------------------------------------------------------------------
// ---- MQTT клиент ТН ----
// ---------------------------------------------------------------------------

/*
 * Клиент MQTT: отдельная единица трансляции.
 *
 * MQTT.h тянет Constant.h, а с ним Config.h, поэтому #ifdef MQTT разрешается
 * так же, как в главной единице трансляции. Проверено сверкой размеров методов:
 * уход в не ту ветку означал бы молча отключённую телеметрию.
 */
#include "MQTT.h"
#include "../core/HeatPump.h"     // HP, set_Error
#include "../hw/Hardware.h"     // readEEPROM_I2C / writeEEPROM_I2C, IPAddress2String
#include "../log/Journal.h"      // journal
#include "Network.h"      // сеть: initW5200, linkStatusWiznet, zeroIP
#include "../core/Rtos.h"         // замки шин, Watchdog_AliveHere
#include "../util/Util.h"         // _crc16, _itoa, my_atof
#include "WebShared.h"    // Socket[] - MQTT одалживает выходной буфер
#include "ParamRegistry.h" // таблица параметров веб-API (внизу файла)
#include <stddef.h>
// #ifdef MQTT стоит ПОСЛЕ включений намеренно: сам макрос приходит из Config.h
// через Constant.h. Если условие поставить раньше, оно окажется ложным и файл
// выпадет целиком - здесь это уже случилось и было поймано компоновщиком.
#ifdef MQTT


// инициализация MQTT параметр - номер потока сервера в котором зупускается MQTT
void clientMQTT::initMQTT(uint8_t web_task)
{
 // инициализации рабочих буферов MQTT
 root=Socket[web_task].outBuf+0;
 root[0]=0x00; // Стереть строку
 topic=Socket[web_task].outBuf+LEN_ROOT;
 topic[0]=0x00;
 temp=Socket[web_task].outBuf+LEN_ROOT+LEN_TOPIC;
 temp[0]=0x00;
 // Установка настроек
 IPAddress zeroIP(0,0,0,0);
 mqttSettintg.flags=0x00;                                 // Бинарные флага настроек
 SETBIT0(mqttSettintg.flags,fMqttUse);                    // флаг использования MQTT
 SETBIT0(mqttSettintg.flags,fTSUse);                      // флаг использования ThingSpeak
 SETBIT0(mqttSettintg.flags,fMqttBig);                    // флаг отправки ДОПОЛНИТЕЛЬНЫХ данных на MQTT
 SETBIT0(mqttSettintg.flags,fMqttSDM120);                 // флаг отправки данных электросчетчика на MQTT
 SETBIT0(mqttSettintg.flags,fMqttFC);                     // флаг отправки данных инвертора на MQTT
 SETBIT0(mqttSettintg.flags,fMqttCOP);                    // флаг отправки данных COP на MQTT
 SETBIT0(mqttSettintg.flags,fNarodMonUse);                // флаг отправки данных на народный мониторинг
 SETBIT0(mqttSettintg.flags,fNarodMonBig);                // флаг отправки данных на народный мониторинг расширенную версию
 mqttSettintg.ttime=DEFAULT_TIME_MQTT;                    // период отправки на сервер в сек. 10...60000
 strcpy(mqttSettintg.mqtt_server,DEFAULT_ADR_MQTT);       // Адрес сервера
 mqttSettintg.mqtt_serverIP=zeroIP;                       // IP Адрес сервера

 mqttSettintg.mqtt_port=DEFAULT_PORT_MQTT;                // Адрес порта сервера
 strcpy(mqttSettintg.mqtt_login,"admin");                 // логин сервера
 strcpy(mqttSettintg.mqtt_password,"admin");              // пароль сервера
 strcpy(mqttSettintg.mqtt_id,"HeatPump");                 // Идентификатор клиента на MQTT сервере
 // NARMON
 strcpy(mqttSettintg.narodMon_server,DEFAULT_ADR_NARMON); // Адрес сервера народного мониторинга
 mqttSettintg.narodMon_serverIP=zeroIP;                   // IP Адрес сервера народного мониторинга
 mqttSettintg.narodMon_port=DEFAULT_PORT_NARMON;          // Адрес порта сервера народного мониторинга (TCP протокол, 8283)
 strcpy(mqttSettintg.narodMon_login,DEFAULT_LOGIN_NARMON);// логин владельца на народном мониторинге (метрика OWNER)
 strcpy(mqttSettintg.narodMon_password,"1234");           // пароль сервера народного мониторинга
 strcpy(mqttSettintg.narodMon_id,"HeatPump");             // Идентификатор клиента на MQTT сервере

 dnsUpadateMQTT=false;                                     // Флаг необходимости обновления через dns IP адреса для MQTT
 dnsUpadateNARMON=false;                                   // Флаг необходимости обновления через dns IP адреса для NARMON
 testNARMON=false;                                         // Внеочередная отправка на народный мониторинг не заказана
 narmonSlow=false;                                         // Начинаем со штатного периода отправки
 stateNARMON=pMQTT_OK;                                     // Состояние передачи, с него начинается лестница реанимации
 stateMQTT=pMQTT_OK;
 numErrNARMON=0;                                           // Счетчики ошибок отправки подряд
 numErrMQTT=0;
 w5200_MQTT.setSock(W5200_SOCK_SYS);                       // Установить сокет с которым работаем
}

 // Получить параметр из строки по имени var, результат ДОБАВЛЯЕТСЯ в строку ret
char*   clientMQTT::get_paramMQTT(char *var, char *ret)
{
    if(strcmp(var, mqtt_USE_TS)==0){if (GETBIT(mqttSettintg.flags,fTSUse))      return  strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero);} else     // флаг использования ThingSpeak
    if(strcmp(var, mqtt_USE_MQTT)==0){  if (GETBIT(mqttSettintg.flags,fMqttUse))    return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero);} else       // флаг использования MQTT
    if(strcmp(var, mqtt_BIG_MQTT)==0){  if (GETBIT(mqttSettintg.flags,fMqttBig))    return strcat(ret,(char*)cOne); else return  strcat(ret,(char*)cZero);} else      // флаг отправки ДОПОЛНИТЕЛЬНЫХ данных на MQTT
    if(strcmp(var, mqtt_SDM_MQTT)==0){   if (GETBIT(mqttSettintg.flags,fMqttSDM120)) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero);} else     // флаг отправки данных электросчетчика на MQTT
    if(strcmp(var, mqtt_FC_MQTT)==0){   if (GETBIT(mqttSettintg.flags,fMqttFC))     return  strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero);} else      // флаг отправки данных инвертора на MQTT
    if(strcmp(var, mqtt_COP_MQTT)==0){    if (GETBIT(mqttSettintg.flags,fMqttCOP))    return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero);} else     // флаг отправки данных COP на MQTT
    if(strcmp(var, mqtt_TIME_MQTT)==0){  return _itoa(mqttSettintg.ttime/60,ret);                                                    } else     // ПРИХОДЯТ МИНУТЫ храним СЕКУНДЫ период отправки на сервер в сек. 10...60000
    if(strcmp(var, mqtt_ADR_MQTT)==0){  return strcat(ret,mqttSettintg.mqtt_server);                                                          } else     // Адрес сервера
    if(strcmp(var, mqtt_IP_MQTT)==0){    return strcat(ret,IPAddress2String(mqttSettintg.mqtt_serverIP));                                      } else      // IP Адрес сервера,  Только на чтение. описание первого параметра для отправки смс
    if(strcmp(var, mqtt_PORT_MQTT)==0){    return _itoa(mqttSettintg.mqtt_port,ret);                                                  } else      // Порт сервера
    if(strcmp(var, mqtt_LOGIN_MQTT)==0){  return strcat(ret,mqttSettintg.mqtt_login);                                                           } else     // логин сервера
    if(strcmp(var, mqtt_PASSWORD_MQTT)==0){  return strcat(ret,mqttSettintg.mqtt_password);                                                     } else     // пароль сервера
    if(strcmp(var, mqtt_ID_MQTT)==0){      return strcat(ret,mqttSettintg.mqtt_id);                                                         } else     // дентификатор клиента на MQTT сервере
    // ---- NARMON ----
    if(strcmp(var, mqtt_USE_NARMON)==0){if (GETBIT(mqttSettintg.flags,fNarodMonUse)) return  strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero);} else      // флаг отправки данных на народный мониторинг
    if(strcmp(var, mqtt_BIG_NARMON)==0){ if (GETBIT(mqttSettintg.flags,fNarodMonBig)) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero); } else      // флаг отправки данных на народный мониторинг  расширенная версия
    // Только чтение. Серия неудач не гасит флаг USE_NARMON (флаг владельца не трогается),
    // поэтому редкий режим надо спрашивать отдельно - иначе часовые пропуски в
    // отправке нечем объяснить, кроме строки в журнале.
    if(strcmp(var, mqtt_SLOW_NARMON)==0){ if (narmonSlow) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero);   } else
    if(strcmp(var, mqtt_ADR_NARMON)==0){ return  strcat(ret,mqttSettintg.narodMon_server);                                                      } else   // Адрес сервера народного мониторинга
    if(strcmp(var, mqtt_IP_NARMON)==0){return strcat(ret,IPAddress2String(mqttSettintg.narodMon_serverIP));                                 } else    // IP Адрес сервера народного мониторинга,
    if(strcmp(var, mqtt_PORT_NARMON)==0){ return _itoa(mqttSettintg.narodMon_port,ret);                                               } else   // Порт сервера народного мониторинга
    if(strcmp(var, mqtt_LOGIN_NARMON)==0){ return strcat(ret,mqttSettintg.narodMon_login);                                                      } else      // логин сервера народного мониторинга
    if(strcmp(var, mqtt_PASSWORD_NARMON)==0){return strcat(ret,mqttSettintg.narodMon_password);                                                  } else      // пароль сервера  народного мониторинга
    if(strcmp(var, mqtt_ID_NARMON)==0){ return strcat(ret,mqttSettintg.narodMon_id);                                                          } else     // дентификатор клиента на MQTT сервере народного мониторинга
    return (char*)cError;
}


// Обновление IP адресов серверов через dns
// Возврат - флаг использования дополнительного сокета и времени (нужен для разгрузки 0 потока сервера)
// возвращает true обновление не было (можно нагружать), false - прошло обновление или ошибка (нагружать не надо до следующего цикла)
boolean clientMQTT::dnsUpdate()
{
	boolean ret = true; // по умолчанию преобразование не было
	if(xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
		dnsUpadateMQTT =dnsUpadateNARMON = true;  // Обновляться надо
		Watchdog_AliveHere();
	}
	if(dnsUpadateMQTT) //надо обновлятся MQTT
	{
		dnsUpadateMQTT = false;   // сбросить флаг
		if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) return false; // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
		check_address(mqttSettintg.mqtt_server, mqttSettintg.mqtt_serverIP); // Получить адрес IP через DNS При не удаче возвращается 0, при удаче: 1 - IP на входе (были цифры, DNS не нужен), 2 - был запрос к DNS и адрес получен
		if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) SemaphoreGive (xWebThreadSemaphore);
		ret = false;  // вызов обновления был
	}
	if(dnsUpadateNARMON) //надо обновлятся NARMON
	{
     	dnsUpadateNARMON = false;   // сбросить флаг
		if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
			if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) return false; // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
		} else Watchdog_AliveHere();
		check_address(mqttSettintg.narodMon_server, mqttSettintg.narodMon_serverIP);// Получить адрес IP через DNS При не удаче возвращается 0, при удаче: 1 - IP на входе (были цифры, DNS не нужен), 2 - был запрос к DNS и адрес получен
		if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) SemaphoreGive (xWebThreadSemaphore);
		ret = false;  // вызов обновления был
	}
	return ret;
}

// Записать настройки в eeprom i2c на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
int32_t clientMQTT::save(int32_t adr)
{
 if (writeEEPROM_I2C(adr, (byte*)&mqttSettintg, sizeof(mqttSettintg))) { set_Error(ERR_SAVE_EEPROM,(char*)nameHeatPump); return ERR_SAVE_EEPROM;}  adr=adr+sizeof(mqttSettintg);           // записать параметры MQTT
 return adr;
}
// Считать настройки из eeprom i2c на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
int32_t clientMQTT::load(int32_t adr)
{
  if (readEEPROM_I2C(adr, (byte*)&mqttSettintg, sizeof(mqttSettintg))) { set_Error(ERR_LOAD_EEPROM,(char*)nameHeatPump); return ERR_LOAD_EEPROM;}  adr=adr+sizeof(mqttSettintg);           // прочитать параметры MQTT
  return adr;
}
// Считать настройки из буфера на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
int32_t clientMQTT::loadFromBuf(int32_t adr, byte *buf)
{
 memcpy((byte*)&mqttSettintg,buf+adr,sizeof(mqttSettintg)); adr=adr+sizeof(mqttSettintg);
 return adr;
}
// Рассчитать контрольную сумму для данных на входе входная сумма на выходе новая
uint16_t clientMQTT::get_crc16(uint16_t crc)
{
  uint16_t i;
  for(i=0;i<sizeof(mqttSettintg);i++) crc=_crc16(crc,*((byte*)&mqttSettintg+i));   // CRC16 настройки MQTT
  return crc;
}

// Сброс числа ошибок отправки. Для народного мониторинга это ещё и возврат
// с редкого периода: удачная отправка означает, что связь восстановилась.
void clientMQTT::resetErrMQTT(boolean NM)
{
 if (NM)
     {
     if(narmonSlow)
         {
         narmonSlow=false;
         journal.jprintf_time((char*)BackService,get_narodMon_server());
         }
     numErrNARMON=0;
     }
 else numErrMQTT=0;
}

// Еще одна ошибка отправки на MQTT параметр тип сервера true - народный мониторинг false MQTT
uint16_t clientMQTT::updateErrMQTT(boolean NM)
{
 if (NM)
     {
     // Отправка на народный мониторинг не выключается никогда. После серии
     // неудач она уходит на редкий период и живёт так до восстановления связи;
     // дальше неудачи не считаются, счётчик стоит на пороге. Пользовательский
     // флаг fNarodMonUse НЕ ТРОГАЕМ - он принадлежит владельцу, а не нам.
     numErrNARMON++;
     if (numErrNARMON>=NARMON_NUM_ERR_SLOW)
         {
         if(!narmonSlow)
             {
             narmonSlow=true;
             journal.jprintf_time((char*)SlowService,get_narodMon_server(),NARMON_SLOW/60);
             }
         numErrNARMON=NARMON_NUM_ERR_SLOW; // не растим дальше, в этом режиме неудачи не важны
         }
      return numErrNARMON;
     }
 else
     {
     numErrMQTT++;
     if (numErrMQTT>=MQTT_NUM_ERR_OFF)
         {
         SETBIT0(mqttSettintg.flags,fMqttUse);
         journal.jprintf_time((char*)BlockService,get_mqtt_server());
         numErrMQTT=0;// сбросить счетчик
         }
      return numErrMQTT;
     }
return 0;
}

// Реанимация связи с народным мониторингом ПЕРЕД очередной попыткой соединения.
// Ступени те же, что и у MQTT в sendTopic(): сокет -> перепроверка DNS -> сброс сетевого чипа.
// ВНИМАНИЕ: мютекс xWebThreadSemaphore ДОЛЖЕН быть захвачен вызывающим - внутри работа с SPI и сокетами!
void clientMQTT::recoverNARMON()
{
 IPAddress zeroIP(0,0,0,0), tempIP;
 uint8_t i;
 switch (stateNARMON)
 {
  case pMQTT_OK:                                     // Предыдущая отправка была удачна, реанимировать нечего
       break;
  case pMQTT_SOCK:                                   // Попытка реанимировать системный сокет
       Diag.num_resMQTT++;
       if(closeSockSys(true)) journal.jprintf((char*)ResSock);
       break;
  case pMQTT_DNS:                                    // Сокет не помог, возможно сервер сменил адрес
       Diag.num_resMQTT++;
       if(closeSockSys(true))
         {
          journal.jprintf((char*)ResDHCP);
          _delay(50);
          check_address(mqttSettintg.narodMon_server,tempIP);      // Адрес НЕ сохраняем в настройках, только в ОЗУ
          if((tempIP!=zeroIP)&&(tempIP!=mqttSettintg.narodMon_serverIP))
            {
             mqttSettintg.narodMon_serverIP=tempIP;
             journal.jprintf((char*)ChangeIP,mqttSettintg.narodMon_server);
            }
          _delay(50);
         }
       break;
  case pMQTT_RES:                                    // Ничего не помогло - сброс сетевого чипа
       Diag.num_resMQTT++;
       journal.jprintf((char*)ResChip);
       initW5200(false);                                           // Инициализация сети без вывода инфы в консоль
       for(i=0;i<W5200_THREAD;i++) SETBIT1(Socket[i].flags,fABORT_SOCK); // Признак инициализации сокета, надо прерывать передачу в сервере
       _delay(50);
       break;
 }
}

// Следующая ступень реанимации, вызывается ПОСЛЕ неудачной отправки на народный мониторинг
void clientMQTT::nextStateNARMON()
{
 switch(stateNARMON)
 {
  case pMQTT_OK:   stateNARMON=pMQTT_SOCK; break;
  case pMQTT_SOCK: stateNARMON=pMQTT_DNS;  break;
  case pMQTT_DNS:  stateNARMON=pMQTT_RES;  break;
  case pMQTT_RES:  stateNARMON=pMQTT_OK;   break;
 }
}


// Внутренная функция послать один топик, возврат удачно или нет послан топик, при не удаче запись в журнал
// NM - true народный мониторинг, false обычный сервер
// debug - выводить отладочные сообщения
// link_close  = true - по завершению закрывать связь  false по завершению не закрывать связь (отсылка нескольких топиков)
boolean clientMQTT::sendTopic(boolean NM, boolean debug, boolean link_close)
{
  TYPE_STATE_MQTT state;
  IPAddress tempIP;
  uint8_t i;
  if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE) {journal.jprintf((char*)cErrorMutex,__FUNCTION__,MutexWebThreadBusy);return false;} // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
 if (NM) state=stateNARMON; else state=stateMQTT;

 if (!linkStatusWiznet(false)) {journal.jprintf((char*)"sendTopic:no link.\n");  SemaphoreGive(xWebThreadSemaphore); return false;}// Нет связи выходим

 if (!w5200_MQTT.connected())   // если нет соединения то возможно требуется реанимация
  {
     switch (state) // Реанимация варианты
     {
          case pMQTT_OK:  break;  // Предыдущая отправка была удачна
          case pMQTT_SOCK:  // Попытка реанимировать сокет
                          Diag.num_resMQTT++;
                          if(closeSockSys(true))  journal.jprintf((char*)ResSock);
                          break;
          case pMQTT_DNS: // Предыдущая отправка была НЕ удачна
                          Diag.num_resMQTT++;
                          if(closeSockSys(true))   // НЕУДАЧА Пытаемся реанимировать сокет
                          {
                           journal.jprintf((char*)ResDHCP);
                          _delay(50);
                          // Пытаемся еще раз узнать адрес через ДНС, возможно он поменялось, если поменялся то меняем не сохраняем настройки
                          if (NM) {check_address(get_narodMon_server(),tempIP);if (tempIP!=get_narodMon_serverIP()){ set_narodMon_serverIP(tempIP);journal.jprintf((char*)ChangeIP,get_narodMon_server());}}
                          else    {check_address(get_mqtt_server(),tempIP);  if (tempIP!=get_mqtt_serverIP())    { set_mqtt_serverIP(tempIP);    journal.jprintf((char*)ChangeIP,get_mqtt_server());}}
                           _delay(50);
                          }
                          break;
          case pMQTT_RES: // Предыдущая отправка - был сброс ДНС адреса теперь НАДО ЧИП СБРОСИТЬ
                         Diag.num_resMQTT++;
                         journal.jprintf((char*)ResChip);
                         initW5200(false);                                  // Инициализация сети без вывода инфы в консоль
                         for (i=0;i<W5200_THREAD;i++) SETBIT1(Socket[i].flags,fABORT_SOCK);  // Признак инициализации сокета, надо прерывать передачу в сервере
                          _delay(50);
                         break;
     }
 } // if (!connect(NM))
// Попытка соединения - и определение что делаем дальше
         if (connect(NM))                                        // Попытка соединения
          {
           w5200_MQTT.publish(topic,temp);                              // Посылка данных топика
           if (link_close) w5200_MQTT.disconnect();              // отсоединение если надо
           if (debug) ShowSockRegisters(W5200_SOCK_SYS);         // выводим состояние регистров ЕСЛИ ОТЛАДКА

           SemaphoreGive(xWebThreadSemaphore);
           resetErrMQTT(NM);                                     // Удачно - сбрасываем счетчик ошибок
           updateState(NM,pMQTT_OK);
           return true;
          }
          else                                  // соединение не установлено
          {
          ShowSockRegisters(W5200_SOCK_SYS);    // выводим состояние регистров ВСЕГДА
          closeSockSys(true);
          SemaphoreGive(xWebThreadSemaphore);
          journal.jprintf(" %s\n",get_stateMQTT());
          switch(state)  // устанавливаем метод реанимации
               {
               case pMQTT_OK:   updateState(NM,pMQTT_SOCK); break;
               case pMQTT_SOCK: updateState(NM,pMQTT_DNS);  break;
               case pMQTT_DNS:  updateState(NM,pMQTT_RES);  break;
               case pMQTT_RES:  updateState(NM,pMQTT_OK);   break;
               }
           updateErrMQTT(NM);                             // Добавляем ошибку
           return false;
          }
         SemaphoreGive(xWebThreadSemaphore);
 return false;
}
 // Попытаться соеденится с сервером МЮТЕКС уже захвачен!!
boolean clientMQTT::connect(boolean NM)
{
if (w5200_MQTT.connected()) return true;  // соединено выходим
w5200_MQTT.setSock(W5200_SOCK_SYS);       // Установить сокет с которым работаем
if (NM) // В зависимости от того с кем надо соединяться
         {
         w5200_MQTT.setServer(mqttSettintg.narodMon_serverIP,mqttSettintg.narodMon_port);                       // установить параметры Народного мониторинга
         w5200_MQTT.connect(HP.get_netMAC(), mqttSettintg.narodMon_login,mqttSettintg.narodMon_password);       // Соедиенение с народным мониторингом
         #ifdef MQTT_REPEAT            // разрешен повтор соединения
         if (!w5200_MQTT.connected()) { _delay(20); ShowSockRegisters(W5200_SOCK_SYS); w5200_MQTT.connect(HP.get_netMAC(), mqttSettintg.narodMon_login,mqttSettintg.narodMon_password); } // вторая попытка
         #endif
         }
         else
         {
          if(HP.clMQTT.get_TSUse())
               {
                w5200_MQTT.setServer(mqttSettintg.mqtt_serverIP,mqttSettintg.mqtt_port);
                w5200_MQTT.connect(mqttSettintg.mqtt_id);   //  установить параметры сервера (используется обращение через ДНС) Соедиенение с ThingSpeak
                #ifdef MQTT_REPEAT            // разрешен повтор соединения
                if (!w5200_MQTT.connected()) { _delay(20); ShowSockRegisters(W5200_SOCK_SYS); w5200_MQTT.connect(mqttSettintg.mqtt_id); }  // вторая попытка
                #endif
               }
               else
               {
                w5200_MQTT.setServer(mqttSettintg.mqtt_serverIP,mqttSettintg.mqtt_port);
                w5200_MQTT.connect(mqttSettintg.mqtt_id,mqttSettintg.mqtt_login,mqttSettintg.mqtt_password); //  установить параметры сервера Соедиенение с сервером MQTT
                #ifdef MQTT_REPEAT            // разрешен повтор соединения
                if (!w5200_MQTT.connected()) { _delay(20); ShowSockRegisters(W5200_SOCK_SYS); w5200_MQTT.connect(mqttSettintg.mqtt_id,mqttSettintg.mqtt_login,mqttSettintg.mqtt_password);}   // вторая попытка
                #endif
               }
         }
return  w5200_MQTT.connected();
}

// Попытаться сбросить системный сокет МЮТЕКС уже захвачен!!
const char* NoClosed={"Problem: W5200_SOCK_SYS sock is not closed, SR=%s\n"};
boolean clientMQTT::closeSockSys(boolean debug)
{
uint8_t x, sr;
sr = W5100.readSnSR(W5200_SOCK_SYS);                                                   // Прочитать статус сокета
if(sr==SnSR::CLOSED) return true;                                                      // Сокет уже закрыт выходим
if ((sr==SnSR::ESTABLISHED)||(sr==SnSR::CLOSE_WAIT)) { W5100.execCmdSn(W5200_SOCK_SYS, Sock_DISCON);_delay(20);}// если надо разобрать соединение

W5100.execCmdSn(W5200_SOCK_SYS, Sock_CLOSE);       // закрыть сокет
W5100.writeSnIR(W5200_SOCK_SYS, 0xFF);             // сбросить прерывания
_delay(20);
if((x=W5100.readSnSR(W5200_SOCK_SYS))!=SnSR::CLOSED) { if(debug) journal.jprintf((char*)NoClosed,byteToHex(x)); return false;}   // проверка на закрытие сокета
return true;
}

#endif

#ifdef MQTT          // без MQTT клиента HP.clMQTT нет, и таблицы тоже
// ---------------------------------------------------------------------------
// Реестр параметров веб-API: MQTT и «Народный мониторинг». Правила - как у oHP
// (HeatPumpSettings.cpp). Пароли серверов читает только admin (PF_AR). Поля IP
// серверов только для чтения: прежняя запись в них молча игнорировалась.
// ---------------------------------------------------------------------------
enum { M_DNS_MQTT = 1, M_DNS_NARMON, M_IP_MQTT, M_IP_NARMON, M_SLOW };
#define MO(f) offsetof(type_mqtt, f)
static const ApiParam apiMqttTab[] = {
	PN_BIT(mqtt_USE_TS,   MO(flags), fTSUse,       PT_B16, 0, 0, 0, 0),
	PN_BIT(mqtt_USE_MQTT, MO(flags), fMqttUse,     PT_B16, 0, 0, 0, 0),
	PN_BIT(mqtt_BIG_MQTT, MO(flags), fMqttBig,     PT_B16, 0, 0, 0, 0),
	PN_BIT(mqtt_SDM_MQTT, MO(flags), fMqttSDM120,  PT_B16, 0, 0, 0, 0),
	PN_BIT(mqtt_FC_MQTT,  MO(flags), fMqttFC,      PT_B16, 0, 0, 0, 0),
	PN_BIT(mqtt_COP_MQTT, MO(flags), fMqttCOP,     PT_B16, 0, 0, 0, 0),
	PN_U16(mqtt_TIME_MQTT, 1, 999, 60, MO(ttime), 0),                         // в вебе минуты, в памяти секунды
	PN_STR(mqtt_ADR_MQTT, MO(mqtt_server), 32, PF_NE, M_DNS_MQTT),
	{ mqtt_IP_MQTT, 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_IP, M_IP_MQTT, 0 },
	PN_U16(mqtt_PORT_MQTT, 2, 65533, 1, MO(mqtt_port), 0),
	PN_STR(mqtt_LOGIN_MQTT,    MO(mqtt_login),    20, PF_NE, 0),
	PN_STR(mqtt_PASSWORD_MQTT, MO(mqtt_password), 20, PF_NE | PF_AR, 0),
	PN_STR(mqtt_ID_MQTT,       MO(mqtt_id),       16, PF_NE, 0),
	PN_BIT(mqtt_USE_NARMON, MO(flags), fNarodMonUse, PT_B16, 0, 0, 0, 0),
	PN_BIT(mqtt_BIG_NARMON, MO(flags), fNarodMonBig, PT_B16, 0, 0, 0, 0),
	{ mqtt_SLOW_NARMON, 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_BOOL, M_SLOW, 0 },   // редкий режим отправки после серии неудач
	PN_STR(mqtt_ADR_NARMON, MO(narodMon_server), 32, PF_NE, M_DNS_NARMON),
	{ mqtt_IP_NARMON, 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_IP, M_IP_NARMON, 0 },
	PN_U16(mqtt_PORT_NARMON, 2, 65533, 1, MO(narodMon_port), 0),
	PN_STR(mqtt_LOGIN_NARMON,    MO(narodMon_login),    16, PF_NE, 0),
	PN_STR(mqtt_PASSWORD_NARMON, MO(narodMon_password), 16, PF_NE | PF_AR, 0),
	PN_STR(mqtt_ID_NARMON,       MO(narodMon_id),       16, PF_NE, 0),
};
static void *api_base_mqtt(uint8_t) { return HP.clMQTT.get_save_addr(); }
static int api_get_mqtt(uint8_t hook, uint8_t, ApiOut *o)
{
	type_mqtt *d = (type_mqtt *) HP.clMQTT.get_save_addr();
	if(hook == M_IP_MQTT) api_put_str(o, IPAddress2String(d->mqtt_serverIP));
	else if(hook == M_IP_NARMON) api_put_str(o, IPAddress2String(d->narodMon_serverIP));
	else if(hook == M_SLOW) api_put(o, HP.clMQTT.get_NarodMonSlow() ? "true" : "false");
	else return AE_FAIL;
	return AE_OK;
}
static void api_after_mqtt(uint8_t hook)
{
	if(hook == M_DNS_MQTT) HP.clMQTT.api_dns_mqtt();            // имя сервера изменилось: IP получаем заново
	else if(hook == M_DNS_NARMON) HP.clMQTT.api_dns_narmon();
}
static const ApiOps apiOpsMqtt = { api_base_mqtt, api_get_mqtt, 0, api_after_mqtt, 0, 0, 0, 0 };
extern const ApiGroup ApiGrpMqtt;
const ApiGroup ApiGrpMqtt = { "mqtt", apiMqttTab, sizeof(apiMqttTab) / sizeof(apiMqttTab[0]), &apiOpsMqtt };
#endif // MQTT
