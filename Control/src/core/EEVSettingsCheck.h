/* Проверка диапазона ЭРВ до изменения действующей конфигурации. */
#ifndef EEVSettingsCheck_h
#define EEVSettingsCheck_h
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline bool eev_bounds_valid(uint8_t minSteps, int16_t maxSteps, int16_t closeStep)
{
 return maxSteps > 0 && maxSteps > minSteps && maxSteps >= closeStep;
}

static inline int16_t eev_calc_percent(int16_t pos, int16_t maxSteps)
{
 return maxSteps > 0 ? (int32_t)pos * 10000 / maxSteps : 0;
}

// Применить к снимку только известную часть записи, как load_struct/copy_min.
// Снимок содержит только два поля; частичная запись maxSteps сохраняет остальные байты.
static inline bool eev_bounds_record(const uint8_t *data, size_t len, size_t minOffset, size_t maxOffset,
                                      uint8_t *minSteps, int16_t *maxSteps, int16_t closeStep)
{
 if(len > minOffset) *minSteps = data[minOffset];
 uint8_t bytes[sizeof(*maxSteps)]; memcpy(bytes, maxSteps, sizeof(bytes));
 for(size_t i = 0; i < sizeof(bytes); ++i)
  if(maxOffset < len && i < len - maxOffset) bytes[i] = data[maxOffset + i];
 memcpy(maxSteps, bytes, sizeof(bytes));
 return eev_bounds_valid(*minSteps, *maxSteps, closeStep);
}

// true только если все встреченные записи ЭРВ и каркас безопасны. Пять первых
// записей старого формата позиционные; теги остальных записей - отрицательные int16.
static inline bool eev_bounds_scan(const uint8_t *p, const uint8_t *end, bool legacy,
                                   int16_t eevTag, size_t minOffset, size_t maxOffset,
                                   uint8_t minSteps, int16_t maxSteps, int16_t closeStep)
{
 int16_t tag = -100;
 unsigned head = legacy ? 5 : 0;
 while(p < end) {
  const size_t remaining = (size_t)(end - p);
  if(!legacy) {
   if(remaining < 4) return false;
   uint16_t t, len; memcpy(&t, p, 2); memcpy(&len, p + 2, 2); p += 4;
   if((size_t)(end - p) < len) return false;
   if(t == 0) return eev_bounds_valid(minSteps, maxSteps, closeStep);
   if(t == (uint16_t)eevTag) eev_bounds_record(p, len, minOffset, maxOffset, &minSteps, &maxSteps, closeStep);
   p += len;
  } else {
   if(!head && remaining >= 2) {
    int16_t t; memcpy(&t, p, 2);
    if(t <= 0 && t > -100) { p += 2; tag = t; if(!tag) return eev_bounds_valid(minSteps, maxSteps, closeStep); if(p == end) return false; }
   }
   if(p == end) return false;
   uint16_t len = *p++;
   if(len & 1) { if(p == end) return false; len |= (uint16_t)*p++ << 8; }
   len >>= 1;
   if((size_t)(end - p) < len) return false;
   if(head) --head;
   else if(tag == eevTag && !eev_bounds_record(p, len, minOffset, maxOffset, &minSteps, &maxSteps, closeStep)) return false;
   p += len;
  }
 }
 return !head && eev_bounds_valid(minSteps, maxSteps, closeStep); // прежние загрузчики допускают отсутствие END
}
#endif
