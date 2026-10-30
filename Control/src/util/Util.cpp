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
 * Чистые функции общего назначения: преобразования чисел и строк, разбор,
 * base64, CRC16, расписания.
 *
 * Модуль: отдельная единица трансляции. Ничего не знает ни о HeatPump, ни о
 * FreeRTOS, ни о железе, ни даже об Arduino - только числа, строки и память.
 * Поэтому проверяется обычными тестами (tests/test_util.cpp).
 *
 * Всё, что работает с железом (SD, flash, регистры SAM, EEPROM и часы по I2C),
 * живёт в слое железа. Функции с IPAddress - в Network.cpp: этот тип из
 * Arduino, а сюда Arduino не входит.
 *
 * Шестнадцатеричный вывод и общий буфер _buf лежат здесь: железа они не касаются,
 * только байты и строки, поэтому их накрывают тесты на хосте.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mini-printf.h"
#include "Util.h"

// На SAM никакого PROGMEM нет, макрос сводится к разыменованию (см. avr/pgmspace.h
// ядра). Определяем сами, чтобы не тянуть в модуль заголовки ядра Arduino.
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#endif

uint8_t calc_bits_in_mask(uint32_t mask)
{
	uint8_t bits = 0;
	while(mask) {
		bits += mask & 1;
		mask >>= 1;
	}
	return bits;
}

// разбор строки побайтно ОШИБКИ ПЛОХО не ловит!
//  для IP          const char* ipStr = "50.100.150.200"; uint8_t ip[4]; parseBytes(ipStr, '.', ip, 4, 10);
//  для mac address const char* macStr = "90-A2-AF-DA-14-11"; uint8_t mac[6]; parseBytes(macStr, '-', mac, 6, 16);
bool parseBytes(const char* str, char sep, uint8_t* bytes, int maxBytes, int base) 
{ int i,x;
  char y;
    for (i = 0; i < maxBytes; i++) {
        y=str[0];
        x = strtoul(str, NULL, base);          // Convert uint8_t
        if (x>255) return false;               // Значение байта не верно
        if ((x==0)&&(y!='0')) return false;    // Значение байта не верно
        bytes[i]=x;
        str = strchr(str, sep);               // Find next separator
        if (str == NULL || *str == '\0') {
            break;                            // No more separators, exit
        }
        str++;                                // Point to next character after separator
    }
 if (i<maxBytes-1) return false; else return true;   
}

// разбор строки с разделителями в int16_t
bool parseInt16_t(const char* str, char sep, int16_t* ints, int maxNum, int base) 
{ int i,x;
  char y;
    for (i = 0; i < maxNum; i++) {
        y=str[0];
        x = strtoul(str, NULL, base);          // Convert uint8_t
        if ((x==0)&&(y!='0')) return false;    // Значение байта не верно
        ints[i]=x;
        str = strchr(str, sep);               // Find next separator
        if (str == NULL || *str == '\0') {
            break;                            // No more separators, exit
        }
        str++;                                // Point to next character after separator
    }
 if (i<maxNum-1) return false; else return true;   
}

// Замена символа в строке
void str_replace(char *str, char find, char replace)
{
	while(*str) {
		if(*str == find) *str = replace;
		str++;
	}
}

// При ошибке возвращает магическое число -9876543.00
//#define ATOF_ERROR -9876543.00
// Цифры копятся целым числом, дробная часть - одним делением в конце: деление точных чисел
// округляется правильно, и "0.001" даёт ровно 0.001f. Прежнее деление множителя на 10 после
// каждой цифры копило ошибку: "0.001" выходило меньше 0.001f, и граница реестра SR(..., 0.001f, ...)
// отвергала собственное значение.
float my_atof(const char* s){
  float rez = 0, div = 1;
  bool neg = false;
  uint8_t digits = 0;                   // была ли хоть одна цифра
  if (*s == '-') { s++;   neg = true; }; // Знак минус
  for (int point_seen = 0; *s; s++){
    if (*s == '.'){  point_seen = 1;  continue;  }; // Дробная часть
    int d = *s - '0';
    if (d >= 0 && d <= 9){
      if (point_seen) div *= 10.0f;
      rez = rez * 10.0f + (float)d;
      digits++;
    }
    else return ATOF_ERROR;   // ошибка преобразования
  };
  // Пустая строка, "-", "." и "-." - ошибка, а не 0.0: иначе пустой параметр не отличить
  // от честного нуля. Самое опасное место - set_sensorIP (WebApiGet.cpp): температура
  // от удалённого датчика проверяется только на диапазон, а ноль в него попадает, и
  // пустое поле записалось бы как правдоподобные 0.00 °C.
  if (!digits) return ATOF_ERROR;
  return (neg ? -rez : rez) / div;
};

// Decimal (дробное приведенное к целому) в строку, precision: 1..10000
void _dtoa(char *outstr, int val, int precision)
{
    while(*outstr) outstr++;
    dptoa(outstr, val, precision);
}

// ---- float в *char в строку ДОБАВЛЯЕТСЯ значение экономим место и скорость и стек ----
uint8_t _ftoa(char *outstr, float val, unsigned char precision)
{
    while(*outstr) outstr++;
	char *instr = outstr;

	// compute the rounding factor and fractional multiplier
	float roundingFactor = 0.5f;
	unsigned long mult = 1;
	unsigned char padding = precision;
	while(precision--) {
		roundingFactor /= 10.0f;
		mult *= 10;
	}
	if(val < 0.0f){
		*outstr++ = '-';
		val = -val;
	}
	val += roundingFactor;
	outstr += i10toa((long)val, outstr, 0);
	if(padding > 0) {
		*(outstr++) = '.';
		outstr += i10toa((val - (long)val) * mult, outstr, padding);
	}
	return outstr - instr;
}

//int в *char в строку ДОБАВЛЯЕТСЯ значение экономим место и скорость и стек radix=10
char* _itoa( int value, char *string)
{
	char *ret = string;
    while(*string) string++;

	char *pbuffer = string;
	unsigned char	negative = 0;

	if (value < 0) {
		negative = 1;
		value = -value;
	}

	/* This builds the string back to front ... */
	do {
		*(pbuffer++) = '0' + value % 10;
		value /= 10;
	} while (value > 0);

	if (negative)
		*(pbuffer++) = '-';

	*(pbuffer) = '\0';

	/* ... now we reverse it (could do it recursively but will
	 * conserve the stack space) */
	uint32_t len = (pbuffer - string);
	for (uint32_t i = 0; i < len / 2; i++) {
		char j = string[i];
		string[i] = string[len-i-1];
		string[len-i-1] = j;
	}

	return ret; 
}

// Сборка float из двух 16-битных слов, как их отдаёт Modbus: первым идёт
// СТАРШЕЕ слово (регистр N), вторым младшее (регистр N+1).
//
// Первый аргумент кладётся в i[1], то есть на Cortex-M3 (little-endian) попадает
// в СТАРШУЮ половину float; регистры 0 и 1 передаются по порядку.
float fromInt16ToFloat(uint16_t wordHigh, uint16_t wordLow)
{
union  float_int {
	             float f;
	             uint16_t i[2];
                 } float_map;

float_map.i[0]=wordLow;
float_map.i[1]=wordHigh;
return float_map.f;
}

// ---- Преобразование строкового расписания в бинарный массив ----
// Одно 32-битное число на день, массив из 7 дней недели, бит j = час j.
//
// Формат входной строки - ровно тот же, что выдаёт get_Schedule: 24 символа
// '0'/'1' на день и разделитель '/' после каждого дня, всего 175 символов.
// Другие разделители ("," и ";") функция отвергает (проверено тестом set/get
// кругорейсом).
// В случае успеха возвращает true.
bool set_Schedule(char *c, uint32_t *sh)
{
  uint32_t t[7];  
  uint16_t i,j, pos=0;

  if (strlen(c)<175) return false;                          // длина входной строки меньше  24*7+7=175 байт, явная ошибка
  for (i=0;i<7;i++)
  {
     t[i]=0;  
     for (j=0;j<24;j++)
     { 
       if (c[pos]=='1') t[i]=t[i]|(0x1u <<j);
       else if (c[pos]!='0') return false;                  // символ не ноль
       pos++;
     }
     if (c[pos]!='/')  return false;                        // не правильный разделитель
     pos++;
  }  
  // Дошли до сюда ошибок нет
  for (i=0;i<7;i++)  sh[i]=t[i];  // копируем результат
  return true;
}

// перевод расписания в символьный вид
char * get_Schedule(uint32_t *sh)
{
	uint16_t i, j, pos = 0;
	static char buf[24 * 7 + 7 + 1];

	for(i = 0; i < 7; i++) {
		for(j = 0; j < 24; j++) {
			if(((0x1u << j) & sh[i]) > 0) buf[pos] = '1';
			else buf[pos] = '0';
			pos++;
		}
		buf[pos] = '/';                    // разделитель
		pos++;
	}
	buf[pos] = 0; // конец строки
	return buf;
}

// ---- base64 -хеш функция ----
/* Copyright (c) 2013 Adam Rudd. */
const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "abcdefghijklmnopqrstuvwxyz"
                            "0123456789+/";

/* 'Private' declarations */
inline void a3_to_a4(unsigned char * a4, unsigned char * a3);
inline void a4_to_a3(unsigned char * a3, unsigned char * a4);
inline unsigned char b64_lookup(char c);

int base64_encode(char *output, char *input, int inputLen) {
  int i = 0, j = 0;
  int encLen = 0;
  unsigned char a3[3];
  unsigned char a4[4];

  while(inputLen--) {
    a3[i++] = *(input++);
    if(i == 3) {
      a3_to_a4(a4, a3);

      for(i = 0; i < 4; i++) {
        output[encLen++] = pgm_read_byte(&b64_alphabet[a4[i]]);
      }

      i = 0;
    }
  }

  if(i) {
    for(j = i; j < 3; j++) {
      a3[j] = '\0';
    }

    a3_to_a4(a4, a3);

    for(j = 0; j < i + 1; j++) {
      output[encLen++] = pgm_read_byte(&b64_alphabet[a4[j]]);
    }

    while((i++ < 3)) {
      output[encLen++] = '=';
    }
  }
  output[encLen] = '\0';
  return encLen;
}

int base64_decode(char * output, char * input, int inputLen) {
  int i = 0, j = 0;
  int decLen = 0;
  unsigned char a3[3];
  unsigned char a4[4];


  while (inputLen--) {
    if(*input == '=') {
      break;
    }

    a4[i++] = *(input++);
    if (i == 4) {
      for (i = 0; i <4; i++) {
        a4[i] = b64_lookup(a4[i]);
      }

      a4_to_a3(a3,a4);

      for (i = 0; i < 3; i++) {
        output[decLen++] = a3[i];
      }
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 4; j++) {
      a4[j] = '\0';
    }

    for (j = 0; j <4; j++) {
      a4[j] = b64_lookup(a4[j]);
    }

    a4_to_a3(a3,a4);

    for (j = 0; j < i - 1; j++) {
      output[decLen++] = a3[j];
    }
  }
  output[decLen] = '\0';
  return decLen;
}

int base64_enc_len(int plainLen) {
  int n = plainLen;
  return (n + 2 - ((n + 2) % 3)) / 3 * 4;
}

int base64_dec_len(char * input, int inputLen) {
  int i = 0;
  int numEq = 0;
  for(i = inputLen - 1; input[i] == '='; i--) {
    numEq++;
  }

  return ((6 * inputLen) / 8) - numEq;
}

inline void a3_to_a4(unsigned char * a4, unsigned char * a3) {
  a4[0] = (a3[0] & 0xfc) >> 2;
  a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
  a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
  a4[3] = (a3[2] & 0x3f);
}

inline void a4_to_a3(unsigned char * a3, unsigned char * a4) {
  a3[0] = (a4[0] << 2) + ((a4[1] & 0x30) >> 4);
  a3[1] = ((a4[1] & 0xf) << 4) + ((a4[2] & 0x3c) >> 2);
  a3[2] = ((a4[2] & 0x3) << 6) + a4[3];
}

inline unsigned char b64_lookup(char c) {
  if(c >='A' && c <='Z') return c - 'A';
  if(c >='a' && c <='z') return c - 71;
  if(c >='0' && c <='9') return c + 4;
  if(c == '+') return 62;
  if(c == '/') return 63;
  return -1;
}

/*
  Name  : CRC-16
  Poly  : 0x8005    x^16 + x^15 + x^2 + 1
  Init  : 0xFFFF
  Revert: true
  XorOut: 0x0000
  Check : 0x4B37 ("123456789")
  MaxLen: 4095 байт (32767 бит) - обнаружение одинарных, двойных, тройных и всех нечетных ошибок
*/
const uint16_t Crc16Table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

// defaults crc = 0xFFFF
uint16_t calc_crc16(unsigned char * pcBlock, unsigned short len, uint16_t crc)
{
    while (len--)
        crc = (crc >> 8) ^ Crc16Table[(crc & 0xFF) ^ *pcBlock++];
    return crc;
}

// Не static: функцию зовут другие единицы трансляции (HeatPumpSettings, MQTT, Nextion,
// Profile, Scheduler и др.), всего 14 вызовов.
uint16_t _crc16(uint16_t crc, uint8_t a)
{
  crc = (crc >> 8) ^ Crc16Table[(crc & 0xFF) ^ a];
  return crc;
} 

// Роль user видит только страницы обзора и статику морды. Раньше проверялось лишь расширение .html, причём ДО urldecode:
// "lan%2Ehtml" проходил проверку и после декодирования отдавал lan.html, а файлы без .html (settings.txt и settings.bin
// с паролями, журнал, графики, история, прошивочные файлы) роли user отдавались вообще без проверки.
// Теперь имя проверяется после urldecode и по белому списку: три страницы (и их заглушки) плюс скрипты, стили и картинки.
bool web_user_file_ok(const char *name)
{
	static const char *const pages[] = {"index.html", "plan.html", "about.html", "_index.html", "_plan.html", "_about.html"};
	if(!name || !*name || strstr(name, "..") || strchr(name, '\\')) return false;
	// Файлы, которые readFileSD не читает с карты, а ГЕНЕРИРУЕТ или переписывает по имени. Расширение у них проверку проходило:
	// "stats_2026.csv.png" readFileSD превращал в stats_2026.dat и отдавал файл целиком, а "TEST.SD:x.png" занимал шину на секунды.
	// Белый список по расширению к таким именам неприменим - закрыты по имени. state/settings/journal/chart сравниваются в readFileSD
	// ТОЧНО (settings.js - обычный файл морды, он нужен страницам user), статистика, история и TEST.* - по префиксу.
	static const char *const exact[] = {"state.txt", "settings.txt", "settings.bin", "settings_eeprom.bin", "journal.txt", "chart.csv"};
	for(uint8_t i = 0; i < sizeof(exact) / sizeof(exact[0]); i++) if(strcmp(name, exact[i]) == 0) return false;
	static const char *const prefix[] = {"stats_", "hist_", "hist2_", "TEST."};
	for(uint8_t i = 0; i < sizeof(prefix) / sizeof(prefix[0]); i++) if(strncmp(name, prefix[i], strlen(prefix[i])) == 0) return false;
	if(strchr(name, ':')) return false;
	for(uint8_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) if(strcmp(name, pages[i]) == 0) return true;
	const char *ext = strrchr(name, '.');
	if(!ext || strchr(ext, '/')) return false;
	static const char *const stat[] = {".js", ".css", ".png", ".jpg", ".ico", ".svg"};
	for(uint8_t i = 0; i < sizeof(stat) / sizeof(stat[0]); i++) if(strcmp(ext, stat[i]) == 0) return true;
	return false;
}

// Сравнение значения заголовка Authorization (или хвоста "&&!Z...") с хешем целиком. Сравнение по префиксу
// (strncmp по длине эталона) пропускало любое продолжение: base64 "admin:abc" - префикс base64 "admin:abcd",
// поэтому к настоящему паролю можно было дописать лишние знаки. После значения обязан идти конец заголовка или адреса.
// Нулевая длина эталона не совпадает ни с чем (strncmp с длиной 0 давал совпадение всегда).
bool web_auth_match(const char *got, const char *hash, uint16_t hashLen)
{
	if(!got || !hash || !hashLen) return false;
	uint8_t diff = 0;
	for(uint16_t i = 0; i < hashLen; i++) {
		if(!got[i]) return false;
		diff |= (uint8_t)(got[i] ^ hash[i]);      // без раннего выхода по первому отличию
	}
	char e = got[hashLen];
	return diff == 0 && (e == '\0' || e == '\r' || e == '\n' || e == ' ' || e == '&');
}

// Перевод строки в кодировке URL в UTF8, len - максимальная длина строки приемника + '\0'
void urldecode(char *dst, char *src, uint16_t len)
{       char a, b;
        uint16_t i=0;
        while (*src) {
			if (*src == '%') {
				a = src[1];
				b = src[2];
				if(a >= '0' && a <= '9') a -= '0';
				else {
					a &= ~0x20;
					if(a >= 'A' && a <= 'F') a -= 'A' - 10; else goto xCopyChar;
				}
				if(b >= '0' && b <= '9') b -= '0';
				else {
					b &= ~0x20;
					if(b >= 'A' && b <= 'F') b -= 'A' - 10; else goto xCopyChar;
				}
				*dst++ = 16*a+b; i++;
				src+=3;
			} else if (*src == '+') {
				*dst++ = ' '; i++;
				src++;
			} else {
xCopyChar:
				*dst++ = *src++; i++;
			}
			if (i >= len-1) break;    // Не забываем про конец строки!
        }
        
       *dst = '\0';  // Добавить конец строки
}

// Заполняет и выбирает нужный элемент (нумерация c 0) для тега select
// Формат select: "Первый элемент:0;Второй элемент:0;Третий элемент:0;"
char *web_fill_tag_select(char *str, const char *select, uint8_t selected)
{
	char *outstr = str + m_strlen(str);
	strcat(outstr, select);
	do {
		if((outstr = strchr(++outstr, ';')) == NULL) break;
	} while(selected--);
	if(outstr != NULL) *(outstr-1) = '1';
	return str;
}

// memcpy: <size[uint8_t: 1|2]><struct>
void load_struct(void *to, uint8_t **from, uint16_t to_size)
{
	uint16_t size;
	memcpy(&size, *from, sizeof(size));   // запись не выровнена: разыменование uint16_t* по нечётному адресу - неопределённое поведение (ловит UBSan)
	if((size & 1) == 0) size &= 0xFF; else (*from)++;
	(*from)++;
	size >>= 1;
	if(to != NULL) memcpy(to, *from, size <= to_size ? size : to_size);
	*from += size;
}

int8_t Record_WriteStable(RecWriteFn wr, void *ctx, const uint8_t *src, uint16_t size, uint16_t *crc, uint8_t attempts)
{
	while(attempts--) {
		uint16_t before = calc_crc16((unsigned char *) src, size, *crc);
		if(wr(ctx, src, size)) return 1;
		uint16_t after = calc_crc16((unsigned char *) src, size, *crc);
		if(before == after) { *crc = after; return 0; }
	}
	return 2;
}

// Заголовок записи нового формата. Порядок байт - как у платформы, теми же
// memcpy, что и везде в разборе: выравнивание записи не гарантировано.
void rec_hdr_put(uint8_t *buf, uint16_t tag, uint16_t len)
{
	memcpy(buf, &tag, sizeof(tag));
	memcpy(buf + sizeof(tag), &len, sizeof(len));
}

uint16_t rec_hdr_tag(const uint8_t *buf)
{
	uint16_t tag;
	memcpy(&tag, buf, sizeof(tag));
	return tag;
}

uint16_t rec_hdr_len(const uint8_t *buf)
{
	uint16_t len;
	memcpy(&len, buf + sizeof(uint16_t), sizeof(len));
	return len;
}

SettingsHead settings_head_check(uint16_t size, uint8_t ver, uint8_t max_ver, uint16_t min_size)
{
	if(size == 0 || size == 0xFFFF || size < min_size) return SETHEAD_EMPTY;
	if(ver == 0 || ver == 0xFF) return SETHEAD_EMPTY;
	if(ver > max_ver) return SETHEAD_NEWER;
	return SETHEAD_OK;
}

bool SockJanitor_Step(uint32_t *since_ms, uint16_t *last_id, uint16_t id, bool candidate, bool in_use, uint32_t now_ms, uint32_t limit_ms)
{
	if(id != *last_id) { *last_id = id; *since_ms = now_ms; return false; }      // на сокете уже другой клиент (id - порт клиента): отсчёт заново
	if(!candidate || in_use) { *since_ms = now_ms; return false; }              // обслуживается или не в подозрительном состоянии: отсчёт заново
	return (uint32_t)(now_ms - *since_ms) > limit_ms;
}

int8_t WebSock_Owner(bool have_client, int8_t sock)
{
	return have_client ? sock : (int8_t) -1;
}

bool WebSock_Busy(int8_t sock, const int8_t *owners, uint8_t n)
{
	for(uint8_t i = 0; i < n; i++) if(owners[i] == sock) return true;
	return false;
}

void SpiRead_Begin(SpiReadStat *r)
{
	r->size = 0; r->us = 0; r->crc = 0xFFFF; r->res = 1;
}

bool SpiRead_Step(SpiReadStat *r, const uint8_t *buf, int n, uint32_t us)
{
	r->us += us;
	if(n < 0) { r->res = -1; return false; }
	if(n == 0) { if(r->us == 0) r->us = 1; return false; }    // время 0 (крошечный файл) не даёт делить на ноль при подсчёте скорости
	r->crc = calc_crc16((unsigned char *) buf, n, r->crc);
	r->size += n;
	return true;
}

uint8_t SD_BlockRun(uint32_t first, uint32_t last, uint32_t ram1, uint32_t ram2, uint8_t maxn)
{
	if(first == ram1 || first == ram2) return 0;
	if(maxn == 0) maxn = 1;
	uint8_t n = 1;
	for(uint32_t b = first; n < maxn && b < last && b + 1 != ram1 && b + 1 != ram2; b++) n++;   // b < last: следующий блок есть (без переполнения b + 1)
	return n;
}

SendWait Send_StallStep(uint16_t free, uint16_t *last_free, uint32_t now_ms, uint32_t *progress_ms, uint32_t limit_ms)
{
	if(free > *last_free) {                        // клиент подтвердил данные: буфер освободился
		*last_free = free;
		*progress_ms = now_ms;
		return SW_PROGRESS;
	}
	*last_free = free;                             // не вырос (или даже уменьшился): запоминаем, чтобы следующий рост считался от него
	return (uint32_t)(now_ms - *progress_ms) >= limit_ms ? SW_STALLED : SW_WAIT;
}

void StatAcc_Add(StatAcc *a, uint32_t v)
{
	a->n++;
	a->sum += v;
	if(v > a->max) a->max = v;
}

uint32_t StatAcc_Avg(const StatAcc *a)
{
	return a->n ? (uint32_t)(a->sum / a->n) : 0;
}

bool SockStat_Due(const SockStat *s, uint32_t now_ms, uint32_t period_ms)
{
	return (uint32_t)(now_ms - s->lastTick) >= period_ms;
}

uint8_t SockState_Index(uint8_t r)
{
	switch(r) {
	case 0x00: return SS_CLOSED;
	case 0x14: return SS_LISTEN;
	case 0x16: return SS_SYNRECV;
	case 0x17: return SS_EST;
	case 0x18: return SS_FINWAIT;
	case 0x1A: return SS_CLOSING;
	case 0x1B: return SS_TIMEWAIT;
	case 0x1C: return SS_CLOSEWAIT;
	case 0x1D: return SS_LASTACK;
	default:   return SS_OTHER;
	}
}

void SockStat_Sample(SockStat *s, const uint8_t *sn_sr, uint8_t n, uint32_t now_ms)
{
	uint8_t listen = 0;
	for(uint8_t i = 0; i < n; i++) {
		const uint8_t k = SockState_Index(sn_sr[i]);
		s->st[k]++;
		if(k == SS_LISTEN) listen++;
	}
	s->lastTick = now_ms;
	s->samples++;
	s->listen[listen > 7 ? 7 : listen]++;
	if(listen == 0) {
		if(!s->inZero) { s->inZero = true; s->zeroSince = now_ms; }          // период без слушающих сокетов начался
	} else if(s->inZero) {                                                   // закончился: длина = от первой выборки без LISTEN до первой с LISTEN
		s->inZero = false;
		s->zeroEvents++;
		uint32_t d = (uint32_t)(now_ms - s->zeroSince);
		if(d > s->zeroMax) s->zeroMax = d;
	}
}

uint32_t flow_freq_mhz(uint32_t edges, uint32_t dt_ms)
{
	if(!dt_ms) return 0;
	uint64_t f = (uint64_t) edges * 500000ULL / dt_ms;
	return f > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (uint32_t) f;
}

uint32_t flow_lph(uint32_t freq_mhz, uint16_t kf)
{
	return kf < FLOW_KF_MIN ? 0 : (uint32_t)((uint64_t) freq_mhz * 360 / kf);
}

int16_t fc_gen_clip(int16_t x, int16_t gen_max, int16_t min)
{
	int16_t top = gen_max > min ? gen_max : min;
	return x > top ? top : x;
}

int16_t fc_gen_freq_fix(int16_t stored, int16_t def)
{
	return stored > 0 ? stored : def;
}

uint16_t url_encode_append(char *dst, uint16_t cap, const char *src)
{
	static const char hex[] = "0123456789ABCDEF";
	uint16_t n = 0;
	while(n < cap && dst[n]) n++;                                // длина уже лежащего
	if(n >= cap) { if(cap) dst[cap - 1] = 0; return cap ? cap - 1 : 0; }
	for(; *src; src++) {
		uint8_t c = (uint8_t) *src;
		bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
		uint8_t need = (plain || c == ' ') ? 1 : 3;
		if(n + need + 1 > cap) break;                            // нужен ещё байт под нуль; символ не режем
		if(plain) dst[n++] = (char) c;
		else if(c == ' ') dst[n++] = '+';
		else { dst[n++] = '%'; dst[n++] = hex[c >> 4]; dst[n++] = hex[c & 15]; }
	}
	dst[n] = 0;
	return n;
}

bool http_request_tail(char *dst, uint16_t cap, const char *host)
{
	static const char a[] = " HTTP/1.0\r\nHost: ", b[] = "\r\nConnection: close\r\n\r\n";
	uint16_t n = strlen(dst), h = strlen(host);
	if((uint32_t) n + (sizeof(a) - 1) + h + (sizeof(b) - 1) + 1 > cap) return false;
	memcpy(dst + n, a, sizeof(a) - 1); n += sizeof(a) - 1;
	memcpy(dst + n, host, h); n += h;
	memcpy(dst + n, b, sizeof(b));                               // вместе с нулём
	return true;
}

const char *http_body(const char *resp)
{
	if(strncmp(resp, "HTTP/", 5) != 0) return resp;
	const char *b = strstr(resp, "\r\n\r\n");
	return b ? b + 4 : resp + strlen(resp);
}

bool settings_len_ok(uint16_t size, int32_t avail)
{
	if(size < 2) return false;                                  // меньше контрольной суммы: size -= 2 заворачивается
	return (int32_t) size + 2 <= avail;                         // поле длины + size байт обязаны уместиться в принятое
}

void settings_scan(const uint8_t *p, const uint8_t *p_max, uint16_t end_tag, uint16_t max_tag, SettingsScan *s)
{
	memset(s, 0, sizeof(*s));
	while(p + REC_HDR_SIZE <= p_max) {
		uint16_t tag = rec_hdr_tag(p);
		uint16_t len = rec_hdr_len(p);
		const uint8_t *data = p + REC_HDR_SIZE;
		if(data + len > p_max) { s->truncated++; break; }
		p = data + len;
		if(tag == end_tag) { s->end_seen = true; break; }
		s->records++;
		if(tag > max_tag) s->unknown++;
	}
}

void profile_scan(const uint8_t *p, const uint8_t *p_max, uint16_t tag, uint16_t end_tag,
                  uint16_t rec_len, uint8_t max_slots, ProfileScan *s)
{
	memset(s, 0, sizeof(*s));
	const uint8_t *p0 = p;
	while(p + REC_HDR_SIZE <= p_max) {
		uint16_t t = rec_hdr_tag(p), l = rec_hdr_len(p);
		const uint8_t *data = p + REC_HDR_SIZE;
		if(t == end_tag) { s->end_seen = true; p = data; break; }
		if(t != tag) { s->bad_tag++; break; }
		if(data + l > p_max) { s->truncated++; break; }
		if(l != rec_len || l == 0) s->bad_len++;
		else if(data[0] >= max_slots) s->bad_slot++;
		else s->slots++;
		p = data + l;
	}
	s->bytes = (uint32_t)(p - p0);
}

// Round float * mul, mul: 10, 100, 1000
// Округление к ближайшему, половина - от нуля. Прежде к num*mul прибавлялось 0.0005/0.005/0.05 вместо 0.5 -
// это почти усечение: «32.767» (32766.998 во float) сохранялось как 32766.
int16_t rd(float num, int16_t mul)
{
	return num * mul + (num < 0 ? -0.5f : 0.5f);
}

// format int/div value to string with decimal point
void int_to_dec_str(int32_t value, int32_t div, char **ret, uint8_t maxfract)
{
	*ret += m_itoa(value / div, *ret, 10, 0);
	if(div > 1 && maxfract) {
		value = abs(value) % div;
		if(value == 0) return;
		**ret = '.';
		m_itoa(value, ++*ret, 10, maxfract);
		*ret += maxfract;
		**ret = '\0'; // max after dot
	}
}

// округление к ближайшему целому, div: 10 / 100 / 1000 / 10000
int32_t round_div_int32(int32_t value, int16_t div)
{
	if(value >= 0) {
		if(value % div >= (div >> 1)) value = value / div + 1; else value /= div;
	} else {
		if(value % div <= -(div >> 1)) value = value / div - 1; else value /= div;
	}
	return value;
}

// ---------------------------------------------------------------------------
// Общий буфер вывода и шестнадцатеричные преобразования.
//
// ВНИМАНИЕ: буфер ОДИН на всех. Пользоваться только в веб-потоке или под
// замком xWebThreadSemaphore - два вызова подряд из разных задач затрут
// друг друга. Кроме этих функций в буфер пишет ещё ResetCause.
// ---------------------------------------------------------------------------
char _buf[UTIL_BUF_SIZE];

static const char codeHex[] = "0123456789ABCDEF";

// MAC-адрес в строку вида 01:23:45:67:89:AB
char *MAC2String(uint8_t *mac)
{
	m_snprintf(_buf, sizeof(_buf), "%02X:%02X:%02X:%02X:%02X:%02X",
	           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return _buf;
}

// Адрес OneWire (восемь байт) в шестнадцатеричную строку
char *addressToHex(uint8_t *f)
{
	uint8_t i = 0;
	for(; i < 8; i++) {
		_buf[i * 2]     = codeHex[0x0f & (f[i] >> 4)];
		_buf[i * 2 + 1] = codeHex[0x0f & f[i]];
	}
	_buf[i * 2] = 0;
	return _buf;
}

// Байт в текстовую строку вида 0xf5
char *byteToHex(uint8_t f)
{
	_buf[0] = '0';
	_buf[1] = 'x';
	_buf[2] = codeHex[0x0f & (f >> 4)];
	_buf[3] = codeHex[0x0f & f];
	_buf[4] = 0;
	return _buf;
}

// uint16_t в текстовую строку вида 0xf50e
char *uint16ToHex(uint16_t f)
{
	_buf[0] = '0';
	_buf[1] = 'x';
	_buf[2] = codeHex[0x0f & (f >> 12)];
	_buf[3] = codeHex[0x0f & (f >> 8)];
	_buf[4] = codeHex[0x0f & (f >> 4)];
	_buf[5] = codeHex[0x0f & f];
	_buf[6] = 0;
	return _buf;
}

// uint32_t в текстовую строку вида 0xabcdf50e
char *uint32ToHex(uint32_t f)
{
	_buf[0] = '0';
	_buf[1] = 'x';
	for(uint8_t i = 0; i < 8; i++) _buf[2 + i] = codeHex[0x0f & (f >> (28 - 4 * i))];
	_buf[10] = 0;
	return _buf;
}

// ---------------------------------------------------------------------------
// Режим загрузки, выбранный кнопкой KEY1 при включении.
//
// Решение отделено от измерения намеренно: измерять приходится выводом и
// задержками в setup, а сами пороги - чистая арифметика, её проверяет
// tests/test_util.cpp. Вход - сколько кнопку продержали; цикл ожидания в
// Control.ino выходит либо по отпусканию, либо по достижению верхнего порога,
// поэтому отдельного признака "отпущена" здесь не нужно.
KeyBootMode key_boot_mode(uint32_t held_ms)
{
	if(held_ms >= KEY_HOLD_SAFENET) return KEY_BOOT_SAFENET;
	if(held_ms >= KEY_PRESS_MIN) return KEY_BOOT_MINWEB;
	return KEY_BOOT_NONE;    // касание или дребезг - нажатия не было
}
