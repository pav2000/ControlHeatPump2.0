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
 * Реестр параметров веб-API: ядро. Описание и границы - в ParamRegistry.h.
 *
 * Правила этого файла: без static-переменных, без strtok, без malloc; все
 * буферы приходят от вызывающего. Числа разбираются и округляются через my_atof и rd.
 */
#include <string.h>
#include "ParamRegistry.h"
#include "../util/Util.h"

const char *const ApiErrName[AE_COUNT] = {
	"ok", "unknown", "bad_value", "range", "na", "busy", "forbidden", "too_big", "conflict", "fail"
};

// ---------------------------------------------------------------------------
// Вывод
// ---------------------------------------------------------------------------
void api_out_init(ApiOut *o, char *buf, uint16_t cap)
{
	o->p = buf; o->cap = cap; o->len = 0; o->over = 0;
	if(cap) buf[0] = 0;
}

void api_put_n(ApiOut *o, const char *s, uint16_t n)
{
	if((uint32_t)o->len + n + 1 > o->cap) { o->over = 1; return; }   // +1: завершающий 0
	memcpy(o->p + o->len, s, n);
	o->len += n;
	o->p[o->len] = 0;
}

void api_put(ApiOut *o, const char *s) { api_put_n(o, s, (uint16_t)strlen(s)); }
void api_putc(ApiOut *o, char c) { api_put_n(o, &c, 1); }

static void put_esc_n(ApiOut *o, const char *s, uint16_t n)       // содержимое JSON-строки без кавычек
{
	static const char hex[] = "0123456789abcdef";   // const, во flash - не изменяемое состояние
	for(uint16_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if(c == '"' || c == '\\') { api_putc(o, '\\'); api_putc(o, (char)c); }
		else if(c == '\n') api_put(o, "\\n");
		else if(c == '\r') api_put(o, "\\r");
		else if(c == '\t') api_put(o, "\\t");
		else if(c < 0x20) { api_put(o, "\\u00"); api_putc(o, hex[c >> 4]); api_putc(o, hex[c & 15]); }
		else api_putc(o, (char)c);
	}
}
static void put_str_n(ApiOut *o, const char *s, uint16_t n)
{
	api_putc(o, '"');
	put_esc_n(o, s, n);
	api_putc(o, '"');
}

void api_put_str(ApiOut *o, const char *s) { put_str_n(o, s, (uint16_t)strlen(s)); }
void api_put_str2(ApiOut *o, const char *a, const char *b)     // одна JSON-строка из двух кусков
{
	api_putc(o, '"');
	put_esc_n(o, a, (uint16_t)strlen(a));
	put_esc_n(o, b, (uint16_t)strlen(b));
	api_putc(o, '"');
}

// Строка, записанная сырой в свободную часть буфера сразу за o->len + 1 (байт перед ней - под открывающую кавычку),
// превращается на месте в JSON-строку. Нужна источникам, которые дописывают текст через strcat и не знают предела
// (опрос регистров инвертора: до двух килобайт, отдельного буфера на стеке задачи для него нет).
void api_str_inplace(ApiOut *o)
{
	static const char hex[] = "0123456789abcdef";
	char *s = o->p + o->len + 1;
	uint16_t n = (uint16_t)strlen(s), extra = 0, i;
	for(i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if(c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t') extra += 1;
		else if(c < 0x20) extra += 5;
	}
	if((uint32_t)o->len + 1 + n + extra + 2 > o->cap) { o->over = 1; o->p[o->len] = 0; return; }
	char *e = s + n + extra;          // сюда встанет закрывающая кавычка
	e[0] = '"'; e[1] = 0;
	char *w = e;
	for(i = n; i > 0; i--) {          // с конца: запись всегда не левее чтения
		unsigned char c = (unsigned char)s[i - 1];
		if(c == '"' || c == '\\') { *--w = (char)c; *--w = '\\'; }
		else if(c == '\n') { *--w = 'n'; *--w = '\\'; }
		else if(c == '\r') { *--w = 'r'; *--w = '\\'; }
		else if(c == '\t') { *--w = 't'; *--w = '\\'; }
		else if(c < 0x20) { *--w = hex[c & 15]; *--w = hex[c >> 4]; *--w = '0'; *--w = '0'; *--w = 'u'; *--w = '\\'; }
		else *--w = (char)c;
	}
	o->p[o->len] = '"';
	o->len += (uint16_t)(n + extra + 2);
}

void api_put_int(ApiOut *o, int32_t v)
{
	char t[12]; uint8_t n = 0;
	uint32_t a = v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
	do { t[n++] = (char)('0' + a % 10); a /= 10; } while(a);
	if(v < 0) api_putc(o, '-');
	while(n) api_putc(o, t[--n]);
}

// raw/scale точно. scale 10/100/1000: десятичная запись без хвостовых нулей.
// Иной scale > 1 (например 60): деление нацело (как в геттерах классов).
void api_put_dec(ApiOut *o, int32_t raw, int16_t scale)
{
	uint8_t d = scale == 10 ? 1 : scale == 100 ? 2 : scale == 1000 ? 3 : 0;
	if(!d) { api_put_int(o, scale > 1 ? raw / scale : raw); return; }
	uint32_t a = raw < 0 ? (uint32_t)(-(int64_t)raw) : (uint32_t)raw;
	if(raw < 0) api_putc(o, '-');
	api_put_int(o, (int32_t)(a / scale));
	uint32_t f = a % scale;
	if(!f) return;
	char t[4]; uint8_t i;
	for(i = d; i > 0; i--) { t[i - 1] = (char)('0' + f % 10); f /= 10; }
	uint8_t e = d;
	while(e && t[e - 1] == '0') e--;
	api_putc(o, '.');
	api_put_n(o, t, e);
}

void api_put_float(ApiOut *o, float v)
{
	if(v == (float)(int32_t)v) { api_put_int(o, (int32_t)v); return; }
	api_put_dec(o, (int32_t)(v * 1000 + (v < 0 ? -0.5f : 0.5f)), 1000);
}

void api_put_hex32(ApiOut *o, uint32_t v)
{
	static const char hex[] = "0123456789abcdef";
	char t[8];
	for(int i = 7; i >= 0; i--) { t[i] = hex[v & 15]; v >>= 4; }
	api_put_n(o, t, 8);
}

// ---------------------------------------------------------------------------
// Разбор запроса
// ---------------------------------------------------------------------------
// Следующая пара "ключ=значение". Пустая пара ("&&" или конец) - конец разбора:
// дальше идёт хвост авторизации.
static int qnext(const char **pp, const char **k, uint16_t *kl, const char **v, uint16_t *vl)
{
	const char *s = *pp;
	if(!s || !*s || *s == '&') return 0;
	const char *e = s;
	const char *eq = 0;
	while(*e && *e != '&') { if(*e == '=' && !eq) eq = e; e++; }
	*k = s;
	if(eq) { *kl = (uint16_t)(eq - s); *v = eq + 1; *vl = (uint16_t)(e - eq - 1); }
	else   { *kl = (uint16_t)(e - s);  *v = e;      *vl = 0; }
	*pp = *e ? e + 1 : e;
	return 1;
}

static bool slice_is(const char *s, uint16_t n, const char *lit)
{
	return strlen(lit) == n && memcmp(s, lit, n) == 0;
}

// Значение параметра запроса с именем name (первое вхождение).
static bool qfind(const char *q, const char *name, const char **v, uint16_t *vl)
{
	const char *k, *vv; uint16_t kl, l;
	while(qnext(&q, &k, &kl, &vv, &l)) {
		if(slice_is(k, kl, name)) { *v = vv; *vl = l; return true; }
	}
	return false;
}

// Процентное декодирование в dst (cap с завершающим 0). -1 если не поместилось.
static int udecode(char *dst, uint16_t cap, const char *s, uint16_t n)
{
	uint16_t o = 0;
	for(uint16_t i = 0; i < n; i++) {
		char c = s[i];
		if(c == '%' && i + 2 < n + 0 + 1 && i + 2 <= n - 1 + 0) {
			int h = 0, ok = 1;
			for(int j = 1; j <= 2; j++) {
				char x = s[i + j]; int d;
				if(x >= '0' && x <= '9') d = x - '0';
				else if(x >= 'a' && x <= 'f') d = x - 'a' + 10;
				else if(x >= 'A' && x <= 'F') d = x - 'A' + 10;
				else { ok = 0; break; }
				h = h * 16 + d;
			}
			if(ok) { c = (char)h; i += 2; }
		}
		if(o + 1 >= cap) return -1;
		dst[o++] = c;
	}
	if(o >= cap) return -1;
	dst[o] = 0;
	return o;
}

// ---------------------------------------------------------------------------
// Поиск параметра
// ---------------------------------------------------------------------------
static bool name_eq(const char *k, uint16_t kl, const char *name)
{
	for(uint16_t i = 0; i < kl; i++) {
		char c = name[i];
		if(!c) return false;
		if(c >= 'A' && c <= 'Z') c = (char)(c + 32);
		if(c != k[i]) return false;
	}
	return name[kl] == 0;
}

static const ApiGroup *find_group(const ApiCtx *c, const char *key, uint16_t kl, uint16_t *dot)
{
	uint16_t d = 0;
	while(d < kl && key[d] != '.') d++;
	if(d >= kl) return 0;
	for(uint8_t i = 0; i < c->ng; i++)
		if(slice_is(key, d, c->groups[i]->sect)) { *dot = d; return c->groups[i]; }
	return 0;
}

static const ApiParam *find_param(const ApiGroup *g, const char *name, uint16_t nl)
{
	for(uint8_t i = 0; i < g->n; i++)
		if(name_eq(name, nl, g->tab[i].name)) return &g->tab[i];
	return 0;
}

static bool is_devgroup(const ApiGroup *g) { return g->ops && g->ops->ndev && g->ops->ndev() != 0; }

// Ключ вида sect.name или (у групп устройств) sect.DEV.name. dev - номер устройства.
static const ApiParam *lookup(const ApiCtx *c, const char *key, uint16_t kl, const ApiGroup **gp, uint8_t *dev)
{
	uint16_t dot;
	const ApiGroup *g = find_group(c, key, kl, &dot);
	if(!g) return 0;
	const char *rest = key + dot + 1;
	uint16_t rl = kl - dot - 1;
	uint8_t d = 0;
	if(is_devgroup(g)) {
		uint16_t j = 0;
		while(j < rl && rest[j] != '.') j++;
		if(j >= rl) return 0;
		uint8_t n = g->ops->ndev(), i;
		for(i = 0; i < n; i++) if(slice_is(rest, j, g->ops->devname(i))) break;
		if(i >= n) return 0;
		d = i; rest += j + 1; rl -= j + 1;
	}
	const ApiParam *p = find_param(g, rest, rl);
	if(p) { *gp = g; *dev = d; }
	return p;
}

static void put_key(ApiOut *o, const ApiGroup *g, const ApiParam *p, int16_t dev)
{
	api_putc(o, '"');
	api_put(o, g->sect);
	api_putc(o, '.');
	if(dev >= 0) { api_put(o, g->ops->devname((uint8_t)dev)); api_putc(o, '.'); }      // dev -1 - без устройства
	else if(dev == -2) api_put(o, "*.");                                                   // meta: ключ для любого устройства
	for(const char *s = p->name; *s; s++) api_putc(o, (*s >= 'A' && *s <= 'Z') ? (char)(*s + 32) : *s);
	api_putc(o, '"');
}

// ---------------------------------------------------------------------------
// Доступ к полю
// ---------------------------------------------------------------------------
static int32_t field_get(const ApiParam *p, const uint8_t *b)
{
	const uint8_t *f = b + p->off;
	switch(p->type) {
	case PT_I8:  return (int8_t)f[0];
	case PT_U8:  case PT_E8: return f[0];
	case PT_I16: { int16_t t;  memcpy(&t, f, 2); return t; }
	case PT_U16: { uint16_t t; memcpy(&t, f, 2); return t; }
	case PT_B8:  return (f[0] >> p->bit) & 1;
	case PT_B16: { uint16_t t; memcpy(&t, f, 2); return (t >> p->bit) & 1; }
	}
	return 0;
}

static void field_set(const ApiParam *p, uint8_t *b, int32_t v)
{
	uint8_t *f = b + p->off;
	switch(p->type) {
	case PT_I8: case PT_U8: case PT_E8: f[0] = (uint8_t)v; break;
	case PT_I16: case PT_U16: { uint16_t t = (uint16_t)v; memcpy(f, &t, 2); break; }
	case PT_B8:  if(v) f[0] |= (uint8_t)(1u << p->bit); else f[0] &= (uint8_t)~(1u << p->bit); break;
	case PT_B16: { uint16_t t; memcpy(&t, f, 2);
	               if(v) t |= (uint16_t)(1u << p->bit); else t &= (uint16_t)~(1u << p->bit);
	               memcpy(f, &t, 2); break; }
	}
}

static uint8_t lab_count(const ApiParam *p)
{
	uint8_t n = 0;
	if(p->lab) while(p->lab[n]) n++;
	return n;
}

// Значение в JSON. AE_OK или код ошибки; при ошибке вывод не гарантирован - откатывает вызывающий.
static int put_value(const ApiGroup *g, const ApiParam *p, uint8_t dev, ApiOut *o)
{
	if(p->type == PT_CALL) {
		if(!g->ops || !g->ops->get) return AE_FAIL;
		return g->ops->get(p->hook, dev, o);
	}
	const uint8_t *b = (const uint8_t *)g->ops->base(p->bs);
	if(!b) return AE_NA;
	if(p->type == PT_STR) {              // массив может оказаться без завершающего 0 - читаем не дальше его размера
		const char *str = (const char *)(b + p->off);
		uint16_t n = 0;
		while(n < (uint16_t)p->scale && str[n]) n++;
		put_str_n(o, str, n);
		return AE_OK;
	}
	int32_t raw = field_get(p, b);
	switch(p->type) {
	case PT_B8: case PT_B16:
		if(p->lab) api_put_int(o, raw); else api_put(o, raw ? "true" : "false");
		break;
	case PT_E8: api_put_int(o, raw); break;
	default:    api_put_dec(o, raw, p->scale); break;
	}
	return AE_OK;
}

// Разбор значения в «сырое» число поля (то же для записи и для сравнения if.).
static int parse_value(const ApiGroup *g, const ApiParam *p, const char *val, int32_t *raw)
{
	if(p->type == PT_B8 || p->type == PT_B16 || p->type == PT_E8) {
		int32_t v;
		if(p->type != PT_E8 && !p->lab && (!strcmp(val, "true") || !strcmp(val, "false"))) v = (val[0] == 't');
		else {
			float x = my_atof(val);
			if(x == ATOF_ERROR || x != (float)(int32_t)x) return AE_BAD_VALUE;
			v = (int32_t)x;
		}
		if(p->lab) {
			if(v < 0 || v >= (int32_t)lab_count(p)) return AE_RANGE;
			if(g->ops && g->ops->mask && (g->ops->mask(p->hook) >> v & 1)) return AE_NA;
		} else if(p->type != PT_E8 && v != 0 && v != 1) return AE_BAD_VALUE;
		*raw = v;
		return AE_OK;
	}
	float x = my_atof(val);
	if(x == ATOF_ERROR) return AE_BAD_VALUE;
	if(x < p->min || x > p->max) return AE_RANGE;
	*raw = p->scale > 1 ? (int32_t)rd(x, p->scale) : (int32_t)x;
	return AE_OK;
}

// Запись одного значения. ifv - ожидаемое прежнее значение или NULL.
static int set_one(const ApiCtx *c, const ApiGroup *g, const ApiParam *p, uint8_t dev, const char *val, const char *ifv)
{
	if((p->flags & PF_RO) || (c->role != ROLE_ADMIN && !(p->flags & PF_UW))) return AE_FORBIDDEN;
	if((p->flags & PF_OFF) && c->running) return AE_BUSY;
	if(is_devgroup(g) && !g->ops->devok(dev)) return AE_NA;
	if(p->type == PT_CALL) {
		if(!g->ops || !g->ops->set) return AE_FAIL;
		if(p->lab) {            // перечисление: индекс в списке и доступность проверяет ядро
			float x = my_atof(val);
			if(x == ATOF_ERROR || x != (float)(int32_t)x) return AE_BAD_VALUE;
			int32_t v = (int32_t)x;
			if(v < 0 || v >= (int32_t)lab_count(p)) return AE_RANGE;
			if(v < 16 && g->ops->mask && (g->ops->mask(p->hook) >> v & 1)) return AE_NA;
		} else if((p->bit == MK_NUM || p->bit == MK_INT) && p->max > p->min) {   // число с границами
			float x = my_atof(val);
			if(x == ATOF_ERROR) return AE_BAD_VALUE;
			if(x < p->min || x > p->max) return AE_RANGE;
		}
		if(ifv) {   // сравнение по тексту текущего значения (без кавычек)
			char t[48]; ApiOut to; api_out_init(&to, t, sizeof(t));
			if(g->ops->get && g->ops->get(p->hook, dev, &to) == AE_OK && !to.over) {
				const char *s = t; size_t n = strlen(s);
				if(n >= 2 && s[0] == '"' && s[n - 1] == '"') { s++; n -= 2; }
				if(strlen(ifv) != n || memcmp(s, ifv, n)) return AE_CONFLICT;
			} else return AE_CONFLICT;
		}
		return g->ops->set(p->hook, dev, val);
	}
	uint8_t *b = (uint8_t *)g->ops->base(p->bs);
	if(!b) return AE_NA;
	if(p->type == PT_STR) {
		char *dst = (char *)(b + p->off);
		size_t n = strlen(val);
		if(ifv && strncmp(dst, ifv, (size_t)p->scale) != 0) return AE_CONFLICT;
		if(!n && (p->flags & PF_NE)) return AE_BAD_VALUE;
		if(p->flags & PF_NC) for(size_t i = 0; i < n; i++) if((uint8_t)val[i] < 0x20 || (uint8_t)val[i] == 0x7F) return AE_BAD_VALUE;
		if(n + 1 > (size_t)p->scale) return AE_RANGE;
		memcpy(dst, val, n + 1);            // как strcpy: хвост массива не трогаем
		if(g->ops && g->ops->after) g->ops->after(p->hook);
		return AE_OK;
	}
	int32_t raw;
	if(ifv) {
		int e = parse_value(g, p, ifv, &raw);
		if(e == AE_NA || e == AE_RANGE) {   // старое значение может лежать вне нынешней доступности
			float x = my_atof(ifv);
			if(x == ATOF_ERROR) return AE_BAD_VALUE;
			raw = p->scale > 1 ? (int32_t)rd(x, p->scale) : (int32_t)x;
		} else if(e) return e;
		if(field_get(p, b) != raw) return AE_CONFLICT;
	}
	int e = parse_value(g, p, val, &raw);
	if(e) return e;
	field_set(p, b, raw);
	if(g->ops && g->ops->after) g->ops->after(p->hook);
	return AE_OK;
}

#define MAXKEYS 40   // ключей запроса с индивидуальным учётом ошибок; остальные получают too_big

static void put_errors(ApiOut *o, const char *q, const char *listname, const uint8_t *ecode, uint8_t cnt, bool skip_if)
{
	// Повторный проход по ключам запроса: печатаем только ошибочные.
	api_put(o, "\"e\":{");
	bool first = true;
	uint16_t idx = 0;
	if(listname) {   // get: список k=a,b,c
		const char *v; uint16_t vl;
		if(qfind(q, listname, &v, &vl)) {
			uint16_t i = 0;
			while(i <= vl) {
				uint16_t j = i;
				while(j < vl && v[j] != ',') j++;
				if(j > i) {
					uint8_t code = idx < MAXKEYS ? ecode[idx] : (uint8_t)AE_TOO_BIG;
					if(code) {
						if(!first) api_putc(o, ',');
						first = false;
						put_str_n(o, v + i, j - i); api_putc(o, ':');
						api_put_str(o, ApiErrName[code]);
					}
					idx++;
				}
				i = j + 1;
			}
		}
	} else {         // set: пары ключ=значение
		const char *k, *v; uint16_t kl, vl;
		while(qnext(&q, &k, &kl, &v, &vl)) {
			if(skip_if && kl > 3 && k[0] == 'i' && k[1] == 'f' && k[2] == '.') continue;
			uint8_t code = idx < MAXKEYS ? ecode[idx] : (uint8_t)AE_TOO_BIG;
			if(code) {
				if(!first) api_putc(o, ',');
				first = false;
				put_str_n(o, k, kl); api_putc(o, ':');
				api_put_str(o, ApiErrName[code]);
			}
			idx++;
		}
	}
	(void)cnt;
	api_putc(o, '}');
}

// Читает один ключ в d; при ошибке откатывает вывод. first - признак «запятая не нужна».
static int emit_key(ApiOut *o, const ApiGroup *g, const ApiParam *p, uint8_t dev, bool *first)
{
	uint16_t mark = o->len;
	uint8_t over = o->over;
	if(is_devgroup(g) && !g->ops->devok(dev)) return AE_NA;
	if(!*first) api_putc(o, ',');
	put_key(o, g, p, is_devgroup(g) ? dev : -1);
	api_putc(o, ':');
	int e = put_value(g, p, dev, o);
	if(e == AE_OK && o->over && !over) e = AE_TOO_BIG;
	if(e != AE_OK) {
		o->len = mark; o->p[mark] = 0;
		if(e == AE_TOO_BIG) o->over = over;   // переполнение одного ключа не губит весь ответ
		return e;
	}
	*first = false;
	return AE_OK;
}

// ---------------------------------------------------------------------------
// Глаголы
// ---------------------------------------------------------------------------
// Имя атрибута (строчными) есть в списке a=x,y,z
static bool attr_wanted(const char *av, uint16_t al, const char *name)
{
	uint16_t i = 0;
	while(i <= al) {
		uint16_t j = i;
		while(j < al && av[j] != ',') j++;
		if(j > i && name_eq(av + i, j - i, name)) return true;
		i = j + 1;
	}
	return false;
}

int api_get(const ApiCtx *c, const char *query, ApiOut *o)
{
	const char *kv, *gv; uint16_t kl = 0, gl = 0;
	bool hk = qfind(query, "k", &kv, &kl);
	bool hg = qfind(query, "g", &gv, &gl);
	if(!hk && !hg) return -1;
	uint8_t ecode[MAXKEYS]; memset(ecode, 0, sizeof(ecode));
	api_put(o, "{\"d\":{");
	bool first = true;
	uint16_t idx = 0;
	if(hk) {
		uint16_t i = 0;
		while(i <= kl) {
			uint16_t j = i;
			while(j < kl && kv[j] != ',') j++;
			if(j > i) {
				if(idx < MAXKEYS) {
					const ApiGroup *g = 0; uint8_t dev = 0;
					const ApiParam *p = lookup(c, kv + i, j - i, &g, &dev);
					int e;
					if(!p) e = AE_UNKNOWN;
					else if((p->flags & PF_AR) && c->role != ROLE_ADMIN) e = AE_FORBIDDEN;
					else e = emit_key(o, g, p, dev, &first);
					ecode[idx] = (uint8_t)e;
				}
				idx++;
			}
			i = j + 1;
		}
	}
	if(hg) {
		const char *av = 0; uint16_t al = 0;
		bool ha = qfind(query, "a", &av, &al);       // a=value,err - только эти атрибуты (для групп устройств)
		for(uint8_t gi = 0; gi < c->ng; gi++) {
			const ApiGroup *g = c->groups[gi];
			if(!slice_is(gv, gl, g->sect)) continue;
			uint8_t nd = is_devgroup(g) ? g->ops->ndev() : 1;
			for(uint8_t d = 0; d < nd; d++) {
				if(is_devgroup(g) && !g->ops->devok(d)) continue;     // устройства нет в конфигурации
				for(uint8_t i = 0; i < g->n; i++) {
					const ApiParam *p = &g->tab[i];
					if((p->flags & PF_AR) && c->role != ROLE_ADMIN) continue;
					if(ha && !attr_wanted(av, al, p->name)) continue;
					emit_key(o, g, p, d, &first);   // недоступное в сборке (na) в группе просто не попадает
				}
			}
		}
	}
	api_put(o, "},");
	put_errors(o, query, hk ? "k" : 0, ecode, 0, false);
	api_putc(o, '}');
	return o->over ? -2 : 0;
}

int api_set(const ApiCtx *c, const char *query, ApiOut *o)
{
	uint8_t ecode[MAXKEYS]; memset(ecode, 0, sizeof(ecode));
	api_put(o, "{\"d\":{");
	bool first = true;
	uint16_t idx = 0;
	const char *q = query, *k, *v; uint16_t kl, vl;
	bool any = false;
	while(qnext(&q, &k, &kl, &v, &vl)) {
		if(kl > 3 && k[0] == 'i' && k[1] == 'f' && k[2] == '.') continue;
		any = true;
		if(idx >= MAXKEYS) { idx++; continue; }
		int e;
		const ApiGroup *g = 0; uint8_t dev = 0;
		const ApiParam *p = lookup(c, k, kl, &g, &dev);
		if(!p) e = AE_UNKNOWN;
		else {
			// ожидаемое прежнее значение: ищем if.<ключ> в том же запросе
			const char *iv = 0; uint16_t il = 0; bool hasif = false;
			{
				const char *qq = query, *k2, *v2; uint16_t k2l, v2l;
				while(qnext(&qq, &k2, &k2l, &v2, &v2l)) {
					if(k2l == kl + 3 && k2[0] == 'i' && k2[1] == 'f' && k2[2] == '.' && memcmp(k2 + 3, k, kl) == 0) {
						iv = v2; il = v2l; hasif = true; break;
					}
				}
			}
			// рабочая область: основная часть под новое значение (расписание бойлера ~180 знаков),
			// хвост в 48 байт под ожидаемое прежнее значение if.
			const uint16_t ifcap = 48;
			char *s1 = c->scratch; uint16_t vcap = c->scratch_cap - ifcap;
			char *s2 = c->scratch + vcap;
			int n1 = udecode(s1, vcap, v, vl);
			int n2 = hasif ? udecode(s2, ifcap, iv, il) : 0;
			if(n1 < 0 || n2 < 0) e = AE_TOO_BIG;
			else {
				e = set_one(c, g, p, dev, s1, hasif ? s2 : 0);
				if(e == AE_OK) e = emit_key(o, g, p, dev, &first);   // значение, прочитанное после записи
			}
		}
		ecode[idx++] = (uint8_t)e;
	}
	if(!any) return -1;
	api_put(o, "},");
	put_errors(o, query, 0, ecode, 0, true);
	api_putc(o, '}');
	return o->over ? -2 : 0;
}

int api_do(const ApiCtx *c, const char *query, ApiOut *o)
{
	const char *av; uint16_t al;
	if(!qfind(query, "a", &av, &al)) return -1;
	uint16_t mark = o->len;
	for(uint8_t i = 0; i < c->na; i++) {
		const ApiAction *a = &c->acts[i];
		if(!slice_is(av, al, a->name)) continue;
		if(c->role != ROLE_ADMIN && !a->user) {
			api_put(o, "{\"d\":{},\"e\":{"); put_str_n(o, av, al); api_put(o, ":\"forbidden\"}}");
			return o->over ? -2 : 0;
		}
		const char *pp = "";
		const char *pv; uint16_t pl;
		if(qfind(query, "p", &pv, &pl)) {              // параметры действия: номер профиля, пара «из:в» ...
			if(!c->scratch || udecode(c->scratch, c->scratch_cap, pv, pl) < 0) {
				api_put(o, "{\"d\":{},\"e\":{"); put_str_n(o, av, al); api_put(o, ":\"too_big\"}}");
				return o->over ? -2 : 0;
			}
			pp = c->scratch;
		}
		api_put(o, "{\"d\":{"); put_str_n(o, av, al); api_putc(o, ':');
		int e = a->fn(o, pp);
		if(e != AE_OK) {
			o->len = mark; o->p[mark] = 0;
			api_put(o, "{\"d\":{},\"e\":{"); put_str_n(o, av, al); api_putc(o, ':');
			api_put_str(o, ApiErrName[e]); api_put(o, "}}");
		} else api_put(o, "},\"e\":{}}");
		return o->over ? -2 : 0;
	}
	api_put(o, "{\"d\":{},\"e\":{"); put_str_n(o, av, al); api_put(o, ":\"unknown\"}}");
	return o->over ? -2 : 0;
}

static void put_meta(ApiOut *o, const ApiGroup *g, const ApiParam *p)
{
	uint8_t dec = p->scale == 10 ? 1 : p->scale == 100 ? 2 : p->scale == 1000 ? 3 : 0;
	const char *t;
	bool isenum = false, hasrange = true;
	switch(p->type) {
	case PT_B8: case PT_B16: if(p->lab) { t = "enum"; isenum = true; } else t = "bool"; hasrange = false; break;
	case PT_E8: t = "enum"; isenum = true; hasrange = false; break;
	case PT_STR: t = "str"; hasrange = false; break;
	case PT_CALL:
		if(p->lab) { t = "enum"; isenum = true; hasrange = false; break; }
		t = p->bit == MK_NUM ? "num" : p->bit == MK_INT ? "int" : p->bit == MK_BOOL ? "bool" :
		    p->bit == MK_STR ? "str" : p->bit == MK_TABLE ? "table" : p->bit == MK_IP ? "ip" : p->bit == MK_MAC ? "mac" : "text";
		hasrange = (p->bit == MK_NUM || p->bit == MK_INT) && p->max > p->min;      // границы у вычисляемых полей не заданы
		break;
	default: t = dec ? "num" : "int";
	}
	put_key(o, g, p, is_devgroup(g) ? -2 : -1);
	api_put(o, ":{\"t\":\""); api_put(o, t); api_putc(o, '"');
	if(hasrange) {
		api_put(o, ",\"dec\":"); api_put_int(o, dec);
		api_put(o, ",\"min\":"); api_put_float(o, p->min);
		api_put(o, ",\"max\":"); api_put_float(o, p->max);
	} else if(p->type == PT_STR) {
		api_put(o, ",\"max\":"); api_put_int(o, p->scale - 1);               // длина строки без завершающего 0
	} else if(p->type == PT_CALL && (p->bit == MK_NUM) && !isenum) {
		api_put(o, ",\"dec\":"); api_put_int(o, dec);
	}
	if(isenum) {
		api_put(o, ",\"o\":[");
		for(uint8_t i = 0; p->lab && p->lab[i]; i++) { if(i) api_putc(o, ','); api_put_str(o, p->lab[i]); }
		api_putc(o, ']');
		uint16_t m = (g->ops && g->ops->mask) ? g->ops->mask(p->hook) : 0;
		if(m) {
			api_put(o, ",\"na\":[");
			bool f = true;
			for(uint8_t i = 0; i < 16; i++) if(m >> i & 1) { if(!f) api_putc(o, ','); f = false; api_put_int(o, i); }
			api_putc(o, ']');
		}
	}
	api_put(o, ",\"w\":\""); api_putc(o, (p->flags & PF_RO) ? '0' : (p->flags & PF_UW) ? 'u' : 'a'); api_putc(o, '"');
	if(p->flags & PF_AR) api_put(o, ",\"r\":\"a\"");
	api_putc(o, '}');
}

int api_meta(const ApiCtx *c, const char *query, ApiOut *o)
{
	const char *gv; uint16_t gl;
	if(!qfind(query, "g", &gv, &gl)) return -1;
	bool found = false;
	api_put(o, "{\"sch\":\""); api_put_hex32(o, api_schema(c)); api_putc(o, '"');
	for(uint8_t gi = 0; gi < c->ng; gi++) {       // у групп устройств - список присутствующих
		const ApiGroup *g = c->groups[gi];
		if(!slice_is(gv, gl, g->sect) || !is_devgroup(g)) continue;
		api_put(o, ",\"devs\":[");
		bool f = true;
		for(uint8_t d = 0; d < g->ops->ndev(); d++) if(g->ops->devok(d)) { if(!f) api_putc(o, ','); f = false; api_put_str(o, g->ops->devname(d)); }
		api_putc(o, ']');
		break;
	}
	api_put(o, ",\"m\":{");
	bool first = true;
	for(uint8_t gi = 0; gi < c->ng; gi++) {
		const ApiGroup *g = c->groups[gi];
		if(!slice_is(gv, gl, g->sect)) continue;
		found = true;
		for(uint8_t i = 0; i < g->n; i++) {
			const ApiParam *p = &g->tab[i];
			if((p->flags & PF_AR) && c->role != ROLE_ADMIN) continue;
			if(!first) api_putc(o, ',');
			first = false;
			put_meta(o, g, p);
		}
	}
	api_put(o, "}}");
	if(!found) return -1;
	return o->over ? -2 : 0;
}

// ---------------------------------------------------------------------------
// Хеш состава реестра (E2): меняется при любом изменении ключей, типов,
// границ, подписей и прав. Считается по запросу, статического кеша нет.
// ---------------------------------------------------------------------------
static uint32_t fnv(uint32_t h, const void *d, size_t n)
{
	const uint8_t *p = (const uint8_t *)d;
	while(n--) { h ^= *p++; h *= 16777619u; }
	return h;
}

static uint32_t fnv_s(uint32_t h, const char *s)
{
	return fnv(h, s, strlen(s) + 1);
}

uint32_t api_schema(const ApiCtx *c)
{
	uint32_t h = 2166136261u;
	for(uint8_t gi = 0; gi < c->ng; gi++) {
		const ApiGroup *g = c->groups[gi];
		h = fnv_s(h, g->sect);
		for(uint8_t i = 0; i < g->n; i++) {
			const ApiParam *p = &g->tab[i];
			h = fnv_s(h, p->name);
			h = fnv(h, &p->min, sizeof(p->min));
			h = fnv(h, &p->max, sizeof(p->max));
			h = fnv(h, &p->scale, sizeof(p->scale));
			h = fnv(h, &p->type, 1);
			h = fnv(h, &p->flags, 1);
			h = fnv(h, &p->bit, 1);
			for(uint8_t k = 0; p->lab && p->lab[k]; k++) h = fnv_s(h, p->lab[k]);
		}
	}
	for(uint8_t i = 0; i < c->na; i++) { h = fnv_s(h, c->acts[i].name); h = fnv(h, &c->acts[i].user, 1); }
	return h;
}
