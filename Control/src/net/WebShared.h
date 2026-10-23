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
// Общее для файлов веб-слоя: WebServer.cpp (транспорт, отдача файлов, POST)
// и WebApiGet.cpp (разбор ajax-запросов): объявления, видимые обеим единицам трансляции.
#ifndef WebShared_h
#define WebShared_h

#include "../../Constant.h"
#include "SdFat.h"   // SdFile и fname_t: заголовок обязан быть самодостаточным

// Разделитель записей в ответе ajax
#define ADD_WEBDELIM(str) strcat(str, WEBDELIM)

// Буферы веб-потоков. Объявление здесь, а не в Control.ino: структура
// нужна не только веб-слою - Journal, Statistics, Profile, MQTT и Message
// одалживают outBuf как рабочий буфер, чтобы не держать свой (об этом прямо
// написано в MQTT.h и Message.h). Сам массив создаётся в Control.ino.
// Флаги поля flags структуры ниже.
#define fABORT_SOCK   0                     // флаг прекращения передачи (произошел сброс сети)
#define fLOCK_LOST    2                     // замок xWebThreadSemaphore потерян (повторный захват не удался): шину и сокет трогать и замок отдавать нельзя
#define fUser         1                     // Признак идентификации как обычный пользователь (нужно подменить файл меню)

struct type_socketData
  {
    EthernetClient client;                  // Клиент для работы с потоком
    byte inBuf[W5200_MAX_LEN + 1];          // буфер для работы с w5200 (входной запрос); +1 байт под терминатор за полным пакетом
    char *inPtr;                            // указатель в входном буфере с именем файла или запросом (копирование не делаем)
    char outBuf[3*W5200_MAX_LEN];           // буфер для формирования ответа на запрос и чтение с карты
    int8_t sock;                            // сокет потока -1 свободный сокет
    uint8_t flags;                          // Флаги состояния потока
    uint16_t http_req_type;                 // Тип запроса
  };
extern type_socketData Socket[W5200_THREAD];

// Определены в WebServer.cpp
extern SdFile     wFile;
extern fname_t    wfname;
extern const char *noteRemarkTest[];

uint8_t sensorIpRequest(uint8_t thread, int8_t sock);   // удалённые датчики IP: узкий путь, 1 - запрос обработан (WebApiGet.cpp)
uint8_t api_dispatch(uint8_t thread);   // новый API (/api/...), определён в WebApiGet.cpp; 0 - не удалось отправить ответ

// Транспорт и разбор запроса. Определены в WebServer.cpp.
void          web_server(uint8_t thread);
uint16_t      GetRequestedHttpResource(uint8_t thread);
TYPE_RET_POST parserPOST(uint8_t thread, uint16_t size);
void          readFileSD(char *filename, uint8_t thread);

// Генерируемые файлы и выгрузки. Определены в WebFiles.cpp.
void     get_txtState(uint8_t thread, boolean header);
void     get_txtJournal(uint8_t thread);
void     get_txtSettings(uint8_t thread);
void     get_csvChart(uint8_t thread);
void     get_datTest(uint8_t thread);
int16_t  get_indexNoSD(uint8_t thread);
bool     get_binSettings(uint8_t thread);
bool     get_binEEPROM(uint8_t thread);

// Наблюдатель задач, отчёт для веб-морды. Определены в Control.ino.
// Watchdog_StatWeb размера буфера не спрашивает, см. комментарий в Constant.h.
void        Watchdog_StatWeb(char *buf, uint8_t clear);
const char *Watchdog_Worst(uint16_t *percent_out);

// Сводка счётчиков и состояния. Определена в WebFiles.cpp.
// Пишет ~1.3 кБ, буфер вызывающего обязан быть не меньше LEN_MAILBUF.
void get_Counts(char *tempBuf);

#endif
