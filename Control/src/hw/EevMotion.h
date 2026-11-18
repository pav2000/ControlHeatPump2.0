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
