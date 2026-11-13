// SPDX-License-Identifier: MIT
// Turns a driver's form into changes in the game: their pace, their driving quality and how hard they defend.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/systems/cars.hpp>

#include "ai_driver.hpp"
#include "form_state.hpp"

#include <cmath>

namespace driver_form {

using cars::live::Cars;

constexpr const char *kBlocking = "ai.blocking.max_blocking_distance"; // how far a driver defends

// Out-of-form drivers lose time through their driving as well, so the dial is smaller on that side.
inline float shift_of(const Driver &driver)
{
    float shift = driver.gap;
    if (driver.form < 0) {
        shift -= driver.form * g_strength.seconds * 0.6f;
    } else {
        shift -= driver.form * g_strength.seconds;
    }
    if (driver.patch == Patch::bad) {
        shift += 1.0f * g_strength.patch;
    }
    if (driver.patch == Patch::hot) {
        shift -= 0.4f * g_strength.patch;
    }
    return shift;
}

inline float quality_of(const Driver &driver)
{
    float quality = 1.0f;
    if (driver.form < 0) {
        quality = 1.0f + driver.form * g_strength.depth;
    }
    if (driver.patch == Patch::bad) {
        quality = quality * 0.6f;
    }
    return quality;
}

// Gives the driver back to the game exactly as it was.
inline void clear_driver(const Driver &driver)
{
    for (uintptr_t state : {driver.state, driver.master}) {
        if (!state) {
            continue;
        }
        for (int characteristic = 0; characteristic < 6; characteristic++) {
            ai_driver::clear_characteristic(state, characteristic);
        }
        ai_driver::clear_difficulty(state);
    }
    if (driver.profile) {
        ai_driver::poke<float>(driver.profile + 0x148, driver.stock.base_lap_time);
    }
}

inline void write_driver(const Driver &driver)
{
    constexpr int kWrittenCharacteristics[] = {0, 1, 4, 5}; // aggression, control, speed in, speed out
    float quality = quality_of(driver);
    for (uintptr_t state : {driver.state, driver.master}) {
        if (!state) {
            continue;
        }
        // A moment: for a few seconds the driver drives far below their level.
        if (driver.patch == Patch::moment) {
            ai_driver::set_difficulty(state, driver.stock.difficulty * 0.35f);
        } else {
            ai_driver::clear_difficulty(state);
        }
        for (int characteristic : kWrittenCharacteristics) {
            if (quality < 0.999f) {
                float stock_value = driver.stock.characteristics[characteristic];
                ai_driver::set_characteristic(state, characteristic, stock_value * quality);
            } else {
                ai_driver::clear_characteristic(state, characteristic);
            }
        }
    }
    if (driver.profile) {
        ai_driver::poke<float>(driver.profile + 0x148, driver.stock.base_lap_time + shift_of(driver));
    }
}

// Good form makes a driver defend harder, bad form makes them give way sooner.
inline void start_layers(Driver &driver)
{
    Layer layer =
        Layer::scale(Cars::car(cars::live::Car(driver.car)), kBlocking, std::exp2(driver.form * g_strength.blocking));
    if (layer.ok()) {
        driver.layers.push_back(std::move(layer));
    }
}

} // namespace driver_form
