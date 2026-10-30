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
// ---- функции общего использования ----
/*
 * Функции общего назначения. Делятся на две части:
 *
 *   Util.cpp - чистые: числа, строки, разбор, base64, CRC16, расписания.
 *              Отдельная единица трансляции, о проекте ничего не знает,
 *              проверяется тестами (tests/test_util.cpp).
 *              Здесь же шестнадцатеричные
 *              преобразования и общий буфер _buf: железа они не касаются.
 *   слой железа - SD, SPI-флеш, регистры SAM, EEPROM и часы по I2C.
 *   Network.cpp - функции с IPAddress: этот тип из Arduino, а сюда Arduino
 *              не входит, и ради него тащить его нельзя.
 *
 * Заголовок обязан быть самодостаточным: его включает и Util.cpp, где
 * никакого Arduino.h нет. Включается из Constant.h, то есть виден всему
 * проекту, поэтому определять здесь объекты нельзя - только объявлять.
 */
#ifndef Util_h
#define Util_h

#include <stdint.h>
#include <stdlib.h>          // abs() для макросов ниже

#define sign(a) (a > 0 ? 1 : a < 0 ? -1 : 0)
#define signm(a,t) ((a > 0 ? 1 : a < 0 ? -1 : 0) * (abs(a) > t ? 2 : 1))

// ---- Util.cpp: числа и строки ----
char* _itoa(int value, char *string);                 // дописывает в конец строки
uint8_t _ftoa(char *outstr, float val, unsigned char precision);
void _dtoa(char *outstr, int val, int precision);     // дробное, приведённое к целому
void int_to_dec_str(int32_t value, int32_t div, char **ret, uint8_t maxfract);
#define ATOF_ERROR       -9876543.00  // Возврат my_atof при ошибке преобразования
float my_atof(const char* s);
int16_t rd(float num, int16_t mul);                   // округление float*mul, mul: 10/100/1000
int32_t round_div_int32(int32_t value, int16_t div);
float fromInt16ToFloat(uint16_t wordHigh, uint16_t wordLow);  // порядок слов Modbus
uint8_t calc_bits_in_mask(uint32_t mask);
void str_replace(char *str, char find, char replace);

// ---- Util.cpp: разбор строк ----
bool parseBytes(const char* str, char sep, uint8_t* bytes, int maxBytes, int base);
bool parseInt16_t(const char* str, char sep, int16_t* ints, int maxNum, int base);
void urldecode(char *dst, char *src, uint16_t len);
bool web_user_file_ok(const char *name);                       // можно ли роли user отдать этот файл (имя после urldecode)
bool web_auth_match(const char *got, const char *hash, uint16_t hashLen);   // значение Authorization совпадает с эталоном целиком
char *web_fill_tag_select(char *str, const char *select, uint8_t selected);

// ---- Util.cpp: расписание (одно число - один день, массив 7 дней) ----
bool set_Schedule(char *c, uint32_t *sh);
char *get_Schedule(uint32_t *sh);

// ---- Util.cpp: base64 ----
int base64_encode(char *output, char *input, int inputLen);
int base64_decode(char *output, char *input, int inputLen);
int base64_enc_len(int plainLen);
int base64_dec_len(char *input, int inputLen);

// ---- Util.cpp: CRC16 и сериализация ----
uint16_t calc_crc16(unsigned char * pcBlock, unsigned short len, uint16_t crc = 0xFFFF);
uint16_t _crc16(uint16_t crc, uint8_t a);             // побайтно, для потоковых расчётов
void load_struct(void *to, uint8_t **from, uint16_t to_size);

// Общий буфер вывода и шестнадцатеричные преобразования. Определены в Util.cpp.
// ВНИМАНИЕ: буфер ОДИН на всех, включая ResetCause. Пользоваться только в
// веб-потоке или под замком xWebThreadSemaphore.
#define UTIL_BUF_SIZE 20
extern char _buf[UTIL_BUF_SIZE];
char *MAC2String(uint8_t *mac);      // MAC в строку 01:23:45:67:89:AB
char *addressToHex(uint8_t *f);      // адрес OneWire, восемь байт
char *byteToHex(uint8_t f);          // байт, вида 0xf5
char *uint16ToHex(uint16_t f);       // слово, вида 0xf50e
char *uint32ToHex(uint32_t f);       // двойное слово, вида 0xabcdf50e
                                     // Вызывающих в прошивке нет, оставлен намеренно: третий
                                     // член семейства byteToHex/uint16ToHex, без него набор
                                     // выглядел бы недосмотром. Во flash не попадает -
                                     // --gc-sections выбрасывает; проверен тестами.

// ---- Util.cpp: режим загрузки, выбранный кнопкой KEY1 ----
//
// Кнопка опрашивается в самом начале setup, и по длительности удержания
// выбирается режим:
//   короче KEY_PRESS_MIN      - нажатия не было (касание, дребезг разъёма)
//   до KEY_HOLD_SAFENET       - грузить встроенную морду из кода
//   KEY_HOLD_SAFENET и дольше - safeNetwork И встроенная морда, аварийный режим
//
// Пороги стоят здесь, а не в Constant.h, потому что Util.cpp его не включает
// (модуль чистый, без Arduino), а пороги - контракт функции. Так же объявлены
// ATOF_ERROR и REC_HDR_SIZE.
// KEY_PRESS_MIN отделяет намеренное нажатие от дребезга, и только. Дребезг
// контакта - единицы миллисекунд, поэтому 50 мсек это десятикратный запас.
// Порог 300 мсек отвергал живое нажатие человека (290 мсек): он был назначен
// на глаз и попал в диапазон обычных нажатий. Опрос идёт шагом 10 мсек, так что
// 50 - это пять отсчётов.
#define KEY_PRESS_MIN       50    // мсек, короче - нажатия не было
#define KEY_HOLD_SAFENET  3000    // мсек, удержание для аварийного режима
enum KeyBootMode { KEY_BOOT_NONE = 0, KEY_BOOT_MINWEB, KEY_BOOT_SAFENET };
KeyBootMode key_boot_mode(uint32_t held_ms);

// ---- Каркас записи НОВОГО формата настроек (версия 150) ----
//
// Запись: [тег: uint16][длина: uint16][данные ... длина байт]
//
// Четыре байта на запись, без хитростей. Старый каркас экономил байт на
// коротких записях: длина была переменной ширины, младший бит служил
// признаком, а теги вкраплялись сырыми словами и отличались от длины только
// тем, что лежали в отрицательном диапазоне. Это работало, пока значение
// длины не могло попасть в диапазон тегов, и ломало номер элемента массива
// у записей длиннее 128 байт - он читался как buffer[1], то есть занимал
// место старшего байта длины.
//
// С фиксированным заголовком незнакомый тег пропускается безусловно:
// сдвиг = REC_HDR_SIZE + длина, гадать не о чем.
#define REC_HDR_SIZE  4

void     rec_hdr_put(uint8_t *buf, uint16_t tag, uint16_t len);  // положить заголовок

// Записать данные так, чтобы контрольная сумма ВСЕГДА соответствовала записанным байтам, даже если
// исходная структура меняется другими задачами. CRC считается до записи и после; если она разошлась,
// структура менялась во время записи, и запись повторяется (до attempts раз). wr - запись одного блока
// (возврат 0 - успех), ctx передаётся ей как есть. crc на входе - начальное значение, на выходе - CRC
// записанных данных. Возврат: 0 - записано и CRC стабильна, 1 - запись отказала, 2 - данные менялись все attempts раз.
// Раньше CRC считалась один раз ПОСЛЕ записи, и изменение структуры между записью и подсчётом давало
// несовпадение при контрольном чтении (ERR_SETTINGS_VERIFY, останов ТН); от него защищала остановка планировщика.
typedef int (*RecWriteFn)(void *ctx, const uint8_t *data, uint16_t size);
int8_t   Record_WriteStable(RecWriteFn wr, void *ctx, const uint8_t *src, uint16_t size, uint16_t *crc, uint8_t attempts);
uint16_t rec_hdr_tag(const uint8_t *buf);                        // тег записи
uint16_t rec_hdr_len(const uint8_t *buf);                        // длина данных записи

// Проверка заголовка и каркаса блока настроек нового формата (чистая логика,
// проверяется на хосте). Блок: [длина:2][пролог][записи][контрольная сумма:2].
enum SettingsHead { SETHEAD_OK = 0, SETHEAD_EMPTY, SETHEAD_NEWER };
// size - длина после поля длины (с контрольной суммой), ver - байт версии.
// EMPTY: область не опознана (пустая память 0xFFFF, нулевая или короче пролога,
// версия 0 или 0xFF). NEWER: версия новее, чем умеет прошивка (max_ver).
// min_size - наименьший допустимый size (константы формата в Util.cpp не видны).
SettingsHead settings_head_check(uint16_t size, uint8_t ver, uint8_t max_ver, uint16_t min_size);
// Длина блока настроек из файла против принятых байт. size - поле длины (с контрольной суммой, без
// самого поля), avail - сколько байт принято, считая поле длины. Старый читатель брал size из первых
// двух байт файла и считал по нему CRC от начала буфера (6 КБ), а size бывает до 65535: чужой или
// испорченный файл уводил чтение за конец буфера, size < 2 заворачивал size -= 2 в 65534.
bool settings_len_ok(uint16_t size, int32_t avail);
// Предел частоты инвертора при работе от генератора (резервное питание) из сохранённых настроек. Поле хранится
// в EEPROM, а в старых настройках его не было: читалось 0, и с флагом fBackupPower задание частоты срезалось
// до нуля (OmronFC.cpp, set_target). Ноль и отрицательное значение заменяются значением по умолчанию.
int16_t fc_gen_freq_fix(int16_t stored, int16_t def);
// Задание частоты при работе от генератора: не выше предела генератора, но и не ниже минимума режима. Предел генератора
// разрешён от 40 Гц, минимум режима - до 80: прежний срез до предела давал задание ниже минимума, set_target его
// отвергал, и частота переставала регулироваться (С-19).
int16_t fc_gen_clip(int16_t x, int16_t gen_max, int16_t min);
// Процентное кодирование значения для строки запроса: буквы, цифры и - _ . ~ идут как есть, пробел - плюсом, остальное - %XX
// (байты UTF-8 по одному). Результат ДОБАВЛЯЕТСЯ к dst (cap - полный размер буфера); не поместившееся отбрасывается целыми
// символами, %XX не разрезается, нуль в конце есть всегда. Прежде в запрос шёл текст уведомления с &, %, #, = как есть и
// заменялись только пробелы: SMS обрезалось или терялось. Возвращает длину dst.
uint16_t url_encode_append(char *dst, uint16_t cap, const char *src);
// Частота импульсов датчика потока в ТЫСЯЧНЫХ герца по числу фронтов за окно: прерывание идёт по обоим фронтам, поэтому Гц = фронты / 2 / секунды.
// Прежнее (cnt * 500 * 1000) / dt считалось в 32 битах и переполнялось уже при 8590 фронтах за окно (быстрый расходомер или длинное окно):
// частота внезапно падала. Здесь промежуточное в 64 битах, результат насыщается в uint32. dt_ms = 0 даёт 0.
uint32_t flow_freq_mhz(uint32_t edges, uint32_t dt_ms);
// Расход, литры в час: частота (тысячные Гц) * 360 / kf (сотые). kf меньше FLOW_KF_MIN не принимают ни сеттер, ни загрузка
// настроек (С-13); если такой всё же попал, расход 0, а не деление на ноль.
#define FLOW_KF_MIN 1
uint32_t flow_lph(uint32_t freq_mhz, uint16_t kf);

// ---- Ожидание клиента при отправке (выгрузка большого файла) ----
// sendPacketRTOS ждёт, пока в буфере передачи чипа освободится место (клиент подтвердил прежние пакеты). Ожидание не имело предела: клиент с нулевым окном
// приёма (вкладка приостановлена, канал оборван без FIN) держал веб-поток в цикле вечно, а ожидание, затянувшееся дольше лимита молчания потока (60 с),
// заканчивалось перезагрузкой контроллера. Теперь продвижением считается рост свободного места в буфере: оно отмечается наблюдателю задач, а отсутствие
// роста дольше limit_ms - застрявший клиент, передача прерывается и сокет закрывается.
enum SendWait { SW_PROGRESS = 0, SW_WAIT, SW_STALLED };
// free - свободно в буфере передачи сейчас; *last_free - свободно при прошлом опросе (в начале ожидания 0); *progress_ms - момент последнего роста
// (в начале ожидания - сейчас). SW_PROGRESS - место прибавилось; SW_WAIT - ждём; SW_STALLED - роста нет дольше limit_ms.
SendWait Send_StallStep(uint16_t free, uint16_t *last_free, uint32_t now_ms, uint32_t *progress_ms, uint32_t limit_ms);

// ---- Выгрузка файла статистики/истории с SD сериями блоков ----
// Файл непрерывный, блоки first...last; блоки ram1 и ram2 (текущие блоки статистики и истории) берутся из ОЗУ, не с карты.
// Сколько блоков, начиная с first, можно прочитать с карты одной командой CMD18: до блока из ОЗУ или конца, не больше maxn.
// 0 - блок first сам из ОЗУ. maxn 0 считается за 1.
uint8_t SD_BlockRun(uint32_t first, uint32_t last, uint32_t ram1, uint32_t ram2, uint8_t maxn);

// ---- Пул веб-сокетов W5500 (F1, F3) ----
// F1. Зачистка зависших сокетов закрывала ЖИВЫЕ соединения: время `since` обновлялось только у сокетов, не бывших ни «кандидатами» (ESTABLISHED,
// CLOSE_WAIT), ни занятыми потоком. Под нагрузкой сокеты 0-2 стоят в ESTABLISHED 87-94 % времени, занятые пропускались без обновления, и свежее
// соединение, ждущее обслуживания, принималось за зависшее: сбрасывалось, клиент получал обрыв. Теперь `since` - начало НЕПРЕРЫВНОГО простоя
// кандидата без хозяина: занятый потоком сокет или любое другое состояние начинают отсчёт заново. true - сокет надо закрыть.
bool SockJanitor_Step(uint32_t *since_ms, uint16_t *last_id, uint16_t id, bool candidate, bool in_use, uint32_t now_ms, uint32_t limit_ms);
// F3. Поток помечал свой сокет номером ПОСЛЕДНЕГО опрошенного сокета даже когда клиента не нашёл: в простое там оставался номер 6, остальные потоки
// считали его занятым и не опрашивали, то есть один из семи сокетов был для них невидим. Метка ставится только если клиент взят.
int8_t WebSock_Owner(bool have_client, int8_t sock);                       // метка потока: номер сокета с клиентом или -1
bool   WebSock_Busy(int8_t sock, const int8_t *owners, uint8_t n);          // сокет занят одним из n потоков

// ---- Метрики веб-сервера (F5) ----
// Нужны, чтобы решать по цифрам, а не по догадкам: под нагрузкой веб отвечает «Connection refused» (нет слушающих сокетов) и обслуживает
// запросы по одному. Все счётчики меняются под замком сети, отдельной защиты не требуют.
struct StatAcc { uint32_t n; uint64_t sum; uint32_t max; };       // n значений, их сумма (64 бита: микросекунды за сутки переполнили бы 32) и наибольшее
void     StatAcc_Add(StatAcc *a, uint32_t v);
uint32_t StatAcc_Avg(const StatAcc *a);                           // среднее, 0 если значений нет

// Замер чтения файла с носителя (TEST.FLASH, TEST.SD, вкладка «SPI&Web»): порция за порцией - байты, время внутри read(), CRC16.
// res: 0 - не мерили (нет носителя или файла), 1 - прочитан, -1 - ошибка чтения.
struct SpiReadStat { uint32_t size, us; uint16_t crc; int8_t res; };
void SpiRead_Begin(SpiReadStat *r);
// Учесть результат одного read(): n - вернул read(), us - сколько он длился. false - чтение закончено (конец файла или ошибка).
bool SpiRead_Step(SpiReadStat *r, const uint8_t *buf, int n, uint32_t us);
// Состояния сокета W5500 (Sn_SR) в порядке счётчиков SockStat.st[]
#define SS_CLOSED  0   // закрыт, ждёт повторного открытия на приём
#define SS_LISTEN  1   // слушает: принимает новое соединение
#define SS_SYNRECV 2   // получен SYN
#define SS_EST     3   // соединение установлено: обслуживается или ждёт обслуживания
#define SS_FINWAIT 4   // мы закрываем, ждём ответ клиента
#define SS_CLOSING 5
#define SS_TIMEWAIT 6
#define SS_CLOSEWAIT 7 // клиент закрыл раньше нас
#define SS_LASTACK 8
#define SS_OTHER   9   // остальное (INIT, SYNSENT, UDP...)
#define SS_COUNT   10
uint8_t SockState_Index(uint8_t sn_sr);                          // код Sn_SR -> SS_*
struct SockStat {                                                 // выборки состояний семи веб-сокетов W5500
	uint32_t samples;       // выборок
	uint32_t listen[8];     // сколько выборок застали N сокетов в LISTEN (N = 0...7)
	uint32_t st[SS_COUNT];  // сумма по выборкам: сколько сокетов было в каждом состоянии
	uint32_t zeroEvents;    // периодов без единого слушающего сокета: в них любое новое соединение получает отказ
	uint32_t zeroMax;       // самый долгий такой период, мс (точность - период выборки)
	uint32_t zeroSince;     // начало текущего периода, мс
	uint32_t lastTick;      // момент последней выборки, мс
	bool     inZero;        // период без LISTEN идёт сейчас
};
bool SockStat_Due(const SockStat *s, uint32_t now_ms, uint32_t period_ms);                        // пора ли брать выборку
void SockStat_Sample(SockStat *s, const uint8_t *sn_sr, uint8_t n, uint32_t now_ms);                // учесть выборку: коды Sn_SR n сокетов
// Хвост запроса HTTP/1.0 с заголовками Host и Connection: close (прежний запрос «GET url\n» был HTTP/0.9 без заголовков).
// true - добавлен целиком, false - не поместился (dst не менялся).
bool http_request_tail(char *dst, uint16_t cap, const char *host);
// Тело ответа HTTP: за пустой строкой, отделяющей заголовки. Ответ без строки статуса «HTTP/» - уже тело;
// с ней, но без конца заголовков (обрезан) - пустая строка: в заголовках искать ответ сервиса нельзя.
const char *http_body(const char *resp);

struct SettingsScan {
	uint16_t records;      // записей разобрано (без END)
	uint16_t truncated;    // записей, вылезающих за границу блока
	uint16_t unknown;      // записей с тегом выше max_tag
	bool     end_seen;     // встретилась запись END
};
// Обход записей от p до p_max. Ничего не копирует, только считает.
void settings_scan(const uint8_t *p, const uint8_t *p_max, uint16_t end_tag, uint16_t max_tag, SettingsScan *s);

// Обход области профилей нового формата: [tag][длина][номер слота][данные]..., END.
// Проверка до записи файла в EEPROM: испорченная область ломает профили при
// следующей загрузке. Ничего не копирует, только считает.
struct ProfileScan {
	uint8_t  slots;        // годных записей
	uint8_t  bad_len;      // длина записи не равна ожидаемой
	uint8_t  bad_slot;     // номер слота не меньше max_slots
	uint8_t  bad_tag;      // чужой тег вместо записи профиля или END
	uint8_t  truncated;    // запись вылезает за границу буфера
	bool     end_seen;     // встретилась запись END
	uint32_t bytes;        // длина от начала до конца записи END (без контрольной суммы)
};
void profile_scan(const uint8_t *p, const uint8_t *p_max, uint16_t tag, uint16_t end_tag,
                  uint16_t rec_len, uint8_t max_slots, ProfileScan *s);

// ---- требуют железа (Board.cpp) ----
void getIDchip(char *outstr);
// Функции работы со временем - в DateTime.h.

#endif
