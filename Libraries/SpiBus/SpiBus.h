// SpiBus.h - общая шина SPI0 контроллера (SAM3X) с DMA, реализация - SpiBus.cpp рядом.
// Отдельная библиотека, а не код скетча: arduino-builder собирает библиотеки без пути к скетчу, а так Ethernet,
// SdFat и SerialFlash подключают её через #include <SpiBus.h> и видят один контракт (зависимости: все -> SpiBus).
//
// На шине три ведомых с программными CS. Аппаратный канал у всех один - CSR[3]; каждый обмен начинается с
// spiBusSelect(): настройка канала под устройство (делитель, режим 0, без пауз), затем CS вниз. Так устройство
// не наследует режим того, кто был на шине последним. Обмены сериализует вызывающий (замок xWebThreadSemaphore).
#ifndef SPIBUS_H
#define SPIBUS_H
#include <stdint.h>
#include <stddef.h>

enum SpiDev : uint8_t { SPIDEV_NET, SPIDEV_SD, SPIDEV_FLASH, SPIDEV_NUM };

// Коды spiBusRec(), можно складывать
#define SPIBUS_OK       0
#define SPIBUS_OVERRUN  1    // переполнение приёмника SPI при чтении по DMA
#define SPIBUS_TIMEOUT  2    // DMA не закончил за SPIBUS_DMA_TIMEOUT мс: в буфере мусор

#define SPIBUS_DMA_MIN     16      // блоки короче - опросом: настройка двух каналов DMAC (1-2 мкс) дороже самой передачи
#define SPIBUS_DMA_MAX     2048    // наибольшая порция одного запуска DMA
#define SPIBUS_DMA_TIMEOUT 50      // мс на порцию приёма по DMA
#define SPIBUS_SRAM_START  0x20000000UL   // ниже - флеш-память МК: DMAC её не читает, отправка опросом

void    spiBusInit();                                         // выводы, тактирование SPI0 и DMAC, сброс и режим SPI0 (ведущий, канал 3)
void    spiBusSetDevice(SpiDev d, uint8_t csPin, uint8_t scbr); // вывод CS (выход, поднят) и делитель: SCK = 84 МГц / scbr, режим 0
void    spiBusSetClock(SpiDev d, uint8_t scbr);               // сменить делитель устройства (SD: 250 кГц на инициализации, потом рабочая)
void    spiBusConfig(SpiDev d);                               // только настройка канала 3 под устройство, CS не трогается
void    spiBusSelect(SpiDev d);                               // настройка канала 3 под устройство, CS вниз
void    spiBusDeselect(SpiDev d);                             // CS вверх
void    spiBusDeselectAll();                                  // CS вверх у всех настроенных устройств (бывшие SPI_switch*())
uint8_t spiBusScbr(SpiDev d);                                 // текущий делитель устройства (SCK = 84 МГц / делитель), 0 - не настроено
uint8_t spiBusXfer(uint8_t b);                                // один байт опросом, возвращает принятый
void    spiBusSend(const uint8_t *buf, size_t n);             // передать (принятое отбрасывается)
uint8_t spiBusRec(uint8_t *buf, size_t n);                    // принять, передавая 0xFF; SPIBUS_OK / OVERRUN / TIMEOUT
bool    spiBusUseDma(const void *buf, size_t n);              // выбор способа: DMA только для блока от SPIBUS_DMA_MIN байт в ОЗУ
uint32_t spiBusCsr(uint8_t scbr);                             // значение CSR канала для делителя: режим 0, 8 бит, без DLYBCT/DLYBS

struct SpiBusStat {
	volatile uint32_t dmaTimeouts;   // порций, принятых по DMA с таймаутом
	volatile uint32_t dmaOverruns;   // переполнений приёмника при чтении по DMA
	volatile uint32_t dmaLate;       // приём по DMA закончился, пока поток был вытеснен дольше таймаута (не ошибка, данные целы)
};
extern SpiBusStat spiBusStat;

#endif // SPIBUS_H
