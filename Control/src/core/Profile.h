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
#ifndef Profile_h
#define Profile_h
#include "../log/Journal.h"
#include "SettingsLegacy.h"   // LegacyProfileSizes, чтение старых эпох
#include "../net/ParamRegistry.h" // ApiGroup для таблиц параметров веб-API



// ---------------------------------------------------------------------------
// Слот профиля в оперативной памяти.
//
// ЗАЧЕМ. В ОЗУ жил только активный профиль, остальные девять лежали в EEPROM.
// Единая запись всех настроек (шаг 6 плана) должна отдать все десять, а
// перекодировать их по одному через активный профиль нельзя: сохранение
// запускается веб-запросом, и общий буфер Socket[0].outBuf в этот момент
// занят его ответом.
//
// ПОРЯДОК ПОЛЕЙ - тот же, в каком блоки уходят в EEPROM и в каком считается
// контрольная сумма. Это не раскладка формата: в EEPROM блоки пишутся по
// отдельности, здесь они просто лежат рядом.
// ---------------------------------------------------------------------------
struct ProfileSlot
{
    type_dataProfile dataProfile;
    type_SaveON      SaveON;
    type_settingHP   Cool;
    type_settingHP   Heat;
    type_boilerHP    Boiler;
    type_DailySwitch DailySwitch[DAILY_SWITCH_MAX];
};

class Profile                         // Класс профиль
{
  public: 
    type_SaveON SaveON;                                     // Структура для хранения состяния ТН в ЕЕПРОМ
   // Настройки для отопления и охлаждения бойлера
    type_settingHP Cool;                                    // Настройки для режима охлаждение
    type_settingHP Heat;                                    // Настройки для режима отопления
    type_boilerHP Boiler;                                   // Настройка бойлера
	type_DailySwitch DailySwitch[DAILY_SWITCH_MAX]; 		// дневное периодическое включение
    
     // Функции работы с профилем
    void initProfile();                                     // инициализация профиля
    void  api_list(ApiOut *o);                              // то же для веб-API: JSON-массив [{i,n,c}], c=1 - текущий
    int8_t set_list(int8_t num);                            // Устанавливает текущий профиль из номера списка, ДОБАВЛЯЕТ к строке с
    int8_t update_list(int8_t num);                         // обновить список имен профилей

    // ---- работа со слотами из таблицы профилей (всё в ОЗУ, в EEPROM пишет общая кнопка) ----
    char    *get_table(char *c);                            // ДОБАВЛЯЕТ таблицу всех слотов: поля через ';', слоты через '|'
    boolean  set_slot_meta(int8_t num, const char *field, char *val, bool decode = true); // имя/описание/"в списке" любого занятого слота; decode=false - текст уже раскодирован (веб-API)
    void     api_table(ApiOut *o);                          // таблица слотов для веб-API: JSON-массив объектов
    void     api_slot_get(uint8_t num, uint8_t what, ApiOut *o); // what: 1 имя, 2 описание, 3 признак «в списке» (текущий профиль - живые данные)
    int8_t   copy_slot(int8_t src, int8_t dst);             // копия слота src в слот dst; в текущий профиль нельзя
    char *get_info(char *c,int8_t num);                     // ДОБАВЛЯЕТ к строке с - описание профиля num
    int16_t save(int8_t num);                               // Записать профайл в еепром под номерм num
    int32_t load(int8_t num);                               // загрузить профайл num из еепром память
    int8_t  loadFromBuf(int32_t adr,byte *buf);             // Считать настройки из буфера на входе адрес с какого, на выходе код ошибки (меньше нуля)
    // convert_to_new_version удалена: чтение старых эпох теперь в cache_load_legacy
    // ---- кеш всех профилей в ОЗУ, см. ProfileSlot выше ----
    int8_t   cache_load_all(void);                          // прочитать все слоты из EEPROM в кеш
    void     cache_store(int8_t num);                       // обновить слот кеша из активных членов
    uint8_t  cache_verify(void);                            // пересчитать CRC слотов, вернуть число расхождений
    uint16_t cache_crc_of(const ProfileSlot &s);            // контрольная сумма слота кеша
    int16_t  save_region(void);                             // записать ВСЮ область профилей новым форматом
    int8_t   load_region(void);                             // прочитать область нового формата в кеш
    int8_t   cache_load_legacy(const LegacyProfileSizes &sz); // прочитать профили старых эпох в кеш
    int8_t   cache_activate(int8_t num);                    // сделать слот кеша текущим профилем (копирование одним блоком, журнал после него)
    uint8_t  cache_copy(int8_t num);                        // само копирование слота в текущие структуры: 0 ок, 1 пуст, 2 повреждён; без журнала и замков
    bool     region_is_new(void);                           // область в новом формате?
    uint16_t region_len(void);                              // длина области профилей на носителе, байт
    inline boolean cache_used(int8_t num) { return cache_magic[num] == 0xaa; } // слот записан?
    int32_t erase(int8_t num);                              // стереть профайл num из еепром  (ставится признак пусто)
    char*   get_paramProfile(char *var,char *ver);          // профиль Получить параметр второй параметр - наличие инвертора
    int16_t get_lenProfile(){return dataProfile.len;}       // получить длину профиля при записи
    int8_t  get_idProfile(){return dataProfile.id;}         // получить номер текущего профиля
      
    // Установка параметров
    char*   get_paramCoolHP(char *var, char *ret, boolean fc);// Охлаждение Получить параметр второй параметр - наличие инвертора
    char*   get_paramHeatHP(char *var, char *ret,boolean fc);// отопление  Получить параметр  второй параметр - наличие инвертора
    char*   get_boiler(char *var, char *ret);               // Получить параметр из строки по имени var, результат ДОБАВЛЯЕТСЯ в строку ret
    int32_t load_from_EEPROM_SaveON_mode(int8_t id);	    // Прочитать из EEPROM режим работы ТН (SaveON)
 
    char list[I2C_PROFIL_NUM*(LEN_PROFILE_NAME+6)+1];         // текущий список: на профиль "N. " + имя + ":1;" = имя+6 байт (раньше +2, и пять длинных имён выходили за массив)
    inline int32_t get_sizeProfile() { // определить длину данных
    	return sizeof(magic) + sizeof(crc16) +
				// данные контрольная сумма считается с этого места
				sizeof(dataProfile) + sizeof(SaveON) + sizeof(Cool) + sizeof(Heat) + sizeof(Boiler)	+ sizeof(DailySwitch);
    };
 private:
  int8_t err;                                               // Ошибка
 
  // заголовок
  byte magic;                                               // признак данных, должно быть  0xaa
  uint16_t crc16;                                           // Контрольная сумма
  type_dataProfile dataProfile;                             // данные профиля
  // Кеш всех профилей. cache_crc хранит сумму, с которой слот был прочитан
  // или записан: расхождение при пересчёте означает порчу памяти, а не
  // изменение настроек, и такой слот писать в EEPROM нельзя.
  ProfileSlot cache[I2C_PROFIL_NUM];
  uint16_t    cache_crc[I2C_PROFIL_NUM];
  uint8_t     cache_magic[I2C_PROFIL_NUM];                  // 0xaa - записан, иначе слот не использован
   uint16_t get_crc16_mem();                                 // Расчитать контрольную сумму
   int8_t   check_crc16_eeprom(int8_t num);   // Проверить контрольную сумму ПРОФИЛЯ в EEPROM для данных на выходе ошибка, длина определяется из заголовка
   int8_t   check_crc16_buf(int32_t adr, byte* buf);         // Проверить контрольную сумму в буфере для данных на выходе ошибка, длина определяется из заголовка
};


// Таблицы параметров веб-API (определены внизу Profile.cpp)
extern const ApiGroup ApiGrpHeat, ApiGrpCool, ApiGrpBoiler, ApiGrpProfile;

#endif
