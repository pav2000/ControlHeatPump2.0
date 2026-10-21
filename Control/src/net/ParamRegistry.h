/*
 * Copyright (c) 2016-2026 by Pavel Panfilov <firstlast2007@gmail.com>  telegram @pav20000
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
 * Реестр параметров веб-API: ядро.
 *
 * ЗАЧЕМ. Параметр описывается один раз: класс-владелец отдаёт константную таблицу ApiParam, а
 * разбор числа, границы, масштаб и формат ответа делает ядро (а не цепочки strcmp в get_* и
 * set_*). Протокол - tools/api/spec.md.
 *
 * ГРАНИЦЫ. Ядро не знает про Arduino и HP: только <stdint.h> и Util.h (my_atof, rd), поэтому
 * собирается и проверяется на ПК (tests/test_paramreg.cpp). Всё, что требует знания об
 * устройстве (адрес структуры, побочные эффекты записи, нестандартные значения), приходит через
 * ApiOps группы.
 *
 * МНОГОПОТОЧНОСТЬ. Ни static-переменных, ни strtok, ни malloc: состояние запроса лежит в
 * ApiCtx/ApiOut на стеке вызывающего и в буферах его потока; таблицы только читаются. ФОРМАТ
 * ХРАНЕНИЯ НЕ ТРОГАЕТСЯ: таблица содержит смещения полей существующих структур (offsetof).
 */
#ifndef ParamRegistry_h
#define ParamRegistry_h

#include <stdint.h>

// ---- типы значений (как поле лежит в памяти) ----
enum {
	PT_I8, PT_U8, PT_I16, PT_U16,   // целое со знаком/без, 1 или 2 байта; scale > 1 - дробное в единицах 1/scale
	PT_B8, PT_B16,                  // один бит слова флагов (bit = номер бита); с lab - перечисление 0/1
	PT_E8,                          // перечисление в одном байте, lab - подписи
	PT_CALL,                        // всё остальное: чтение/запись через ApiOps.get/set по hook
	PT_STR                          // символьный массив в структуре: scale - размер массива вместе с завершающим 0
};

// ---- флаги параметра ----
#define PF_RO   1   // только чтение
#define PF_UW   2   // роль user может писать (по умолчанию писать может только admin)
#define PF_AR   4   // читать может только admin
#define PF_OFF  8   // писать можно только при остановленном ТН
#define PF_NE   16  // строка не может быть пустой
#define PF_NC   32  // в строке нет управляющих символов (<0x20, 0x7F): иначе ломаются заголовки письма и команды SMTP

// ---- вид значения для meta у PT_CALL (поле bit) ----
enum { MK_NUM, MK_INT, MK_BOOL, MK_STR, MK_TEXT, MK_TABLE, MK_IP, MK_MAC };   // MK_TABLE - значение уже готовый JSON (массив/объект), только чтение

// ---- коды ошибок по ключам ----
enum {
	AE_OK = 0, AE_UNKNOWN, AE_BAD_VALUE, AE_RANGE, AE_NA, AE_BUSY,
	AE_FORBIDDEN, AE_TOO_BIG, AE_CONFLICT, AE_FAIL,
	AE_COUNT
};
extern const char *const ApiErrName[AE_COUNT];   // "unknown", "bad_value", ...

// ---- роли ----
#define ROLE_ADMIN 0
#define ROLE_USER  1

// Выходной буфер. Запись никогда не выходит за cap: при нехватке места
// ставится over, а ответ остаётся синтаксически целым (за это отвечает вызывающий).
struct ApiOut {
	char    *p;
	uint16_t cap;
	uint16_t len;
	uint8_t  over;
};
void api_out_init(ApiOut *o, char *buf, uint16_t cap);
void api_put(ApiOut *o, const char *s);                 // строка как есть
void api_putc(ApiOut *o, char c);
void api_put_n(ApiOut *o, const char *s, uint16_t n);
void api_put_str(ApiOut *o, const char *s);             // JSON-строка в кавычках с экранированием
void api_put_str2(ApiOut *o, const char *a, const char *b);   // одна JSON-строка из двух кусков (без буфера под сумму)
void api_str_inplace(ApiOut *o);                       // сырой текст, дописанный по адресу o->p + o->len + 1, -> JSON-строка на месте
void api_put_int(ApiOut *o, int32_t v);
void api_put_dec(ApiOut *o, int32_t raw, int16_t scale);// raw/scale точно, без хвостовых нулей (scale 10/100/1000), иначе целое деление
void api_put_float(ApiOut *o, float v);                 // границы для meta: целое или до 3 знаков

// Описание одного параметра. Ключ = секция группы + '.' + name строчными:
// name - это ПРЕЖНЯЯ строка STRCONST (hp_TEMP1 = "TEMP1" -> "heat.temp1"),
// своей копии строки нет.
struct ApiParam {
	const char *name;
	const char *const *lab;   // подписи перечисления (NULL-терминированный массив) или NULL
	float       min, max;     // границы в единицах пользователя (до масштаба), как в прежних проверках
	uint16_t    off;          // смещение поля от базы (offsetof)
	int16_t     scale;        // 1 - целое; 10/100/1000 - дробное; прочие (60) - деление нацело
	uint8_t     type;         // PT_*
	uint8_t     flags;        // PF_*
	uint8_t     bit;          // номер бита (PT_B*) или вид MK_* (PT_CALL)
	uint8_t     hook;         // номер для ApiOps (0 - нет)
	uint8_t     bs;           // какая база из ApiOps.base
};

struct ApiOps {
	void     *(*base)(uint8_t sel);                      // адрес структуры-хозяина
	int       (*get)(uint8_t hook, uint8_t dev, ApiOut *o);        // PT_CALL: записать JSON-значение, вернуть AE_*
	int       (*set)(uint8_t hook, uint8_t dev, const char *val);  // PT_CALL: записать, вернуть AE_*
	void      (*after)(uint8_t hook);                    // после успешной записи обычного поля (побочные эффекты)
	uint16_t  (*mask)(uint8_t hook);                     // перечисление: бит i = индекс i недоступен в этой сборке
	// Группа устройств (температурные датчики, реле...): ключ temp.TOUT.value. Для обычных групп поля нулевые.
	uint8_t     (*ndev)(void);                           // число устройств; 0 или NULL - обычная группа
	const char *(*devname)(uint8_t i);                   // имя устройства: регистр сохраняется в ключе
	uint8_t     (*devok)(uint8_t i);                     // 1 - устройство есть в конфигурации, иначе ключ даёт na
};

struct ApiGroup {
	const char     *sect;   // "heat"
	const ApiParam *tab;
	uint8_t         n;
	const ApiOps   *ops;
};

// Действие do. fn пишет в o значение результата (JSON) и возвращает AE_*.
struct ApiAction {
	const char *name;       // "power.on"
	uint8_t     user;       // 1 - разрешено роли user
	int       (*fn)(ApiOut *o, const char *p);     // p - значение параметра p= (уже раскодированное, "" если не задано)
};

struct ApiCtx {
	const ApiGroup *const *groups;
	uint8_t         ng;
	const ApiAction *acts;
	uint8_t         na;
	uint8_t         role;      // ROLE_*
	uint8_t         running;   // ТН работает: PF_OFF даёт busy
	char           *scratch;   // рабочая область для разбора значения
	uint16_t        scratch_cap;
};

// Запрос - часть адреса после '?', без '/api/<глагол>?'. Разбор прекращается
// на "&&" (дальше хвост авторизации) и на конце строки.
// Возврат: 0 - ответ сформирован (ошибки по ключам внутри), иначе -ошибка запроса:
//   -1 не распознан запрос, -2 не поместилось.
int  api_get (const ApiCtx *c, const char *query, ApiOut *o);
int  api_set (const ApiCtx *c, const char *query, ApiOut *o);
int  api_do  (const ApiCtx *c, const char *query, ApiOut *o);
int  api_meta(const ApiCtx *c, const char *query, ApiOut *o);
uint32_t api_schema(const ApiCtx *c);        // хеш состава реестра (E2)
void api_put_hex32(ApiOut *o, uint32_t v);

// ---- помощники для таблиц в прошивке ----
// FIT проверяет на этапе компиляции, что граница*масштаб помещается в поле: rd() возвращает int16,
// выход за него - неопределённое поведение. Выражение равно 0, прибавляется к смещению.
#define FIT(lo,hi,sc,L,H) (sizeof(char[((long)((lo)*(sc)) >= (L) && (long)((hi)*(sc)) <= (H)) ? 1 : -1]) - 1)
// числа: (имя, нижняя, верхняя граница, масштаб, смещение, флаги[, хук])
#define PNH_I16(nm,lo,hi,sc,off,fl,hk) { nm, 0, (float)(lo), (float)(hi), (uint16_t)((off) + FIT(lo,hi,sc,-32768L,32767L)), sc, PT_I16, fl, 0, hk, 0 }
#define PNH_U16(nm,lo,hi,sc,off,fl,hk) { nm, 0, (float)(lo), (float)(hi), (uint16_t)((off) + FIT(lo,hi,sc,0L,65535L)),     sc, PT_U16, fl, 0, hk, 0 }
#define PNH_I8(nm,lo,hi,sc,off,fl,hk)  { nm, 0, (float)(lo), (float)(hi), (uint16_t)((off) + FIT(lo,hi,sc,-128L,127L)),    sc, PT_I8,  fl, 0, hk, 0 }
#define PNH_U8(nm,lo,hi,sc,off,fl,hk)  { nm, 0, (float)(lo), (float)(hi), (uint16_t)((off) + FIT(lo,hi,sc,0L,255L)),       sc, PT_U8,  fl, 0, hk, 0 }
#define PN_I16(nm,lo,hi,sc,off,fl) PNH_I16(nm,lo,hi,sc,off,fl,0)
#define PN_U16(nm,lo,hi,sc,off,fl) PNH_U16(nm,lo,hi,sc,off,fl,0)
#define PN_I8(nm,lo,hi,sc,off,fl)  PNH_I8(nm,lo,hi,sc,off,fl,0)
#define PN_U8(nm,lo,hi,sc,off,fl)  PNH_U8(nm,lo,hi,sc,off,fl,0)
// строка в структуре: (имя, смещение, размер массива, флаги, хук)
#define PN_STR(nm,off,cap,fl,hk) { nm, 0, 0, (float)((cap) - 1), (uint16_t)(off), cap, PT_STR, fl, 0, hk, 0 }
// бит слова флагов: (имя, смещение слова, номер бита, тип PT_B8/PT_B16, подписи, хук, флаги, база)
#define PN_BIT(nm,off,bit,ty,lab,hk,fl,bs) { nm, lab, 0, 1, (uint16_t)(off), 1, ty, fl, bit, hk, bs }

#endif
