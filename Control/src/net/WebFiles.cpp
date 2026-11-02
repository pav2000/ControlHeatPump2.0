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
#include "WebShared.h"    // Socket[], генерируемые файлы
#include "../core/HeatPump.h"     // HP - всё содержимое выгрузок
#include "Network.h"      // sendPrintfRTOS, sendConstRTOS
#include "../util/Util.h"         // _itoa, ftoa, dptoa
#include "NoSDpage.h"     // index_noSD - страница без карты


// Генерация различных файлов для выгрузки
#define STR_END   strcat(Socket[thread].outBuf,"\r\n")      // Макрос на конец строки

// Значение параметра из реестра (раздел 6.7): name - строка STRCONST, ключ = sect + "." + name строчными
#define TXT_VAL(sect, name)  api_text_cat(Socket[thread].outBuf, sizeof(Socket[thread].outBuf) - 2, sect, name)

// Строки «подпись значение» таблицей: подписи здесь, значения - из реестра. Буфер уходит в сокет, когда заполнится.
struct TxtKey { const char *label; const char *name; };
static void txt_keys(uint8_t thread, const char *sect, const TxtKey *t, uint8_t n)
{
	for(uint8_t i = 0; i < n; i++) {
		if(strlen(Socket[thread].outBuf) > sizeof(Socket[thread].outBuf) - 512) {
			sendBufferRTOS(thread, (byte*)Socket[thread].outBuf, strlen(Socket[thread].outBuf));
			Socket[thread].outBuf[0] = 0;
		}
		strcat(Socket[thread].outBuf, t[i].label);
		TXT_VAL(sect, t[i].name);
		STR_END;
	}
}
#define TXT_KEYS(sect, ...) do { static const TxtKey t_[] = { __VA_ARGS__ }; txt_keys(thread, sect, t_, sizeof(t_) / sizeof(t_[0])); } while(0)
extern uint16_t sendPacketRTOS(uint8_t thread, const uint8_t * buf, uint16_t len,uint16_t pause);

// Генерация заголовка
void get_Header(uint8_t thread,char *name_file)
{
    strcpy(Socket[thread].outBuf, WEB_HEADER_OK_CT);
    strcat(Socket[thread].outBuf, WEB_HEADER_TEXT_ATTACH);
    strcat(Socket[thread].outBuf, name_file);
    strcat(Socket[thread].outBuf, "\"");
    strcat(Socket[thread].outBuf, WEB_HEADER_END);
	sendPacketRTOS(thread, (byte*)Socket[thread].outBuf, strlen(Socket[thread].outBuf), 0);
	char tb_[NOW_TIME_LEN], db_[NOW_DATE_LEN];
	sendPrintfRTOS(thread, " ------ Народный контроллер теплового насоса ver. %s  сборка %s %s ------\r\nКонфигурация: %s: %s\r\nСоздание файла: %s %s \r\n\r\n", VERSION,__DATE__,__TIME__,CONFIG_NAME,CONFIG_NOTE,NowTimeToStr(tb_),NowDateToStr(db_));
}

// Сформировать значения счётчиков в текстовый буфере (отсылки нет),
// используется для получения состояния ТН в виде txt и получения состояния при отсылке по почте
void get_Counts(char *tempBuf)
{

       strcpy(tempBuf,"\n  Сохраненные в ЕЕПРОМ значения внутренних счетчиков\r\n");
       strcat(tempBuf,"-- Глобальные счетчики (Всего за весь период) --\r\n");
       strcat(tempBuf,"Дата сброса общих счетчиков: ");                                 DecodeTimeDate(HP.get_motoHour()->D1,tempBuf); strcat(tempBuf,"\r\n");
       strcat(tempBuf,"Моточасы ТН ВСЕГО [ч]: ");                                       _itoa(HP.get_motoHour()->H1/60,tempBuf);  strcat(tempBuf,"\r\n");
       strcat(tempBuf,"Моточасы компрессора ВСЕГО [ч]: ");                              _itoa(HP.get_motoHour()->C1/60,tempBuf); strcat(tempBuf,"\r\n");
       strcat(tempBuf,"Потребленная энергия ВСЕГО [кВт*ч]: ");                          _dtoa(tempBuf, HP.get_motoHour()->E1 / 1000, 3); strcat(tempBuf,"\r\n");
       strcat(tempBuf,"Выработанная энергия ВСЕГО [кВт*ч]: ");                          _dtoa(tempBuf, HP.get_motoHour()->P1 / 1000, 3); strcat(tempBuf,"\r\n");

       strcat(tempBuf,"-- Сезонные счетчики --\r\n");
       strcat(tempBuf,"Дата сброса сезонных счетчиков: ");                              DecodeTimeDate(HP.get_motoHour()->D2,tempBuf); strcat(tempBuf,"\r\n");
       strcat(tempBuf,"Моточасы ТН сбрасываемый счетчик (сезон) [ч]: ");                _itoa(HP.get_motoHour()->H2/60,tempBuf);strcat(tempBuf,"\r\n");
       strcat(tempBuf,"Моточасы компрессора сбрасываемый счетчик (сезон) [ч]: ");       _itoa(HP.get_motoHour()->C2/60,tempBuf); strcat(tempBuf,"\r\n");
       strcat(tempBuf,"Потребленная энергия сбрасываемый счетчик (сезон) [кВт*ч]: ");   _dtoa(tempBuf, HP.get_motoHour()->E2 / 1000, 3); strcat(tempBuf,"\r\n");
       strcat(tempBuf,"Выработанная энергия сбрасываемый счетчик (сезон) [кВт*ч]: ");   _dtoa(tempBuf, HP.get_motoHour()->P2 / 1000, 3); strcat(tempBuf,"\r\n");
}

// Получить состояние теплового насоса
// header - заголовок файла ставить или нет
void get_txtState(uint8_t thread, boolean header)
{   uint8_t i,j; 
    int16_t x; 
     if (header) get_Header(thread,(char*)"state.txt");   // Если нужен заголовок
     sendPrintfRTOS(thread, "\n  1. Тепловой насос\r\nСостояниеТН: %s\r\nПоследняя ошибка: %d - %s\r\n", HP.StateToStr(),HP.get_errcode(),HP.get_lastErr());
     
     // Состояние ТН
     strcpy(Socket[thread].outBuf,"Режим работы:");strcat(Socket[thread].outBuf,HP.TestToStr()); 
     strcat(Socket[thread].outBuf,"\r\nПоследняя перезагрузка: ");DecodeTimeDate(HP.get_startDT(),Socket[thread].outBuf); 
     strcat(Socket[thread].outBuf,"\r\nВремя с последней перезагрузки: "); TimeIntervalToStr(HP.get_uptime(),Socket[thread].outBuf);  
     strcat(Socket[thread].outBuf,"\r\nПричина последней перезагрузки: ");strcat(Socket[thread].outBuf,ResetCause()); 
       
     strcat(Socket[thread].outBuf,"\r\n\r\n  2. Датчики температуры\r\n");
     for(i=0;i<TNUMBER;i++)   // Информация по  датчикам температуры
         {
              strcat(Socket[thread].outBuf,HP.sTemp[i].get_name()); if((x=8-strlen(HP.sTemp[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(Socket[thread].outBuf," "); }
              strcat(Socket[thread].outBuf,"["); strcat(Socket[thread].outBuf,addressToHex(HP.sTemp[i].get_address())); strcat(Socket[thread].outBuf,"] "); 
              strcat(Socket[thread].outBuf,HP.sTemp[i].get_note());  strcat(Socket[thread].outBuf,": "); 
              if (HP.sTemp[i].get_present())
                {
                  _ftoa(Socket[thread].outBuf,(float)HP.sTemp[i].get_Temp()/100.0,2);
                  if (HP.sTemp[i].get_lastErr()!=OK) { strcat(Socket[thread].outBuf," error:"); _itoa(HP.sTemp[i].get_lastErr(),Socket[thread].outBuf); } 
                  STR_END; 
                }
                else strcat(Socket[thread].outBuf," absent\r\n"); 
         }
      if (HP.get_modeHouse() ==pCOOL) strcat(Socket[thread].outBuf,"Внимание! ТН в режиме охлаждения роли испарителя и конденсатора меняются местами");
      sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));
      
      strcpy(Socket[thread].outBuf,"\n  3. Аналоговые датчики\r\n"); // Новый пакет
      for(i=0;i<ANUMBER;i++)   // Информация по  датчикам температуры
         {   
            strcat(Socket[thread].outBuf,HP.sADC[i].get_name()); if((x=8-strlen(HP.sADC[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(Socket[thread].outBuf," "); }
            strcat(Socket[thread].outBuf,HP.sADC[i].get_note());  strcat(Socket[thread].outBuf,": "); 
            if (HP.sADC[i].get_present()) 
                { 
                  _ftoa(Socket[thread].outBuf,(float)HP.sADC[i].get_Value()/100.0,2); if (HP.sADC[i].get_lastErr()!=OK ) { strcat(Socket[thread].outBuf," error:"); _itoa(HP.sADC[i].get_lastErr(),Socket[thread].outBuf); }
                  STR_END; 
      
                } 
            else strcat(Socket[thread].outBuf," absent\r\n"); 
         }
   
       strcat(Socket[thread].outBuf,"\n  4. Датчики 'сухой контакт'\r\n");
       for(i=0;i<INUMBER;i++)  
           {
            strcat(Socket[thread].outBuf,HP.sInput[i].get_name()); if((x=8-strlen(HP.sInput[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(Socket[thread].outBuf," "); }
            strcat(Socket[thread].outBuf,HP.sInput[i].get_note());  strcat(Socket[thread].outBuf,": "); 
      
            if (HP.sInput[i].get_present()) 
                { 
                  _itoa(HP.sInput[i].get_Input(),Socket[thread].outBuf); 
                  strcat(Socket[thread].outBuf," alarm:"); _itoa(HP.sInput[i].get_alarmInput(),Socket[thread].outBuf); STR_END;   
                }
                else strcat(Socket[thread].outBuf," absent\r\n");      
           }
       
              
       strcat(Socket[thread].outBuf,"\n  5. Реле\r\n");
       for(i=0;i<RNUMBER;i++)  
           {
            strcat(Socket[thread].outBuf,HP.dRelay[i].get_name()); if((x=8-strlen(HP.dRelay[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(Socket[thread].outBuf," "); }
            strcat(Socket[thread].outBuf,HP.dRelay[i].get_note());  strcat(Socket[thread].outBuf,": "); 
      
            if (HP.dRelay[i].get_present()) _itoa(HP.dRelay[i].get_Relay(),Socket[thread].outBuf); 
            else strcat(Socket[thread].outBuf," absent"); 
            STR_END;         
           }
       #ifdef EEV_DEF
        strcat(Socket[thread].outBuf,"\n  6. ЭРВ - ");  strcat(Socket[thread].outBuf,HP.dEEV.get_note()); STR_END;
        if (HP.dEEV.get_present()) 
         {
             strcat(Socket[thread].outBuf,"Минимальное положение (шаги): ");  _itoa(HP.dEEV.get_minEEV(),Socket[thread].outBuf); STR_END;
             strcat(Socket[thread].outBuf,"Полное открыте (шаги):");  _itoa(HP.dEEV.get_maxEEV(),Socket[thread].outBuf);STR_END;
             strcat(Socket[thread].outBuf,"Правило управления ЭРВ: ");
             HP.dEEV.get_ruleEEVtext(Socket[thread].outBuf); STR_END;
             strcat(Socket[thread].outBuf,"\nТекущее положение: ");
#ifdef EEV_PREFER_PERCENT
			_dtoa(Socket[thread].outBuf, HP.dEEV.calc_percent(HP.dEEV.get_EEV()), 2);
#else
			_itoa(HP.dEEV.get_EEV(),Socket[thread].outBuf);
#endif
            STR_END;
             strcat(Socket[thread].outBuf,"Текущий перегрев (градусы): ");  _ftoa(Socket[thread].outBuf,(float)HP.dEEV.get_Overheat()/100.0,2); STR_END;
         }  
        #else
        strcat(Socket[thread].outBuf,"\n  6. EEV absent\r\n");    
        #endif
         
       strcat(Socket[thread].outBuf,"\n  7. Частотный преобразователь\r\n");
       if (HP.dFC.get_present()) 
         {
#ifdef FC_VACON
             strcat(Socket[thread].outBuf,"Целевая скорость [%]: ");    _ftoa(Socket[thread].outBuf,(float)HP.dFC.get_target()/100.0,2); STR_END;
             strcat(Socket[thread].outBuf,"Текущая частота [Гц]: ");    _ftoa(Socket[thread].outBuf,(float)HP.dFC.get_frequency()/100.0,2); STR_END;
#else
              strcat(Socket[thread].outBuf,"Целевая частота [Гц]: ");    _ftoa(Socket[thread].outBuf,(float)HP.dFC.get_target()/100.0,2); STR_END;
              strcat(Socket[thread].outBuf,"Текущая частота [Гц]: ");    _ftoa(Socket[thread].outBuf,(float)HP.dFC.get_frequency()/100.0,2); STR_END;
#endif
              strcat(Socket[thread].outBuf,"Текущая мощность [кВт]: ");  _ftoa(Socket[thread].outBuf,(float)HP.dFC.get_power()/1000.0,3); STR_END;
#ifdef FC_ANALOG_CONTROL // Аналоговое управление
              strcat(Socket[thread].outBuf,"ЦАП дискреты: ");            _itoa(HP.dFC.get_DAC(),Socket[thread].outBuf); STR_END;
#endif
              }
        else strcat(Socket[thread].outBuf,"FC absent\r\n");    

        strcat(Socket[thread].outBuf,"\n  8. Электросчетчик SDM\r\n");
         #ifdef USE_ELECTROMETER_SDM
           TXT_KEYS("sdm",
           	{ "Текущее входное напряжение [В]: ", sdm_VOLTAGE },
           	{ "Текущий потребляемый ток ТН [А]: ", sdm_CURRENT },
           	{ "Текущая потребляемая активная мощность ТН [Вт]: ", sdm_ACPOWER },
           	{ "Коэффициент мощности: ", sdm_POW_FACTOR },
           	{ "Угол фазы (градусы): ", sdm_PHASE },
           	{ "Суммарная активная энергия [кВт/ч]: ", sdm_ACENERGY },
           	{ "Cостояние связи со счетчиком: ", sdm_LINK });
         #else
           strcat(Socket[thread].outBuf,"SDM absent\r\n");
         #endif  
   
   strcat(Socket[thread].outBuf,"\n  9. Частотные датчики потока\r\n");
     for(i=0;i<FNUMBER;i++)  
           {
            strcat(Socket[thread].outBuf,HP.sFrequency[i].get_name()); if((x=8-strlen(HP.sFrequency[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(Socket[thread].outBuf," "); }
            strcat(Socket[thread].outBuf,HP.sFrequency[i].get_note());  strcat(Socket[thread].outBuf,": "); 
            if (HP.sFrequency[i].get_present()) _ftoa(Socket[thread].outBuf,(float)HP.sFrequency[i].get_Value()/1000.0,3);
            else strcat(Socket[thread].outBuf," absent"); 
            STR_END;         
           }
        
  sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));  
 
  get_Counts(Socket[thread].outBuf); // Счетчики
  sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));     
   
}


// Получить настройки в текстовом виде
void get_txtSettings(uint8_t thread)
{
     uint8_t i,j;
     int16_t x; 
     
     get_Header(thread,(char*)"settings.txt");
     sendPrintfRTOS(thread, "Состояние ТН: %s\r\nПоследняя ошибка: %d - %s\r\n", HP.StateToStr(),HP.get_errcode(),HP.get_lastErr());
     strcpy(Socket[thread].outBuf,"\n  1. Общие настройки\r\n");
     strcat(Socket[thread].outBuf,"Режим работы :");
     switch ((MODE_HP)HP.get_modeHouse() )   // режим работы отопления
                   {
                    case pOFF:  strcat(Socket[thread].outBuf,"Выключено\r\n"); break;
                    case pHEAT: strcat(Socket[thread].outBuf,"Отопление\r\n"); break;
                    case pCOOL: strcat(Socket[thread].outBuf,"Охлаждение\r\n"); break;
                    default:    strcat(Socket[thread].outBuf,"Выключено\r\n"); break;
                   }  
     TXT_KEYS("boiler",
     	{ "Бойлер:", boil_BOILER_ON });
     
 
     strcat(Socket[thread].outBuf,"\n  1.1 Отопление\r\n");
     TXT_KEYS("heat",
     	{ "Алгоритм работы отопления: ", hp_RULE },
     	{ "Целевая температура дома (C°): ", hp_TEMP1 },
     	{ "Целевая температура обратки (C°):", hp_TEMP2 },
     	{ "Что является целью: ", hp_TARGET },
     	{ "Гистерезис целевой температуры (C°): ", hp_DTEMP });
    
     strcat(Socket[thread].outBuf,"  - Использование ночного тарифа -\r\n");
     TXT_KEYS("heat",
     	{ "Добавка к целевой температуре по часам (C°): ", ADD_DELTA_TEMP },
     	{ "Начальный час добавки к целевой температуре: ", ADD_DELTA_HOUR },
     	{ "Конечный час добавки к целевой температуре: ", ADD_DELTA_END_HOUR });
       
     if (HP.get_ruleHeat()!=pHYSTERESIS)
         {
         strcat(Socket[thread].outBuf," - ПИД -\r\n");
         TXT_KEYS("heat",
         	{ "Целевая температура подачи ПИД ТН (C°): ", hp_TEMP_PID },
         	{ "Постоянная интегрирования времени в секундах ПИД ТН: ", hp_HP_TIME },
         	{ "Пропорциональная составляющая ПИД ТН: ", hp_HP_PRO },
         	{ "Интегральная составляющая ПИД ТН: ", hp_HP_IN },
         	{ "Дифференциальная составляющая ПИД ТН: ", hp_HP_DIF });
         }
         
     strcat(Socket[thread].outBuf,"  - Опции -\r\n"); 
     TXT_KEYS("heat",
     	{ "Использование погодозависимости: ", hp_WEATHER },
     	{ "Коэффициент погодозависимости: ", hp_K_WEATHER });
        
     strcat(Socket[thread].outBuf," - Защиты - \r\n");
     TXT_KEYS("heat",
     	{ "Tемпература подачи максимальная (C°): ", hp_TEMP_IN },
     	{ "Tемпература обратки минимальная (C°): ", hp_TEMP_OUT },
     	{ "Максимальная разность температур конденсатора (C°): ", hp_D_TEMP });
     sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf)); 
 
     strcpy(Socket[thread].outBuf,"\n  1.2 Охлаждение\r\n");
     TXT_KEYS("cool",
     	{ "Алгоритм работы отопления: ", hp_RULE },
     	{ "Целевая температура дома (C°): ", hp_TEMP1 },
     	{ "Целевая температура обратки (C°):", hp_TEMP2 },
     	{ "Что является целью: ", hp_TARGET },
     	{ "Гистерезис целевой температуры (C°): ", hp_DTEMP });

     if (HP.get_ruleCool()!=pHYSTERESIS)
         {
         strcat(Socket[thread].outBuf," - ПИД -\r\n");	
         TXT_KEYS("cool",
         	{ "Целевая температура подачи ПИД ТН (C°): ", hp_TEMP_PID },
         	{ "Постоянная интегрирования времени в секундах ПИД ТН: ", hp_HP_TIME },
         	{ "Пропорциональная составляющая ПИД ТН: ", hp_HP_PRO },
         	{ "Интегральная составляющая ПИД ТН: ", hp_HP_IN },
         	{ "Дифференциальная составляющая ПИД ТН: ", hp_HP_DIF });
         }
     strcat(Socket[thread].outBuf," - Опции -\r\n"); 
     TXT_KEYS("cool",
     	{ "Использование погодозависимости: ", hp_WEATHER },
     	{ "Коэффициент погодозависимости: ", hp_K_WEATHER });
         
     strcat(Socket[thread].outBuf," - Защиты -\r\n");
     TXT_KEYS("cool",
     	{ "Tемпература подачи максимальная (C°): ", hp_TEMP_IN },
     	{ "Tемпература обратки минимальная (C°): ", hp_TEMP_OUT },
     	{ "Максимальная разность температур конденсатора (C°): ", hp_D_TEMP });
     sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));   
     
     strcpy(Socket[thread].outBuf,"\n  1.3 ГВС\r\n");
     TXT_KEYS("boiler",
     	{ "Целевая температура бойлера (C°): ", boil_TEMP_TARGET },
     	{ "Гистерезис целевой температуры (C°): ", boil_DTARGET });
     
     strcat(Socket[thread].outBuf," - Использование ночного тарифа - \r\n");
     TXT_KEYS("boiler",
     	{ "Добавка к целевой температуре по часам (C°): ", ADD_DELTA_TEMP },
     	{ "Начальный час добавки к целевой температуре: ", ADD_DELTA_HOUR },
     	{ "Конечный час добавки к целевой температуре: ", ADD_DELTA_END_HOUR });
     
     strcat(Socket[thread].outBuf," - Встроенный ТЭН -\r\n");
     TXT_KEYS("boiler",
     	{ "Бойлер греет только ТЭН (ТН бойлер не греет): ", boil_fHeaterOnly },
     	{ "Ускоренный нагрев ГВС (одновременное использование ТЭНа и ТН для нагрева): ", boil_TURBO_BOILER },
     	{ "Использование ТЭНа для догрева бойлера до высоких температур: ", boil_ADD_HEATING },
     	{ "Значение температуры для включения догрева бойлера (C°): ", boil_TEMP_RBOILER },
     	{ "Гистерезис температуры догрева бойлера, если догрев не по расписанию °C: ", boil_dAddHeat },
     	{ "Включать догрев, если компрессор не нагрел бойлер до температуры догрева: ", boil_fAddHeatingForce });
     
     
     if (HP.dFC.get_present()) // Только для инвертора
         {
         strcat(Socket[thread].outBuf," - ПИД -\r\n");
         TXT_KEYS("boiler",
         	{ "Использовать ПИД управление инвертором: ", boil_fBoilerPID },
         	{ "Целевая температура подачи (C°): ", boil_BOIL_TEMP },
         	{ "Постоянная интегрирования времени (сек.): ", boil_BOIL_TIME },
         	{ "Пропорциональная составляющая: ", boil_BOIL_PRO },
         	{ "Интегральная составляющая: ", boil_BOIL_IN },
         	{ "Дифференциальная составляющая: ", boil_BOIL_DIF });
         }
         
     strcat(Socket[thread].outBuf," - Защиты при работе теплового насоса -\r\n");
     TXT_KEYS("boiler",
     	{ "Tемпература подачи максимальная (C°): ", boil_TEMP_MAX });
     
     strcat(Socket[thread].outBuf," - Опции -\r\n");
     TXT_KEYS("boiler",
     	{ "Греть совместно с отоплением, если ТН работает на отопление: ", boil_TOGETHER_HEAT },
     	{ "Профилактика сальмонеллы (1 раз в неделю): ", boil_SALMONELLA },
     	{ "Длительная обработка сальмонеллы: ", boil_fSalmonellaHold },
     	{ "Управления циркуляционным насосом ГВС: ", boil_CIRCULATION },
     	{ "Время работы насоса ГВС (мин.): ", boil_CIRCUL_WORK },
     	{ "Пауза в работе насоса ГВС (мин.): ", boil_CIRCUL_PAUSE },
     	{ "При нагреве бойлера сбрасывать ""избыточное"" тепло систему отопления: ", boil_RESET_HEAT },
     	{ "Время сброса тепла (мин.): ", boil_RESET_TIME });
     
   
     strcat(Socket[thread].outBuf," - Расписание работы -\r\n");
     TXT_KEYS("boiler",
     	{ "Использование расписания: ", boil_SCHEDULER_ON },
     	{ "Работа по расписанию только для ТЭНа: ", boil_SCHEDULER_ADDHEAT },
     	{ "", boil_SCHEDULER });
     sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));  

     strcpy(Socket[thread].outBuf,"\n  1.4 Текущий профиль ТН\r\n");  
     HP.Prof.info_txt(Socket[thread].outBuf);
     sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));  
     
     strcpy(Socket[thread].outBuf,"\n  1.5 Опции ТН\r\n");
     TXT_KEYS("ohp",
     	{ "Расположение файлов веб-сервера на SPI Flash (иначе на SD карте): ", option_WebOnSPIFlash },
     	{ "Сохранение состояния ТН в ЕЕПРОМ, для восстановления его после сброса: ", option_SAVE_ON },
     	{ "Число попыток пуска компрессора: ", option_ATTEMPT },
     	{ "Интервал накопления точек графиков в памяти, сек.: ", option_TIME_CHART },
     	{ "Графики в памяти пишутся только во время работы компрессора: ", option_Charts_when_comp_on },
     	{ "Запись истории на SD карту (раз в минуту): ", option_History },
     	{ "Детализация ВКЛ/ВЫКЛ компрессора в истории (10 с, 5 мин): ", option_HistoryDetail });
     

     strcat(Socket[thread].outBuf," - Настройки контура отопления -\r\n");
     TXT_KEYS("ohp",
     	{ "Использование дополнительного ТЭНа отопления: ", option_ADD_HEAT },
     	{ "Температура для управления дополнительным ТЭНом (C°): ", option_TEMP_RHEAT },
     	{ "Время работы насоса отопления при выключенном компрессоре (сек): ", option_PUMP_WORK },
     	{ "Время паузы насоса отопления при выключенном компрессоре (сек): ", option_PUMP_PAUSE });
     
     strcat(Socket[thread].outBuf," - Времена и задержки -\r\n");
     TXT_KEYS("ohp",
     	{ "Минимальное время простоя компрессора (мин): ", option_PAUSE },
     	{ "Задержка включения компрессора после включения насосов (сек): ", option_DELAY_ON_PUMP },
     	{ "Задержка выключения насосов после выключения компрессора (сек): ", option_DELAY_OFF_PUMP },
     	{ "Задержка включения ТН после внезапного сброса контроллера (сек): ", option_DELAY_START_RES },
     	{ "Задержка перед повторным включением ТН при ошибке (попытки пуска) (сек): ", option_DELAY_REPEAD_START },
     	{ "Задержка между переключением реверсивного клапана и включением компрессора (сек): ", option_DELAY_R4WAY },
     	{ "Пауза после переключение ГВС (сек): ", option_DELAY_BOILER_SW },
     	{ "Время на сколько блокируются защиты при переходе с ГВС (сек): ", option_DELAY_BOILER_OFF });
#ifdef DEFROST     // ключи разморозки есть только у воздушного ТН
     TXT_KEYS("ohp",
     	{ "Задержка после срабатывания датчика перед включением разморозки (сек): ", option_DELAY_DEFROST_ON },
     	{ "Задержка перед выключением разморозки (сек): ", option_DELAY_DEFROST_OFF });
#endif
  
     strcat(Socket[thread].outBuf," - Дополнительное оборудование -\r\n");
     TXT_KEYS("ohp",
     	{ "Использование резервного питания от генератора (ограничение мощности): ", option_fBackupPower },
     	{ "Максимальная мощность при питании от генератора (вт.): ", option_maxBackupPower });
     strcat(Socket[thread].outBuf,"Включённые флаги отладки: ");
     for(uint8_t i = 0; i < DbgCount; i++) if(dbgFlags & DbgTable[i].bit) { strcat(Socket[thread].outBuf, DbgTable[i].name); strcat(Socket[thread].outBuf, " "); }
     STR_END;
     TXT_KEYS("ohp",
     	{ "Генерация звукового сигнала: ", option_BEEP },
     	{ "Многоблочное чтение SD карты: ", option_SDMB },
     	{ "Использование Nextion дисплея: ", option_NEXTION },
     	{ "Включать дисплей при пуске компрессора: ", option_NEXTION_WORK },
     	{ "Время засыпания дисплея Nextion (мин.): ", option_NEXT_SLEEP },
     	{ "Яркость дисплея Nextion в %: ", option_NEXT_DIM });
     sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));  
    
     strcpy(Socket[thread].outBuf,"\n\n  1.6 Сетевые настройки\r\n");
     TXT_KEYS("net",
     	{ "Использование DHCP: ", net_DHCP },
     	{ "IP адрес контроллера: ", net_IP },
     	{ "DNS сервер: ", net_DNS },
     	{ "Шлюз: ", net_GATEWAY },
     	{ "Маска подсети: ", net_SUBNET },
     	{ "MAC адрес контроллера: ", net_MAC });

     TXT_KEYS("net",
     	{ "Запрет пинга контроллера: ", net_NO_PING },
     	{ "Использование паролей: ", net_PASS },
     	{ "Пароль пользователя (user): ", net_PASSUSER },
     	{ "Пароль администратора (admin): ", net_PASSADMIN });

     TXT_KEYS("net",
     	{ "Проверка ping до сервера: ", net_PING_TIME },
     	{ "Адрес пингуемого сервера: ", net_PING_ADR },
     	{ "Контроль связи с чипом Wiznet W5xxx раз 10 сек.: ", net_INIT_W5200 },
     	{ "Время очистки сокетов Wiznet W5xxx: ", net_RES_SOCKET });
     strcat(Socket[thread].outBuf,"Время сброса Wiznet W5xxx: "); strcat(Socket[thread].outBuf,nameWiznet);strcat(Socket[thread].outBuf,": "); TXT_VAL("net", net_RES_W5200); STR_END;
     TXT_KEYS("net",
     	{ "Размер пакета для отправки в байтах: ", net_SIZE_PACKET },
     	{ "Не ожидать получения ACK при посылке следующего пакета: ", net_NO_ACK },
     	{ "Пауза перед отправкой следующего пакета, если нет ожидания ACK. (мсек): ", net_DELAY_ACK });

     
     strcat(Socket[thread].outBuf,"\n  1.7 Настройки даты и времени\r\n");
     TXT_KEYS("time",
     	{ "Установленная дата: ", time_DATE },
     	{ "Установленное время: ", time_TIME },
     	{ "Адрес NTP сервера: ", time_NTP },
     	{ "Синхронизация времени по NTP раз в сутки: ", time_UPDATE },
     	{ "Синхронизация раз в час с I2C часами: ", time_UPDATE_I2C });
  
     sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));  
      
     strcpy(Socket[thread].outBuf,"\n  1.8 Уведомления\r\n");
     TXT_KEYS("message",
     	{ "Сброс контроллера: ", mess_MESS_RESET },
     	{ "Возникновение ошибок: ", mess_MESS_ERROR },
     	{ "Сигнал «жизни» (ежедневно в 12-00): ", mess_MESS_LIFE },
     	{ "Достижение граничной температуры: ", mess_MESS_TEMP },
     	{ " Граничная температура в доме (если меньше то посылается уведомление): ", mess_MESS_TIN },
     	{ " Граничная температура бойлера (если меньше то посылается уведомление): ", mess_MESS_TBOILER },
     	{ " Граничная температура компрессора (если больше то посылается уведомление): ", mess_MESS_TCOMP },
     	{ "Проблемы с SD картой и SPI flash: ", mess_MESS_SD },
     	{ "Прочие уведомления: ", mess_MESS_WARNING });
     strcat(Socket[thread].outBuf," - Настройка отправки почты -\r\n");
     TXT_KEYS("message",
     	{ "Посылать уведомления по почте: ", mess_MAIL },
     	{ "Адрес smtp сервера: ", mess_SMTP_SERVER },
     	{ "Порт smtp сервера: ", mess_SMTP_PORT },
     	{ "Необходимость авторизации на сервере: ", mess_MAIL_AUTH },
     	{ "Логин для входа: ", mess_SMTP_LOGIN },
     	{ "Пароль для входа: ", mess_SMTP_PASS },
     	{ "Адрес отправителя: ", mess_SMTP_MAILTO },
     	{ "Адрес получателя: ", mess_SMTP_RCPTTO },
     	{ "Добавлять в уведомление информацию о состоянии ТН: ", mess_MAIL_INFO });
     strcat(Socket[thread].outBuf," - Настройка отправки SMS -\r\n");
     TXT_KEYS("message",
     	{ "Посылать уведомления по SMS: ", mess_SMS },
     	{ "Телефон получателя: ", mess_SMS_PHONE },
     	{ "Сервис отправки SMS: ", mess_SMS_SERVICE },
     	{ "Адрес сервиса отправки SMS: ", mess_SMS_IP });
     TXT_VAL("message", mess_SMS_NAMEP1); strcat(Socket[thread].outBuf,": "); TXT_VAL("message", mess_SMS_P1); STR_END;
     TXT_VAL("message", mess_SMS_NAMEP2); strcat(Socket[thread].outBuf,": "); TXT_VAL("message", mess_SMS_P2); STR_END;
     sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf)); 
      
      // MQTT
     strcpy(Socket[thread].outBuf,"\n  1.9 Настройка MQTT\r\n");
      #ifdef MQTT
     TXT_KEYS("mqtt",
     	{ "Включить отправку на сервер MQTT: ", mqtt_USE_MQTT },
     	{ "Отправка на сервер ThingSpeak: ", mqtt_USE_TS },
     	{ "Включить отсылку дополнительных данных: ", mqtt_BIG_MQTT },
     	{ "Включить передачу данных с электросчетчика SDM120: ", mqtt_SDM_MQTT },
     	{ "Включить передачу данных об инверторе: ", mqtt_FC_MQTT },
     	{ "Включить передачу данных об эффективности ТН: ", mqtt_COP_MQTT },
     	{ "Интервал передачи данных [1...1000] (минут): ", mqtt_TIME_MQTT },
     	{ "Адрес MQTT сервера: ", mqtt_ADR_MQTT },
     	{ "Порт MQTT сервера: ", mqtt_PORT_MQTT },
     	{ "Логин для входа: ", mqtt_LOGIN_MQTT },
     	{ "Пароль для входа: ", mqtt_PASSWORD_MQTT },
     	{ "Идентификатор клиента: ", mqtt_ID_MQTT });
     strcat(Socket[thread].outBuf," - Сервис <Народный мониторинг> -\r\n");
     TXT_KEYS("mqtt",
     	{ "Включить передачу данных: ", mqtt_USE_NARMON },
     	{ "Посылать расширенный набор данных: ", mqtt_BIG_NARMON },
     	{ "Адрес сервиса: ", mqtt_ADR_NARMON },
     	{ "Порт сервиса: ", mqtt_PORT_NARMON },
     	{ "Логин для входа (получается при регистрации): ", mqtt_LOGIN_NARMON },
     	{ "Личный код для передачи (смотреть в разделе API MQTT сервиса): ", mqtt_PASSWORD_NARMON },
     	{ "Имя устройства (корень всех топиков): ", mqtt_ID_NARMON });
     #else
      strcat(Socket[thread].outBuf,"Не поддерживается, нет в прошивке");
     #endif
     
     strcat(Socket[thread].outBuf,"\n  2.1 Датчики температуры\r\n");
     for(i=0;i<TNUMBER;i++)   // Информация по  датчикам температуры
         {
              strcat(Socket[thread].outBuf,HP.sTemp[i].get_name()); if((x=8-strlen(HP.sTemp[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(Socket[thread].outBuf," "); } 
              strcat(Socket[thread].outBuf,"["); strcat(Socket[thread].outBuf,addressToHex(HP.sTemp[i].get_address())); strcat(Socket[thread].outBuf,"] ");
              strcat(Socket[thread].outBuf,HP.sTemp[i].get_note());  strcat(Socket[thread].outBuf,": "); 
        
              if (HP.sTemp[i].get_present())
                {
                  strcat(Socket[thread].outBuf," T=");      _ftoa(Socket[thread].outBuf,(float)HP.sTemp[i].get_Temp()/100.0,2);
                  strcat(Socket[thread].outBuf,", Tmin=");  _ftoa(Socket[thread].outBuf,(float)HP.sTemp[i].get_minTemp()/100.0,2);
                  strcat(Socket[thread].outBuf,", Tmax=");  _ftoa(Socket[thread].outBuf,(float)HP.sTemp[i].get_maxTemp()/100.0,2);
                  strcat(Socket[thread].outBuf,", Terr=");  _ftoa(Socket[thread].outBuf,(float)HP.sTemp[i].get_errTemp()/100.0,2);
                  strcat(Socket[thread].outBuf,", Ttest="); _ftoa(Socket[thread].outBuf,(float)HP.sTemp[i].get_testTemp()/100.0,2);
                  if (HP.sTemp[i].get_lastErr()!=OK) { strcat(Socket[thread].outBuf," error:"); _itoa(HP.sTemp[i].get_lastErr(),Socket[thread].outBuf); }  STR_END;
                }
                else strcat(Socket[thread].outBuf," absent \r\n"); 
         }   

     sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));   
    
          
      strcpy(Socket[thread].outBuf,"\n  2.2 Аналоговые датчики\r\n");
      for(i=0;i<ANUMBER;i++)   // Информация по  аналоговым датчикам - например давление
         {   
            strcat(Socket[thread].outBuf,HP.sADC[i].get_name()); if((x=8-strlen(HP.sADC[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(Socket[thread].outBuf," "); }
            strcat(Socket[thread].outBuf,HP.sADC[i].get_note());  strcat(Socket[thread].outBuf,": "); 
            if (HP.sADC[i].get_present()) 
                { 
                  strcat(Socket[thread].outBuf," P=");    _ftoa(Socket[thread].outBuf,(float)HP.sADC[i].get_Value()/100.0,2);
                  strcat(Socket[thread].outBuf," Pmin="); _ftoa(Socket[thread].outBuf,(float)HP.sADC[i].get_minValue()/100.0,2);
                  strcat(Socket[thread].outBuf," Pmax="); _ftoa(Socket[thread].outBuf,(float)HP.sADC[i].get_maxValue()/100.0,2);
                  strcat(Socket[thread].outBuf," Ptest=");_ftoa(Socket[thread].outBuf,(float)HP.sADC[i].get_testValue()/100.0,2);
                  strcat(Socket[thread].outBuf," Zero="); _itoa(HP.sADC[i].get_zeroValue(),Socket[thread].outBuf);
                  strcat(Socket[thread].outBuf," Kof=");  _ftoa(Socket[thread].outBuf,(float)HP.sADC[i].get_transADC(),3);
                  strcat(Socket[thread].outBuf," Pin=AD");_itoa(HP.sADC[i].get_pinA(),Socket[thread].outBuf); 
                  if (HP.sADC[i].get_lastErr()!=OK ) { strcat(Socket[thread].outBuf," error:"); _itoa(HP.sADC[i].get_lastErr(),Socket[thread].outBuf); }  STR_END;
                } 
            else strcat(Socket[thread].outBuf," absent \r\n"); 
         }  
         
  
       strcat(Socket[thread].outBuf,"\n  2.3 Датчики сухой контакт\r\n");
       for(i=0;i<INUMBER;i++)  
           {
            strcat(Socket[thread].outBuf,HP.sInput[i].get_name()); if((x=8-strlen(HP.sInput[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(Socket[thread].outBuf," "); }
            strcat(Socket[thread].outBuf,HP.sInput[i].get_note());  strcat(Socket[thread].outBuf,": "); 
      
            if (HP.sInput[i].get_present()) 
                { 
                 strcat(Socket[thread].outBuf," In=");       _itoa(HP.sInput[i].get_Input(),Socket[thread].outBuf); 
                 strcat(Socket[thread].outBuf," Alarm=");    _itoa(HP.sInput[i].get_alarmInput(),Socket[thread].outBuf); 
                 strcat(Socket[thread].outBuf," Test=");     _itoa(HP.sInput[i].get_testInput(),Socket[thread].outBuf); 
                 strcat(Socket[thread].outBuf," Pin=");      _itoa(HP.sInput[i].get_pinD(),Socket[thread].outBuf);  
                 strcat(Socket[thread].outBuf," Type=");
                 switch((int)HP.sInput[i].get_typeInput())
                   {
                    case pALARM: strcat(Socket[thread].outBuf,"Alarm"); break;                // 0 Аварийный датчик, его срабатываение приводит к аварии и останове Тн
                    case pSENSOR:strcat(Socket[thread].outBuf,"Work");  break;                // 1 Обычный датчик, его значение используется в алгоритмах ТН
                    case pPULSE: strcat(Socket[thread].outBuf,"Pulse"); break;                // 2 Импульсный висит на прерывании и считает частоты - выходная величина ЧАСТОТА
                    default:     strcat(Socket[thread].outBuf,"err_type"); break;             // Ошибка??
                   }
                  if (HP.sInput[i].get_lastErr()!=OK) { strcat(Socket[thread].outBuf," error:"); _itoa(HP.sInput[i].get_lastErr(),Socket[thread].outBuf);}   STR_END;
                }
                else strcat(Socket[thread].outBuf," absent \r\n");      
           } 

     sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));   
          
        strcpy(Socket[thread].outBuf,"\n  3.1 Реле\r\n");
        for(i=0;i<RNUMBER;i++)  
           {
            strcat(Socket[thread].outBuf,HP.dRelay[i].get_name()); if((x=8-strlen(HP.dRelay[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(Socket[thread].outBuf," "); }
            strcat(Socket[thread].outBuf,HP.dRelay[i].get_note()); 
            strcat(Socket[thread].outBuf," Pin:"); _itoa(HP.dRelay[i].get_pinD(),Socket[thread].outBuf); strcat(Socket[thread].outBuf," Status: "); 
      
            if (HP.dRelay[i].get_present()) { _itoa(HP.dRelay[i].get_Relay(),Socket[thread].outBuf); STR_END;}
            else strcat(Socket[thread].outBuf," absent \r\n");      
           }       

       #ifdef EEV_DEF
       strcat(Socket[thread].outBuf,"\n  3.2 ЭРВ - ");  strcat(Socket[thread].outBuf,HP.dEEV.get_note()); STR_END;
        if (HP.dEEV.get_present()) 
         {
			 strcat(Socket[thread].outBuf,"Ноги куда присоединен ЭРВ: ");
			 strcat(Socket[thread].outBuf,"D"); _itoa(PIN_EEV1_D24,Socket[thread].outBuf);
			 strcat(Socket[thread].outBuf," D");_itoa(PIN_EEV2_D25,Socket[thread].outBuf);
			 strcat(Socket[thread].outBuf," D");_itoa(PIN_EEV3_D26,Socket[thread].outBuf);
			 strcat(Socket[thread].outBuf," D");_itoa(PIN_EEV4_D27,Socket[thread].outBuf);STR_END;

			 TXT_KEYS("eev",
			 	{ "Минимальное положение (шаги): ", eev_MIN },
			 	{ "Полное открыте (шаги):", eev_MAX });
			 strcat(Socket[thread].outBuf,"Формула перегрева: ");
 			 HP.dEEV.get_ruleEEVtext(Socket[thread].outBuf); STR_END;
             strcat(Socket[thread].outBuf,"Алгоритм ЭРВ: ");
             strcat(Socket[thread].outBuf, GETBIT(HP.dEEV.get_flags(), fEEV_Hybrid) ? "Гибрид" : "ПИД"); STR_END;
             TXT_KEYS("eev",
             	{ "Целевой перегрев (C°): ", eev_TARGET },
             	{ "ПИД, период (сек): ", eev_TIME },
             	{ "Пропорциональная составляющая: ", eev_KP },
             	{ "Интегральная составляющая: ", eev_KI },
             	{ "Дифференциальная составляющая: ", eev_KD },
             	{ "Ограничение сдвига за период, 0 - нет (шаги): ", eev_PID_MAX },
             	{ "Гибрид, усиление (шагов на 1 K): ", eev_KC },
             	{ "Гибрид, время интегрирования (сек): ", eev_TI },
             	{ "Гибрид, подача по частоте: ", eev_KFF },
             	{ "Гибрид, предел подачи за период (шаги): ", eev_FFMAX },
             	{ "Гибрид, защита от низкого перегрева (C°): ", eev_OHMIN },
             	{ "Стартовая позиция по стартовой частоте: ", eev_SPF },
             	{ "Старт по частоте, отопление и охлаждение (шагов на Гц): ", eev_SPHZ },
             	{ "Старт по частоте, ГВС (шагов на Гц): ", eev_SPHZB },
             	{ "Ручное управление, открытие ЭРВ (шаги): ", eev_MANUAL },
             	{ "Поправка (C°): ", eev_CONST });
             strcat(Socket[thread].outBuf,"Используемый фреон: ");
             strcat(Socket[thread].outBuf, noteFreon[HP.dEEV.get_typeFreon()]); STR_END;
             strcat(Socket[thread].outBuf,"Текущее положение: ");
#ifdef EEV_PREFER_PERCENT
			_dtoa(Socket[thread].outBuf, HP.dEEV.calc_percent(HP.dEEV.get_EEV()), 2);
#else
			_itoa(HP.dEEV.get_EEV(),Socket[thread].outBuf);
#endif
             STR_END;
             strcat(Socket[thread].outBuf,"Текущий перегрев (градусы): ");   _ftoa(Socket[thread].outBuf,(float)HP.dEEV.get_Overheat()/100.0,2); STR_END;
             strcat(Socket[thread].outBuf," - Корректировка перегрева -\r\n");
             TXT_KEYS("eev",
             	{ "Флаг включения корректировки перегрева от разности температур конденсатора и испарителя: ", eev_cCORRECT },
             	{ "Задержка после старта компрессора (сек): ", eev_cDELAY },
             	{ "Период в циклах ЭРВ, сколько пропустить (циклы): ", eev_cPERIOD },
             	{ "Температура нагнетания - конденсации (C°): ", eev_cDELTA },
             	{ "Стартовый перегрев (C°): ", eev_cOH_START },
             	{ "Минимальный перегрев (C°): ", eev_cOH_MIN },
             	{ "Максимальный перегрев (C°): ", eev_cOH_MAX });

             strcat(Socket[thread].outBuf," - Глобальные настройки -\r\n");
             TXT_KEYS("eev",
             	{ "Ошибка при которой происходит уменьшение ПИД ЭРВ (C°): ", eev_PID2_delta },
             	{ "Скорость шагового двигателя ЭРВ (импульсы в сек.): ", eev_SPEED },
             	{ "ПУСКОВАЯ позиция ЭРВ - то что при старте компрессора, при раскрутке (шаги): ", eev_PRE_START_POS },
             	{ "СТАРТОВАЯ позиция ЭРВ после раскрутки компрессора т.е. позиция скоторой начинается работа ПИД (шаги): ", eev_START_POS },
             	{ "Задержка включения ЭРВ после включения компрессора (сек.): ", eev_DELAY_ON_PID },
             	{ "Время после старта компрессора когда ЭРВ выходит на стартовую позицию - облегчение пуска вначале ЭРВ (сек.): ", eev_DELAY_START_POS },
             	{ "Задержка закрытия ЭРВ после выключения насосов (се.к): ", eev_DELAY_OFF },
             	{ "Задержка между открытием ЭРВ и включением компрессора, для выравнивания давлений (сек.): ", eev_DELAY_ON },
             	{ "Флаг удержания мотора шагового двигателя ЭРВ: ", eev_HOLD_MOTOR },
             	{ "Закрытие ЭРВ при выключении компрессора: ", eev_CLOSE },
             	{ "Всегда начинать работу ЭРВ со стратовой позиции: ", eev_START },
             	{ "Использовать спецальную позицию ЭРВ при пуске компрессора: ", eev_LIGHT_START });
         }
        #else 
            strcat(Socket[thread].outBuf,"\n  3.2 EEV absent \r\n");    
        #endif
       sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));   
        
       strcpy(Socket[thread].outBuf,"\n  3.3 Частотный преобразователь\r\n");
       if (HP.dFC.get_present()) 
         {
         TXT_KEYS("fc",
         	{ "Текущая частота (гц): ", fc_cFC });
         #ifdef FC_ANALOG_CONTROL // Аналоговое управление
              strcat(Socket[thread].outBuf," - Аналоговое управление -\r\n");
              TXT_KEYS("fc",
              	{ "Отсчеты ЦАП соответсвующие частоте 0 гц (отсчеты): ", fc_LEVEL0 },
              	{ "Отсчеты ЦАП соответсвующие максимальной частоте (отсчеты): ", fc_LEVEL100 });
          #else
             strcat(Socket[thread].outBuf," - Управление идет через Modbus - \r\n"); 
             TXT_KEYS("fc",
             	{ "Блокировка инвертора: ", fc_BLOCK },
             	{ "Флаг автоматического сброса не критичной ошибки инвертора: ", fc_AUTO_RESET_FAULT });
          #endif
	       TXT_KEYS("fc",
	       	{ "Время обновления алгоритма пид регулятора (сек): ", fc_UPTIME },
	       	{ "Проценты от уровня защит (мощность, ток, давление, температура) при которой происходит блокировка роста частоты (%): ", fc_PID_STOP });
	       
           strcat(Socket[thread].outBuf," - Граничные температуры - \r\n");
	       TXT_KEYS("fc",
	       	{ "Защита по температуре нагнетания - сколько градусов не доходит до максимальной и при этом происходит уменьшение частоты (C°): ", fc_DT_COMP_TEMP },
	       	{ "Превышение температуры от уставок (подача) при которой срабатыват защита, уменьшается частота ((C°): ", fc_DT_TEMP },
	       	{ "Превышение температуры от уставок (подача) при которой срабатыват защита ГВС, уменьшается частота (C°): ", fc_DT_TEMP_BOILER });
	
	       strcat(Socket[thread].outBuf," - Стартовые частоты - \r\n");
	       TXT_KEYS("fc",
	       	{ "Стартовая частота инвертора (гц): ", fc_START_FREQ },
	       	{ "Стартовая частота инвертора ГВС (гц): ", fc_START_FREQ_BOILER });
	       
	       strcat(Socket[thread].outBuf," - Минимальные частоты - \r\n");
	       TXT_KEYS("fc",
	       	{ "Минимальная частота инвертора (гц): ", fc_MIN_FREQ },
	       	{ "Mинимальная частота инвертора при охлаждении (гц): ", fc_MIN_FREQ_COOL },
	       	{ "Минимальная частота инвертора при нагреве ГВС (гц): ", fc_MIN_FREQ_BOILER },
	       	{ "Минимальная частота инвертора РУЧНОЙ РЕЖИМ (гц): ", fc_MIN_FREQ_USER });
	      
	       strcat(Socket[thread].outBuf," - Максимальные частоты - \r\n");
	       TXT_KEYS("fc",
	       	{ "Максимальная частота инвертора (гц): ", fc_MAX_FREQ },
	       	{ "Максимальная частота инвертора в режиме охлаждения (гц): ", fc_MAX_FREQ_COOL },
	       	{ "Максимальная частота инвертора в режиме ГВС (гц): ", fc_MAX_FREQ_BOILER },
	       	{ "Максимальная частота инвертора РУЧНОЙ РЕЖИМ (гц): ", fc_MAX_FREQ_USER });
	
           strcat(Socket[thread].outBuf," - Ограничение ПИД регулятора - \r\n");	    
	       TXT_KEYS("fc",
	       	{ "Максимальный шаг (на увеличение) изменения частоты при ПИД регулировании (Гц): ", fc_PID_FREQ_STEP },
	       	{ "Шаг уменьшения инвертора при достижении максимальной температуры, мощности и тока (гц): ", fc_STEP_FREQ },
	       	{ "Шаг уменьшения инвертора при достижении максимальной температуры, мощности и тока ГВС (гц): ", fc_STEP_FREQ_BOILER });
	     } 
        else strcat(Socket[thread].outBuf,"FC absent \r\n");    
        sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));  
        
       strcpy(Socket[thread].outBuf,"\n  3.4. Электросчетчик SDM120\r\n");
         #ifdef USE_ELECTROMETER_SDM
           TXT_KEYS("sdm",
           	{ "MAX напряжение при контроле входного напряжения (В): ", sdm_MAX_VOLTAGE },
           	{ "MIN напряжение при контроле входного напряжения (В): ", sdm_MIN_VOLTAGE },
           	{ "MAX максимальная мощность при контроле мощности (Вт): ", sdm_MAX_POWER });
        #else
           strcat(Socket[thread].outBuf,"SDM120 absent\r\n");    
         #endif  

        strcat(Socket[thread].outBuf,"\n  3.5. Частотные датчики потока\r\n");
        for(i=0;i<FNUMBER;i++)  
           {
            strcat(Socket[thread].outBuf,HP.sFrequency[i].get_name()); if((x=8-strlen(HP.sFrequency[i].get_name()))>0) { for(j=0;j<x;j++)  strcat(Socket[thread].outBuf," "); }
            strcat(Socket[thread].outBuf,HP.sFrequency[i].get_note());  strcat(Socket[thread].outBuf,": "); 
            if (HP.sFrequency[i].get_present()) 
            {
             strcat(Socket[thread].outBuf," Контроль мин. потока: ");  if (HP.sFrequency[i].get_checkFlow())  strcat(Socket[thread].outBuf,(char*)cOne);else strcat(Socket[thread].outBuf,(char*)cZero);
             strcat(Socket[thread].outBuf,", Минимальный поток (куб/ч)="); _ftoa(Socket[thread].outBuf,(float)HP.sFrequency[i].get_minValue()/1000.0,3);
             strcat(Socket[thread].outBuf,", Коэффициент (имп*л)=");       _ftoa(Socket[thread].outBuf,(float)HP.sFrequency[i].get_kfValue()/100.0,3);
             strcat(Socket[thread].outBuf,", Теплоемкость, Дж/(кг*град)="); _ftoa(Socket[thread].outBuf,(float)HP.sFrequency[i].get_Capacity(),3);
             strcat(Socket[thread].outBuf,", Тест (куб/ч)="); _ftoa(Socket[thread].outBuf,(float)HP.sFrequency[i].get_testValue()/1000,3);
             
            }
            else strcat(Socket[thread].outBuf," absent"); 
            STR_END;         
           }

     sendBufferRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf));   

}

// Выгрузка ВСЕЙ микросхемы I2C-памяти одним файлом.
//
// Кнопка "Прочитать всю EEPROM" в морде (Web/files.html) ссылается на
// settings_eeprom.bin; без этого обработчика запрос провалился бы в поиск файла на
// SD и получил 404.
//
// В отличие от settings.bin здесь отдаётся чип целиком, включая журнал и
// незанятые области, - тем и полезно при разборе неисправностей.
bool get_binEEPROM(uint8_t thread)
{
	int len;
	strcpy(Socket[thread].outBuf, WEB_HEADER_OK_CT);
	strcat(Socket[thread].outBuf, WEB_HEADER_BIN_ATTACH);
	strcat(Socket[thread].outBuf, "settings_eeprom.bin\"\r\n\r\n");
	len = strlen(Socket[thread].outBuf);
	if(sendPacketRTOS(thread, (byte*)Socket[thread].outBuf, len, 0) != len) return 0;

	const uint32_t total = I2C_JOURNAL_EEPROM_NEXT;   // размер чипа в байтах
	for(uint32_t adr = 0; adr < total; adr += W5200_MAX_LEN) {
		uint16_t n = total - adr > W5200_MAX_LEN ? W5200_MAX_LEN : (uint16_t)(total - adr);
		if(readEEPROM_I2C(adr, (byte*)Socket[thread].outBuf, n)) {
			journal.jprintf("Error read EEPROM at 0x%X\n", (unsigned int)(adr));
			return 0;
		}
		if(sendBufferRTOS(thread, (uint8_t*)Socket[thread].outBuf, n) != n) return 0;
	}
	journal.jprintf("Read all EEPROM: %d bytes\n", (int)total);
	return true;
}

// Записать в клиента бинарный файл настроек
bool get_binSettings(uint8_t thread)
{
	int len;
	// 1. Заголовок
    strcpy(Socket[thread].outBuf, WEB_HEADER_OK_CT);
    strcat(Socket[thread].outBuf, WEB_HEADER_BIN_ATTACH);
    strcat(Socket[thread].outBuf, "settings.bin\"\r\n\r\n");
	strcat(Socket[thread].outBuf, "Ver. "); // Записать номер версии в которой делалось сохранение
    strcat(Socket[thread].outBuf, VERSION);
    strcat(Socket[thread].outBuf, " ");
    // Сюда можно запихивать текстовую информацию, при чтении бинарных данных она будет игнорироваться
    strcat(Socket[thread].outBuf, HEADER_BIN); // Заголовок по которому определяется начало "бинарных данных"
    len = strlen(Socket[thread].outBuf);
    if(sendPacketRTOS(thread, (byte*)Socket[thread].outBuf, len, 0) != len) return 0;

	// 2. Запись настроек ТН
    uint16_t *size = (uint16_t *)Socket[thread].outBuf;
	if(readEEPROM_I2C(I2C_SETTING_EEPROM, (byte*)size, sizeof(*size))) {
		journal.jprintf("Error read EEPROM at 0x%X\n", I2C_SETTING_EEPROM);
		return 0;
	}
	len = *size;
	if(len > W5200_MAX_LEN) return 0;
	if(readEEPROM_I2C(I2C_SETTING_EEPROM + 2, (byte*)Socket[thread].outBuf + 2, len)) {
		journal.jprintf("Error read EEPROM at 0x%X\n", I2C_SETTING_EEPROM);
		return 0;
	}
	len += 2;
	if(sendBufferRTOS(thread, (uint8_t*)Socket[thread].outBuf, len) != len) return 0; // передать пакет, при ошибке выйти
	// 3. Запись всех профилей
	len = HP.Prof.region_len();      // в новом формате незаписанные слоты в файл не попадают
	if(len > (int)sizeof(Socket[thread].outBuf)) return 0;
	if(readEEPROM_I2C(I2C_PROFILE_EEPROM, (byte*)Socket[thread].outBuf, len)) {
		journal.jprintf("Error read EEPROM at 0x%X\n", I2C_PROFILE_EEPROM);
		return 0;
	}
	if(sendBufferRTOS(thread, (uint8_t*)Socket[thread].outBuf, len) != len) return 0; // передать пакет, при ошибке выйти
	
    // 4. Расписание
	if((len = HP.Schdlr.load((byte*) Socket[thread].outBuf)) <= 0) {
		journal.jprintf("Error read EEPROM at 0x%X\n", I2C_SCHEDULER_EEPROM);
		return 0;
	}
	if(sendBufferRTOS(thread, (uint8_t*)Socket[thread].outBuf, len) != len) return 0; // передать пакет, при ошибке выйти
	return true;
}

// Получить файл со графиками возвращает число отправленных байт
void get_csvChart(uint8_t thread)
{ 
	// заголовок
	strcpy(Socket[thread].outBuf, WEB_HEADER_OK_CT);
	strcat(Socket[thread].outBuf, WEB_HEADER_TEXT_ATTACH);
	strcat(Socket[thread].outBuf, "chart.csv\"\r\n\r\n");
	sendPacketRTOS(thread, (byte*) Socket[thread].outBuf, strlen(Socket[thread].outBuf), 0);

	strcpy(Socket[thread].outBuf,"Point;");
	HP.get_listChart(Socket[thread].outBuf, ";");
	Socket[thread].outBuf[strlen(Socket[thread].outBuf) - 1] = '\0';
	STR_END;
	if(sendPacketRTOS(thread, (byte*) Socket[thread].outBuf, strlen(Socket[thread].outBuf), 0) == 0) return; // передать пакет, при ошибке выйти

	for(uint16_t point = 0; point < HP.Charts[0].get_num(); point++) { // По всем точкам
		itoa(point + 1, Socket[thread].outBuf, 10);
		for(uint8_t i = 0; i < sizeof(HP.Charts) / sizeof(HP.Charts[0]); i++) {
			strcat(Socket[thread].outBuf, ";");
			_dtoa(Socket[thread].outBuf, HP.Charts[i].get_Point(point), 2);
		}
		for(uint8_t index = 0; index < sizeof(ChartsOnFlySetup) / sizeof(ChartsOnFlySetup[0]); index++) {
			strcat(Socket[thread].outBuf, ";");
			int32_t value = -32767;
			switch (ChartsOnFlySetup[index].object) {
#ifdef TCONOUT
				case STATS_OBJ_Overcool:
					value = HP.Charts[HP.Chart_PressTemp_PCON].get_Point(point) - HP.Charts[HP.Chart_Temp_TCONOUT].get_Point(point);
					break;
#endif
				case STATS_OBJ_TCOMP_TCON:
					value = HP.Charts[HP.Chart_Temp_TCOMP].get_Point(point) - HP.Charts[HP.Chart_PressTemp_PCON].get_Point(point);
					break;
				case STATS_OBJ_Delta_GEO:
					value = HP.Charts[HP.Chart_Temp_TEVAING].get_Point(point) - HP.Charts[HP.Chart_Temp_TEVAOUTG].get_Point(point);
					break;
				case STATS_OBJ_Delta_OUT:
					value = HP.Charts[HP.Chart_Temp_TCONOUTG].get_Point(point) - HP.Charts[HP.Chart_Temp_TCONING].get_Point(point);
					break;
#ifdef FLOWEVA
				case STATS_OBJ_Power_GEO:
					value = (HP.Charts[HP.Chart_Temp_TEVAOUTG].get_Point(point) - HP.Charts[HP.Chart_Temp_TEVAING].get_Point(point)) * HP.Charts[HP.Chart_Flow_FLOWEVA].get_Point(point) / 10 * HP.sFrequency[FLOWEVA].get_Capacity() / 3600;
					break;
#endif
#ifdef FLOWCON
				case STATS_OBJ_Power_OUT:
					value = (HP.Charts[HP.Chart_Temp_TCONOUTG].get_Point(point) - HP.Charts[HP.Chart_Temp_TCONING].get_Point(point)) * HP.Charts[HP.Chart_Flow_FLOWCON].get_Point(point) / 10 * HP.sFrequency[FLOWCON].get_Capacity() / 3600;
					break;
#endif
			}
			_dtoa(Socket[thread].outBuf, value, 2);
		}
		STR_END;
		if(sendPacketRTOS(thread, (byte*) Socket[thread].outBuf, strlen(Socket[thread].outBuf), 0) == 0) return; // передать пакет, при ошибке выйти
	}
}

// Получить индексный файл при отсутвии SD карты
// выдача массива index_noSD в index_noSD
int16_t get_indexNoSD(uint8_t thread)
{
    uint16_t n,i=0;
       if(DBG_ON(DBG_HTTP)) { journal.jprintf("$Read: indexNoSD from memory\n"); }
   	strcpy(Socket[thread].outBuf, WEB_HEADER_OK_CT);
   	strcat(Socket[thread].outBuf, WEB_HEADER_TXT_KEEP);
   	strcat(Socket[thread].outBuf, WEB_HEADER_END);
   	sendPacketRTOS(thread, (byte*)Socket[thread].outBuf, strlen(Socket[thread].outBuf), 0);
    n=sizeof(index_noSD);              // сколько надо передать байт
    while (n>0)                        // Пока есть не отправленные данные
      {
       if(n>=W5200_MAX_LEN)
           {
            memcpy(Socket[thread].outBuf,index_noSD+i,W5200_MAX_LEN);
            if(sendPacketRTOS(thread,(byte*)Socket[thread].outBuf,W5200_MAX_LEN,0)==0) return 0 ;                      // не последний пакет
            i=i+W5200_MAX_LEN;n=n-W5200_MAX_LEN;
           }
       else 
           {
            memcpy(Socket[thread].outBuf,(index_noSD+i),n);
            if(sendPacketRTOS(thread,(byte*)Socket[thread].outBuf,n,0)==0) return 0; else return sizeof(index_noSD);  // последний пакет
           }
     } // while (n>0)
  return n;
}   

// Выдать файл журнала в виде файла
void get_txtJournal(uint8_t thread)
{
   get_Header(thread,(char*)"journal.txt");
   sendPrintfRTOS(thread, "  Системный журнал (размер %d из %d байт)\r\n", (int)(journal.available()),JOURNAL_LEN);
   STORE_DEBUG_INFO(17);
   journal.send_Data(thread);
}

// Сгенерировать тестовый файл
#define SIZE_TEST 2*1024*1024  // размер теста в байтах, реально передается округление до целого числа sizeof(Socket[thread].outBuf)
void get_datTest(uint8_t thread)
{
   uint16_t j;
   unsigned long startTick;
   // Заголовок
   strcpy(Socket[thread].outBuf, WEB_HEADER_OK_CT);
   strcat(Socket[thread].outBuf, "application/x-binary\r\nContent-Length:");
   _itoa((SIZE_TEST/sizeof(Socket[thread].outBuf))*sizeof(Socket[thread].outBuf),Socket[thread].outBuf);  //реальный размер передачи
   strcat(Socket[thread].outBuf,"\r\nContent-Disposition: attachment; filename=\"test.dat\"\r\n\r\n");
   sendPacketRTOS(thread,(byte*)Socket[thread].outBuf,strlen(Socket[thread].outBuf),0);
   
   // Генерация файла используется выходной буфер
   memset(Socket[thread].outBuf,0x55,sizeof(Socket[thread].outBuf));   // Заполнение буфера
   startTick=GetTickCount();                                      // Запомнить время старта
   for(j=0;j<SIZE_TEST/sizeof(Socket[thread].outBuf);j++)  
   {
    if (sendBufferRTOS(thread,(byte*)(Socket[thread].outBuf),sizeof(Socket[thread].outBuf))==0) break;
   }
   startTick=GetTickCount()-startTick;
   journal.jprintf("Download test.dat speed:%d bytes/sec\n",(int)(SIZE_TEST*1000/startTick));

}
