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
 * Профили настроек ТН: отдельная единица трансляции.
 *
 * Profile.h тянет Journal.h, а тот Constant.h и Config.h, поэтому #ifdef
 * LOAD_VERIFICATION и #ifdef TBOILER разрешаются так же, как в главной
 * единице трансляции. Проверено сверкой размеров методов.
 */
#include "Profile.h"
#include "HeatPump.h"     // HP, set_Error, ПИД
#include "../hw/Hardware.h"     // readEEPROM_I2C / writeEEPROM_I2C, uint16ToHex
#include "../log/Journal.h"      // journal
#include "Rtos.h"         // замки шин
#include "../util/Util.h"         // _crc16, urldecode, _itoa, my_atof
#include "../util/DateTime.h"     // DecodeTimeDate
#include "../net/WebShared.h"    // Socket[]
#include "../net/ParamRegistry.h" // таблицы параметров веб-API (внизу файла)
#include <stddef.h>               // offsetof

// Сокращать число профилей можно только вниз: старшие слоты отбрасываются при
// чтении, а обратный случай означал бы, что на носителе лежат номера, для
// которых в старой раскладке нет места.
static_assert(I2C_PROFIL_NUM <= I2C_PROFIL_NUM_LEGACY,
              "число профилей больше, чем слотов в старой раскладке области");
// Область профилей в EEPROM задана адресами и при смене числа профилей не
// двигается. Проверяем, что поток записей в неё влезает: запись это заголовок,
// номер слота и слот целиком, плюс NTAG_END и контрольная сумма в конце.
static_assert((uint32_t)I2C_PROFIL_NUM * (REC_HDR_SIZE + 1 + sizeof(ProfileSlot))
              + REC_HDR_SIZE + sizeof(uint16_t) <= I2C_SCHEDULER_EEPROM - I2C_PROFILE_EEPROM,
              "профили не помещаются в отведённую область EEPROM");

// ---------------------------------------------------------------------------
// ---- Класс Профиль ТН ----
// ---------------------------------------------------------------------------
// Класс предназначен для работы с настройками ТН. в пямяти хранится текущий профиль, а в еепром можно хранить до I2C_PROFIL_NUM профилей, и загружать их по одному в пямять
// также есть функции по сохраннию и удалению из еепром
// номер текущего профиля хранится в структуре type_SaveON
// инициализация профиля
void Profile::initProfile()
{
  err=OK;
  magic=0xaa;
  crc16=0;
  strcpy(dataProfile.name,"unknow");
  strcpy(dataProfile.note,"default profile");
  dataProfile.flags=0x00;
  dataProfile.len=get_sizeProfile();
  dataProfile.id=0;
  
  // Состояние ТН структура SaveON
  SaveON.magic=0x55;                   // признак данных, должно быть  0x55
  SETBIT0(SaveON.flags,fBoilerON);     // Бойлер выключен
  SaveON.startTime=0;                  // нет времени включения
  SaveON.mode=pOFF;                    // выключено
  
  // Охлаждение
  Cool.Rule=pHYSTERESIS,               // алгоритм гистерезис, интервальный режим
  Cool.Temp1=2000;                     // Целевая температура дома
  Cool.Temp2=1200;                     // Целевая температура Обратки
  SETBIT0(Cool.flags,fTarget);         // Что является целью ПИД - значения true (температура в доме), false (температура обратки).
  SETBIT0(Cool.flags,fWeather);        // флаг Погодозависимости
  Cool.dTemp=200;                      // Гистерезис целевой температуры
  Cool.pid_time=90;                    // Постоянная интегрирования времени в секундах ПИД ТН
  Cool.pid.Kp=1;                      // Пропорциональная составляющая ПИД ТН
  Cool.pid.Ki=0;                       // Интегральная составляющая ПИД ТН
  Cool.pid.Kd=3;                       // Дифференциальная составляющая ПИД ТН
  Cool.tempPID=2200;                // Целевая температура ПИД
 
 // Защиты
  Cool.tempInLim=1000;                    // Температура подачи (минимальная)
  Cool.tempOutLim=3500;                   // Температура обратки (макс)
  Cool.dt=1500;                        // Максимальная разность температур конденсатора.
  Cool.kWeatherPID=10;                    // Коэффициент погодозависимости в СОТЫХ градуса на градус
  
  
// Отопление
  Heat.Rule=pPID,              		 // алгоритм гистерезис, интервальный режим
  Heat.Temp1=2200;                     // Целевая температура дома
  Heat.Temp2=3500;                     // Целевая температура Обратки
  SETBIT1(Heat.flags,fTarget);         // Что является целью ПИД - значения true (температура в доме), false (температура обратки).
  SETBIT1(Heat.flags,fWeather);        // флаг Погодозависимости
  Heat.dTemp=50;                       // Гистерезис целевой температуры
  Heat.pid_time=60;                    // Постоянная интегрирования времени в секундах ПИД ТН
  Heat.pid.Kp=100;                      // Пропорциональная составляющая ПИД ТН
  Heat.pid.Ki=480;                       // Интегральная составляющая ПИД ТН
  Heat.pid.Kd=10;                       // Дифференциальная составляющая ПИД ТН
  Heat.tempPID=3200;                // Целевая температура ПИД
  Heat.add_delta_temp = 150;	 	   // Добавка температуры к установке бойлера, в градусах
  Heat.add_delta_hour = 5;		   	   // Начальный Час добавки температуры к установке бойлера
  Heat.add_delta_end_hour = 6;         // Конечный Час добавки температуры к установке
  Heat.tempInLim=4700;                    // Температура подачи (макс)
  Heat.tempOutLim=-5;                      // Температура обратки (минимальная)
  Heat.dt=1500;                        // Максимальная разность температур конденсатора.
  Heat.kWeatherPID=0;                    // Коэффициент погодозависимости в СОТЫХ градуса на градус
  Heat.WeatherBase = 0;
  Heat.WeatherTargetRange = 0;
  
 
 // Бойлер
  SETBIT1(Boiler.flags,fSchedule);      // !save! флаг Использование расписания выключено
  SETBIT0(Boiler.flags,fTurboBoiler);    // !save! флаг использование ТЭН для нагрева  выключено
  SETBIT0(Boiler.flags,fSalmonella);    // !save! флаг Сальмонела раз внеделю греть бойлер  выключено
  SETBIT0(Boiler.flags,fSalmonellaHold); // флаг Длительная обработка сальмонеллы выключен
  SETBIT0(Boiler.flags,fCirculation);   // !save! флаг управления циркуляционным насосом ГВС  выключено
  SETBIT1(Boiler.flags,fAddHeating);    // флаг флаг догрева ГВС ТЭНом
  SETBIT1(Boiler.flags,fScheduleAddHeat);
  SETBIT0(Boiler.flags,fResetHeat);     // флаг Сброса лишнего тепла в СО
  SETBIT0(Boiler.flags,fBoilerHeaterOnly); // флаг Бойлер греет только ТЭН: выключен (initProfile зовёт и reset.settings, биты не обнуляются сами)
  Boiler.TempTarget=5000;               // !save! Целевая температура бойлера
  Boiler.dTemp=500;                     // !save! гистерезис целевой температуры
  Boiler.tempInLim=5400;                   // !save! Температура подачи максимальная
  for (uint8_t i=0;i<7; i++) Boiler.Schedule[i]=0;             // !save! Расписание бойлера
  Boiler.Circul_Work=60*3;              // Время  работы насоса ГВС секунды (fCirculation)
  Boiler.Circul_Pause=60*10;            // Пауза в работе насоса ГВС  секунды (fCirculation)
  Boiler.Reset_Time=30;                 // время сброса излишков тепла в секундах (fResetHeat)
  Boiler.pid_time=20;                   // Постоянная интегрирования времени в секундах ПИД ГВС
  Boiler.pid.Kp=1;                      // Пропорциональная составляющая ПИД ГВС
  Boiler.pid.Ki=0;                      // Интегральная составляющая ПИД ГВС
  Boiler.pid.Kd=3;                      // Дифференциальная составляющая ПИД ГВС
  Boiler.tempPID=3800;                  // Целевая температура ПИД ГВС
  Boiler.tempRBOILER=3500;              // Температура ГВС при котором включается бойлер и отключатся ТН
  Boiler.dAddHeat = HYSTERESIS_BoilerAddHeat;
  Boiler.add_delta_temp = 1800;		    // Добавка температуры к установке бойлера, в градусах
  Boiler.add_delta_hour = 6;		    // Начальный Час добавки температуры к установке бойлера
  Boiler.add_delta_end_hour = 6;        // Конечный Час добавки температуры к установке
  Boiler.DischargeDelta = 10;
}

//Охлаждение Получить параметр в виде строки  второй параметр - наличие инвертора
char* Profile::get_paramCoolHP(char *var, char *ret, boolean fc)  
{
   if(strcmp(var,hp_RULE)==0)     {if (fc)   // Есть инвертор
	 								return web_fill_tag_select(ret, "HYSTERESIS:0;PID:0;HYBRID:0;", Cool.Rule);
				                  else {Cool.Rule=pHYSTERESIS;return strcat(ret,(char*)"HYSTERESIS:1;");}} else             // инвертора нет единсвенный алгоритм гистерезис
   if(strcmp(var,hp_TEMP1)==0)    {_dtoa(ret,Cool.Temp1/10,1); return ret;               } else             // целевая температура в доме
   if(strcmp(var,hp_TEMP2)==0)    {_dtoa(ret,Cool.Temp2/10,1); return ret;               } else             // целевая температура обратки
   if(strcmp(var,hp_TARGET)==0)   {if (!(GETBIT(Cool.flags,fTarget))) return strcat(ret,(char*)"Дом:1;Обратка:0;");
                                  else return strcat(ret,(char*)"Дом:0;Обратка:1;");           } else             // что является целью значения  0 (температура в доме), 1 (температура обратки).
   if(strcmp(var,hp_DTEMP)==0)    {_dtoa(ret,Cool.dTemp/10,1); return ret;               } else             // гистерезис целевой температуры
   if(strcmp(var,hp_HP_TIME)==0)  {return  _itoa(Cool.pid_time,ret);                               } else             // Постоянная интегрирования времени в секундах ПИД ТН
   if(strcmp(var,hp_HP_PRO)==0)   {_dtoa(ret,Cool.pid.Kp,3); return ret;              } else             // Пропорциональная составляющая ПИД ТН
   if(strcmp(var,hp_HP_IN)==0)    {_dtoa(ret,Cool.pid.Ki,3); return ret;              } else             // Интегральная составляющая ПИД ТН
   if(strcmp(var,hp_HP_DIF)==0)   {_dtoa(ret,Cool.pid.Kd,3); return ret;              } else             // Дифференциальная составляющая ПИД ТН
   if(strcmp(var,hp_TEMP_IN)==0)  {_dtoa(ret,Cool.tempInLim/10,1); return ret;              } else             // температура подачи (минимальная)
   if(strcmp(var,hp_TEMP_OUT)==0) {_dtoa(ret,Cool.tempOutLim/10,1); return ret;             } else             // температура обратки (максимальная)
   if(strcmp(var,hp_D_TEMP)==0)   {_dtoa(ret,Cool.dt/10,1); return ret;                  } else             // максимальная разность температур конденсатора.
   if(strcmp(var,hp_TEMP_PID)==0) {_dtoa(ret,Cool.tempPID/10,1); return ret;          } else             // Целевая темпеартура ПИД
   if(strcmp(var,hp_WEATHER)==0)  { if(GETBIT(Cool.flags,fWeather)) return strcat(ret,(char*)cOne);else return strcat(ret,(char*)cZero);} else // Использование погодозависимости
   if(strcmp(var,hp_targetPID)==0){_dtoa(ret,HP.CalcTargetPID(Cool),2); return ret;      } else
   if(strcmp(var,hp_K_WEATHER)==0){_dtoa(ret,Cool.kWeatherPID,3); return ret;            }                 // Коэффициент погодозависимости
 return  strcat(ret,(char*)cInvalid);   
}

// Отопление Получить параметр в виде строки  второй параметр - наличие инвертора
char* Profile::get_paramHeatHP(char *var,char *ret, boolean fc)
{
  if(strcmp(var,hp_RULE)==0)     {if (fc)   // Есть инвертор
	  	  	  	  	  	  	  	  	  return web_fill_tag_select(ret, "HYSTERESIS:0;PID:0;HYBRID:0;", Heat.Rule);
				                  else { Heat.Rule=pHYSTERESIS;return strcat(ret,(char*)"HYSTERESIS:1;");}} else             // инвертора нет единсвенный алгоритм гистерезис
   if(strcmp(var,hp_TEMP1)==0)    { _dtoa(ret,Heat.Temp1/10,1); return ret;                } else             // целевая температура в доме
   if(strcmp(var,ADD_DELTA_TEMP)==0) 	{  _dtoa(ret,Heat.add_delta_temp/10, 1); return ret;}else
   if(strcmp(var,ADD_DELTA_HOUR)==0) 	{  _itoa(Heat.add_delta_hour, ret); return ret;         }else
   if(strcmp(var,ADD_DELTA_END_HOUR)==0){  _itoa(Heat.add_delta_end_hour, ret); return ret;    	}else
   if(strcmp(var,hp_TEMP2)==0)    { _dtoa(ret,Heat.Temp2/10,1); return ret;                } else            // целевая температура обратки
   if(strcmp(var,hp_TARGET)==0)   { if (!(GETBIT(Heat.flags,fTarget))) return strcat(ret,(char*)"Дом:1;Обратка:0;");
                                  else return strcat(ret,(char*)"Дом:0;Обратка:1;");           } else             // что является целью значения  0 (температура в доме), 1 (температура обратки).
   if(strcmp(var,hp_DTEMP)==0)    { _dtoa(ret,Heat.dTemp/10,1); return ret;                } else             // гистерезис целевой температуры
   if(strcmp(var,hp_dTempGen)==0) { _dtoa(ret, Heat.dTempGen / 10, 1); return ret; } else
   if(strcmp(var,hp_HP_TIME)==0)  { return _itoa(Heat.pid_time,ret);                                } else             // Постоянная интегрирования времени в секундах ПИД ТН
   if(strcmp(var,hp_HP_PRO)==0)   { _dtoa(ret,Heat.pid.Kp,3); return ret;               } else             // Пропорциональная составляющая ПИД ТН
   if(strcmp(var,hp_HP_IN)==0)    { _dtoa(ret,Heat.pid.Ki,3); return ret;               } else             // Интегральная составляющая ПИД ТН
   if(strcmp(var,hp_HP_DIF)==0)   { _dtoa(ret,Heat.pid.Kd,3); return ret;               } else             // Дифференциальная составляющая ПИД ТН
   if(strcmp(var,hp_TEMP_IN)==0)  { _dtoa(ret,Heat.tempInLim/10,1); return ret;               } else             // температура подачи (минимальная)
   if(strcmp(var,hp_TEMP_OUT)==0) { _dtoa(ret,Heat.tempOutLim/10,1); return ret;              } else             // температура обратки (максимальная)
   if(strcmp(var,hp_D_TEMP)==0)   { _dtoa(ret,Heat.dt/10,1); return ret;                   } else             // максимальная разность температур конденсатора.
   if(strcmp(var,hp_TEMP_PID)==0) { _dtoa(ret,Heat.tempPID/10,1); return ret;              } else             // Целевая темпеартура ПИД
   if(strcmp(var,hp_WEATHER)==0)  { if(GETBIT(Heat.flags,fWeather)) return strcat(ret,(char*)cOne);else return strcat(ret,(char*)cZero);} else // Использование погодозависимости
   if(strcmp(var,hp_targetPID)==0){ _dtoa(ret,HP.CalcTargetPID(Heat),2); return ret;    } else
   if(strcmp(var,hp_K_WEATHER)==0){ _dtoa(ret,Heat.kWeatherPID,3); return ret;            }                 // Коэффициент погодозависимости
   if(strcmp(var,hp_kWeatherTarget)==0){ _dtoa(ret,Heat.kWeatherTarget,3); return ret;    }
   if(strcmp(var,hp_WeatherBase)==0){ _dtoa(ret,Heat.WeatherBase,0); return ret;    }
   if(strcmp(var,hp_WeatherTargetRange)==0){ _dtoa(ret,Heat.WeatherTargetRange,1); return ret;    }
   return  strcat(ret,(char*)cInvalid);
}

// Получить параметр из строки по имени var, результат ДОБАВЛЯЕТСЯ в строку ret
char* Profile::get_boiler(char *var, char *ret)
{ 
 if(strcmp(var,boil_BOILER_ON)==0){       if (GETBIT(SaveON.flags,fBoilerON))   return  strcat(ret,(char*)cOne); else return  strcat(ret,(char*)cZero); }else
 if(strcmp(var,boil_SCHEDULER_ON)==0){    if (GETBIT(Boiler.flags,fSchedule))   return  strcat(ret,(char*)cOne); else return  strcat(ret,(char*)cZero); }else
 if(strcmp(var,boil_SCHEDULER_ADDHEAT)==0){ if (GETBIT(Boiler.flags,fScheduleAddHeat)) return  strcat(ret,(char*)cOne); else return  strcat(ret,(char*)cZero); }else
 if(strcmp(var,boil_TOGETHER_HEAT)==0){ if (GETBIT(Boiler.flags,fBoilerTogetherHeat)) return  strcat(ret,(char*)cOne); else return  strcat(ret,(char*)cZero); }else
 if(strcmp(var,boil_fBoilerPID)==0){ if (GETBIT(Boiler.flags,fBoilerPID)) return  strcat(ret,(char*)cOne); else return  strcat(ret,(char*)cZero); }else
 if(strcmp(var,boil_TURBO_BOILER)==0){    if (GETBIT(Boiler.flags,fTurboBoiler))return  strcat(ret,(char*)cOne); else return  strcat(ret,(char*)cZero); }else
 if(strcmp(var,boil_SALMONELLA)==0){      if (GETBIT(Boiler.flags,fSalmonella)) return  strcat(ret,(char*)cOne); else return  strcat(ret,(char*)cZero); }else
 if(strcmp(var,boil_fSalmonellaHold)==0){ if (GETBIT(Boiler.flags,fSalmonellaHold)) return  strcat(ret,(char*)cOne); else return  strcat(ret,(char*)cZero); }else
 if(strcmp(var,boil_CIRCULATION)==0){     if (GETBIT(Boiler.flags,fCirculation))return  strcat(ret,(char*)cOne); else return  strcat(ret,(char*)cZero); }else
 if(strcmp(var,boil_TEMP_TARGET)==0){     _dtoa(ret,Boiler.TempTarget/10,1); return ret;    }else
 if(strcmp(var,ADD_DELTA_TEMP)==0) 		{ _dtoa(ret,Boiler.add_delta_temp/10, 1); return ret; }else
 if(strcmp(var,ADD_DELTA_HOUR)==0) 		{ _itoa(Boiler.add_delta_hour, ret); return ret;           }else
 if(strcmp(var,ADD_DELTA_END_HOUR)==0) 	{ _itoa(Boiler.add_delta_end_hour, ret); return ret;    	}else
 if(strcmp(var,boil_DTARGET)==0){         _dtoa(ret,Boiler.dTemp/10,1); return ret;        }else
 if(strcmp(var,boil_TEMP_MAX)==0){        _dtoa(ret,Boiler.tempInLim/10,1); return ret;       }else
 if(strcmp(var,boil_SCHEDULER)==0){       return strcat(ret,get_Schedule(Boiler.Schedule));          }else  
 if(strcmp(var,boil_CIRCUL_WORK)==0){     return _itoa(Boiler.Circul_Work/60,ret);                   }else                            // Время  работы насоса ГВС секунды (fCirculation)
 if(strcmp(var,boil_CIRCUL_PAUSE)==0){    return _itoa(Boiler.Circul_Pause/60,ret);                  }else                            // Пауза в работе насоса ГВС  секунды (fCirculation)
 if(strcmp(var,boil_RESET_HEAT)==0){      if (GETBIT(Boiler.flags,fResetHeat))   return  strcat(ret,(char*)cOne); else return  strcat(ret,(char*)cZero); }else       // флаг Сброса лишнего тепла в СО
 if(strcmp(var,boil_RESET_TIME)==0){      return  _itoa(Boiler.Reset_Time,ret);                      }else                            // время сброса излишков тепла в секундах (fResetHeat)
 if(strcmp(var,boil_BOIL_TIME)==0){       return  _itoa(Boiler.pid_time,ret);                        }else                            // Постоянная интегрирования времени в секундах ПИД ГВС
 if(strcmp(var,boil_BOIL_PRO)==0){        _dtoa(ret,Boiler.pid.Kp,3); return ret;        }else                            // Пропорциональная составляющая ПИД ГВС
 if(strcmp(var,boil_BOIL_IN)==0){         _dtoa(ret,Boiler.pid.Ki,3); return ret;       }else                            // Интегральная составляющая ПИД ГВС
 if(strcmp(var,boil_BOIL_DIF)==0){        _dtoa(ret,Boiler.pid.Kd,3); return ret;       }else                            // Дифференциальная составляющая ПИД ГВС
 if(strcmp(var,boil_BOIL_TEMP)==0){       _dtoa(ret,Boiler.tempPID/10,1); return ret;       }else                            // Целевая темпеартура ПИД ГВС
 if(strcmp(var,boil_ADD_HEATING)==0){     if(GETBIT(Boiler.flags,fAddHeating)) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero); }else   // флаг использования тена для догрева ГВС
 if(strcmp(var,boil_fAddHeatingForce)==0){if(GETBIT(Boiler.flags,fAddHeatingForce)) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero); }else
 if(strcmp(var,boil_TEMP_RBOILER)==0){    _dtoa(ret,Boiler.tempRBOILER/10,1); return ret;    }else                            // температура включения догрева бойлера
 if(strcmp(var,boil_dAddHeat)==0){        _dtoa(ret,Boiler.dAddHeat/10,1); return ret;       }else
 if(strcmp(var,boil_DischargeDelta)==0){  _dtoa(ret, Boiler.DischargeDelta, 1); return ret;       }else
 if(strcmp(var,boil_HeatUrgently)==0){if(HP.HeatBoilerUrgently) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero); }else
 if(strcmp(var,boil_fHeaterOnly)==0){ if(GETBIT(Boiler.flags, fBoilerHeaterOnly)) return strcat(ret,(char*)cOne); else return strcat(ret,(char*)cZero); }else
 return strcat(ret,(char*)cInvalid);
}

// ---------------------------------------------------------------------------
// КЕШ ВСЕХ ПРОФИЛЕЙ В ОЗУ. Пояснение - у объявления ProfileSlot в Profile.h.
// ---------------------------------------------------------------------------

// Контрольная сумма слота кеша. Считается ровно теми же кусками и в том же
// порядке, что и get_crc16_mem() для активного профиля, иначе суммы не
// сойдутся с записанными в EEPROM.
uint16_t Profile::cache_crc_of(const ProfileSlot &sl)
{
	uint16_t crc = 0xFFFF;
	crc = calc_crc16((uint8_t*)&sl.dataProfile, sizeof(sl.dataProfile), crc);
	crc = calc_crc16((uint8_t*)&sl.SaveON, sizeof(sl.SaveON), crc);
	crc = calc_crc16((uint8_t*)&sl.Cool, sizeof(sl.Cool), crc);
	crc = calc_crc16((uint8_t*)&sl.Heat, sizeof(sl.Heat), crc);
	crc = calc_crc16((uint8_t*)&sl.Boiler, sizeof(sl.Boiler), crc);
	crc = calc_crc16((uint8_t*)&sl.DailySwitch, sizeof(sl.DailySwitch), crc);
	return crc;
}

// Область профилей в НОВОМ формате - поток записей, а не массив слотов:
//
//   [NTAG_Profile][длина][номер слота][данные слота]   * только занятые
//   [NTAG_END][0]
//   [crc16]
//
// Незаписанный слот в поток не попадает: иначе в скачиваемый settings.bin утекли бы
// байты, лежавшие в его ячейках.
bool Profile::region_is_new(void)
{
	uint8_t hdr[2];
	if(readEEPROM_I2C(I2C_PROFILE_EEPROM, hdr, sizeof(hdr))) return false;
	return rec_hdr_tag(hdr) == NTAG_Profile;
}

// Длина области профилей на носителе. В старом формате это массив слотов
// подряд, в новом - только занятые, поэтому выгрузка после конверсии
// становится короче ровно на незаписанные слоты. Их содержимое и утекало
// в settings.bin.
//
// Для старой раскладки берётся I2C_PROFIL_NUM_LEGACY, а не I2C_PROFIL_NUM:
// число слотов на носителе задано тем, чем его записали, и сокращение числа
// профилей его не меняет. По новой константе область обрезалась бы вдвое.
uint16_t Profile::region_len(void)
{
	if(!region_is_new()) return get_sizeProfile() * I2C_PROFIL_NUM_LEGACY;
	// Длина считается по САМОМУ носителю, а не по числу занятых слотов кеша.
	// Кеш и область совпадают не всегда: после неудачного чтения кеш пуст, а
	// в области лежит поток. Считать по кешу значило бы обрезать выгрузку
	// посреди записи и выдать испорченный settings.bin.
	uint32_t addr = I2C_PROFILE_EEPROM;
	uint8_t  hdr[REC_HDR_SIZE];
	while(addr < I2C_SCHEDULER_EEPROM) {
		if(readEEPROM_I2C(addr, hdr, sizeof(hdr))) break;
		if(rec_hdr_tag(hdr) != NTAG_Profile) break;         // NTAG_END или мусор
		addr += REC_HDR_SIZE + rec_hdr_len(hdr);
	}
	return addr - I2C_PROFILE_EEPROM + REC_HDR_SIZE + sizeof(uint16_t);  // плюс END и сумма
}

int16_t Profile::save_region(void)
{
	uint32_t addr = I2C_PROFILE_EEPROM;
	uint16_t crc = 0xFFFF;
	uint8_t  hdr[REC_HDR_SIZE + 1];                  // заголовок и номер слота
	uint8_t  written = 0;
	for(uint8_t i = 0; i < I2C_PROFIL_NUM; i++) {
		if(cache_magic[i] != 0xaa) continue;         // незаписанный слот пропускается
		if(cache_crc_of(cache[i]) != cache_crc[i]) { // содержимое изменилось мимо cache_store
			journal.jprintf(" Profile #%d: RAM corrupted, not saved!\n", i);
			continue;                                // писать испорченное в EEPROM нельзя
		}
		rec_hdr_put(hdr, NTAG_Profile, 1 + sizeof(ProfileSlot));
		hdr[REC_HDR_SIZE] = i;
		if(writeEEPROM_I2C(addr, hdr, sizeof(hdr))) goto xErr;
		crc = calc_crc16(hdr, sizeof(hdr), crc);
		addr += sizeof(hdr);
		if(writeEEPROM_I2C(addr, (uint8_t*)&cache[i], sizeof(ProfileSlot))) goto xErr;
		crc = calc_crc16((uint8_t*)&cache[i], sizeof(ProfileSlot), crc);
		addr += sizeof(ProfileSlot);
		written++;
	}
	rec_hdr_put(hdr, NTAG_END, 0);
	if(writeEEPROM_I2C(addr, hdr, REC_HDR_SIZE)) goto xErr;
	crc = calc_crc16(hdr, REC_HDR_SIZE, crc);
	addr += REC_HDR_SIZE;
	if(writeEEPROM_I2C(addr, (uint8_t*)&crc, sizeof(crc))) goto xErr;
	addr += sizeof(crc);
	journal.jprintf(" Save profiles OK: %d of %d, %d bytes, crc: %04x\n",
	                written, (int)I2C_PROFIL_NUM, (int)(addr - I2C_PROFILE_EEPROM), crc);
	return addr - I2C_PROFILE_EEPROM;
xErr:
	set_Error(ERR_SAVE_PROFILE, (char*)nameHeatPump);
	return ERR_SAVE_PROFILE;
}

int8_t Profile::load_region(void)
{
	uint32_t addr = I2C_PROFILE_EEPROM;
	uint16_t crc = 0xFFFF;
	uint8_t  hdr[REC_HDR_SIZE + 1];
	uint8_t  used = 0;
	for(uint8_t i = 0; i < I2C_PROFIL_NUM; i++) { cache_magic[i] = PROFILE_EMPTY; cache_crc[i] = 0; }
	while(addr < I2C_SCHEDULER_EEPROM) {
		if(readEEPROM_I2C(addr, hdr, REC_HDR_SIZE)) goto xErr;
		uint16_t tag = rec_hdr_tag(hdr), len = rec_hdr_len(hdr);
		if(tag == NTAG_END) {
			crc = calc_crc16(hdr, REC_HDR_SIZE, crc);
			addr += REC_HDR_SIZE;
			uint16_t stored;
			if(readEEPROM_I2C(addr, (uint8_t*)&stored, sizeof(stored))) goto xErr;
			if(crc != stored) {
				journal.jprintf(" Profiles: crc %04x != %04x!\n", crc, stored);
				for(uint8_t i = 0; i < I2C_PROFIL_NUM; i++) cache_magic[i] = PROFILE_EMPTY;
				return err = ERR_CRC16_PROFILE;
			}
			journal.jprintf(" Profiles loaded: %d, crc: %04x\n", used, crc);
			return OK;
		}
		if(tag != NTAG_Profile) goto xErr;                                     // поток испорчен
		if(len != 1 + sizeof(ProfileSlot)) {                                  // другая раскладка слота (запись другой прошивки)
			journal.jprintf(" Profile record len %d, expected %d\n", len, (int)(1 + sizeof(ProfileSlot)));
			for(uint8_t i = 0; i < I2C_PROFIL_NUM; i++) cache_magic[i] = PROFILE_EMPTY;   // с чужой раскладкой слоты не берём
			set_Error(ERR_BAD_LEN_PROFILE, (char*)nameHeatPump);
			return err = ERR_BAD_LEN_PROFILE;
		}
		if(readEEPROM_I2C(addr + REC_HDR_SIZE, hdr + REC_HDR_SIZE, 1)) goto xErr;
		crc = calc_crc16(hdr, sizeof(hdr), crc);
		uint8_t n = hdr[REC_HDR_SIZE];
		addr += sizeof(hdr);
		if(n >= I2C_PROFIL_NUM) {
			// Область записана прошивкой, где профилей было больше. Отбрасывать
			// такие слоты здесь незачем: формат 150 существует только на стенде,
			// в поле его нет, и конверсия 150 -> 150 никому не нужна. Пусть это
			// будет отказом чтения (решение владельца).
			//
			// Отбрасывание старших слотов работает на переходе 147 -> 150:
			// старые читатели берут первые I2C_PROFIL_NUM слотов, и первое же
			// сохранение записывает область уже в сокращённом виде.
			journal.jprintf(" Profile slot #%d >= %d, area written by older build\n",
			                n, (int)I2C_PROFIL_NUM);
			goto xErr;
		}
		if(readEEPROM_I2C(addr, (uint8_t*)&cache[n], sizeof(ProfileSlot))) goto xErr;
		crc = calc_crc16((uint8_t*)&cache[n], sizeof(ProfileSlot), crc);
		addr += sizeof(ProfileSlot);
		cache_magic[n] = 0xaa;
		cache_crc[n] = cache_crc_of(cache[n]);
		used++;
	}
xErr:
	set_Error(ERR_LOAD_PROFILE, (char*)nameHeatPump);
	return err = ERR_LOAD_PROFILE;
}

// Сделать слот кеша текущим профилем. Заменяет чтение из EEPROM: после
// шага 4 все профили и так лежат в оперативной памяти.
int8_t Profile::cache_activate(int8_t num)
{
	if(num < 0 || num >= I2C_PROFIL_NUM) return err = ERR_LOAD_PROFILE;
	if(cache_magic[num] != 0xaa) { journal.jprintf(" Profile #%d is empty\n", num); return OK; }
	if(cache_crc_of(cache[num]) != cache_crc[num]) {
		journal.jprintf(" Profile #%d: RAM corrupted!\n", num);
		return err = ERR_CRC16_PROFILE;
	}
	const ProfileSlot &sl = cache[num];
	memcpy(&dataProfile, &sl.dataProfile, sizeof(dataProfile));
	memcpy(&SaveON, &sl.SaveON, sizeof(SaveON));
	SaveON.mode = HouseModeAllowed(SaveON.mode, HouseOnlyHeat);   // ONLY_HEAT: охлаждение из старого профиля -> отопление
	memcpy(&Cool, &sl.Cool, sizeof(Cool));
	memcpy(&Heat, &sl.Heat, sizeof(Heat));
	memcpy(&Boiler, &sl.Boiler, sizeof(Boiler));
	memcpy(&DailySwitch, &sl.DailySwitch, sizeof(DailySwitch));
	magic = 0xaa;
	crc16 = cache_crc[num];
	update_list(num);
#ifdef TBOILER   // сальмонелла меняет верхнюю границу датчика бойлера
	if(GETBIT(HP.Prof.Boiler.flags, fSalmonella)) HP.sTemp[TBOILER].set_maxTemp(SALMONELLA_TEMP + 300);
	else HP.sTemp[TBOILER].set_maxTemp(TempSetup[TBOILER].max);
#endif
	journal.jprintf(" Profile #%d activated\n", num);
	return OK;
}

// Прочитать все слоты из EEPROM в кеш. Читается по одному блоку, как это
// делает load(), - общий буфер здесь не используется намеренно: он занят
// ответом веб-запроса всякий раз, когда сохранение идёт из морды.
int8_t Profile::cache_load_all(void)
{
	if(region_is_new()) return load_region();    // область опознаёт себя сама
	LegacyProfileSizes sz;
	if(legacy_profile_sizes(HP.Option.ver, &sz, sizeof(magic) + sizeof(crc16)))
		return cache_load_legacy(sz);            // формат старше 147
	// Слоты 5..9 старой раскладки сюда не попадают: цикл идёт до нового
	// I2C_PROFIL_NUM. Так и задумано - число профилей сокращено, старшие
	// отбрасываются при конверсии. Шаг по носителю остаётся прежним, он
	// задан get_sizeProfile(), а не числом слотов.
	uint8_t used = 0, bad = 0;
	for(uint8_t i = 0; i < I2C_PROFIL_NUM; i++) {
		int32_t adr = I2C_PROFILE_EEPROM + (int32_t)get_sizeProfile() * i;
		uint8_t m = PROFILE_EMPTY;
		if(readEEPROM_I2C(adr, &m, sizeof(m))) { set_Error(ERR_LOAD_PROFILE, (char*)nameHeatPump); return ERR_LOAD_PROFILE; }
		cache_magic[i] = m;
		memset(&cache[i], 0, sizeof(cache[i]));
		cache_crc[i] = 0;
		if(m != 0xaa) continue;                       // слот не использован - читать нечего
		adr += sizeof(magic);
		uint16_t stored;
		if(readEEPROM_I2C(adr, (uint8_t*)&stored, sizeof(stored))) { set_Error(ERR_LOAD_PROFILE, (char*)nameHeatPump); return ERR_LOAD_PROFILE; }
		adr += sizeof(crc16);
		ProfileSlot &sl = cache[i];
		if(   readEEPROM_I2C(adr, (uint8_t*)&sl.dataProfile, sizeof(sl.dataProfile))
		   || readEEPROM_I2C(adr += sizeof(sl.dataProfile), (uint8_t*)&sl.SaveON, sizeof(sl.SaveON))
		   || readEEPROM_I2C(adr += sizeof(sl.SaveON), (uint8_t*)&sl.Cool, sizeof(sl.Cool))
		   || readEEPROM_I2C(adr += sizeof(sl.Cool), (uint8_t*)&sl.Heat, sizeof(sl.Heat))
		   || readEEPROM_I2C(adr += sizeof(sl.Heat), (uint8_t*)&sl.Boiler, sizeof(sl.Boiler))
		   || readEEPROM_I2C(adr += sizeof(sl.Boiler), (uint8_t*)&sl.DailySwitch, sizeof(sl.DailySwitch))) {
			set_Error(ERR_LOAD_PROFILE, (char*)nameHeatPump);
			return ERR_LOAD_PROFILE;
		}
		used++;
		cache_crc[i] = stored;
		if(cache_crc_of(sl) != stored) {              // слот в EEPROM испорчен
			cache_magic[i] = PROFILE_EMPTY;           // не давать писать его обратно
			bad++;
		}
	}
	journal.jprintf(" Profiles cached: %d used, %d bad, slots %d..%d dropped\n",
	                used, bad, (int)I2C_PROFIL_NUM, (int)I2C_PROFIL_NUM_LEGACY - 1);
	return bad ? ERR_CRC16_PROFILE : OK;
}

// Чтение профилей СТАРЫХ эпох (формат до версии 147) в кеш.
//
// Чистое чтение: запись идёт только в новом формате и только по сохранению,
// поэтому читателю писать нечего.
//
// Слоты читаются по одному, без общего буфера: cache_load_all вызывается и
// после восстановления настроек из файла, а там Socket[0].outBuf занят.
int8_t Profile::cache_load_legacy(const LegacyProfileSizes &sz)
{
	uint8_t tmp[128];                       // самый длинный старый блок - 120 байт
	uint8_t used = 0, bad = 0;
	// Как и в ветке 147: читаются первые I2C_PROFIL_NUM слотов, старшие
	// отбрасываются. Шаг по носителю - sz.total, от числа слотов не зависит.
	for(uint8_t i = 0; i < I2C_PROFIL_NUM; i++) {
		cache_magic[i] = PROFILE_EMPTY;
		cache_crc[i] = 0;
		memset(&cache[i], 0, sizeof(cache[i]));
		uint32_t adr = I2C_PROFILE_EEPROM + (uint32_t)sz.total * i;
		uint8_t m;
		if(readEEPROM_I2C(adr, &m, sizeof(m))) return err = ERR_LOAD_PROFILE;
		if(m != 0xaa) continue;             // слот не записан
		uint16_t stored;
		if(readEEPROM_I2C(adr + sizeof(magic), (uint8_t*)&stored, sizeof(stored))) return err = ERR_LOAD_PROFILE;
		adr += sizeof(magic) + sizeof(crc16);

		struct { uint16_t had; uint8_t *to; uint16_t want; } blk[] = {
			{ sz.dataProfile, (uint8_t*)&cache[i].dataProfile, sizeof(cache[i].dataProfile) },
			{ sz.SaveON,      (uint8_t*)&cache[i].SaveON,      sizeof(cache[i].SaveON)      },
			{ sz.HeatCool,    (uint8_t*)&cache[i].Cool,        sizeof(cache[i].Cool)        },
			{ sz.HeatCool,    (uint8_t*)&cache[i].Heat,        sizeof(cache[i].Heat)        },
			{ sz.Boiler,      (uint8_t*)&cache[i].Boiler,      sizeof(cache[i].Boiler)      },
			{ sz.DailySwitch, (uint8_t*)&cache[i].DailySwitch, sizeof(cache[i].DailySwitch) },
		};
		uint16_t crc = 0xFFFF;
		bool ok = true;
		for(uint8_t b = 0; b < sizeof(blk)/sizeof(blk[0]); b++) {
			uint16_t n = blk[b].had;
			if(n == 0) continue;            // в той версии поля не было
			if(n > sizeof(tmp)) { ok = false; break; }
			if(readEEPROM_I2C(adr, tmp, n)) return err = ERR_LOAD_PROFILE;
			crc = calc_crc16(tmp, n, crc);  // сумма считается по СТАРОЙ раскладке
			memcpy(blk[b].to, tmp, n <= blk[b].want ? n : blk[b].want);
			adr += n;
		}
		if(!ok || crc != stored) { bad++; continue; }   // слот испорчен - не берём
		cache_magic[i] = 0xaa;
		cache_crc[i] = cache_crc_of(cache[i]);          // сумма уже по НОВОЙ раскладке
		used++;
	}
	journal.jprintf(" Profiles read as v.%d: %d used, %d bad\n", HP.Option.ver, used, bad);
	return bad ? ERR_CRC16_PROFILE : OK;
}

// Обновить слот кеша из активных членов. Вызывается после успешной записи
// профиля в EEPROM, чтобы кеш не разошёлся с носителем.
void Profile::cache_store(int8_t num)
{
	if(num < 0 || num >= I2C_PROFIL_NUM) return;
	ProfileSlot &sl = cache[num];
	memcpy(&sl.dataProfile, &dataProfile, sizeof(sl.dataProfile));
	memcpy(&sl.SaveON, &SaveON, sizeof(sl.SaveON));
	memcpy(&sl.Cool, &Cool, sizeof(sl.Cool));
	memcpy(&sl.Heat, &Heat, sizeof(sl.Heat));
	memcpy(&sl.Boiler, &Boiler, sizeof(sl.Boiler));
	memcpy(&sl.DailySwitch, &DailySwitch, sizeof(sl.DailySwitch));
	cache_magic[num] = 0xaa;
	cache_crc[num] = cache_crc_of(sl);
}

// Пересчитать суммы всех занятых слотов и сверить с запомненными.
// Расхождение означает, что содержимое кеша изменилось не через cache_store,
// то есть порчу памяти. Возвращает число таких слотов.
uint8_t Profile::cache_verify(void)
{
	uint8_t bad = 0;
	for(uint8_t i = 0; i < I2C_PROFIL_NUM; i++) {
		if(cache_magic[i] != 0xaa) continue;
		if(cache_crc_of(cache[i]) != cache_crc[i]) bad++;
	}
	return bad;
}

uint16_t Profile::get_crc16_mem()  // Расчитать контрольную сумму
{
	uint16_t crc = 0xFFFF;
	crc = calc_crc16((uint8_t*)&dataProfile, sizeof(dataProfile), crc);
	crc = calc_crc16((uint8_t*)&SaveON, sizeof(SaveON), crc);
	crc = calc_crc16((uint8_t*)&Cool, sizeof(Cool), crc);
	crc = calc_crc16((uint8_t*)&Heat, sizeof(Heat), crc);
	crc = calc_crc16((uint8_t*)&Boiler, sizeof(Boiler), crc);
	crc = calc_crc16((uint8_t*)&DailySwitch, sizeof(DailySwitch), crc);
	return crc;
}

// Проверить контрольную сумму ПРОФИЛЯ в EEPROM для данных на выходе ошибка, длина определяется из заголовка
int8_t Profile::check_crc16_eeprom(int8_t num)
{
  uint16_t i, crc16tmp;
  byte x;
  uint16_t crc= 0xFFFF;
  int32_t adr=I2C_PROFILE_EEPROM+dataProfile.len*num;     // вычислить адрес начала данных
  
  if (readEEPROM_I2C(adr, (byte*)&x, sizeof(x))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return ERR_SAVE_PROFILE;}  adr=adr+sizeof(x);              // прочитать заголовок
  if (x!=0xaa)   return OK;                                                                                                                              // профиль пустой, или его вообще нет, контрольная сумма не актуальна
  if (readEEPROM_I2C(adr, (byte*)&crc16tmp, sizeof(crc16tmp))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return ERR_SAVE_PROFILE;}  adr=adr+sizeof(crc16tmp);        // прочитать crc16
  
  for (i=0;i<dataProfile.len-1-2;i++) {if (readEEPROM_I2C(adr+i, (byte*)&x, sizeof(x))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return ERR_LOAD_PROFILE;}  crc=_crc16(crc,x);} // расчет - записанной в еепром сумму за вычетом заголовка
  if (crc==crc16tmp) return OK; 
  else            return err=ERR_CRC16_PROFILE;
}
// Проверить контрольную сумму в буфере для данных на выходе ошибка, длина определяется из заголовка
int8_t  Profile::check_crc16_buf(int32_t adr, byte* buf)   
{
  uint16_t i, readCRC, crc=0xFFFF;
  byte x;
  memcpy((byte*)&x,buf+adr,sizeof(x)); adr=adr+sizeof(x);                                              // заголовок
  if (x!=0xaa)   return OK;                                                                           // профиль пустой, или его вообще нет, контрольная сумма не актуальна
  memcpy((byte*)&readCRC,buf+adr,sizeof(readCRC)); adr=adr+sizeof(readCRC);                            // прочитать crc16
  // Расчет контрольной суммы
  crc = calc_crc16(buf + adr, dataProfile.len - 1 - 2, crc);                                           // расчет -2 за вычетом CRC16 из длины и заголовка один байт
  if (crc==readCRC) return OK; 
  else              return err=ERR_CRC16_EEPROM;
}

// Чтение старых эпох (cache_load_legacy выше) ничего не пишет: по решению владельца
// запись в EEPROM идёт только по кнопке сохранения, а чтение настроек не должно
// молча писать в EEPROM при каждой загрузке.


// Записать профайл в еепром под номерм num
// Возвращает число записанных байт или ошибку
int16_t  Profile::save(int8_t num)
{
  // Область профилей - поток записей, отдельный слот в ней не переписать.
  // Поэтому запись профиля это "положить в кеш и переписать область".
  magic = 0xaa;
  dataProfile.len = get_sizeProfile();          // длина данных профиля
  dataProfile.saveTime = rtcSAM3X8.unixtime();  // запомнить время сохранения профиля
  cache_store(num);
  int16_t ret = save_region();
  if(ret > 0) update_list(num);                 // обновить список
  return ret > 0 ? dataProfile.len : ret;
}


// загрузить профайл num из еепром память
int32_t Profile::load(int8_t num)
{
  // Сохранённый Option.numProf может указывать на слот, которого больше нет:
  // число профилей сокращено с 10 до 5, а настройки писались прошивкой, где
  // их было десять. Это не ошибка чтения, а ожидаемое следствие сокращения,
  // поэтому берём первый слот и говорим об этом в журнале.
  if(num >= I2C_PROFIL_NUM) {
    journal.jprintf(" Profile #%d dropped (max %d), using #0\n", num, (int)I2C_PROFIL_NUM);
    num = 0;
  }
  // Профили живут в оперативной памяти, поэтому "загрузить профиль" - это
  // сделать слот кеша текущим. Чтение EEPROM здесь было бы вдобавок неверным:
  // в новом формате область это поток записей, и слота по адресу
  // I2C_PROFILE_EEPROM + len*num там просто нет.
  return cache_activate(num);
}


 // профиль Получить параметры по имени var, результат ДОБАВЛЯЕТСЯ в строку ret
char*   Profile::get_paramProfile(char *var, char *ret)
{
	if(strcmp(var,prof_NAME_PROFILE)==0)   { return strcat(ret,dataProfile.name);                             }else
	if(strcmp(var,prof_ENABLE_PROFILE)==0) { if (GETBIT(dataProfile.flags,fEnabled)) return  strcat(ret,(char*)cOne);
											 else                                    return  strcat(ret,(char*)cZero);}else
	if(strcmp(var,prof_ID_PROFILE)==0)     { return _itoa(dataProfile.id + 1,ret);                            }else
	if(strcmp(var,prof_NOTE_PROFILE)==0)   { return strcat(ret,dataProfile.note);                             }else
	if(strcmp(var,prof_DATE_PROFILE)==0)   { return DecodeTimeDate(dataProfile.saveTime,ret);                 }else// параметры только чтение
	if(strcmp(var,prof_CRC16_PROFILE)==0)  { return strcat(ret,uint16ToHex(crc16));                           }else
	if(strcmp(var,prof_NUM_PROFILE)==0)    { return _itoa(I2C_PROFIL_NUM,ret);                                }else
	if(strcmp(var,prof_TBL_PROFILE)==0)    { return get_table(ret);                                           }else
	if(strncmp(var, prof_DailySwitch, sizeof(prof_DailySwitch)-1) == 0) {
		var += sizeof(prof_DailySwitch)-1;
		uint8_t i = *(var + 1) - '0';
		if(i >= DAILY_SWITCH_MAX) return false;
		if(*var == prof_DailySwitchDevice) {
		 _itoa(DailySwitch[i].Device, ret);
		} else if(*var == prof_DailySwitchOn) {
		 m_snprintf(ret + m_strlen(ret), 32, "%02d:%d0", DailySwitch[i].TimeOn / 10, DailySwitch[i].TimeOn % 10);
		} else if(*var == prof_DailySwitchOff) {
		 m_snprintf(ret + m_strlen(ret), 32, "%02d:%d0", DailySwitch[i].TimeOff / 10, DailySwitch[i].TimeOff % 10);
		}
		return ret;
	} else
		return  strcat(ret,(char*)cInvalid);
}

// Временные данные для профиля
static type_dataProfile temp_prof;

// Имя и описание в таблицу идут текстом: разделители заменяем пробелом.
static void append_clean(char *ret, const char *src)
{
	char *p = ret + m_strlen(ret);
	while(*src) { char ch = *src++; *p++ = (ch == ';' || ch == '|' || ch == '=' || ch == '\r' || ch == '\n') ? ' ' : ch; }
	*p = 0;
}

// Таблица слотов одним ответом. Поля: номер(1..N);занят;текущий;в списке;режим дома;
// используется в расписании;дата;имя;описание. Для текущего профиля берутся
// живые данные (режим дома может быть изменён и ещё не сохранён).
char *Profile::get_table(char *c)
{
	for(uint8_t i = 0; i < I2C_PROFIL_NUM; i++) {
		bool used = cache_magic[i] == 0xaa;
		bool cur = (int8_t)i == dataProfile.id;
		const type_dataProfile &d = cur ? dataProfile : cache[i].dataProfile;
		uint8_t mode = cur ? SaveON.mode : cache[i].SaveON.mode;
		_itoa(i + 1, c); strcat(c, ";");
		strcat(c, used || cur ? "1;" : "0;");
		strcat(c, cur ? "1;" : "0;");
		strcat(c, (used || cur) && GETBIT(d.flags, fEnabled) ? "1;" : "0;");
		if(used || cur) _itoa(mode, c); else strcat(c, "0");
		strcat(c, ";");
		strcat(c, HP.Schdlr.UsesProfile(i) ? "1;" : "0;");
		if(used) DecodeTimeDate(d.saveTime, c);
		strcat(c, ";");
		if(used || cur) { append_clean(c, d.name); strcat(c, ";"); append_clean(c, d.note); } else strcat(c, ";");
		strcat(c, "|");
	}
	return c;
}

// Копия строки с гарантированным нулём (текст из веб-API уже раскодирован и проверен по длине вызывающим)
static void strlcpy_safe(char *dst, const char *src, size_t cap) { strncpy(dst, src, cap - 1); dst[cap - 1] = 0; }

// Таблица слотов для веб-API (то же, что get_table, но объектами: разделителей в тексте больше нет, чистить нечего)
void Profile::api_table(ApiOut *o)
{
	char t[24];
	api_putc(o, '[');
	for(uint8_t i = 0; i < I2C_PROFIL_NUM; i++) {
		bool used = cache_magic[i] == 0xaa;
		bool cur = (int8_t)i == dataProfile.id;
		const type_dataProfile &d = cur ? dataProfile : cache[i].dataProfile;
		uint8_t mode = cur ? SaveON.mode : cache[i].SaveON.mode;
		bool has = used || cur;
		if(i) api_putc(o, ',');
		api_put(o, "{\"i\":"); api_put_int(o, i);
		api_put(o, has ? ",\"used\":true" : ",\"used\":false");
		api_put(o, cur ? ",\"cur\":true" : ",\"cur\":false");
		api_put(o, has && GETBIT(d.flags, fEnabled) ? ",\"en\":true" : ",\"en\":false");
		api_put(o, ",\"mode\":"); api_put_int(o, has ? mode : 0);
		api_put(o, HP.Schdlr.UsesProfile(i) ? ",\"sch\":true" : ",\"sch\":false");
		t[0] = 0; if(used) DecodeTimeDate(d.saveTime, t);
		api_put(o, ",\"date\":"); api_put_str(o, t);
		api_put(o, ",\"name\":"); api_put_str(o, has ? d.name : "");
		api_put(o, ",\"note\":"); api_put_str(o, has ? d.note : "");
		api_putc(o, '}');
	}
	api_putc(o, ']');
}

void Profile::api_slot_get(uint8_t num, uint8_t what, ApiOut *o)
{
	bool cur = (int8_t)num == dataProfile.id;
	const type_dataProfile &d = cur ? dataProfile : cache[num].dataProfile;
	if(what == 1) api_put_str(o, d.name);
	else if(what == 2) api_put_str(o, d.note);
	else api_put(o, GETBIT(d.flags, fEnabled) ? "true" : "false");
}

// Имя, описание или признак "в списке" любого занятого слота. Для текущего
// профиля правится и рабочая копия, чтобы сохранение не вернуло старое значение.
boolean Profile::set_slot_meta(int8_t num, const char *field, char *val, bool decode)
{
	if(num < 0 || num >= I2C_PROFIL_NUM || cache_magic[num] != 0xaa) return false;
	type_dataProfile &d = cache[num].dataProfile;
	bool cur = num == dataProfile.id;
	if(strcmp(field, prof_NAME_PROFILE) == 0) {
		if(decode) urldecode(d.name, val, sizeof(d.name)); else strlcpy_safe(d.name, val, sizeof(d.name));
		if(!d.name[0]) m_snprintf(d.name, sizeof(d.name), "Профиль %d", num + 1);
		if(cur) memcpy(dataProfile.name, d.name, sizeof(d.name));
	} else if(strcmp(field, prof_NOTE_PROFILE) == 0) {
		if(decode) urldecode(d.note, val, sizeof(d.note)); else strlcpy_safe(d.note, val, sizeof(d.note));
		if(cur) memcpy(dataProfile.note, d.note, sizeof(d.note));
	} else if(strcmp(field, prof_ENABLE_PROFILE) == 0) {
		if(strcmp(val, cZero) == 0) SETBIT0(d.flags, fEnabled);
		else if(strcmp(val, cOne) == 0) SETBIT1(d.flags, fEnabled);
		else return false;
		if(cur) dataProfile.flags = d.flags;
	} else return false;
	cache_crc[num] = cache_crc_of(cache[num]);
	update_list(dataProfile.id);
	return true;
}

// Копия слота src в слот dst. Возврат: 1 - сделано, 0 - нельзя (dst - текущий профиль,
// src == dst), -1 - исходный слот пуст или номер неверный.
int8_t Profile::copy_slot(int8_t src, int8_t dst)
{
	if(src < 0 || src >= I2C_PROFIL_NUM || dst < 0 || dst >= I2C_PROFIL_NUM) return -1;
	if(dst == dataProfile.id || src == dst) return 0;
	if(src == dataProfile.id) cache_store(dst);                    // источник - живой текущий профиль
	else if(cache_magic[src] == 0xaa) memcpy(&cache[dst], &cache[src], sizeof(ProfileSlot));
	else return -1;
	ProfileSlot &s = cache[dst];
	s.dataProfile.id = dst;                                        // в слоте лежит собственный номер
	s.dataProfile.saveTime = rtcSAM3X8.unixtime();
	SETBIT0(s.dataProfile.flags, fEnabled);                        // копия в меню не попадает, пока не включат
	static const char suffix[] = " (копия)";
	size_t l = m_strlen(s.dataProfile.name);
	if(l + sizeof(suffix) <= sizeof(s.dataProfile.name)) strcat(s.dataProfile.name, suffix);
	cache_magic[dst] = 0xaa;
	cache_crc[dst] = cache_crc_of(s);
	update_list(dataProfile.id);
	return 1;
}

// ДОБАВЛЯЕТ к строке с - описание профиля num
char *Profile::get_info(char *c,int8_t num)  
{
  // Всё из кеша: профили и так в оперативной памяти.
  if (num<0 || num>=I2C_PROFIL_NUM) {strcat(c,"Error read profile"); return c; }
  if (cache_magic[num]!=0xaa)  {strcat(c,"Empty profile"); return c;}
  uint16_t crc16temp = cache_crc[num];
  memcpy(&temp_prof, &cache[num].dataProfile, sizeof(temp_prof));
    // прочли формируем строку с описанием
    if(GETBIT(temp_prof.flags, fEnabled)) strcat(c,"* ");                         // отметка об использовании в списке
    strcat(c,temp_prof.name);
    strcat(c,":  ");
    strcat(c,temp_prof.note);
    strcat(c," [");
    DecodeTimeDate(temp_prof.saveTime,c);
    strcat(c,"]");
    return c;
}

// стереть профайл num из еепром  (ставится признак пусто, данные не стираются)
int32_t Profile::erase(int8_t num)
{
   // Отдельный слот в потоке не стереть: освобождаем его в кеше и
   // переписываем область целиком. Освобождённый слот в поток не попадёт.
   if (num<0 || num>=I2C_PROFIL_NUM) return ERR_SAVE_PROFILE;
   cache_magic[num] = PROFILE_EMPTY;
   int16_t ret = save_region();
   return ret > 0 ? dataProfile.len : ret;
}

// Список для веб-API: те же правила, что у update_list (записанные слоты, разрешённые в списке или текущий)
void Profile::api_list(ApiOut *o)
{
	api_putc(o, '[');
	bool first = true;
	for(uint8_t i = 0; i < I2C_PROFIL_NUM; i++) {
		if(cache_magic[i] != 0xaa) continue;
		const type_dataProfile &d = cache[i].dataProfile;
		if(!GETBIT(d.flags, fEnabled) && i != (uint8_t)dataProfile.id) continue;
		if(!first) api_putc(o, ',');
		first = false;
		api_put(o, "{\"i\":"); api_put_int(o, i);
		api_put(o, ",\"n\":"); api_put_str(o, d.name);
		api_put(o, i == (uint8_t)dataProfile.id ? ",\"c\":1}" : ",\"c\":0}");
	}
	api_putc(o, ']');
}

// Устанавливает текущий профиль из номера списка, новый профиль;
int8_t Profile::set_list(int8_t num)
{
	if(num != dataProfile.id) { // new
		if(load(num) < 0) journal.jprintf(" Profile #%d not selected!\n", num);
	}
	return dataProfile.id;
}

// обновить список имен профилей, запоминается в строке list
// Возможно что будет отсутствовать выбранный элемент - это нормально
// такое будет тогда когда текущий профиль не отмечен что участвует в списке
int8_t Profile::update_list(int8_t num)
{
  byte xx;
  uint8_t i;
  int32_t adr;
  strcpy(list,"");                                                                              // стереть список
  char *p = list;
  for (i=0;i<I2C_PROFIL_NUM;i++)                                                                // перебор по всем профилям
  {
    if (cache_magic[i]!=0xaa) continue;                                                         // слот не записан, пропускаем - это не ошибка
    memcpy(&temp_prof, &cache[i].dataProfile, sizeof(temp_prof));                                // данные берём из кеша, EEPROM не нужен
    if ((GETBIT(temp_prof.flags,fEnabled))||(i==num))                                                // Если разрешено использовать или ТЕКУЩИЙ профиль
     { 
    	p = list + m_strlen(list);
    	_itoa(i + 1, p);
    	strcat(p, ". ");
    	strcat(p, temp_prof.name);
    	if (i==num) strcat(p,":1;"); else strcat(p,":0;");
     }                    
  }
 return OK;
}

// Прочитать из EEPROM структуру: режим работы ТН (SaveON), возврат OK - успешно
int32_t Profile::load_from_EEPROM_SaveON_mode(int8_t id)
{
	// Профили в оперативной памяти: обращение к шине для одного байта не нужно, а в
	// новом формате такого смещения в области и нет.
	if(id < 0 || id >= I2C_PROFIL_NUM || cache_magic[id] != 0xaa) return ERR_LOAD_PROFILE;
	return HouseModeAllowed(cache[id].SaveON.mode, HouseOnlyHeat);
}


// ---------------------------------------------------------------------------
// Реестр параметров веб-API: отопление, охлаждение, бойлер.
//
// Ядро - net/ParamRegistry.cpp, протокол - tools/api/spec.md. Здесь только
// описание: для каждого параметра смещение поля в УЖЕ СУЩЕСТВУЮЩЕЙ структуре
// (type_settingHP, type_boilerHP), масштаб, границы и флаги. Сами структуры и
// формат их хранения в EEPROM не тронуты.
//
// ПРАВИЛА ЭТОЙ ТАБЛИЦЫ
//  - name - прежняя строка STRCONST (hp_TEMP1 = "TEMP1"), ключ = секция + строчные
//    буквы: heat.temp1. Своих копий строк нет.
//  - границы и масштаб скопированы из прежних set_paramHeatHP/CoolHP/set_boiler
//    без изменений. Где прежний код границы не проверял (запись в int16 с
//    переполнением), границей стал диапазон самого поля: раньше это было
//    молчаливое переполнение, теперь range.
//  - макросы PN_* (ParamRegistry.h) проверяют на этапе компиляции, что граница*масштаб
//    помещается в поле: rd() возвращает int16, и выход за него - неопределённое поведение.
//  - UpdatePIDbyTime() при записи периода ПИД не зовётся: это пустая заглушка.
// ---------------------------------------------------------------------------
enum { H_RULE = 1, H_TP, H_SCHON, H_SCHAH, H_SALM, H_SCHED, H_URG };

static_assert(sizeof(RULE_HP) == 1, "RULE_HP хранится одним байтом (CHECK_SAVE_SIZE type_settingHP = 38)");

static const char *const apiLabRule[]   = { "HYSTERESIS", "PID", "HYBRID", 0 };
static const char *const apiLabTarget[] = { "Дом", "Обратка", 0 };

#define HO(f) offsetof(type_settingHP, f)
#define BO(f) offsetof(type_boilerHP, f)
#define PIDO(f) (offsetof(type_settingHP, pid) + offsetof(PID_STRUCT, f))
#define BPIDO(f) (offsetof(type_boilerHP, pid) + offsetof(PID_STRUCT, f))

// Отопление: 21 запись + расчётная цель ПИД (только чтение)
static const ApiParam apiHeatTab[] = {
	{ hp_RULE, apiLabRule, 0, 2, (uint16_t)HO(Rule), 1, PT_E8, 0, 0, H_RULE, 0 },
	PN_I16(hp_TEMP1,          0,   40,  100, HO(Temp1),              PF_UW),
	PN_I16(ADD_DELTA_TEMP,   -30,  50,  100, HO(add_delta_temp),     0),
	PN_U8 (ADD_DELTA_HOUR,    0,   23,  1,   HO(add_delta_hour),     0),
	PN_U8 (ADD_DELTA_END_HOUR,0,   23,  1,   HO(add_delta_end_hour), 0),
	PN_I16(hp_TEMP2,          10,  50,  100, HO(Temp2),              0),
	PN_BIT(hp_TARGET, HO(flags), fTarget, PT_B8, apiLabTarget, 0, 0, 0),
	PN_I16(hp_DTEMP,          0,   30,  100, HO(dTemp),              0),
	PN_I16(hp_dTempGen,       0,   30,  100, HO(dTempGen),           0),
	PN_U16(hp_HP_TIME,        10,  1000,1,   HO(pid_time),           0),
	PN_I16(hp_HP_PRO,         0,   32,  1000,PIDO(Kp),               0),
	PN_I16(hp_HP_IN,          0,   32,  1000,PIDO(Ki),               0),
	PN_I16(hp_HP_DIF,         0,   32,  1000,PIDO(Kd),               0),
	PN_I16(hp_TEMP_IN,        0,   70,  100, HO(tempInLim),          0),
	PN_I16(hp_TEMP_OUT,      -10,  70,  100, HO(tempOutLim),         0),
	PN_I16(hp_D_TEMP,         0,   40,  100, HO(dt),                 0),
	PN_I16(hp_TEMP_PID,       10,  50,  100, HO(tempPID),            0),
	PN_BIT(hp_WEATHER, HO(flags), fWeather, PT_B8, 0, 0, 0, 0),
	PN_I16(hp_K_WEATHER,     -32.768f, 32.767f, 1000, HO(kWeatherPID),    0),
	PN_I16(hp_kWeatherTarget,-32.768f, 32.767f, 1000, HO(kWeatherTarget), 0),
	PN_I8 (hp_WeatherBase,   -128, 127, 1,   HO(WeatherBase),        0),
	PN_U8 (hp_WeatherTargetRange, 0, 25.5f, 10, HO(WeatherTargetRange), 0),
	{ hp_targetPID, 0, 0, 0, 0, 100, PT_CALL, PF_RO, MK_NUM, H_TP, 0 },
};

// Охлаждение: те же поля, но границы свои (как в прежнем set_paramCoolHP)
static const ApiParam apiCoolTab[] = {
	{ hp_RULE, apiLabRule, 0, 2, (uint16_t)HO(Rule), 1, PT_E8, 0, 0, H_RULE, 0 },
	PN_I16(hp_TEMP1,          0,   30,  100, HO(Temp1),              PF_UW),
	PN_I16(hp_TEMP2,          10,  50,  100, HO(Temp2),              0),
	PN_BIT(hp_TARGET, HO(flags), fTarget, PT_B8, apiLabTarget, 0, 0, 0),
	PN_I16(hp_DTEMP,          0,   30,  100, HO(dTemp),              0),
	PN_U16(hp_HP_TIME,        10,  1000,1,   HO(pid_time),           0),
	PN_I16(hp_HP_PRO,         0,   32,  1000,PIDO(Kp),               0),
	PN_I16(hp_HP_IN,          0,   32,  1000,PIDO(Ki),               0),
	PN_I16(hp_HP_DIF,         0,   32,  1000,PIDO(Kd),               0),
	PN_I16(hp_TEMP_IN,        0,   30,  100, HO(tempInLim),          0),
	PN_I16(hp_TEMP_OUT,       0,   35,  100, HO(tempOutLim),         0),
	PN_I16(hp_D_TEMP,         0,   40,  100, HO(dt),                 0),
	PN_I16(hp_TEMP_PID,       0,   30,  100, HO(tempPID),            0),
	PN_BIT(hp_WEATHER, HO(flags), fWeather, PT_B8, 0, 0, 0, 0),
	PN_I16(hp_K_WEATHER,     -32.768f, 32.767f, 1000, HO(kWeatherPID), 0),
	{ hp_targetPID, 0, 0, 0, 0, 100, PT_CALL, PF_RO, MK_NUM, H_TP, 0 },
};

// Бойлер: база 0 - type_boilerHP, база 1 - type_SaveON (там лежит флаг «бойлер включён»)
static const ApiParam apiBoilerTab[] = {
	PN_BIT(boil_BOILER_ON,       offsetof(type_SaveON, flags), fBoilerON, PT_B8, 0, 0, PF_UW, 1),
	PN_BIT(boil_SCHEDULER_ON,    BO(flags), fSchedule,         PT_B16, 0, H_SCHON, 0, 0),
	PN_BIT(boil_SCHEDULER_ADDHEAT,BO(flags), fScheduleAddHeat, PT_B16, 0, H_SCHAH, 0, 0),
	PN_BIT(boil_TOGETHER_HEAT,   BO(flags), fBoilerTogetherHeat,PT_B16, 0, 0, 0, 0),
	PN_BIT(boil_fBoilerPID,      BO(flags), fBoilerPID,        PT_B16, 0, 0, 0, 0),
	PN_BIT(boil_TURBO_BOILER,    BO(flags), fTurboBoiler,      PT_B16, 0, 0, 0, 0),
	PN_BIT(boil_SALMONELLA,      BO(flags), fSalmonella,       PT_B16, 0, H_SALM, 0, 0),
	PN_BIT(boil_fSalmonellaHold, BO(flags), fSalmonellaHold, PT_B16, 0, 0, 0, 0),
	PN_BIT(boil_CIRCULATION,     BO(flags), fCirculation,      PT_B16, 0, 0, 0, 0),
	PN_I16(boil_TEMP_TARGET,  5,   95,  100, BO(TempTarget),         PF_UW),
	PN_I16(ADD_DELTA_TEMP,   -50,  50,  100, BO(add_delta_temp),     0),
	PN_U8 (ADD_DELTA_HOUR,    0,   23,  1,   BO(add_delta_hour),     0),
	PN_U8 (ADD_DELTA_END_HOUR,0,   23,  1,   BO(add_delta_end_hour), 0),
	PN_I16(boil_DTARGET,      0,   30,  100, BO(dTemp),              0),
	PN_I16(boil_TEMP_MAX,     20,  70,  100, BO(tempInLim),          0),
	{ boil_SCHEDULER, 0, 0, 0, 0, 1, PT_CALL, 0, MK_STR, H_SCHED, 0 },
	PN_U16(boil_CIRCUL_WORK,  0,   60,  60,  BO(Circul_Work),       0),        // в вебе минуты, в памяти секунды
	PN_U16(boil_CIRCUL_PAUSE, 0,   60,  60,  BO(Circul_Pause),      0),
	PN_BIT(boil_RESET_HEAT,      BO(flags), fResetHeat,        PT_B16, 0, 0, 0, 0),
	PN_U16(boil_RESET_TIME,   1,   10000,1,  BO(Reset_Time),        0),
	PN_U16(boil_BOIL_TIME,    2,   1000,1,   BO(pid_time),          0),
	PN_I16(boil_BOIL_PRO,     0,   32,  1000,BPIDO(Kp),             0),
	PN_I16(boil_BOIL_IN,      0,   32,  1000,BPIDO(Ki),             0),
	PN_I16(boil_BOIL_DIF,     0,   32,  1000,BPIDO(Kd),             0),
	PN_I16(boil_BOIL_TEMP,    30,  70,  100, BO(tempPID),           0),
	PN_BIT(boil_ADD_HEATING,     BO(flags), fAddHeating,       PT_B16, 0, 0, 0, 0),
	PN_BIT(boil_fAddHeatingForce,BO(flags), fAddHeatingForce,  PT_B16, 0, 0, 0, 0),
	{ boil_HeatUrgently, 0, 0, 1, 0, 1, PT_CALL, PF_UW, MK_BOOL, H_URG, 0 },
	PN_I16(boil_TEMP_RBOILER, 0,   60,  100, BO(tempRBOILER),       0),
	PN_I16(boil_dAddHeat,    -327.68f, 327.67f, 100, BO(dAddHeat),  0),
	PN_U8 (boil_DischargeDelta,0,  25.5f, 10, BO(DischargeDelta),   0),
	PN_BIT(boil_fHeaterOnly,     BO(flags), fBoilerHeaterOnly, PT_B16, 0, 0, 0, 0),         // бойлер греет только ТЭН
};

static void *api_base_heat(uint8_t) { return &HP.Prof.Heat; }
static void *api_base_cool(uint8_t) { return &HP.Prof.Cool; }
static void *api_base_boiler(uint8_t sel) { return sel ? (void *)&HP.Prof.SaveON : (void *)&HP.Prof.Boiler; }

// Индексы перечисления, недоступные без инвертора (прежний get_param..HP(RULE) при !fc показывал только гистерезис)
static uint16_t api_mask_prof(uint8_t hook) { return (hook == H_RULE && !HP.dFC.get_present()) ? 6 : 0; }

// Побочные эффекты записи - те же, что были в прежних ветках set_*
static void api_after_prof(uint8_t hook)
{
	switch(hook) {
	case H_RULE: HP.resetPID(); break;
	case H_SCHON: if(!GETBIT(HP.Prof.Boiler.flags, fSchedule)) SETBIT0(HP.Prof.Boiler.flags, fScheduleAddHeat); break;
	case H_SCHAH: if(GETBIT(HP.Prof.Boiler.flags, fScheduleAddHeat)) SETBIT1(HP.Prof.Boiler.flags, fSchedule); break;
	case H_SALM: // максимальная температура датчика бойлера при режиме сальмонелла
		HP.sTemp[TBOILER].set_maxTemp(GETBIT(HP.Prof.Boiler.flags, fSalmonella) ? SALMONELLA_TEMP + 300 : TempSetup[TBOILER].max);
		break;
	}
}

static int api_get_heat(uint8_t hook, uint8_t, ApiOut *o)
{
	if(hook != H_TP) return AE_FAIL;
	api_put_dec(o, HP.CalcTargetPID(HP.Prof.Heat), 100);
	return AE_OK;
}
static int api_get_cool(uint8_t hook, uint8_t, ApiOut *o)
{
	if(hook != H_TP) return AE_FAIL;
	api_put_dec(o, HP.CalcTargetPID(HP.Prof.Cool), 100);
	return AE_OK;
}
static int api_get_boiler(uint8_t hook, uint8_t, ApiOut *o)
{
	if(hook == H_SCHED) { api_put_str(o, get_Schedule(HP.Prof.Boiler.Schedule)); return AE_OK; }
	if(hook == H_URG) { api_put(o, HP.HeatBoilerUrgently ? "true" : "false"); return AE_OK; }
	return AE_FAIL;
}
static int api_set_boiler(uint8_t hook, uint8_t, const char *val)
{
	if(hook == H_SCHED) return set_Schedule((char *)val, HP.Prof.Boiler.Schedule) ? AE_OK : AE_BAD_VALUE;   // val - рабочая копия ядра, менять можно
	if(hook == H_URG) {
		if(!strcmp(val, "1") || !strcmp(val, "true")) HP.set_HeatBoilerUrgently(true);
		else if(!strcmp(val, "0") || !strcmp(val, "false")) HP.set_HeatBoilerUrgently(false);
		else return AE_BAD_VALUE;
		return AE_OK;
	}
	return AE_FAIL;
}

static const ApiOps apiOpsHeat   = { api_base_heat,   api_get_heat,   0,              api_after_prof, api_mask_prof };
static const ApiOps apiOpsCool   = { api_base_cool,   api_get_cool,   0,              api_after_prof, api_mask_prof };
static const ApiOps apiOpsBoiler = { api_base_boiler, api_get_boiler, api_set_boiler, api_after_prof, api_mask_prof };

const ApiGroup ApiGrpHeat   = { "heat",   apiHeatTab,   sizeof(apiHeatTab)   / sizeof(apiHeatTab[0]),   &apiOpsHeat };
const ApiGroup ApiGrpCool   = { "cool",   apiCoolTab,   sizeof(apiCoolTab)   / sizeof(apiCoolTab[0]),   &apiOpsCool };
const ApiGroup ApiGrpBoiler = { "boiler", apiBoilerTab, sizeof(apiBoilerTab) / sizeof(apiBoilerTab[0]), &apiOpsBoiler };

// Профили: список слотов и номер текущего. Выбор профиля (profile.cur) разрешён роли user.
enum { P_LIST = 1, P_CUR, P_TABLE };
static const ApiParam apiProfileTab[] = {
	{ "list", 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TABLE, P_LIST, 0 },
	{ "cur",  0, 0, I2C_PROFIL_NUM - 1, 0, 1, PT_CALL, PF_UW, MK_INT, P_CUR, 0 },
	{ "table", 0, 0, 0, 0, 1, PT_CALL, PF_RO, MK_TABLE, P_TABLE, 0 },   // все слоты с именем, описанием, датой, режимом
};
static int api_get_profile(uint8_t hook, uint8_t, ApiOut *o)
{
	if(hook == P_LIST) { HP.Prof.api_list(o); return AE_OK; }
	if(hook == P_CUR) { api_put_int(o, HP.Prof.get_idProfile()); return AE_OK; }
	if(hook == P_TABLE) { HP.Prof.api_table(o); return AE_OK; }
	return AE_FAIL;
}
static int api_set_profile(uint8_t hook, uint8_t, const char *val)
{
	if(hook != P_CUR) return AE_FAIL;
	int8_t n = (int8_t)my_atof(val);                 // границы уже проверило ядро
	HP.Prof.set_list(n);
	return HP.Prof.get_idProfile() == n ? AE_OK : AE_FAIL;   // слот не записан или не загрузился
}
static const ApiOps apiOpsProfile = { 0, api_get_profile, api_set_profile, 0, 0, 0, 0, 0 };
const ApiGroup ApiGrpProfile = { "profile", apiProfileTab, sizeof(apiProfileTab) / sizeof(apiProfileTab[0]), &apiOpsProfile };

// ---- группа slot: имя, описание и признак «в меню» любого занятого слота профилей (slot.<номер с 0>.name ...) ----
enum { SL_NAME = 1, SL_NOTE, SL_EN };
static const ApiParam apiSlotTab[] = {
	{ "name",   0, 0, 0, 0, 1, PT_CALL, 0, MK_TEXT, SL_NAME, 0 },
	{ "note",   0, 0, 0, 0, 1, PT_CALL, 0, MK_TEXT, SL_NOTE, 0 },
	{ "enable", 0, 0, 0, 0, 1, PT_CALL, 0, MK_BOOL, SL_EN,   0 },
};
static_assert(I2C_PROFIL_NUM <= 10, "имена слотов в таблице api_slot_name - увеличить при росте I2C_PROFIL_NUM");
static uint8_t api_slot_n(void) { return I2C_PROFIL_NUM; }
static const char *api_slot_name(uint8_t i) { static const char *const n[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" }; return n[i]; }   // const
static uint8_t api_slot_ok(uint8_t i) { return HP.Prof.cache_used(i) || (int8_t)i == HP.Prof.get_idProfile(); }
static int api_get_slot(uint8_t hook, uint8_t dev, ApiOut *o)
{
	if(hook < SL_NAME || hook > SL_EN) return AE_FAIL;
	HP.Prof.api_slot_get(dev, hook, o);
	return AE_OK;
}
static int api_set_slot(uint8_t hook, uint8_t dev, const char *val)
{
	static const char *const field[] = { 0, prof_NAME_PROFILE, prof_NOTE_PROFILE, prof_ENABLE_PROFILE };
	if(hook < SL_NAME || hook > SL_EN) return AE_FAIL;
	if(hook == SL_NAME && strlen(val) >= LEN_PROFILE_NAME) return AE_RANGE;
	if(hook == SL_NOTE && strlen(val) >= 85) return AE_RANGE;
	if(hook == SL_EN) {                                 // set_slot_meta принимает флаг строкой "0"/"1"
		if(!strcmp(val, "true")) val = cOne; else if(!strcmp(val, "false")) val = cZero;
	}
	return HP.Prof.set_slot_meta(dev, field[hook], (char *) val, false) ? AE_OK : AE_BAD_VALUE;   // val - рабочая копия ядра
}
static const ApiOps apiOpsSlot = { 0, api_get_slot, api_set_slot, 0, 0, api_slot_n, api_slot_name, api_slot_ok };
extern const ApiGroup ApiGrpSlot;
const ApiGroup ApiGrpSlot = { "slot", apiSlotTab, sizeof(apiSlotTab) / sizeof(apiSlotTab[0]), &apiOpsSlot };
