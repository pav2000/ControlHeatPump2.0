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
#ifndef MQTT_h
#define MQTT_h
// Заголовок самодостаточен.
#include "../../Constant.h"
#include <Ethernet.h>     // EthernetClient
#include <PubSubClient.h> // PubSubClient
// ---------------------------------------------------------------------------
// ---- Структура MQTT клиент ТН ----
// ---------------------------------------------------------------------------
// Работа с отдельными флагами type_mqtt
#define fMqttUse         0                   // флаг использования MQTT
#define fMqttBig         1                   // флаг отправки ДОПОЛНИТЕЛЬНЫХ данных на MQTT
#define fMqttSDM120      2                   // флаг отправки данных электросчетчика на MQTT
#define fMqttFC          3                   // флаг отправки данных инвертора на MQTT
#define fMqttCOP         4                   // флаг отправки данных COP на MQTT
#define fNarodMonUse     5                   // флаг отправки данных на народный мониторинг
#define fNarodMonBig     6                   // флаг отправки данных на народный мониторинг, большая версия
#define fTSUse           7                   // флаг использования ThingSpeak
// Размеры рабочих буферов MQTT
#define LEN_ROOT          100                // максимальная длина корня топика
#define LEN_TOPIC         200                // максимальная длина данных топика

enum TYPE_STATE_MQTT
{
 pMQTT_OK,             // Последняя передача была удачна
 pMQTT_SOCK,           // Попытка реанимировать сокет
 pMQTT_DNS,            // Надо проверить имя
 pMQTT_RES             // надо сбросить чип
};

STRCONST(BlockService,  "Too many sending errors, sending %s is off . . .\n");
STRCONST(SlowService,  "Too many sending errors, sending %s switched to %d min interval . . .\n");
STRCONST(BackService,  "Sending %s recovered, interval back to normal\n");
STRCONST(ChangeIP,  "\n Change IP address %s\n");
STRCONST(ResSock,  "\n$WARNING: Reset W5200_SOCK_SYS sock\n");
STRCONST(ResDHCP,  "\n$WARNING: Update server IP\n");
STRCONST(ResChip,  "\n$WARNING: Reset W5XXX chip\n");

// Настройки относящиеся к MQTT
struct type_mqtt                             // настройки MQTT клиента
{
 uint16_t flags;                             // Бинарные флага настроек
 // Сервер MQTT
 uint16_t ttime;                             // период отправки на сервер в сек. 10...60000
 char mqtt_server[32];                       // Адрес сервера
 IPAddress mqtt_serverIP;                    // IP Адрес сервера
 uint16_t mqtt_port;                         // Адрес порта сервера
 char mqtt_login[20];                        // логин сервера
 char mqtt_password[20];                     // пароль сервера
 char mqtt_id[16];                           // Идентификатор клиента на MQTT сервере
 // Народный мониторинг
 char narodMon_server[32];                    // Адрес сервера народного мониторинга
 IPAddress narodMon_serverIP;                 // IP Адрес сервера народного мониторинга
 uint16_t narodMon_port;                      // Адрес порта сервера  народного мониторинга
 char narodMon_login[16];                     // логин сервера народного мониторинга
 char narodMon_password[16];                  // пароль сервера народного мониторинга
 char narodMon_id[16];                        // Идентификатор клиента на народного мониторинга
};

// Клиент PubSubClient создаётся в Control.ino, состояние печатает Telemetry.cpp.
extern PubSubClient w5200_MQTT;
char *get_stateMQTT();

// Отправители телеметрии. Определены в Telemetry.cpp, вызываются из Control.ino.
// Все четыре существуют только при определённом MQTT - под этим же условием
// стоят и вызовы. Аргумент debug просит вывести отправляемое в журнал.
boolean sendNarodMon(boolean debug);
boolean sendNarodMon2022(boolean debug);
boolean sendMQTT(boolean debug);
boolean sendThingSpeak(boolean debug);

class clientMQTT                              // Класс клиент MQTT
 {
 public:
    void initMQTT(uint8_t web_task);            // инициализация MQTT параметр - номер потока сервера в котором зупускается MQTT
    uint8_t *get_save_addr(void) { return (uint8_t *)&mqttSettintg; } // Адрес структуры сохранения
    void api_dns_mqtt()   { dnsUpadateMQTT = true; }    // после записи адреса через веб-API: IP надо получить заново
    void api_dns_narmon() { dnsUpadateNARMON = true; }
    uint16_t get_save_size(void) { return sizeof(mqttSettintg); } // Размер структуры сохранения
    int32_t save(int32_t adr);                  // Записать MQTT в еепром под номерм num
    int32_t load(int32_t adr);                  // загрузить MQTT num из еепром память
    int32_t loadFromBuf(int32_t adr,byte *buf); // Считать настройки из буфера на входе адрес с какого, на выходе код ошибки (меньше нуля)
    uint16_t get_crc16(uint16_t crc);           // Рассчитать контрольную сумму для данных на входе входная сумма на выходе новая

    boolean dnsUpdate();                        // Обновление IP адресов MQTT через dns
    boolean sendTopic(boolean NM, boolean debug, boolean link_close);// Внутренная функция послать один топик
    void    recoverNARMON();                    // Реанимация связи с народным мониторингом. МЮТЕКС xWebThreadSemaphore ДОЛЖЕН быть захвачен!

    uint16_t updateErrMQTT(boolean NM);         // Еще одна ошибка отправки на MQTT параметр тип сервера true - народный мониторинг false MQTT
    void resetErrMQTT(boolean NM);               // Сброс числа ошибок отправки; для народного мониторинга ещё и возврат с редкого периода
    void updateState(boolean NM,TYPE_STATE_MQTT state){if(NM) stateNARMON=state;else stateMQTT=state;}  // Установка состояния соединения MQTT

    // Установка параметров
    char*   get_paramMQTT(char *var, char *ret);  // Получить параметр из строки по имени var, результат ДОБАВЛЯЕТСЯ в строку ret

    void set_mqtt_serverIP(IPAddress x){mqttSettintg.mqtt_serverIP=x; }          // Установить IP Адрес сервера MQTT
    void set_narodMon_serverIP(IPAddress x){mqttSettintg.narodMon_serverIP=x; }  // Установить IP Адрес сервера народного мониторинга
    // чтение настроек
    boolean get_MqttUse() {return GETBIT(mqttSettintg.flags,fMqttUse);}          // Получить флаг отправки на сервер MQTT
    boolean get_TSUse() {return GETBIT(mqttSettintg.flags,fTSUse);}              // Получить флаг отправки на  ThingSpeak
    boolean get_MqttBig() {return GETBIT(mqttSettintg.flags,fMqttBig);}          // Получить флаг отправки доп данных на сервер MQTT
    boolean get_MqttSDM120() {return GETBIT(mqttSettintg.flags,fMqttSDM120);}    // Получить флаг отправки данных электросчетчика на сервер MQTT
    boolean get_MqttFC() {return GETBIT(mqttSettintg.flags,fMqttFC);}            // Получить флаг отправки данных инвертора на MQTT
    boolean get_MqttCOP() {return GETBIT(mqttSettintg.flags,fMqttCOP);}          // Получить флаг отправки данных COP на MQTT

    IPAddress get_mqtt_serverIP(){return mqttSettintg.mqtt_serverIP; }           // IP Адрес сервера MQTT
    char*  get_mqtt_server(){return mqttSettintg.mqtt_server; }                  // Адрес сервера MQTT

    uint16_t get_mqtt_port(){return mqttSettintg.mqtt_port;}                     // Адрес порта сервера MQTT
    uint16_t get_ttime(){return mqttSettintg.ttime;}                             // Время отправки

    boolean get_NarodMonUse() {return GETBIT(mqttSettintg.flags,fNarodMonUse);}  // Получить флаг отправки на народный мониторинг
    boolean get_NarodMonBig() {return GETBIT(mqttSettintg.flags,fNarodMonBig);}  // Получить флаг отправки на народный мониторинг
    IPAddress get_narodMon_serverIP(){return mqttSettintg.narodMon_serverIP; }   // IP Адрес сервера народного мониторинга
    char*   get_narodMon_server(){return mqttSettintg.narodMon_server; }         // Адрес сервера народного мониторинга
    uint16_t get_narodMon_port(){return mqttSettintg.narodMon_port;}             // Адрес порта сервера  народного мониторинга
    char*   get_narodMon_login(){return mqttSettintg.narodMon_login;}            // Логин владельца, передается метрикой OWNER
    char*   get_narodMon_id(){return mqttSettintg.narodMon_id;}                  // Имя прибора на народном мониторинге
    void    nextStateNARMON();                                                   // Перевести состояние на следующую ступень реанимации
    // Ручная отправка по кнопке из веб морды. Взводить флаг можно из ЛЮБОГО потока, сама отправка
    // идет в MAIN_WEB_TASK - только там рабочие буфера гарантированно свободны.
    // Редкий режим отправки на народный мониторинг: включается после
    // NARMON_NUM_ERR_SLOW неудач подряд, снимается первой удачной отправкой.
    // Состояние РАБОЧЕЕ, в настройках не сохраняется: после перезагрузки
    // отправка начинается со штатного периода.
    boolean get_NarodMonSlow(){return narmonSlow;}
    void    setTestNARMON(){testNARMON=true;}                                    // Заказать внеочередную отправку
    boolean getTestNARMON(){return testNARMON;}                                  // Заказана ли внеочередная отправка
    void    clearTestNARMON(){testNARMON=false;}                                 // Снять заказ
    void clearBuf() {root[0]=0x00;topic[0]=0x00;temp[0]=0x00;};                  // очистка рабочих буферов перед отправкой
    // Рабочие буфера для отправки MQTT
    // Для экономии места используется выходной буфер MAIN_WEB_TASK потока веб сервера Socket[MAIN_WEB_TASK].outBuf[3*W5200_MAX_LEN] (в этом потоке ДОЛЖЕН проводится запуск MQTT и буфер гарантированно не используется другими задачами)
    // Размер выходного буфера char outBuf[3*W5200_MAX_LEN] - 6 кБ - хватит на все. Распределение памяти в выходном буфере:
    //  смещение 0 -                        root[LEN_ROOT]  - корень топика, длина до LEN_ROOT
    //  смещение 100 (LEN_ROOT) -           topic[LEN_TOPIC] - сам топик, длина до  LEN_TOPIC
    //  смещение 300 (LEN_ROOT+LEN_TOPIC) - temp[16] - временный буфер для получения строки флоат
    // ВНИМАНИЕ: отправка на народный мониторинг (sendNarodMon2022) собирает ВЕСЬ пакет в root,
    // занимая NARMON_PACKET_MAX байт от его начала плюс NARMON_ANSWER_MAX под ответ сервера.
    // Раскладка выше при этом перекрывается, topic и temp в той функции не используются.
    char *root,*topic, *temp;                                                      // указатели на рабочие буфера

 private:
   boolean connect(boolean NM);                 // Попытаться соеденится с сервером МЮТЕКС уже захвачен!!
   boolean closeSockSys(boolean debug);         // Попытаться сбросить системный сокет МЮТЕКС уже захвачен!!
   boolean dnsUpadateMQTT;                      // Флаг необходимости обновления через dns IP адреса для MQTT
   boolean dnsUpadateNARMON;                    // Флаг необходимости обновления через dns IP адреса для NARMON
   boolean testNARMON;                          // Заказана внеочередная отправка на народный мониторинг (кнопка в веб морде)
   boolean narmonSlow;                          // Редкий период отправки на народный мониторинг после серии неудач
   uint16_t numErrNARMON;                       // число ошибок отправки на народный мониторинг
   uint16_t numErrMQTT;                         // число ошибок отправки на MQTT
   TYPE_STATE_MQTT stateNARMON;                 // Состояние передачи что надо делать
   TYPE_STATE_MQTT stateMQTT;                   // Состояние передачи что надо делать
   type_mqtt mqttSettintg;                      // настройки клиента
 } ;

#endif
