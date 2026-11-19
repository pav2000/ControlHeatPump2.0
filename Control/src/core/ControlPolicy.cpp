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

// Реализует правила регистрации первой аварии, повторных пусков и диагностики оборудования.

#include <string.h>
#include "ControlPolicy.h"
#include "ErrorCatalog.h"

// Таблица коротких меток TYPE_RET_HP для журнала и интерфейса.
static const char * const codeRet[] ={ "none","MinPause","Bh1","Bh2","Bh3","Bh4","Bh5","Bh22","Bp3","Bp1","Bp2","Bp6","Bp7","Bp8","Bp9","Bp5","Bp10","Bp11","Bp12","Bp14","Bp16","Bp17","Bp18","Bp19","Bp20","Bp21","Bp22", "Bp23","Bp24","Bp25","Bp26","Bp27","Bp28","Bp29","Bdis","Bgen",\
                       "Hh3","Hh1","Hh2","Hh13","Hh4","Hp3","Hp1","Hp2","Hp6","Hp7","Hp8","Hp9","Hp5","Hp10","Hp11","Hp12","Hp15","Hp16","Hp17","Hp18","Hp19","Hp20","Hp21","Hp23","Hp24","Hp25","Hp26","Hp27","Hp28","Hp29",\
                       "Ch3","Ch1","Ch2","Ch13","Ch4","Cp3","Cp1","Cp2","Cp6","Cp7","Cp8","Cp9","Cp5","Cp10","Cp11","Cp12","Cp15","Cp16","Cp17","Cp18","Cp19","Cp20","Cp21","Cp23","Cp24","Cp25","Cp26","Cp27","Cp28","Cp29","null"};

// Размер таблицы меток точек возврата автоматики.
#define CODE_RET_COUNT   (sizeof(codeRet) / sizeof(codeRet[0]))

static_assert((int)CODE_RET_COUNT >= (int)pEND18,
              "codeRet[] короче перечисления TYPE_RET_HP");

// Возвращает описание точки возврата ret или «?» за границей таблицы.
const char *Ret_Text(uint8_t ret)
{
	if(ret >= CODE_RET_COUNT) return "?";
	return codeRet[ret];
}

// Возвращает true, если код err требует остановки при запрете повтора критической ошибки.
bool Error_IsCritical(int8_t err)
{
	return err == ErrorNumber(ErrorCatalog::Code::kCOMP_START_BLOCKED);
}

// По состоянию ТН, результату останова и лимиту повторов выбирает останов или повторный пуск.
// stop_command_failed запрещает повтор при отказе команды, err задаёт политику критичности.
ERROR_ACTION Error_Decide(bool stop_command_failed, uint8_t nStart, uint8_t num_repeat,
                          bool no_restart, int8_t err)
{
	if(stop_command_failed) return ERR_ACT_Stop;              // привод не принял команду останова
	if(nStart == 0) return ERR_ACT_Stop;                      // повторные пуски запрещены
	if(no_restart && Error_IsCritical(err)) return ERR_ACT_Stop;
	return num_repeat < nStart ? ERR_ACT_Repeat : ERR_ACT_Stop;
}

// По числу сбоев fails и длительности since_ms проверяет предел max_fails или тайм-аут max_ms_run/max_ms_idle по running. Возвращает true при потере связи.
bool Error_LinkLost(uint8_t fails, uint32_t since_ms, bool running,
                    uint8_t max_fails, uint32_t max_ms_run, uint32_t max_ms_idle)
{
	if(fails >= max_fails) return true;
	return since_ms > (running ? max_ms_run : max_ms_idle);
}

// По running, шагу останова stage и времени since_ms возвращает состояние останова. wait_ms и fallback_ms задают пределы ожидания до и после резервной команды.
STOP_CHECK Error_StopCheck(bool running, uint8_t stage, uint32_t since_ms,
                           uint32_t wait_ms, uint32_t fallback_ms)
{
	if(!running) return STOPCHK_DONE;
	if(stage <= 1) return since_ms > wait_ms ? STOPCHK_FALLBACK : STOPCHK_WAIT;
	return since_ms > fallback_ms ? STOPCHK_FAIL : STOPCHK_WAIT;
}

// По признакам stopped и lost, времени elapsed_ms и пределу limit_ms возвращает исход проверки останова при инициализации.
INIT_STOP Error_InitStopWait(bool stopped, bool lost, uint32_t elapsed_ms, uint32_t limit_ms)
{
	if(stopped) return INITSTOP_DONE;
	if(lost || elapsed_ms > limit_ms) return INITSTOP_FAIL;
	return INITSTOP_WAIT;
}

// Сравнивает полученные регистры got со снимком base. Возвращает первую обнаруженную причину или NCF_NONE.
NETCHIP_FAULT NetChip_Check(const NetChipRegs &got, const NetChipRegs &base)
{
	if(got.version != base.version) return NCF_VERSION;
	if(got.mode != base.mode) return NCF_MODE;
	if(memcmp(got.mac, base.mac, sizeof(got.mac))) return NCF_MAC;
	if(memcmp(got.ip, base.ip, sizeof(got.ip))) return NCF_IP;
	if(memcmp(got.gw, base.gw, sizeof(got.gw))) return NCF_GATEWAY;
	if(memcmp(got.mask, base.mask, sizeof(got.mask))) return NCF_MASK;
	if(!(got.phy & 0x01)) return NCF_LINK;
	return NCF_NONE;
}

// Учитывает причину fault в состоянии w на монотонной секунде now_s. Возвращает действие по восстановлению сети.
NETCHIP_ACTION NetChip_Decide(NetChipWatch *w, NETCHIP_FAULT fault, uint32_t now_s)
{
	if(fault == NCF_NONE) { w->fails = 0; return NCA_NONE; }
	if(w->fails < 255) w->fails++;
	if(w->fails < (fault == NCF_PING ? 1 : NETCHIP_FAILS)) return NCA_NONE;
	if(!w->ever || (uint32_t)(now_s - w->window_start_s) >= NETCHIP_WINDOW_S) {    // новое окно подсчёта
		w->window_start_s = now_s; w->recoveries = 0; w->warned = false;
	}
	const uint32_t gap = w->recoveries >= NETCHIP_MANY ? NETCHIP_SLOW_GAP_S : NETCHIP_MIN_GAP_S;
	if(w->ever && (uint32_t)(now_s - w->last_recover_s) < gap) return NCA_NONE;     // рано: расхождения копятся, сброс будет позже
	w->fails = 0; w->ever = true; w->last_recover_s = now_s;
	if(w->recoveries < 255) w->recoveries++;
	if(w->recoveries >= NETCHIP_MANY && !w->warned) { w->warned = true; return NCA_RECOVER_NOTIFY; }
	return NCA_RECOVER;
}

// Возвращает битовую маску отклонений конфигурации SPI по регистрам mr и csr3.
uint8_t Spi0_Check(uint32_t mr, uint32_t csr3)
{
	uint8_t a = 0;
	if(!(mr & (1UL << 0))) a |= SPI_ANOM_MASTER;                       // MSTR
	if(mr & (1UL << 1)) a |= SPI_ANOM_VARIABLE;                        // PS
	if(((mr >> 16) & 0xF) != 0x7) a |= SPI_ANOM_CHANNEL;               // PCS = 0111: выбран канал 3 (активный низкий уровень в поле)
	if((csr3 & (1UL << 0)) || !(csr3 & (1UL << 1))) a |= SPI_ANOM_MODE; // CPOL = 0 и NCPHA = 1: режим 0
	if(csr3 & (0xFUL << 4)) a |= SPI_ANOM_BITS;                        // BITS = 0: 8 бит
	const uint32_t scbr = (csr3 >> 8) & 0xFF;
	if(scbr < 2 || scbr > 8) a |= SPI_ANOM_SCBR;
	if(csr3 & (0xFFUL << 24)) a |= SPI_ANOM_DLYBCT;                   // DLYBCT: 32 * DLYBCT / MCK после каждого байта
	return a;
}

// По num_repeat и work_ok обновляет отсчёт w на момент now_ms. Возвращает true по истечении limit_ms непрерывной работы без ошибок.
bool Error_RepeatReset(RepeatWatch &w, uint8_t num_repeat, bool work_ok, uint32_t now_ms, uint32_t limit_ms)
{
	if(!work_ok || num_repeat == 0) { w.since_ms = now_ms; return false; }   // окно идёт только при работе без ошибок и с ненулевым счётом
	if((uint32_t)(now_ms - w.since_ms) < limit_ms) return false;            // разность беззнаковая: переход тиков через ноль не ломает отсчёт
	w.since_ms = now_ms;
	return true;
}

// Возвращает уровень выключения реле по relay_invert и r4way_invert. is_r4way указывает на выход четырёхходового клапана.
bool Fault_RelayOffLevel(bool relay_invert, bool r4way_invert, bool is_r4way)
{
	bool level = !relay_invert;                // без инверсии реле включается НИЗКИМ уровнем
	if(r4way_invert && is_r4way) level = !level;
	return level;
}
