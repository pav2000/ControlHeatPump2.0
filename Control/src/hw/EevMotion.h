#ifndef EEV_MOTION_H
#define EEV_MOTION_H

#include <stdint.h>

// Логика цели и поиска нуля. Внешняя задача сериализует вызовы и выводит фазы.
class EevMotion {
public:
    enum Mode { IDLE, MOVING, HOMING };
    enum Result { ACCEPTED, BUSY, INVALID };
    struct Snapshot { Mode mode; int16_t position; int16_t target; };

    EevMotion();
    void reset(int16_t position);
    Result moveTo(int16_t target, int16_t minimum, int16_t maximum);
    Result home(int16_t maximum, int16_t overrun);
    // Вычислить следующую фазу, затем после вывода на пины вызвать commitStep.
    bool prepareStep(int16_t &position);
    void commitStep();
    Snapshot snapshot() const;
private:
    Mode mode_;
    int16_t position_;
    int16_t target_;
    int16_t prepared_;
    bool stepPending_;
};

#endif
