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

#include "EevMotion.h"

EevMotion::EevMotion() : mode_(IDLE), position_(-1), target_(-1), prepared_(-1), stepPending_(false) {}

void EevMotion::reset(int16_t position)
{
    position_ = target_ = position;
    mode_ = IDLE;
    stepPending_ = false;
}

EevMotion::Result EevMotion::moveTo(int16_t target, int16_t minimum, int16_t maximum)
{
    if(minimum < 0 || maximum <= minimum || target < minimum || target > maximum) return INVALID;
    if(mode_ == HOMING) return BUSY;
    target_ = target;
    mode_ = !stepPending_ && position_ == target_ ? IDLE : MOVING;
    return ACCEPTED;
}

EevMotion::Result EevMotion::home(int16_t maximum, int16_t overrun)
{
    if(maximum <= 0 || overrun < 0 || (int32_t)maximum + overrun > INT16_MAX - 7) return INVALID;
    if(mode_ != IDLE) return BUSY;
    // Старый алгоритм начинает с полного хода и перебега, округлённых до 8 фаз.
    position_ = (maximum + overrun + 7) / 8 * 8;
    target_ = 0;
    mode_ = HOMING;
    return ACCEPTED;
}

bool EevMotion::prepareStep(int16_t &position)
{
    if(stepPending_) return false;
    if(mode_ != MOVING && mode_ != HOMING) return false;
    if(position_ == target_) {
        if(mode_ == HOMING) position_ = 0;
        mode_ = IDLE;
        return false;
    }
    prepared_ = position_ + (position_ < target_ ? 1 : -1);
    stepPending_ = true;
    position = prepared_;
    return true;
}

void EevMotion::commitStep()
{
    if(!stepPending_) return;
    position_ = prepared_;
    stepPending_ = false;
    // IDLE будет опубликован в prepareStep после физического шага.
}

EevMotion::Snapshot EevMotion::snapshot() const
{
    Snapshot s = {mode_, position_, target_};
    return s;
}
