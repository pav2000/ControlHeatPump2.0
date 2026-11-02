// Выбор значения температуры датчика с учётом удалённого (IP) датчика: чистые функции без железа, проверяются на ПК
// (tests/test_sensorpick.cpp). Правило: пропажа IP-датчика ТН не останавливает; значение берётся так, чтобы регулирование
// продолжалось, а когда пакеты вернутся, удалённый датчик снова в работе сам (состояния «потерян» не хранится в значении).
#ifndef SensorPick_h
#define SensorPick_h

#include <stdint.h>

struct SensorPickIn {
	bool    linked;     // к проводному датчику привязан удалённый и он включён (fUse, link > -1)
	bool    got;        // от удалённого датчика хотя бы раз пришёл пакет (до него Temp - заглушка, а uptime < UPDATE_IP выглядит «свежим»)
	bool    fresh;      // последний пакет моложе UPDATE_IP
	bool    has_wire;   // у проводного датчика есть адрес: его значение настоящее
	bool    avg;        // правило «усреднять с проводным» (иначе заменять)
	int16_t wire;       // значение проводного датчика (сотые градуса)
	int16_t ip;         // последнее значение удалённого датчика (хранится и после потери связи)
};

enum SensorPickSrc { SPK_WIRE = 0, SPK_IP, SPK_AVG, SPK_HELD };

static inline int16_t Sensor_Pick(const SensorPickIn &in, uint8_t *src)
{
	uint8_t s = SPK_WIRE;
	int16_t v = in.wire;
	if(in.linked && in.got) {
		if(in.fresh) {
			if(in.avg && in.has_wire) { v = (int16_t) (((int32_t) in.ip + in.wire) / 2); s = SPK_AVG; }
			else { v = in.ip; s = SPK_IP; }
		} else if(!in.has_wire) { v = in.ip; s = SPK_HELD; }   // провода нет: держим последнее показание, а не 0 и не тестовое
		// провод есть: откат на проводное, как и было
	}
	if(src) *src = s;
	return v;
}

// Переходы «связь с удалённым датчиком потеряна / вернулась» - по одной записи в журнал, без повторов каждый цикл.
enum SensorIpEdge { SIE_NONE = 0, SIE_LOST, SIE_RESTORED };

static inline uint8_t Sensor_IpEdge(bool *lost, bool linked, bool got, bool fresh)
{
	bool stale = linked && got && !fresh;
	if(stale && !*lost) { *lost = true; return SIE_LOST; }
	if(!stale && *lost) { *lost = false; return SIE_RESTORED; }
	return SIE_NONE;
}

#endif
