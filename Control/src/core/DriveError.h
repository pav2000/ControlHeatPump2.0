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

#ifndef Control_DriveError_h
#define Control_DriveError_h

#include "ErrorCatalog.h"

enum class ErrorDetailKind : uint8_t {
	None, // Дополнительных данных нет
	Fault, // Собственный код отказа привода
	Exception, // Байт исключения ведомого Modbus
	Transport, // Статус транспорта Modbus
	Local, // Внутренний статус контроллера обмена
	State, // Прочитанное состояние привода
};

struct ErrorDetail {
	ErrorDetailKind kind; // Тип дополнительных данных
	uint16_t value; // Исходный код или состояние, заданное типом
	uint16_t state; // Состояние Omron в записи отказа
};

struct ErrorEventInfo {
	ErrorCatalog::ErrCode code; // Номер события или 0 при отсутствии активной аварии
	ErrorDetail detail;         // Исходные данные устройства, если они подтверждены
	char source[16];            // Короткое имя устройства или операции
};

struct ErrorEvent : ErrorEventInfo {
	char text[161]; // Полная строка журнала без отметки времени
};

// Возвращает имя подтверждённого исходного кода для API и MQTT либо nullptr.
inline const char *ErrorDetailToken(ErrorDetailKind kind)
{
	switch(kind) {
	case ErrorDetailKind::Fault: return "FAULT";
	case ErrorDetailKind::Exception: return "EXC";
	case ErrorDetailKind::Transport: return "RX";
	default: return nullptr;
	}
}

// Убирает неполный символ UTF-8 с конца строки после форматирования в ограниченный буфер.
inline void ErrorTrimUtf8(char *text)
{
	char *end = text;
	while(*end) ++end;
	if(end == text) return;
	char *lead = end - 1;
	while(lead > text && (static_cast<uint8_t>(*lead) & 0xC0) == 0x80) --lead;
	const uint8_t byte = static_cast<uint8_t>(*lead);
	const int width = byte < 0x80 ? 1 : byte < 0xE0 ? 2 : byte < 0xF0 ? 3 : 4;
	if(end - lead < width) *lead = 0;
}

enum class ModbusKind : uint8_t {
	Success, // Запрос выполнен
	Exception, // Ведомый вернул валидное исключение
	Transport, // Ответ потерян или повреждён
	Busy, // Замок шины занят
	Unavailable, // Интерфейс недоступен
	Local, // Запрос отклонён внутри контроллера
	EchoMismatch, // Контрольное слово Omron не совпало
};

struct ModbusResult {
	int8_t status; // Знаковый результат операции для действующих вызывающих
	ModbusKind kind; // Классификация результата
	uint8_t raw; // Исходный байт статуса или исключения

	// Возвращает прежний знаковый статус для вызывающего кода, проверяющего результат операции.
	operator int8_t() const { return status; }
};

struct InverterLink {
	uint8_t failures; // Число последовательных транспортных сбоев
	uint32_t lastReply; // Монотонное время последнего валидного ответа, мс

	// По типу обмена обновляет число транспортных сбоев и время последнего ответа now.
	void Observe(ModbusKind kind, uint32_t now)
	{
		switch(kind) {
		case ModbusKind::Transport:
			if(failures < UINT8_MAX) ++failures;
			break;
		case ModbusKind::Success:
		case ModbusKind::Exception:
		case ModbusKind::EchoMismatch:
			failures = 0;
			lastReply = now;
			break;
		default: break;
		}
	}
};

// Возвращает код события по типу Modbus. Для транспорта можно задать код по контексту.
inline ErrorCatalog::Code ModbusAlarm(ModbusKind kind, ErrorCatalog::Code transport = ErrorCatalog::kMODBUS_EXCHANGE)
{
	switch(kind) {
	case ModbusKind::Exception: return ErrorCatalog::kMODBUS_EXCEPTION;
	case ModbusKind::Transport: return transport;
	case ModbusKind::Busy: return ErrorCatalog::kMODBUS_BUSY;
	case ModbusKind::Unavailable: return ErrorCatalog::kMODBUS_UNAVAILABLE;
	case ModbusKind::EchoMismatch: return ErrorCatalog::kFC_INIT;
	default: return ErrorCatalog::kMODBUS_UNKNOWN;
	}
}

// Возвращает исходный байт результата с типом или пустые детали для локального статуса без байта.
inline ErrorDetail ModbusDetail(ModbusResult result)
{
	switch(result.kind) {
	case ModbusKind::Exception: return {ErrorDetailKind::Exception, result.raw, 0};
	case ModbusKind::Transport: return {ErrorDetailKind::Transport, result.raw, 0};
	case ModbusKind::Local: return {ErrorDetailKind::Local, result.raw, 0};
	default: return {};
	}
}

// Проверяет, что счётчик вырос на один с previousCount до count и остался таким после чтения.
// Возвращает true, если fault ненулевой и счётчик подтверждает текущий отказ.
inline bool OmronCurrentFault(uint16_t previousCount, uint16_t count, uint16_t after, uint16_t fault)
{
	return previousCount < 65530 && count == previousCount + 1 && after == count && fault != 0;
}

#endif
