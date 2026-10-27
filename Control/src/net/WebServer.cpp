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
#include <SerialFlash.h>

#include "WebShared.h"    // Socket[], генерируемые файлы
#include "../core/HeatPump.h"     // HP - состояние и настройки
#include "Network.h"      // транспорт, server1
#include "../core/Rtos.h"         // _delay, SemaphoreTake/Give
#include "../log/Statistics.h"   // Stats



// ---------------------------------------------------------------------------
// Простой веб сервер с обработкой запросов
// ---------------------------------------------------------------------------
#include "utility/socket.h" 
#include "FreeRTOS_ARM.h"     
// HTTP типы запросов
#define HTTP_invalid   0
#define HTTP_GET       1      // Обычный запрос GET
#define HTTP_REQEST    2      // Наш запрос "GET /&"
#define HTTP_POST      3      // POST передача файла
#define HTTP_POST_     4      // POST - подготовка
#define UNAUTHORIZED   5      // Авторизация не пройдена
#define BAD_LOGIN_PASS 6      // Не верный логин или пароль
#define HTTP_API       7      // Запрос нового API: GET /api/<глагол>?...


extern "C" void TaskGetRunTimeStats(void);
extern void  get_txtSettings(uint8_t thread);
extern void  get_txtJournal(uint8_t thread);
extern void  get_datTest(uint8_t thread);
extern void  get_csvChart(uint8_t thread);
extern int16_t  get_indexNoSD(uint8_t thread);


// Названия режимов теста
// Описание режима теста
const char *noteRemarkTest[] = {"Тестирование отключено, основной режим работы.",
                                       "Значения датчиков берутся из полей 'Тест', работа исполнительных устройств эмулируется - Безопасно.",
                                       "Значения датчиков берутся из полей 'Тест', исполнительные устройства работают за исключением компрессора (FC и RCOMP) - Почти безопасно.",
                                       "Значения датчиков берутся из полей 'Тест', все исполнительные устройства работают. Внимание! Может быть поврежден компрессор!"};
                               
                               
const char* file_types[] = {"text/html", "image/x-icon", "text/css", "application/javascript", "image/jpeg", "image/png", "image/gif", "text/plain", "text/ajax"};

const char header_Authorization_1[] = "Authorization: Basic ";
const char header_Authorization_2[] = "&&!Z";
const char* pageUnauthorized      = {"HTTP/1.0 401 Unauthorized\r\nWWW-Authenticate: Basic real_m=Admin Zone\r\nContent-Type: text/html\r\nAccess-Control-Allow-Origin: *\r\n\r\n"};
// Отказ на запрос API - БЕЗ WWW-Authenticate. Страница морды опрашивает /api/get каждые несколько секунд с тайм-аутом 3,5 с; заголовок заставлял браузер
// открывать собственное окно входа на каждый такой запрос, а тайм-аут закрывал окно раньше, чем его успевали заполнить (и оно появлялось снова).
// Без заголовка отказ получает сама морда и показывает свой диалог входа (shLogin); окно браузера остаётся для открытия страниц.
const char* pageUnauthorizedApi   = {"HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: 0\r\nConnection: close\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n\r\n"};
static void sendUnauthorized(uint8_t thread)
{
	// на отказе по паролю строка запроса ещё не разобрана (GetRequestedHttpResource вернул отказ первым действием), на прочих отказах она разобрана и это не API
	sendConstRTOS(thread, strncmp((char*)Socket[thread].inBuf, "GET /api/", 9) == 0 ? pageUnauthorizedApi : pageUnauthorized);
}
const char* NO_STAT               = {"Statistics are not supported in the firmware"};

const char *postRet[]            = {"Настройки из файла восстановлены\r\n\r\n",                           //  Ответы на пост запросы
									"Ошибка восстановления настроек из файла (см. журнал)\r\n\r\n",
									"", // пустая строка - ответ не выводится
									"Файлы загружены, подробности в журнале\r\n\r\n",
									"Ошибка загрузки файла, подробности в журнале\r\n\r\n",
									"Внутренняя ошибка парсера POST запросов\r\n\r\n",
									"Флеш диск не найден, загружать файлы некуда\r\n\r\n",
									"Размер файла настроек превышает 6144 байт!\r\n\r\n",
									"Настройки можно загружать только при остановленном ТН\r\n\r\n"
									};

SdFile wFile;
fname_t wfname;


void web_server(uint8_t thread)
{
	int32_t len;
	int8_t sock;

	if(SemaphoreTake(xWebThreadSemaphore, (W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {
		return;
	}          // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим

	Socket[thread].sock = -1;                      // Сокет свободный

	SPI_switchW5200();                    // Это лишнее но для надежности пусть будет
	for(sock = 0; sock < W5200_SOCK_SYS; sock++)  // Цикл по сокетам веб сервера!!!! служебный не трогаем!!
	{

#if    W5200_THREAD < 2
		if (Socket[0].sock==sock) continue;   // исключение повторного захвата сокетов
#elif  W5200_THREAD < 3
		if((Socket[0].sock==sock)||(Socket[1].sock==sock)) continue; // исключение повторного захвата сокетов
#elif  W5200_THREAD < 4
		if((Socket[0].sock == sock) || (Socket[1].sock == sock) || (Socket[2].sock == sock)) continue; // исключение повторного захвата сокетов
#else
		if((Socket[0].sock==sock)||(Socket[1].sock==sock)||(Socket[2].sock==sock)||(Socket[3].sock==sock)) continue; // исключение повторного захвата сокетов
#endif

		// Настройка  переменных потока для работы
		Socket[thread].http_req_type = HTTP_invalid;        // нет полезной инфы
		SETBIT0(Socket[thread].flags, fABORT_SOCK);          // Сокет сброса нет
		SETBIT0(Socket[thread].flags, fLOCK_LOST);
		SETBIT0(Socket[thread].flags, fUser); // Признак идентификации как обычный пользователь (нужно подменить файл меню)
		Socket[thread].client = server1.available_(sock);  // надо обработать
		Socket[thread].sock = sock;                           // запомнить сокет с которым работает поток

		if(Socket[thread].client) // запрос http заканчивается пустой строкой
		{
			while(Socket[thread].client.connected()) {
				// Ставить вот сюда
				WEB_STORE_DEBUG_INFO(10);
				if(Socket[thread].client.available()) {
					len = Socket[thread].client.get_ReceivedSizeRX();                  // получить длину входного пакета
					if(len > W5200_MAX_LEN) {
						journal.jprintf("WEB:Big packet: %d - truncated!\n", (int)(len));
						len = W5200_MAX_LEN; // Ограничить размером в максимальный размер пакета w5200
					}
					if(Socket[thread].client.read(Socket[thread].inBuf, len) != len) {
						journal.jprintf("WEB:Read error\n");
						break;
					}
					// Терминатор ставится ДО разбора и авторизации. Раньше он ставился после: между запросами
					// сбрасывался только inBuf[0], и короткий запрос без пароля оставлял за собой хвост прежнего,
					// более длинного. Поиск заголовка Authorization в GetRequestedHttpResource шёл дальше конца
					// новых данных, находил в хвосте пароль администратора из прежнего запроса, и новый запрос
					// выполнялся с правами админа. Массив на байт длиннее W5200_MAX_LEN, поэтому и для полного
					// пакета терминатор встаёт за данные (inBuf[W5200_MAX_LEN]), а не поверх последнего байта.
					Socket[thread].inBuf[len] = 0;
					// Ищем в запросе полезную информацию (имя файла или запрос ajax)
					// пройти авторизацию и разобрать заголовок -  получить имя файла, тип, тип запроса, и признак меню пользователя
					Socket[thread].http_req_type = GetRequestedHttpResource(thread);
					if(DBG_ON(DBG_HTTP)) {
						journal.jprintf("$QUERY: %s\n",Socket[thread].inPtr);
						journal.jprintf("$INPUT: %s\n",(char*)Socket[thread].inBuf);
					}
					switch(Socket[thread].http_req_type)  // По типу запроса
					{
					case HTTP_invalid: {
						if(DBG_ON(DBG_WERR | DBG_WFULL)) {
							uint8_t ip[4];
							W5100.readSnDIPR(sock, ip);
							journal.jprintf("WEB:Wrong request %d.%d.%d.%d (%d): ", ip[0], ip[1], ip[2], ip[3], (int)(len));
							if(DBG_ON(DBG_WFULL)) {
								if(len > 256) len = 256;
								for(uint16_t i = 0; i < len; i++) journal.jprintf("%c(%d) ", (char)Socket[thread].inBuf[i], Socket[thread].inBuf[i]);
							} else {
								for(len = 0; len < 4; len++) journal.jprintf("%d ", Socket[thread].inBuf[len]);
							}
							journal.jprintf("...\n");
						}
						break;
					}
					case HTTP_GET:     // чтение файла
					{
						// Имя декодируем ДО проверки доступа: "lan%2Ehtml" иначе проходил белый список и отдавал lan.html
						urldecode(Socket[thread].inPtr, Socket[thread].inPtr, len + 1);
						// Для обычного пользователя подменить файл меню, для сокращения функционала; остальное - по белому списку
						if(GETBIT(Socket[thread].flags, fUser)) {
							if(strcmp(Socket[thread].inPtr, "menu.js") == 0) strcpy(Socket[thread].inPtr, "menu-user.js");
							else if(strcmp(Socket[thread].inPtr, "_menu.js") == 0) strcpy(Socket[thread].inPtr, "_menu-user.js"); // мобильная морда
							else if(!web_user_file_ok(Socket[thread].inPtr)) goto xUNAUTHORIZED;
						}
						readFileSD(Socket[thread].inPtr, thread);
						break;
					}
					case HTTP_REQEST:  // Запрос AJAX
					{
						// Из прежнего API "GET /&команда&&" жива только узкая ветка удалённых датчиков IP (WiFi), остальное - 404
						strcpy(Socket[thread].outBuf, HEADER_ANSWER);   // Начало ответа
						if(!sensorIpRequest(thread, sock)) { sendConstRTOS(thread, HEADER_FILE_NOT_FOUND); break; }
						if(DBG_ON(DBG_HTTP)) { journal.jprintf("$RETURN: %s\n",Socket[thread].outBuf); }
						if(sendBufferRTOS(thread, (byte*) (Socket[thread].outBuf), strlen(Socket[thread].outBuf)) == 0) {
							if(DBG_ON(DBG_WERR)) {
								uint8_t ip[4];
								W5100.readSnDIPR(sock, ip);
								journal.jprintf("$Error send AJAX(%d.%d.%d.%d): %s\n", ip[0], ip[1], ip[2], ip[3], (char*) Socket[thread].inBuf);
							}
						}
						break;
					}
					case HTTP_API:     // Запрос нового API
					{
						if(api_dispatch(thread) == 0 && DBG_ON(DBG_WERR)) {
							uint8_t ip[4];
							W5100.readSnDIPR(sock, ip);
							journal.jprintf("$Error send API(%d.%d.%d.%d): %s\n", ip[0], ip[1], ip[2], ip[3], Socket[thread].inPtr);
						}
						if(DBG_ON(DBG_HTTP)) { journal.jprintf("$RETURN: %s\n", Socket[thread].outBuf); }
						break;
					}
					case HTTP_POST:    // загрузка настроек
					{
						if(GETBIT(Socket[thread].flags, fUser)) goto xUNAUTHORIZED;   // запись файлов, веба и настроек - только администратору
                        uint8_t ret= parserPOST(thread, len);         // разобрать и получить тип ответа
                        strcpy(Socket[thread].outBuf, HEADER_ANSWER);
						strcat(Socket[thread].outBuf, postRet[ret]);   // вернуть текстовый ответ, который надо вывести
               			if(sendBufferRTOS(thread, (byte*) (Socket[thread].outBuf), strlen(Socket[thread].outBuf)) == 0) {
							if(DBG_ON(DBG_WERR)) {
								uint8_t ip[4];
								W5100.readSnDIPR(sock, ip);
								journal.jprintf("$Error send POST(%d.%d.%d.%d): %s\n", ip[0], ip[1], ip[2], ip[3], (char*) Socket[thread].inBuf);
							}
               			}
						break;
					}
					case HTTP_POST_: // предварительный запрос post
					{
						sendConstRTOS(thread,
								"HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: HEAD, OPTIONS, GET, POST\r\nAccess-Control-Allow-Headers: Overwrite, Content-Type, Cache-Control, Title\r\n\r\n");
						break;
					}
					case UNAUTHORIZED: {
xUNAUTHORIZED:
						if(DBG_ON(DBG_WERR)) {
							uint8_t ip[4];
							W5100.readSnDIPR(sock, ip);
							journal.jprintf("$UNAUTHORIZED (%d.%d.%d.%d)\n", ip[0], ip[1], ip[2], ip[3]);
						}
						sendUnauthorized(thread);
						break;
					}
					case BAD_LOGIN_PASS: {
						if(DBG_ON(DBG_WERR)) {
							uint8_t ip[4];
							W5100.readSnDIPR(sock, ip);
							journal.jprintf("$Wrong login or password (%d.%d.%d.%d)\n", ip[0], ip[1], ip[2], ip[3]);
						}
						sendUnauthorized(thread);
						break;
					}

					default:
						journal.jprintf("$Unknow  %s\n", (char*) Socket[thread].inBuf);
					}

					// Обработчик потерял замок сети (отпустил на время ожидания и не взял обратно): шина SPI сейчас у другой
					// задачи, поэтому ни SPI_switchW5200, ни client.stop, ни отдача замка нам не принадлежат. Раньше здесь
					// всё это выполнялось: assert ядра при отдаче чужого мьютекса либо обмен по SPI без замка. Незакрытый
					// сокет подберёт checkSockStatus.
					if(GETBIT(Socket[thread].flags, fLOCK_LOST)) { Socket[thread].sock = -1; return; } // SEM-OK: замка нет, отдавать нечего
					SPI_switchW5200();
					Socket[thread].inBuf[0] = 0;
					break;   // Подготовить к следующей итерации
				} // end if (client.available())
			} // end while (client.connected())
			Socket[thread].client.stop();   // close the connection
			Socket[thread].sock = -1;
			taskYIELD();
		} // end if (client)
	}  // for (int sock = 0; sock < W5200_SOCK_SYS; sock++)
	SemaphoreGive (xWebThreadSemaphore);              // Семафор отдать
}

const char filename_subst_scheme[] = "HPscheme";

// Заголовок ответа для файла морды. Меню (menu.js) зависит от роли: роли user отдаётся menu-user.js под тем же адресом menu.js. Если разрешить браузеру
// держать его час по адресу, то после смены пользователя остаётся меню прежней роли (лечится только Ctrl+F5), поэтому для него кэш по адресу отключён.
static const char* webFileHeader(const char *filename)
{
	if(strstr(filename, ".css") != NULL) return HEADER_FILE_CSS;
	if(strncmp(filename, "menu", 4) == 0 || strncmp(filename, "_menu", 5) == 0) return HEADER_FILE_NOCACHE;
	return HEADER_FILE_WEB;
}

//  Чтение файла с SD или его генерация
void readFileSD(char *filename, uint8_t thread)
{
	int n;
	SdFile webFile;
	// Реализация функционала подмены для имен файлов по типу: plan[HPscheme].png -> plan2.png
	char *str;
	WEB_STORE_DEBUG_INFO(11);
	if((str = strchr(filename, '[')) != NULL) // скобка найдена надо обрабатывать
	{
		if(strncmp(str + 1, filename_subst_scheme, sizeof(filename_subst_scheme)-1) == 0) // найден аргумент (схема ТН) надо подменять на значение HP_SHEME
		{
			if(*(str + 1 + sizeof(filename_subst_scheme)-1) == ']') {
				const char *tail = str + 1 + sizeof(filename_subst_scheme)-1 + 1;   // остаток имени после ']' лежит в том же буфере, что и место вставки
				size_t tail_len = strlen(tail) + 1;
				itoa(HP_SCHEME, str, 10); // вставить номер схемы
				memmove(str + strlen(str), tail, tail_len);                         // области перекрываются: strcat тут - неопределённое поведение
			} else journal.jprintf("Not found ] in: %s", filename); // нет закрывающейся скобки
		}
	}

	// В начале обрабатываем генерируемые файлы (для выгрузки из контроллера)
	if(strcmp(filename, "state.txt") == 0) { get_txtState(thread, true); return; }
	if(strncmp(filename, "settings", 8) == 0) {
		filename += 8;
		if(strcmp(filename, ".txt") == 0) {	get_txtSettings(thread); return; }
		else if(strcmp(filename, ".bin") == 0) {
			if(!get_binSettings(thread)) journal.jprintf("Error download %s%s\n", "settings", filename);
			return;
		}
		else if(strcmp(filename, "_eeprom.bin") == 0) {   // кнопка "Прочитать всю EEPROM"
			if(!get_binEEPROM(thread)) journal.jprintf("Error download %s%s\n", "settings", filename);
			return;
		}
		filename -= 8;
	}
	if(strcmp(filename, "chart.csv") == 0) { get_csvChart(thread); return; }
	if(strcmp(filename, "journal.txt") == 0) { get_txtJournal(thread); return; }
	if(strncmp(filename, stats_file_start, sizeof(stats_file_start)-1) == 0) { // Файл статистики, stats_yyyy.dat, stats_yyyy.csv
		WEB_STORE_DEBUG_INFO(12);
	    strcpy(Socket[thread].outBuf, WEB_HEADER_OK_CT);
	    strcat(Socket[thread].outBuf, WEB_HEADER_BIN_ATTACH);
	    strcat(Socket[thread].outBuf, filename);
	    strcat(Socket[thread].outBuf, "\"");
	    strcat(Socket[thread].outBuf, WEB_HEADER_END);
		n = strncmp(filename + sizeof(stats_file_start)-1, stats_file_header, sizeof(stats_file_header)-1) == 0;
		if((str = strstr(filename, stats_csv_file_ext)) != NULL) { // формат csv - нужен заголовок
			Stats.StatsFileHeader(Socket[thread].outBuf, n);
			strcpy(str, stats_file_ext);
		}
		if(!n) {
			Stats.SendFileData(thread, &webFile, filename);
		} else {
			sendPacketRTOS(thread, (byte*)Socket[thread].outBuf, strlen(Socket[thread].outBuf), 0);
		}
		return;
	}
	const char *hpfx = strncmp(filename, history2_file_start, sizeof(history2_file_start)-1) == 0 ? history2_file_start :
	                   strncmp(filename, history_file_start, sizeof(history_file_start)-1) == 0 ? history_file_start : NULL;
	if(hpfx) {   // hist2_ - текущий формат, hist_ - прежний (минутные записи без секунд). Файл Истории полностью: только для бакапа - hist_yyyy.dat, hist_yyyy.csv, обрезанный по периоду - hist__yyyymmdd-yyyymmdd
		WEB_STORE_DEBUG_INFO(13);
		const uint8_t hl = strlen(hpfx);
	    strcpy(Socket[thread].outBuf, WEB_HEADER_OK_CT);
	    strcat(Socket[thread].outBuf, WEB_HEADER_BIN_ATTACH);
	    strcat(Socket[thread].outBuf, filename);
	    strcat(Socket[thread].outBuf, "\"");
	    strcat(Socket[thread].outBuf, WEB_HEADER_END);
	    str = strchr(filename, '-');
	    if(str) { // Period format: "hist__yyyymmdd-yyyymmdd"
	    	filename[hl] = '\0';
	    	*str = '\0';
	    	uint32_t step = 0;			// необязательное прореживание: "hist2__<от>-<до>_s<секунд>"
	    	char *sp = strstr(str + 1, "_s");
	    	if(sp) { step = atoi(sp + 2); *sp = '\0'; }
	    	Stats.SendFileDataByPeriod(thread, &webFile, filename, filename + hl + 1, str + 1, step);
	    } else {
			n = strncmp(filename + hl, stats_file_header, sizeof(stats_file_header)-1) == 0;
			bool csv = false;
			if((str = strstr(filename, stats_csv_file_ext)) != NULL) { // формат csv - нужен заголовок
				csv = true;
				if(hpfx == history_file_start) strcat(Socket[thread].outBuf, n ? history_old_header_axes : history_old_header);	// прежний формат: свои столбцы
				else Stats.HistoryFileHeader(Socket[thread].outBuf, n);
				strcpy(str, stats_file_ext);
			}
			if(!n) {
				Stats.SendFileData(thread, &webFile, filename, csv && hpfx == history2_file_start);	// csv: шапка уже отправлена, строки-описания раскладки не нужны
			} else {
				sendPacketRTOS(thread, (byte*)Socket[thread].outBuf, strlen(Socket[thread].outBuf), 0);
			}
	    }
		return;
	}
	if(strncmp(filename, "TEST.", 5) == 0) {
		WEB_STORE_DEBUG_INFO(14);
		filename += 5;
		if(strcmp(filename, "DAT") == 0) { // TEST.DAT
			get_datTest(thread);
		} else if(strcmp(filename, "NOSD") == 0) { // TEST.NOSD
			get_indexNoSD(thread); // минимальная морда
		} else if(strncmp(filename, "SD:", 3) == 0) {  // TEST.SD:<filename> - Тестирует скорость чтения файла с SD карты
			filename += 3;
			journal.jprintf("SD card test: %s - ", filename);
			sendConstRTOS(thread, HEADER_FILE_WEB);
			SPI_switchSD();
			if(webFile.open(filename, O_READ)) {
				uint32_t startTick = GetTickCount();
				uint32_t size = 0;
				for(;;) {
					int n = webFile.read(Socket[thread].outBuf, sizeof(Socket[thread].outBuf));
					if(n < 0) journal.jprintf("Read SD error (%d,%d)!\n", card.cardErrorCode(), (int)(card.cardErrorData()));
					if(n <= 0) break;
					size += n;
					if(GetTickCount() - startTick > (3*W5200_TIME_WAIT/portTICK_PERIOD_MS) - 1000) break; // на секунду меньше, чем блок семафора
					Watchdog_AliveHere();
				}
				startTick = GetTickCount() - startTick;
				webFile.close();
				// Скорость считается в 64 битах, но ПЕЧАТАЛАСЬ как 32: mini-printf берёт из
				// va_list unsigned int, а 64-битный аргумент по ABI лежит с выравниванием
				// на 8 байт - выводилось не то число. В байты в секунду 32 бит хватает,
				// поэтому сужаем явно.
				journal.jprintf("read %u bytes, %u b/sec\n", (unsigned int)(size),
				                (unsigned int)((uint64_t)size * 1000 / startTick));
				/*/ check write!
				if(!webFile.open(filename, O_RDWR)) journal.jprintf("Error open for writing!\n");
				else {
					n = webFile.write("Test write!");
					journal.jprintf("Wrote %d byte\n", n);
					if(!webFile.sync()) journal.jprintf("Sync failed (%d,%d)\n", card.cardErrorCode(), card.cardErrorData());
					webFile.close();
				}
				//*/
			} else {
				journal.jprintf("not found (%d,%d)!\n", card.cardErrorCode(), (int)(card.cardErrorData()));
			}
		}
		return;
	}

// ---- загрузка файла ----
	// Разбираемся откуда грузить надо (три варианта)
	switch(HP.get_SourceWeb()) {
	case pMIN_WEB: // минимальная морда
		get_indexNoSD(thread);
		break;
	case pSD_WEB:
		{ // Чтение с карты  файлов
			WEB_STORE_DEBUG_INFO(15);
			SPI_switchSD();
			if(!webFile.open(filename, O_READ))
			{
				if(card.cardErrorCode() > SD_CARD_ERROR_NONE && card.cardErrorCode() < SD_CARD_ERROR_READ
						&& card.cardErrorData() == 255) { // reinit card
					if(card.begin(PIN_SPI_CS_SD, SD_SCK_MHZ(SD_CLOCK))) {
						if(webFile.open(filename, O_READ)) goto xFileFound;
					} else {
						journal.jprintf("Reinit SD card failed!\n");
					}
				}
				sendConstRTOS(thread, HEADER_FILE_NOT_FOUND);
				uint8_t ip[4];
				W5100.readSnDIPR(Socket[thread].sock, ip);
				journal.jprintf((char*) "WEB: %d.%d.%d.%d - File not found: %s\n", ip[0], ip[1], ip[2], ip[3], filename);
				return;
			} // файл не найден
xFileFound:
			// Файл открыт читаем данные и кидаем в сеть
			if(DBG_ON(DBG_HTTP)) { journal.jprintf("$Thread: %d socket: %d read file: %s\n",thread,Socket[thread].sock,filename); }
			sendConstRTOS(thread, webFileHeader(filename));
			SPI_switchSD();
			while((n = webFile.read(Socket[thread].outBuf, sizeof(Socket[thread].outBuf))) > 0) {
				if(sendBufferRTOS(thread, (byte*) (Socket[thread].outBuf), n) == 0) break;
				SPI_switchSD();
			} // while
			if(n < 0) journal.jprintf("Read SD error (%d,%d)!\n", card.cardErrorCode(), (int)(card.cardErrorData()));
			SPI_switchSD();
			webFile.close();
		}
		break;
	case pFLASH_WEB:
		{
			WEB_STORE_DEBUG_INFO(16);
			SPI_switchFlash();                                  // все CS вверх: флеш выбирает своя библиотека (SPI_switchFlash раньше нигде не вызывалась)
			SerialFlashFile ff = SerialFlash.open(filename);
			if(ff) {
				if(DBG_ON(DBG_HTTP)) { journal.jprintf("$Thread: %d socket: %d read file: %s\n",thread,Socket[thread].sock,filename); }
				sendConstRTOS(thread, webFileHeader(filename));
				SPI_switchFlash();
				while((n = ff.read(Socket[thread].outBuf, sizeof(Socket[thread].outBuf))) > 0) {
					if(sendBufferRTOS(thread, (byte*) (Socket[thread].outBuf), n) == 0) break;
					SPI_switchFlash();                          // после отдачи страницы в сеть (замок отпускался) - снова все CS вверх
				} // while
			} else {
				sendConstRTOS(thread, HEADER_FILE_NOT_FOUND);
				uint8_t ip[4];
				W5100.readSnDIPR(Socket[thread].sock, ip);
				journal.jprintf((char*) "WEB GET(%d.%d.%d.%d) - Not found: %s\n", ip[0], ip[1], ip[2], ip[3], filename);
				return;
			}
		}
		break;
	default:
		get_indexNoSD(thread);
		break;
	}
}

// Выделение имени файла (или содержания запроса) и типа файла и типа запроса клиента
// thread - номер потока, возвращает тип запроса
uint16_t GetRequestedHttpResource(uint8_t thread)
{
	WEB_STORE_DEBUG_INFO(50);
	if((HP.get_fPass()) && (!HP.safeNetwork))  // идентификация если установлен флаг и перемычка не в нуле
	{
		// Хвост "&&!Z..." в адресе - явные данные этого запроса (вход, выполненный в самой морде), он сильнее заголовка Authorization, который браузер
		// подставляет из кэша прежнего входа: иначе после входа другим пользователем в морде сервер продолжал бы видеть прежнего
		char *str = strstr((char*)Socket[thread].inBuf, header_Authorization_2);
		if(str) str += sizeof(header_Authorization_2) - 1;
		else if((str = strstr((char*)Socket[thread].inBuf, header_Authorization_1))) str += sizeof(header_Authorization_1) - 1;
		if(str) {
			if(web_auth_match(str, HP.Security.hashAdmin, HP.Security.hashAdminLen)) goto x_ok;
			else if(web_auth_match(str, HP.Security.hashUser, HP.Security.hashUserLen) || !*HP.get_passUser()) SETBIT1(Socket[thread].flags, fUser); else return BAD_LOGIN_PASS;
		} else if(!*HP.get_passUser()) SETBIT1(Socket[thread].flags, fUser); else return UNAUTHORIZED;
	}
x_ok:

	// Идентификация пройдена
	char *str_token, *tmpptr;
	str_token = strtok_r((char*) Socket[thread].inBuf, " ", &tmpptr);    // Обрезаем по пробелам
	if(!str_token) return HTTP_invalid;     // пакет из одних пробелов или нулей: токенов нет, в strcmp NULL нельзя
	if(strcmp(str_token, "GET") == 0)   // Ищем GET
	{
		str_token = strtok_r(NULL, " ", &tmpptr);                       // get the file name
		if(!str_token) return HTTP_invalid;   // "GET" без имени
		if(strcmp(str_token, "/") == 0)                   // Имени файла нет, берем файл по умолчанию
		{
			// Имя файла по умолчанию копируем в буфер запроса: дальше его декодируют НА МЕСТЕ (urldecode(inPtr, inPtr)),
			// а запись в строковый литерал - это запись во flash. Токены разбора в этом буфере уже не нужны.
			strcpy((char*) Socket[thread].inBuf, INDEX_FILE);
			Socket[thread].inPtr = (char*) Socket[thread].inBuf;   // Указатель на имя файла по умолчанию
			return HTTP_GET;
		} else if(strcmp(str_token, (char*) MOB_PATH) == 0) // Имени файла нет, но указан путь до мобильной морды
		{
			Socket[thread].inPtr = (char*) (str_token + 1);   // Указатель на путь до мобильной морды
			strcat(Socket[thread].inPtr, (char*) INDEX_MOB_FILE);
			return HTTP_GET;
		} else if(strncmp(str_token, "/api/", 5) == 0)      // Новый API; слишком длинный адрес api_dispatch отвергает кодом 414
		{
			Socket[thread].inPtr = (char*) (str_token + 1);
			return HTTP_API;
		} else if(strlen(str_token) <= W5200_MAX_LEN - 100)   // Проверка на длину запроса или имени файла
		{
			Socket[thread].inPtr = (char*) (str_token + 1);       // Указатель на имя файла
			if(Socket[thread].inPtr[0] == '&') return HTTP_REQEST;       // Проверка на аякс запрос
			return HTTP_GET;
		} // if ((len=strlen(str_token)) <= W5200_MAX_LEN-100)
		else return HTTP_invalid;  // слишком длинная строка HTTP_invalid
	}   //if (strcmp(str_token, "GET") == 0)
	else if(strcmp(str_token, "POST") == 0) {Socket[thread].inPtr = (char*) (str_token +strlen("POST") + 1);  return HTTP_POST;}    // Запрос POST Socket[thread].inPtr - указывает на начало запроса (начало полезных данных)
	else if(strcmp(str_token, "OPTIONS") == 0) return HTTP_POST_;
	return HTTP_invalid;
}

// ---- P A R S E R  P O S T ----
#define emptyStr			WEB_HEADER_END  	   // пустая строка после которой начинаются данные
#define MAX_FILE_LEN		64  	              // максимальная длина имени файла
const char Title[]          = "Title: ";          // где лежит имя файла
const char Length[]         = "Content-Length: "; // где лежит длина файла
const char SETTINGS[]       = "*SETTINGS*";       // Идентификатор передачи настроек (лежит в Title:)
const char LOAD_FLASH_START[]= "*SPI_FLASH*";     // Идентификатор начала загрузки веб морды в SPI Flash (лежит в Title:)
const char LOAD_FLASH_END[]  = "*SPI_FLASH_END*"; // Идентификатор кольца загрузки веб морды в SPI Flash (лежит в Title:)
const char LOAD_SD_START[]   = "*SD_CARD*";       // Идентификатор начала загрузки веб морды в SD (лежит в Title:)
const char LOAD_SD_END[]     = "*SD_CARD_END*";   // Идентификатор кольца загрузки веб морды в SD (лежит в Title:)

// Защита заливки веб-морды. Раньше цикл дочитывания файла ждал данные, пока соединение считалось открытым: оборвавшийся клиент
// держал веб-поток (и общий замок xWebThreadSemaphore) бессрочно, а признак «заливка идёт» (xLoadingWebSemaphore) после сбоя снимался
// только следующим запросом начала или перезагрузкой.
#define WEB_UPLOAD_IDLE_MS   10000    // нет ни байта от клиента дольше - файл считается оборванным
#define WEB_UPLOAD_STALE_MS  30000    // последняя активность заливки старше - признак «заливка идёт» устарел и новое начало его перехватывает
static uint32_t webUploadTick = 0;    // millis() последней активности заливки (начало, начало файла, принятый пакет)

// SEM-HOLD: xLoadingWebSemaphore
// Признак нарочно остаётся взятым при выходе true: он живёт между запросами «начало заливки» и «конец заливки» (см. parserPOST).
// Взять признак «заливка идёт». false - занят свежей заливкой (признак при этом освобождается: повтор начала пройдёт, как и раньше);
// устаревший признак перехватывается сразу.
static bool webUploadBegin(void)
{
	if(SemaphoreTake(xLoadingWebSemaphore, 10) == pdFALSE) {
		bool stale = (uint32_t)(millis() - webUploadTick) >= WEB_UPLOAD_STALE_MS;
		journal.jprintf(stale ? "%s: stale upload dropped\n" : "%s: Upload already started\n", "webUploadBegin");
		SemaphoreGive(xLoadingWebSemaphore);
		if(!stale || SemaphoreTake(xLoadingWebSemaphore, 10) == pdFALSE) return false;
	}
	webUploadTick = millis();
	return true;
}

uint16_t numFilesWeb = 0;                   // Число загруженных файлов

// Область профилей в файле настроек: длина вместе с END и контрольной суммой или -1, если область негодна. Проверка
// ничего не записывает, поэтому идёт до применения настроек и до записи в EEPROM: испорченная область (чужая раскладка
// слота, обрубленный файл, неверная сумма) на носителе сломала бы профили при ближайшей загрузке.
static int32_t fileProfilesLen(const uint8_t *ptr, const uint8_t *file_end)
{
	int32_t len;
	if(rec_hdr_tag(ptr) == NTAG_Profile) {
		ProfileScan ps;
		profile_scan(ptr, file_end, NTAG_Profile, NTAG_END, 1 + sizeof(ProfileSlot), I2C_PROFIL_NUM, &ps);
		len = ps.bytes + sizeof(uint16_t);               // плюс END (уже в bytes) и контрольная сумма
		if(ps.bad_len || ps.bad_slot || ps.bad_tag || ps.truncated || !ps.end_seen || ptr + len > file_end) {
			journal.jprintf("Profiles in file rejected: len %d, slot %d, tag %d, cut %d, end %d (code %d)\n",
			                ps.bad_len, ps.bad_slot, ps.bad_tag, ps.truncated, ps.end_seen, ps.bad_len ? ERR_BAD_LEN_PROFILE : ERR_LOAD_PROFILE);
			return -1;
		}
		// Контрольная сумма области: по всем байтам записей и заголовку END, как при записи в save_region
		uint16_t crc_stored;
		memcpy(&crc_stored, ptr + ps.bytes, sizeof(crc_stored));
		uint16_t crc_file = calc_crc16((unsigned char*) ptr, ps.bytes, 0xFFFF);
		if(crc_file != crc_stored) {
			journal.jprintf("Profiles in file rejected: crc %04x != %04x\n", crc_file, crc_stored);
			return -1;
		}
	} else {
		len = HP.Prof.get_sizeProfile() * I2C_PROFIL_NUM_LEGACY;  // старый файл - массив из десяти слотов
		if(ptr + len > file_end) { journal.jprintf("Profiles in file rejected: file too short\n"); return -1; }
	}
	return len;
}

// Разбор и обработка POST запроса inPtr входная строка использует outBuf для хранения файла настроек!
// Сейчас реализована загрузка настроек и загрузка веб морды в спи диск
// Возврат тип ответа (потом берется из массива строк)
//
// SEM-HOLD: xWebThreadSemaphore, xLoadingWebSemaphore
// Оба замка удерживаются на выходе по замыслу, а не по недосмотру.
// xWebThreadSemaphore одолжен у web_server(): он держит его на всё
// обслуживание запроса и сам же отдаёт, поэтому parserPOST не имеет права
// его отдавать - отдача из неё освободила бы шину SPI из-под владельца.
// xLoadingWebSemaphore здесь вообще не замок по смыслу, а флаг состояния
// загрузки веб-морды: он взводится запросом LOAD_*_START и снимается
// запросом LOAD_*_END, то есть живёт между разными HTTP-запросами.
TYPE_RET_POST parserPOST(uint8_t thread, uint16_t size)
{
	byte *ptr, *pStart;
	char *nameFile;      // указатель имя файла
	int32_t buf_len, lenFile;

	WEB_STORE_DEBUG_INFO(51);

	// Поиски во входном буфере: данных, имени файла и длины файла
	ptr = (byte*) strstr(Socket[thread].inPtr, emptyStr);     // поиск начала данных
	if(!ptr) return pLOAD_ERR;
	ptr += sizeof(emptyStr) - 1;
	nameFile = strstr(Socket[thread].inPtr, Title);
	pStart = (byte*) strstr(Socket[thread].inPtr, Length);
	if(nameFile) {
		char *p = strchr(nameFile += sizeof(Title) - 1, '\r');
		if(p) *p = '\0'; else nameFile = NULL;
	}
	if(!nameFile) { // Имя файла не найдено, запрос не верен, выходим
		journal.jprintf("Upload: Name not found!\n");
		return pLOAD_ERR;
	}
	urldecode(nameFile, nameFile, MAX_FILE_LEN + 10);
	if(strlen(nameFile) > MAX_FILE_LEN) {
		journal.jprintf("Upload: %s name length > %d bytes!\n", nameFile, MAX_FILE_LEN - 1);
		return pLOAD_ERR;
	}
	if(pStart) {
		char *p = strchr((char*)(pStart += sizeof(Length) - 1), '\r');
		if(p) *p = '\0'; else pStart = NULL;
	}
	if(!pStart) { // Размер файла не найден, запрос не верен, выходим
xLenErr:
		journal.jprintf("Upload: %s - length not found!\n", nameFile);
		return pLOAD_ERR;
	}
	lenFile = atoi((char*) pStart);	// получить длину
	// все нашлось, можно обрабатывать
	buf_len = size - (ptr - (byte *) Socket[thread].inBuf);                  // длина (остаток) данных (файла) в буфере
	// В зависимости от имени файла (Title)
	if(strcmp(nameFile, SETTINGS) == 0) {  // Чтение настроек
		if(lenFile <= 0) goto xLenErr;
		// Файл заменяет настройки, профили и расписание целиком: на пускающемся, работающем или останавливающемся ТН
		// это меняло бы предел частоты инвертора, параметры ЭРВ и профиль посреди процесса. Кнопка в морде тоже заблокирована.
		if(HP.IsWorkingNow()) {
			journal.jprintf("Upload: settings refused, HP is working\n");
			return pSETTINGS_BUSY;
		}
		WEB_STORE_DEBUG_INFO(52);
		int32_t len;
		// Тело файла может прийти НЕ в первом пакете: при xhr.send(file) браузер отправляет
		// заголовки и данные отдельно, и в первом пакете остаются одни заголовки. Поэтому
		// сначала собираем в outBuf всё, что лежит после заголовков (возможно, 0 байт), и
		// дочитываем остальное, а маркер HEADER_BIN ищем уже в собранных данных.
		// buf_len знаковый, а memcpy принимает size_t: отрицательная длина
		// превратилась бы в ~4 ГБ и затёрла бы всю память.
		if(buf_len < 0 || buf_len > (int32_t) sizeof(Socket[thread].outBuf)) {
			journal.jprintf("Upload: bad data length %d!\n", (int) buf_len);
			return pSETTINGS_ERR;
		}
		memcpy(Socket[thread].outBuf, ptr, buf_len);                            // данные после заголовков (в первом пакете)
		while(1)  // Чтение остальных бинарных данных по сети
		{
			for(uint8_t i = 0; i < 255; i++) {
				if(!Socket[thread].client.available()) _delay(1); else break; // ждем получение пакета
			}
			if(!Socket[thread].client.available()) break;                                          // пакета нет - выходим
			len = Socket[thread].client.get_ReceivedSizeRX();                                      // получить длину входного пакета
			if(len > W5200_MAX_LEN) len = W5200_MAX_LEN;                                   		// Ограничить размером в максимальный размер пакета w5200
			if(buf_len + len > (int32_t)sizeof(Socket[thread].outBuf)) return pSETTINGS_MEM;     // проверить длину если не влезает то выходим
			Socket[thread].client.read((uint8_t*)Socket[thread].outBuf + buf_len, len);            // прочитать буфер
			buf_len = buf_len + len;                                                               // определить размер данных
		}
		{   // Поиск маркера HEADER_BIN в собранных данных (бинарно-безопасный, строка не завершена нулём)
			const int32_t ml = sizeof(HEADER_BIN) - 1;
			int32_t k = -1;
			for(int32_t n = 0; n + ml <= buf_len; n++)
				if(memcmp(Socket[thread].outBuf + n, HEADER_BIN, ml) == 0) { k = n + ml; break; }
			if(k < 0) {                                                          // заголовок не найден
				journal.jprintf("Upload: Wrong save format: %s! (received %d bytes)\n", nameFile, (int) buf_len);
				return pSETTINGS_ERR;
			}
			buf_len -= k;                                                        // дальше идут бинарные данные, без текстового заголовка
			memmove(Socket[thread].outBuf, Socket[thread].outBuf + k, buf_len);
		}
	    ptr = (byte*) Socket[thread].outBuf;     
		journal.jprintf("Loading %s, length data %d bytes:\n", SETTINGS, (int)(buf_len));
		// Чтение настроек из ptr. Читатель выбирается по версии - там же,
		// на смещении 3 от начала блока, что и при чтении из EEPROM.
		// Без этого файл, скачанный после конверсии, разобрался бы старым
		// читателем и разложился не туда с правильной контрольной суммой.
		const uint8_t *file_end = (byte*) Socket[thread].outBuf + buf_len;
		// Весь файл проверяется ДО применения чего-либо: иначе отказ по профилям оставлял бы уже применённые настройки.
		// В новом формате за блоком настроек (поле длины + длина) сразу идёт область профилей.
		if(ptr[NEW_OFS_VER] >= SETTINGS_VER_NEW) {
			uint32_t blk = *((uint16_t*) ptr) + sizeof(uint16_t);
			if(buf_len < (int32_t) (blk + REC_HDR_SIZE) || fileProfilesLen(ptr + blk, file_end) < 0) {
				journal.jprintf("Upload: file rejected before applying\n");
				return pSETTINGS_ERR;
			}
		}
		if(ptr[NEW_OFS_VER] < SETTINGS_VER_NEW && !settings_len_ok(*((uint16_t*) ptr), buf_len)) {   // старый формат: длина из файла против принятого
			journal.jprintf("Upload: bad settings length %d of %d bytes received\n", (int) *((uint16_t*) ptr), (int) buf_len);
			return pSETTINGS_ERR;
		}
		len = ptr[NEW_OFS_VER] >= SETTINGS_VER_NEW ? HP.load_new(ptr, 1) : HP.load(ptr, 1);
		if(len <= 0) return pSETTINGS_ERR; // ошибка загрузки настроек
		boolean ret = true;
		ptr += len;
		// Чтение профилей
		// Длина области профилей в файле: в новом формате это поток записей,
		// и он короче десяти слотов ровно на незаписанные.
		len = fileProfilesLen(ptr, file_end);
		if(len < 0) return pSETTINGS_ERR;
		if(writeEEPROM_I2C(I2C_PROFILE_EEPROM, ptr, len)) {
			journal.jprintf("Error write EEPROM at 0x%X\n", I2C_PROFILE_EEPROM);
			ret = false;
		} else {
			HP.Prof.cache_load_all();                                 // профили записаны мимо кеша - перечитать
			HP.Prof.load(HP.Option.numProf);
			HP.Prof.update_list(HP.Prof.get_idProfile());             // обновить список
		}
		// чтение расписания
		// Расписание в файле тоже может быть в новом формате - тогда перед
		// данными стоит заголовок записи.
		{
			uint8_t *sch = ptr + len;
			uint16_t hint = 0;
			if(sch + REC_HDR_SIZE <= file_end && rec_hdr_tag(sch) == NTAG_Scheduler) { hint = rec_hdr_len(sch); sch += REC_HDR_SIZE; }
			uint16_t avail = sch < file_end ? file_end - sch : 0;
			if(ret && HP.Schdlr.loadFromBuf(sch, avail, hint) != OK) ret = false;
		}

		if(ret) return pSETTINGS_OK;
		else return pSETTINGS_ERR;
	} //if (strcmp(nameFile,"*SETTINGS*")==0)

	// загрузка вебморды
	 else if(HP.get_fSPIFlash() || HP.get_fSD())  { // если есть куда писать
		WEB_STORE_DEBUG_INFO(53);
		if(strcmp(nameFile, LOAD_FLASH_START) == 0) {  // начало загрузки вебморды в SPI Flash
			if(!HP.get_fSPIFlash()) {
				journal.jprintf("Upload: No SPI Flash installed!\n");
				SemaphoreGive (xLoadingWebSemaphore);
				return pLOAD_ERR;
			}
			fWebUploadingFilesTo = 1;
			if(!webUploadBegin()) return pLOAD_ERR;
			numFilesWeb = 0;
			journal.jprintf_time("Start upload, erase SPI disk ");
			SPI_switchFlash();                                  // все CS вверх: флеш выбирает своя библиотека
			SerialFlash.eraseAll();
			while(SerialFlash.ready() == false) {
				xSemaphoreGive(xWebThreadSemaphore); // отдать семафор вебморды, что бы обработались другие потоки веб морды
				vTaskDelay(1000 / portTICK_PERIOD_MS);
				if(SemaphoreTake(xWebThreadSemaphore, (3 * W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) { // получить семафор веб морды
					SETBIT1(Socket[thread].flags, fLOCK_LOST);
					journal.jprintf("%s: Socket %d %s\n", (char*) __FUNCTION__, Socket[thread].sock, MutexWebThreadBusy);
					SemaphoreGive(xLoadingWebSemaphore);                                       // заливка прервана: признак «идёт» не оставляем
					return pLOAD_ERR;
				} // если не удается захватить мютекс, то ошибка и выход
				journal.jprintf(".");
			}
			journal.jprintf(" Ok, free %d bytes\n", (int)(SerialFlash.free_size()));
			return pNULL;
		} else if(strcmp(nameFile, LOAD_SD_START) == 0) {  // начало загрузки вебморды в SD
			if(!HP.get_fSD()) {
				journal.jprintf("Upload: No SD card available!\n");
				SemaphoreGive (xLoadingWebSemaphore);
				return pLOAD_ERR;
			}
			fWebUploadingFilesTo = 2;
			if(!webUploadBegin()) return pLOAD_ERR;
			numFilesWeb = 0;
			journal.jprintf_time("Start upload to SD.\n");
			return pNULL;
		} else if(strcmp(nameFile, LOAD_FLASH_END) == 0 || strcmp(nameFile, LOAD_SD_END) == 0) {  // Окончание загрузки вебморды
			if(SemaphoreTake(xLoadingWebSemaphore, 0) == pdFALSE) { // Семафор не захвачен (был захвачен ранее) все ок
				journal.jprintf_time("Ok, %d files uploaded, free %.1f KB\n", numFilesWeb, fWebUploadingFilesTo == 1 ? (float)SerialFlash.free_size() / 1024 : (float)card.vol()->freeClusterCount() * card.vol()->blocksPerCluster() * 512 / 1024);
				fWebUploadingFilesTo = 0;
				SemaphoreGive (xLoadingWebSemaphore);
				return pLOAD_OK;
			} else { 	// семафор БЫЛ захвачен, ошибка, отдать обратно
				journal.jprintf("%s: Unable to finish upload!\n", (char*) __FUNCTION__);
				fWebUploadingFilesTo = 0;
				SemaphoreGive (xLoadingWebSemaphore);
				return pLOAD_ERR;
			}
		} else { // загрузка отдельных файлов веб морды
			if(SemaphoreTake(xLoadingWebSemaphore, 0) == pdFALSE) { // Семафор занят - загрузка файла
				webUploadTick = millis();
				if(lenFile == 0) {
					journal.jprintf("Upload: %s length = %s!\n", nameFile, pStart);
					return pLOAD_ERR;
				}
				// Файл может лежать во множестве пакетов. Если в SPI Flash, то считается что spi диск отформатирован и ожидает запись файлов с "нуля"
				// Входные параметры:
				// nameFile - имя файла
				// lenFile - общая длина файла
				// thread - поток веб сервера,который обрабатывает post запрос
				// ptr - указатель на начало данных (файла) в буфере Socket[thread].inPtr.
				// buf_len - размер данных в буфере ptr (по сети осталось принять lenFile-buf_len)
				if(fWebUploadingFilesTo == 1) {
					uint16_t numPoint = 0;
					int32_t loadLen; // Обработанная (загруженная) длина
					WEB_STORE_DEBUG_INFO(54);
					journal.jprintf("%s (%d) ", nameFile, (int)(lenFile));
					SPI_switchFlash();
					loadLen = SerialFlash.free_size();
					if(lenFile > loadLen) {
						journal.jprintf("Not enough space, free: %d\n", (int)(loadLen));
						loadLen = 0;
					} else {
						loadLen = SerialFlash.create(nameFile, lenFile);
						if(loadLen == 0) {
							SerialFlashFile ff = SerialFlash.open(nameFile);
							if(ff) {
								if(buf_len > 0) loadLen = ff.write(ptr, buf_len); // первый пакет упаковали если он не нулевой
								uint32_t tIdle = millis();                                 // последний принятый пакет
								while(loadLen < lenFile)  // Чтение остальных пакетов из сети
								{
									_delay(2);                                                 // время на приход данных
									buf_len = Socket[thread].client.get_ReceivedSizeRX(); // получить длину входного пакета
									if(buf_len == 0) {
										if(!Socket[thread].client.connected()) break;
										if((uint32_t)(millis() - tIdle) > WEB_UPLOAD_IDLE_MS) { journal.jprintf("idle timeout "); break; }   // клиент замолчал
										continue;
									}
									tIdle = webUploadTick = millis();
									Watchdog_AliveHere();   // пакет пришёл: заливка по медленному каналу идёт, а не висит
									Socket[thread].client.read(Socket[thread].inBuf, buf_len);        // прочитать буфер
									SPI_switchFlash();
									loadLen = loadLen + ff.write(Socket[thread].inBuf, buf_len);             // записать
									numPoint++;
									if(numPoint >= 20) {                   // точка на 30 кб приема (20 пакетов по 1540)
										numPoint = 0;
										journal.jprintf(".");
									}
								}
								ff.close();
								if(loadLen == lenFile) journal.jprintf("Ok\n");
								else { // Длины не совпали
									journal.jprintf("%db, Error length!\n", (int)(loadLen));
									loadLen = 0;
								}
							} else journal.jprintf("Error Open!\n");
						} else journal.jprintf("Error Create %d!\n", (int)(loadLen));
					}
					if(loadLen == lenFile) {
						numFilesWeb++;
						return pNULL;
					} else {
						SemaphoreGive (xLoadingWebSemaphore);
						return pLOAD_ERR;
					}
				} else if(fWebUploadingFilesTo == 2) { // Запись на SD,
					WEB_STORE_DEBUG_INFO(54);
					journal.jprintf("%s (%d) ", nameFile, (int)(lenFile));
					for(uint16_t _timeout = 0; _timeout < 2000 && card.card()->isBusy(); _timeout++) _delay(1);
					if(wFile.opens(nameFile, O_CREAT | O_TRUNC | O_RDWR, &wfname)) {
						wFile.timestamp(T_CREATE | T_ACCESS | T_WRITE, rtcSAM3X8.get_years(), rtcSAM3X8.get_months(), rtcSAM3X8.get_days(), rtcSAM3X8.get_hours(), rtcSAM3X8.get_minutes(), rtcSAM3X8.get_seconds());
						if((int32_t)wFile.write(ptr, buf_len) != buf_len) {
							journal.jprintf("Error write file %s (%d,%d)!\n", nameFile, card.cardErrorCode(), (int)(card.cardErrorData()));
						} else {
							uint16_t numPoint = 0;
							uint32_t tIdle = millis();                                                     // последний принятый пакет
							while((lenFile -= buf_len) > 0)  // Чтение остальных пакетов из сети
							{
								_delay(2);                                                                 // время на приход данных
								buf_len = Socket[thread].client.get_ReceivedSizeRX();                  // получить длину входного пакета
								if(buf_len == 0) {
									if(!Socket[thread].client.connected()) break;
									if((uint32_t)(millis() - tIdle) > WEB_UPLOAD_IDLE_MS) { journal.jprintf("idle timeout "); break; }   // клиент замолчал
									continue;
								}
								tIdle = webUploadTick = millis();
								Watchdog_AliveHere();   // пакет пришёл: заливка по медленному каналу идёт, а не висит
								Socket[thread].client.read(Socket[thread].inBuf, buf_len);                      // прочитать буфер
								WEB_STORE_DEBUG_INFO(56);
								for(uint16_t _timeout = 0; _timeout < 2000 && card.card()->isBusy(); _timeout++) _delay(1);
								if((int32_t)wFile.write(Socket[thread].inBuf, buf_len) != buf_len) {
									journal.jprintf("Error write file %s (%d,%d)!\n", nameFile, card.cardErrorCode(), (int)(card.cardErrorData()));
									break;
								}
								WEB_STORE_DEBUG_INFO(57);
								if(++numPoint >= 20) {// точка на 30 кб приема (20 пакетов по 1540)
									numPoint = 0;
									journal.jprintf(".");
								}
							}
						}
						WEB_STORE_DEBUG_INFO(58);
						if(!wFile.close()) {
							journal.jprintf("Error close file (%d,%d)!\n", card.cardErrorCode(), (int)(card.cardErrorData()));
						}
						if(lenFile == 0) journal.jprintf("Ok\n"); else journal.jprintf("Error - rest %d!\n", (int)(lenFile));
					} else journal.jprintf("Error create (%d,%d)!\n", card.cardErrorCode(), (int)(card.cardErrorData()));
					if(lenFile == 0) {
						numFilesWeb++;
						return pNULL;
					} else {
						SemaphoreGive (xLoadingWebSemaphore);
						return pLOAD_ERR;
					}
				}
			} else { // семафор БЫЛ захвачен, ошибка, отдать обратно
				uint8_t ip[4];
				W5100.readSnDIPR(Socket[thread].sock, ip);
				journal.jprintf("Unable to upload file %s (%d.%d.%d.%d)!\n", nameFile, ip[0], ip[1], ip[2], ip[3]);
				SemaphoreGive (xLoadingWebSemaphore);
				return pLOAD_ERR;
			}
		}
	} else {
		journal.jprintf("%s: Upload: No web store!\n", (char*) __FUNCTION__);
		SemaphoreGive (xLoadingWebSemaphore);
		return pNO_DISK;
	}
	return pPOST_ERR; // До сюда добегать не должны
}

