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
 * Тексты ошибок и точек возврата. Обоснование и интерфейс - ErrorCodes.h.
 *
 * Отдельная единица трансляции: ни HeatPump, ни FreeRTOS, ни железа.
 * Таблицы объявлены static - наружу они не видны, доступ только через
 * функции с проверкой границ, поэтому рассинхронизацию индекса и текста
 * нельзя внести из другого файла.
 */
#include "mini-printf.h"
#include "ErrorCodes.h"

// ОБЕ таблицы ниже - позиционные: n-я строка описывает n-й код. Соответствие
// поддерживается вручную, поэтому вставка строки в середину сдвигает все
// последующие описания. Размеры закреплены static_assert ниже, а содержимое -
// тестом tests/test_errorcodes.cpp.

static const char * const noteError[] = {
	"Ok",  // 0
	"Выход за нижнюю границу температурного датчика",  // -1
	"Выход за верхнюю границу температурного датчика",  // -2
	"Выход за нижнюю границу датчика давления",  // -3
	"Выход за верхнюю границу датчика давления",  // -4
	"Нужный датчик отсутствует в конфигурации или не привязан",  // -5
	"Адрес датчика температуры не установлен",  // -6
	"Срабатывание контактного датчика - авария",  // -7
	"ЭРВ выход за диапазон (по шагам) вверх (отказ ЭРВ?)",  // -8
	"ЭРВ выход за диапазон (по шагам) вниз (отказ ЭРВ?)",  // -9
	"Устройство запрещено в текущей конфигурации",  // -10
	"Ошибка сброса на OneWire шине (обрыв или замыкание)",  // -11
	"ЭРВ получило отрицательный перегрев",  // -12
	"Free RTOS не может создать задачу, не хватает памяти",  // -13
	"Отсутствует датчик давления, и выбран алгоритм ЭРВ который его использует",  // -14
	"Ошибка записи настроек",  // -15
	"Ошибка чтения настроек",  // -16
	"Ошибка контрольной суммы для настроек в I2C",  // -17
	"Несовпадение размера данных при чтении настроек",  // -18
	"Настройки не найдены в I2C или версия новее прошивки",  // -19
	"Нарушена структура записей настроек",  // -20
	"Не загружены расписание или профили",  // -21
	"Ошибка контрольного чтения после записи настроек",  // -22
	"Ошибка записи счетчиков в I2C",  // -23
	"Ошибка чтения счетчиков из I2C",  // -24
	"Неверное состояние ТН",  // -25
	"Превышена разность температур на конденсаторе",  // -26
	"Превышена разность температур на испарителе (не используется)",  // -27
	"Отсутствует насос на конденсаторе, проверьте конфигурацию",  // -28
	"Отсутствует насос на испарителе, проверьте конфигурацию",  // -29
	"Ошибка чтения датчика давления (данные не готовы) (не используется)",  // -30
	"Отсутствует компрессор, проверьте конфигурацию (RCOMP и FC)",  // -31
	"Нет работы для ТН, проверьте настройки ГВС и отопления",  // -32
	"Попытка включить компрессор при ошибке (обратитесь к разработчику)",  // -33
	"Сбой внутренней конфигурации (обратитесь к разработчику)",  // -34
	"Ошибка инициализации SD карты",  // -35
	"Файл index.xxx не найден на SD карте (не используется)",  // -36
	"Ошибка чтения файла с SD карты",  // -37
	"Правило вычисления перегрева не соответствует датчикам",  // -38
	"Инвертор на шине Modbus не найден (работа инвертора запрещена)",  // -39
	"При обращении к 485 порту превышено время ожидания его освобождения",  // -40
	"Modbus error 0x01 protocol illegal function exception",  // -41
	"Modbus error 0x02 protocol illegal data address exception",  // -42
	"Modbus error 0x03 protocol illegal data value exception",  // -43
	"Modbus error 0x04 protocol slave device failure exception",  // -44
	"Modbus error 0xe0 Master invalid response slave ID exception",  // -45
	"Modbus error 0xe1 Master invalid response function exception",  // -46
	"Modbus error 0xe2 Master response timed out exception",  // -47
	"Modbus error 0xe3 Master invalid response CRC exception",  // -48
	// -49..-55: коды ОБЩИЕ для Omron MX2 и Vacon, значения у них разные.
	// Здесь лежат формулировки MX2 - инвертора текущей конфигурации.
	//
	// Разветвить таблицу по #ifdef FC_VACON здесь НЕЛЬЗЯ: этот файл - отдельная
	// единица трансляции и Config.h не включает (тот определяет объекты), поэтому
	// условие всегда молча взяло бы ветку Omron. При переходе на Vacon семь строк
	// ниже надо заменить вручную на варианты из комментариев; включённый FC_VACON
	// напомнит об этом предупреждением компилятора из ErrorCodes.h.
	"Код исключения 0x01 Указанная функция не поддерживается",  // -49, Vacon 0x05: ведомое приняло запрос и обрабатывает его
	"Код исключения 0x02 Указанная функция не обнаружена",  // -50, Vacon 0x06: ведомое занято обработкой команды
	"(код не используется)",  // -51, Vacon: у Vacon тоже не используется
	"Код исключения 0x03 неприемлемый формат данных, либо 0x05 ошибка связи по Modbus",  // -52, Vacon 0x07 не выполнить функцию либо 0x08 ошибка контроля чётности
	"Код исключения 0x21 Данные, записываемые в регистр, находятся за пределами ПЧ",  // -53, Vacon: у Vacon свободен
	"Код исключения 0x22 Указанные функции не доступны для ПЧ",  // -54, Vacon: у Vacon свободен
	"Код исключения 0x23 Регистр (бит), доступен только для чтения",  // -55, Vacon: у Vacon свободен
	"Modbus неизвестная ошибка (сбой протокола)",  // -56
	"Запрещенное (неверное) состояние инвертора",  // -57
	"Попытка включения ТН при заблокированном инверторе",  // -58
	"Алгоритм ПИД - достижение максимальной температуры подачи (защита)",  // -59
	"Не хватает памяти для выделения под данные (см. конфигурацию).",  // -60
	"Ошибка записи профиля в I2C",  // -61
	"Ошибка чтения профиля из I2C",  // -62
	"Ошибка контрольной суммы для профиля",  // -63
	"Длина записи профиля не совпала с раскладкой прошивки",  // -64
	"Мастер DS2482 не найден на шине, возможно ошибка шины I2C",  // -65
	"Мастер DS2482 не может сбросить шину OneWire бит PPD равен 0",  // -66
	"При обращении к I2C шине превышено время ожидания ее освобождения",  // -67
	"Отказ драйвера L9333 ЭРВ (сработала защита драйвера)",  // -68
	"Ошибка заголовка счетчиков в I2C",  // -69
	"Ошибка открытия журнала в I2C (инициализация чипа)",  // -70
	"Ошибка чтения журнала из I2C памяти",  // -71
	"Ошибка записи журнала в I2C память",  // -72
	"(код не используется)",  // -73
	"Поток в ПТО ниже установленного уровня (проблема - насос, фильтр)",  // -74
	"Слишком большое напряжение сети (данные SDM счетчика)",  // -75
	"Слишком большая потребляемая мощность (данные SDM счетчика)",  // -76
	"Modbus требуется, но отсутствует в конфигурации",  // -77
	"Не удалось сбросить инвертор после ошибки",  // -78
	"Отсутствует проток в испарителе (срабатывание SEVA)",  // -79
	"Попытка включить компрессор при неработающем насосе контура",  // -80
	"Попытка разморозки при невозможности переключения на охлаждение (нет R4WAY)",  // -81
	"Разморозка не завершилась за допустимое время (датчик обмерзания или испаритель)",  // -82
	"Ошибка использования аналогового управления инвертором без выхода ХОД",  // -83
	"Ошибка чтения температурного датчика (лимит попыток чтения исчерпан)",  // -84
	"Микросхема памяти I2C не та, что выбрана в Config.h",  // -85
	"Ошибка CRC чтения OneWire",  // -86
	"Ошибка чтения/записи OneWire",  // -87
	"Сбой инвертора",  // -88
	"Ошибка программы управления инвертором",  // -89
	"Ошибка записи на SD карту",  // -90
	"Невозможно остановить инвертор с помощью RCOMP",  // -91
	"Нет связи с инвертором по MODBUS",  // -92
	"Аварии ядра контроллера подряд, автопуск ТН отменён",  // -93
};

static const char * const codeRet[] ={ "none","MinPause","Bh1","Bh2","Bh3","Bh4","Bh5","Bh22","Bp3","Bp1","Bp2","Bp6","Bp7","Bp8","Bp9","Bp5","Bp10","Bp11","Bp12","Bp14","Bp16","Bp17","Bp18","Bp19","Bp20","Bp21","Bp22", "Bp23","Bp24","Bp25","Bp26","Bp27","Bp28","Bp29","Bdis","Bgen",\
                       "Hh3","Hh1","Hh2","Hh13","Hh4","Hp3","Hp1","Hp2","Hp6","Hp7","Hp8","Hp9","Hp5","Hp10","Hp11","Hp12","Hp15","Hp16","Hp17","Hp18","Hp19","Hp20","Hp21","Hp23","Hp24","Hp25","Hp26","Hp27","Hp28","Hp29",\
                       "Ch3","Ch1","Ch2","Ch13","Ch4","Cp3","Cp1","Cp2","Cp6","Cp7","Cp8","Cp9","Cp5","Cp10","Cp11","Cp12","Cp15","Cp16","Cp17","Cp18","Cp19","Cp20","Cp21","Cp23","Cp24","Cp25","Cp26","Cp27","Cp28","Cp29","null"};

#define NOTE_ERROR_COUNT (sizeof(noteError) / sizeof(noteError[0]))
#define CODE_RET_COUNT   (sizeof(codeRet) / sizeof(codeRet[0]))

// Таблица обязана покрывать весь диапазон кодов. ERR_ERRMAX - последняя
// ошибка (отрицательная), поэтому нужен индекс -ERR_ERRMAX включительно.
static_assert((int)NOTE_ERROR_COUNT > -(int)ERR_ERRMAX,
              "noteError[] короче диапазона кодов ошибок");
// codeRet индексируется значением TYPE_RET_HP, последнее рабочее - перед pEND18.
static_assert((int)CODE_RET_COUNT >= (int)pEND18,
              "codeRet[] короче перечисления TYPE_RET_HP");

bool Error_IsKnown(int8_t code)
{
	return code <= 0 && code >= ERR_ERRMAX;
}

const char *Error_Text(int8_t code)
{
	if(!Error_IsKnown(code)) {
		// Не код ошибки ТН. Чаще всего это статус, пришедший из драйвера шины
		// (см. комментарий в ErrorCodes.h). Показываем как есть, чтобы при
		// разборе аварии было видно, что источник другой.
		// m_snprintf, а не snprintf: обычный тянет за собой всю машинерию
		// форматирования из stdio - проверено, это 12.5 КБ flash. В проекте
		// нет ни одного sprintf, и нарушать это правило нельзя.
		// 48 байт, хотя результат занимает 32. m_snprintf требует ЗАПАСА
		// около 16 байт сверх результата: при меньшем буфере она молча
		// обрывает строку перед числом, не сигнализируя об этом. Измерено:
		// с буфером 40 выходило "Неизвестный код " без кода, с 48 - полностью.
		// Сама строка в UTF-8 занимает 30 байт, кириллица тут ни при чём -
		// дело в запасе на преобразование числа. Поймано тестом.
		static char unknown[48];
		m_snprintf(unknown, sizeof(unknown), "Неизвестный код %d", (int)code);
		return unknown;
	}
	return noteError[-code];
}

const char *Ret_Text(uint8_t ret)
{
	if(ret >= CODE_RET_COUNT) return "?";
	return codeRet[ret];
}

// Дописать в buf не более чем до границы, всегда оставляя место под ноль.
// Возвращает новую длину строки.
static uint16_t append(char *buf, uint16_t size, uint16_t len, const char *add)
{
	if(!add) return len;
	while(*add && len + 1 < size) buf[len++] = *add++;
	buf[len] = '\0';
	return len;
}

void Error_Store(char *source, uint16_t source_size,
                 char *note,   uint16_t note_size,
                 const char *now, const char *src, int8_t err)
{
	if(source_size) { source[0] = '\0'; append(source, source_size, 0, src); }
	if(!note_size) return;
	note[0] = '\0';
	uint16_t len = append(note, note_size, 0, now);
	len = append(note, note_size, len, " ");
	len = append(note, note_size, len, src);
	len = append(note, note_size, len, ": ");
	      append(note, note_size, len, Error_Text(err));
}

bool Error_Claim(volatile int8_t *field, int8_t code)
{
	int8_t expected = OK;
	return __atomic_compare_exchange_n(field, &expected, code, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

bool Error_IsCritical(int8_t err)
{
	for(uint8_t i = 0; i < sizeof(CRITICAL_ERRORS) / sizeof(CRITICAL_ERRORS[0]); i++)
		if(CRITICAL_ERRORS[i] == err) return true;
	return false;
}

ERROR_ACTION Error_Decide(bool hp_off, uint8_t nStart, uint8_t num_repeat,
                          bool no_restart, int8_t err)
{
	if(hp_off) return ERR_ACT_Nothing;                        // выключать нечего
	if(nStart == 0) return ERR_ACT_Stop;                      // повторные пуски запрещены
	if(no_restart && Error_IsCritical(err)) return ERR_ACT_Stop;
	return num_repeat < nStart ? ERR_ACT_Repeat : ERR_ACT_Stop;
}

bool Error_IsLinkLoss(int8_t err)
{
	return err == ERR_MODBUS_0xe0 || err == ERR_MODBUS_0xe1 || err == ERR_MODBUS_0xe2 || err == ERR_MODBUS_0xe3;
}

int8_t Error_LinkCode(int8_t err)
{
	return Error_IsLinkLoss(err) ? ERR_FC_NO_LINK : err;
}

bool Error_LinkLost(uint8_t fails, uint32_t since_ms, bool running,
                    uint8_t max_fails, uint32_t max_ms_run, uint32_t max_ms_idle)
{
	if(fails >= max_fails) return true;
	return since_ms > (running ? max_ms_run : max_ms_idle);
}

int8_t Error_TempReadCode(int8_t err)
{
	return (err == ERR_ONEWIRE_CRC || err == ERR_ONEWIRE_RW || err == ERR_ONEWIRE) ? ERR_READ_TEMP : err;
}

STOP_CHECK Error_StopCheck(bool running, uint8_t stage, uint32_t since_ms,
                           uint32_t wait_ms, uint32_t fallback_ms)
{
	if(!running) return STOPCHK_DONE;
	if(stage <= 1) return since_ms > wait_ms ? STOPCHK_FALLBACK : STOPCHK_WAIT;
	return since_ms > fallback_ms ? STOPCHK_FAIL : STOPCHK_WAIT;
}

INIT_STOP Error_InitStopWait(bool stopped, bool lost, uint32_t elapsed_ms, uint32_t limit_ms)
{
	if(stopped) return INITSTOP_DONE;
	if(lost || elapsed_ms > limit_ms) return INITSTOP_FAIL;
	return INITSTOP_WAIT;
}

uint8_t Fault_Resets(uint32_t gpbr)
{
	if((gpbr & 0xFFFF0000UL) != FAULT_MARK) return 0;
	uint32_t n = gpbr & 0xFFFF;
	return n > 255 ? 255 : (uint8_t) n;
}

uint32_t Fault_Count(uint32_t gpbr)
{
	uint32_t n = (gpbr & 0xFFFF0000UL) == FAULT_MARK ? (gpbr & 0xFFFF) : 0;
	if(n < 0xFFFF) n++;
	return FAULT_MARK | n;
}

bool Fault_NoAutoStart(uint32_t gpbr)
{
	return Fault_Resets(gpbr) >= FAULT_MAX_RESETS;
}

bool Fault_RelayOffLevel(bool relay_invert, bool r4way_invert, bool is_r4way)
{
	bool level = !relay_invert;                // без инверсии реле включается НИЗКИМ уровнем
	if(r4way_invert && is_r4way) level = !level;
	return level;
}
