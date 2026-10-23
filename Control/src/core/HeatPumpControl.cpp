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
 * Управление тепловым насосом: приём и выполнение команд (пуск, останов,
 * сброс, смена режима), пауза, а также строковое представление состояния
 * для web-морды и журнала. Отдельная единица
 * трансляции.
 *
 * HeatPump.h тянет Constant.h, а с ним Config.h, поэтому три условия
 * (USE_UPS, EEV_DEF, EEV_PREFER_PERCENT) разрешаются так же, как в главной
 * единице трансляции. Проверено сверкой размеров методов.
 */
#include "HeatPump.h"     // HeatPump, HP, Status, dRelay, dFC, dEEV, sTemp, sADC, sInput, rtcSAM3X8, NO_Power
#include "../net/Network.h"      // initW5200
#include "Rtos.h"         // Tasks, _delay, SemaphoreTake/Give
#include "../log/Statistics.h"   // Stats
#include "../net/WebShared.h"    // Socket[], fABORT_SOCK

// Булевые константы для читаемости вызовов StartResume()/StopWait():
// других вызывающих у этих двух методов нет, поэтому константы живут здесь.
const boolean _stop =  true;   // Команда останов ТН
const boolean _wait =  false;  // Команда перевода в режим ожидания ТН
const boolean _start = true;   // Команда запуска ТН
const boolean _resume = false; // Команда возобновления работы ТН

// Флаги команды для очереди: останов не теряется при переполнении, а длинный пуск смотрит, нет ли его в очереди
static uint8_t commandFlags(TYPE_COMMAND c)
{
	if(c == pSTOP || c == pREPEAT) return CQ_STOP | CQ_CRITICAL;
	if(c == pRESET) return CQ_CRITICAL;
	return 0;
}

// Поставить команду в очередь и разбудить CommandHP. Выбор "выполняется / в очереди" и постановка идут
// под одним запретом прерываний: иначе две команды, пришедшие вместе, затирали друг друга. Пробуждение -
// уведомление задачи: оно не теряется, даже если CommandHP ещё не дошла до ожидания.
void HeatPump::sendCommand(TYPE_COMMAND c)
{
	uint32_t pm = irqSave();
	TYPE_COMMAND running = command;
	bool idle = (running == pEMPTY && cmdq.count == 0);
	CqResult r = CQ_DUPLICATE;
	// Пришла команда на старт, а насос останавливается и ничего не выполняется: игнорируем
	if(!(idle && c == pSTART && get_State() == pSTOPING_HP)) r = CommandQueue_Push(&cmdq, running, c, commandFlags(c));
	irqRestore(pm);
	if(r == CQ_DUPLICATE) return;
	if(!idle) {
		if(r == CQ_FULL) journal.jprintf("Command queue FULL, dropped: %s\n", get_command_name(c));
		else journal.jprintf("Active command: %s, queued: %s\n", get_command_name(running), get_command_name(c));
	}
	if(Tasks.xHandleUpdateCommand != NULL) xTaskNotifyGive(Tasks.xHandleUpdateCommand);
}

// В очереди есть STOP/REPEAT
boolean HeatPump::is_next_command_stop()
{
	uint32_t pm = irqSave();
	boolean r = CommandQueue_Has(&cmdq, CQ_STOP);
	irqRestore(pm);
	return r;
}

// CommandHP: перейти к следующей команде. Взятие и запись в command - одной операцией: пока очередь
// непуста или команда выполняется, command != pEMPTY, и sendCommand не видит "пустого окна".
boolean HeatPump::takeCommand()
{
	uint32_t pm = irqSave();
	uint8_t c = pEMPTY;
	command = CommandQueue_Pop(&cmdq, &c) ? (TYPE_COMMAND) c : pEMPTY;
	irqRestore(pm);
	return command != pEMPTY;
}
// Выполнить команду по управлению ТН true-команда выполнена
int8_t HeatPump::runCommand()
{
	uint16_t i;
	while(1) {
		journal.jprintf_time("Run: %s\n", get_command_name(command));

		switch(command)
		{
		case pEMPTY:  return true; break;     // 0 Команд нет
		case pSTART:                          // 1 Пуск теплового насоса
			num_repeat=0;           // обнулить счетчик повторных пусков
			StartResume(_start);    // включить ТН
			break;
		case pAUTOSTART:                      // 2 Пуск теплового насоса автоматический
			StartResume(_start);    // включить ТН
			break;
		case pSTOP:                           // 3 Стоп теплового насоса
		    PauseStart = 0;
			StopWait(_stop);        // Выключить ТН
			break;
		case pRESET:                          // 4 Сброс контроллера
		    PauseStart = 0;
			StopWait(_stop);        // Выключить ТН
			journal.jprintf_time("$SOFTWARE RESET . . .\n\n");
			save_motoHour();
			Stats.SaveStats(0);
			Stats.SaveHistory(0);
			_delay(500);            // задержка что бы вывести сообщение в консоль
			Software_Reset() ;      // Сброс
			break;
		case pREPEAT:
			if(NO_Power) { // Нет питания - ожидание
				NO_Power = 2;
				goto xWait;
			}
			StopWait(_stop);                            // Попытка запустит ТН (по числу пусков)
			num_repeat++;                               // увеличить счетчик повторов пуска ТН
			journal.jprintf("Repeat start %s (attempts remaining %d) . . .\n",(char*)nameHeatPump,get_nStart()-num_repeat);
			PauseStart = 1;   							// Запустить выполнение отложенного старта
			break;
		case pRESTART:
			journal.jprintf("Restart %s . . .\n",(char*)nameHeatPump);
			PauseStart = 1;									// Запустить выполнение отложенного старта
			break;
		case pNETWORK:
			_delay(1000);               						// задержка что бы вывести сообщение в консоль и на веб морду
			if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE) {journal.jprintf((char*)cErrorMutex,__FUNCTION__,MutexWebThreadBusy); break;} // Захват мютекса потока или ОЖИДАНИНЕ W5200_TIME_WAIT
			initW5200(true);                                  // Инициализация сети с выводом инфы в консоль
			for (i=0;i<W5200_THREAD;i++) SETBIT1(Socket[i].flags,fABORT_SOCK);                                 // Признак инициализации сокета, надо прерывать передачу в сервере
			SemaphoreGive(xWebThreadSemaphore);                                                                // Мютекс потока отдать
			break;
		case pPROG_FC:                                             // Программировать инвертор первоначальными данными (настройка инвертора)
		    #ifndef FC_VACON  // Omron , если в Vacon будет определен метод progFC этот дефайн убрать
            dFC.progFC();
            #endif
			break;
		case pSAVE:                                                      // Сохранить настройки
			_delay(2000);              				      		 // задержка что бы вывести сообщение в консоль и на веб морду
			save_all();                                        // сохранить ВСЕ настройки, см. save_all
			break;
		case pWAIT:   // Перевод в состяние ожидания  - особенность возможна блокировка задач - используем семафор
xWait:
			if(SemaphoreTake(Tasks.xCommandSemaphore,(60*1000/portTICK_PERIOD_MS))==pdPASS)    // Семафор  захвачен ОЖИДАНИНЕ ДА 60 сек
			{
				Tasks.Task_vUpdate_run = false;					      // Остановить задачу обновления ТН vUpdate (Tasks.xHandleUpdate)
				StopWait(_wait);                                  // Ожидание
				SemaphoreGive(Tasks.xCommandSemaphore);                 // Семафор отдать
				Wake_UpdateHP();
			}
			else  journal.jprintf((char*)cErrorMutex,__FUNCTION__,MutexCommandBusy);
			break;
		case pRESUME:   // Восстановление работы после ожидания -особенность возможна блокировка задач - используем семафор
			if(SemaphoreTake(Tasks.xCommandSemaphore,(60*1000/portTICK_PERIOD_MS))==pdPASS)    // Семафор  захвачен ОЖИДАНИНЕ ДА 60 сек
			{
				Tasks.Task_vUpdate_run = false;					      // Остановить задачу обновления ТН vUpdate (Tasks.xHandleUpdate)
				StartResume(_resume);                             // восстановление ТН
				SemaphoreGive(Tasks.xCommandSemaphore);                 // Семафор отдать
				Wake_UpdateHP();
			}
			else  journal.jprintf((char*)cErrorMutex,__FUNCTION__,MutexCommandBusy);
			break;

		default:                                                         // Не известная команда
			journal.jprintf("Unknown command: %d !!!", command);
			break;
		}
		if(command != pSFORMAT && command != pSAVE && command != pNETWORK) command_completed = rtcSAM3X8.unixtime();
		if(!takeCommand()) break;   // очередь пуста, command сброшен в pEMPTY
		_delay(1);
	}
	return error;
}

// Возвращает 1, если ТН в паузе
uint8_t HeatPump::is_pause()
{
	return ((get_State() == pWORK_HP && (get_modWork() == pOFF || !is_compressor_on())) || get_State() == pWAIT_HP);
}

// ---- Строковые функции ----
const char *strRusPause={"Пауза"};
const char *strEngPause={"Pause"};
// Получить строку состояния ТН в виде строки
char *HeatPump::StateToStr()
{
	switch ((int)get_State())  //TYPE_STATE_HP
	{
	case pOFF_HP:     return (char*)(PauseStart == 0 ? "Выключен" : "Перезапуск...");  break;   // 0 ТН выключен или Перезапуск
	case pSTARTING_HP:return (char*)"Пуск...";   break;         // 1 Стартует
	case pSTOPING_HP: return (char*)"Останов...";break;         // 2 Останавливается
	case pWORK_HP:                                              // 3 Работает
		if(!is_compressor_on()) {
			if(get_modWork() == pHEAT) return (char*)"Ожид. Нагр.";         // Включить отопление
			if(get_modWork() == pCOOL) return (char*)"Ожид. Охл.";          // Включить охлаждение
			if(get_modWork() == pBOILER) return (char*)"Ожид. ГВС";         // Включить бойлер
			return (char*)strRusPause;
		} else {
			if(get_modWork() == pOFF) return (char*)strRusPause;
			if(get_modWork() & pHEAT)    return (char*)"Отопление";
			if(get_modWork() & pCOOL)    return (char*)"Охлаждение";
			if(get_modWork() & pBOILER)  return (char*)"ГВС";
			if(get_modWork() & pDEFROST) return (char*)"Разморозка";
			return (char*)"...";
		}
	case pWAIT_HP:    return (char*)"Ожидание";         	// 4 Ожидание
	case pERROR_HP:   return (char*)"Ошибка";  				// 5 Ошибка ТН
	}
	return (char*)"...."; 							        // 6 - Эта ошибка возникать не должна!
}

// Получить строку состояния ТН в виде строки АНГЛИЙСКИЕ буквы
char *HeatPump::StateToStrEN()
{
	switch ((int)get_State())  //TYPE_STATE_HP
	{
	case pOFF_HP:     return (char*)(PauseStart == 0 ? "Off" : "Restart...");  break;   // 0 ТН выключен или Перезапуск
	case pSTARTING_HP:return (char*)"Start...";   break;         // 1 Стартует
	case pSTOPING_HP: return (char*)"Stop...";    break;         // 2 Останавливается
	case pWORK_HP:                                               // 3 Работает
		if(!is_compressor_on()) {
			if(get_modWork() == pHEAT)   return (char*)"Wait Heat";         // Включить отопление
			if(get_modWork() == pCOOL)   return (char*)"Wait Cool";         // Включить охлаждение
			if(get_modWork() == pBOILER) return (char*)"Wait Boiler";       // Включить бойлер
			return (char*)strEngPause;
		} else {
			if(get_modWork() == pOFF)    return (char*)strEngPause;
			if(get_modWork() & pHEAT)    return (char*)"Heating";
			if(get_modWork() & pCOOL)    return (char*)"Cooling";
			if(get_modWork() & pBOILER)  return (char*)"Boiler";
			if(get_modWork() & pDEFROST) return (char*)"Defrost";
			return (char*)"...";
		}
	case pWAIT_HP:    return (char*)"Wait";  break;         // 4 Ожидание
	case pERROR_HP:   return (char*)"Error"; break;       // 5 Ошибка ТН
	}
	return (char*)"Status ?"; 							   // 6 - Эта ошибка возникать не должна!
}

void HeatPump::get_StateModworkStr(char *strReturn)
{
	if(get_State() == pOFF_HP) {
		strcat(strReturn, MODE_HP_STR[0]);
	} else if(get_State() == pWAIT_HP) {
#ifdef USE_UPS
		if(NO_Power) strcat(strReturn,"No Power!");
		else
#endif
			strcat(strReturn, "...");
	} else /*if(get_State() == pWORK_HP)*/ {
		if((get_modWork() & pHEAT)) strcat(strReturn, MODE_HP_STR[1]);
		else if((get_modWork() & pCOOL)) strcat(strReturn, MODE_HP_STR[2]);
		else if((get_modWork() & pBOILER)) strcat(strReturn, MODE_HP_STR[3]);
		else if((get_modWork() & pDEFROST)) strcat(strReturn, MODE_HP_STR[4]);
		else strcat(strReturn, MODE_HP_STR[0]);
		if((get_modWork() & pCONTINUE)) strcat(strReturn, MODE_HP_STR[5]);
		strcat(strReturn, " ["); strcat(strReturn, Ret_Text(get_ret())); strcat(strReturn, "]");
	}
}

// получить режим тестирования
char * HeatPump::TestToStr()
{
 switch ((int)get_testMode())
             {
              case NORMAL:    return (char*)"NORMAL";    break;
              case SAFE_TEST: return (char*)"SAFE_TEST"; break;
              case TEST:      return (char*)"TEST";      break;
              case HARD_TEST: return (char*)"HARD_TEST"; break;
              default:        return (char*)cError;     break;    
             }    
}
// Записать состояние теплового насоса в журнал
// Параметр на входе true - вывод в журнал и консоль false - консоль
int8_t HeatPump::save_DumpJournal(boolean f)
{
	uint8_t i;
	void (Journal::*fn)(const char *format, ...) = f ? &Journal::jprintf : &Journal::printf;
	((journal).*(fn))(" modWork:%X[%s]", (int) get_modWork(), Ret_Text(Status.ret));
	for(i = 0; i < RNUMBER; i++) ((journal).*(fn))(" %s:%d", dRelay[i].get_name(), dRelay[i].get_Relay());
	if(dFC.get_present()) ((journal).*(fn))(" freqFC:%.2d", dFC.get_frequency());
	if(dFC.get_present()) ((journal).*(fn))(" Power:%.3d", dFC.get_power());
#ifdef EEV_DEF
#ifdef EEV_PREFER_PERCENT
	((journal).*(fn))(" EEV:%.2d", dEEV.calc_percent(dEEV.get_EEV()));
#else
	((journal).*(fn))(" EEV:%d", dEEV.get_EEV());
#endif
#endif
	for(i = 0; i < INUMBER; i++) ((journal).*(fn))(" %s:%d", sInput[i].get_name(), sInput[i].get_Input());
	((journal).*(fn))(cStrEnd);
	// Доп инфо
	for(i = 0; i < TNUMBER; i++) if(sTemp[i].get_present() && !(TempSetup[i].cfg_flags & 4)) ((journal).*(fn))(" %s:%.2d", sTemp[i].get_name(), sTemp[i].get_Temp());
	for(i = 0; i < ANUMBER; i++) if(sADC[i].get_present()) ((journal).*(fn))(" %s:%.2d", sADC[i].get_name(), sADC[i].get_Value());
	((journal).*(fn))(cStrEnd);
	return OK;
}
