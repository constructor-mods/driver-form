// SPDX-License-Identifier: MIT
// What happens when a car crosses the line or comes into the pits.
#pragma once
#include <swofty/mixin.hpp>

#include "form_state.hpp"
#include "messages.hpp"

#include <algorithm>
#include <string>

namespace driver_form {

// Line crossings give the running order: who gained or lost places over the last lap.
inline void on_lap(uint32_t car)
{
    board();
    if (!g_race_running) {
        g_race_running = true; // a benchmark race fires no race.start
        g_race_started = Clock::now();
    }
    for (Driver &driver : g_drivers) {
        if (driver.car != car) {
            continue;
        }
        driver.crossings++;
        int crossing = std::min(driver.crossings, int(g_cars_at_crossing.size()) - 1);
        int position = ++g_cars_at_crossing[crossing];
        auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
        log("[driver-form] lap car %u %s form %+.2f P%d t=%lldms", driver.car, driver.label.c_str(),
            double(driver.form), position, (long long)milliseconds);
        bool just_pitted = driver.pit_crossing == crossing || driver.pit_crossing == crossing - 1;
        if (driver.last_position && !driver.human && !just_pitted) {
            int gain = driver.last_position - position;
            if (gain >= 3) {
                say("toast_places", driver.label + " UP " + std::to_string(gain) + " PLACES",
                    "P" + std::to_string(position), "checkpoint");
            } else if (gain <= -3) {
                say("toast_places", driver.label + " DOWN " + std::to_string(-gain) + " PLACES",
                    "P" + std::to_string(position), "generic");
            }
        }
        driver.last_position = position;
    }
}

inline void on_pit(uint32_t car)
{
    for (Driver &driver : g_drivers) {
        if (driver.car != car) {
            continue;
        }
        if (driver.pit_crossing != driver.crossings && !driver.human) {
            std::string detail;
            if (driver.form > 0.3f) {
                detail = "In form";
            } else if (driver.form < -0.3f) {
                detail = "Out of form";
            }
            say("toast_pits", driver.label + " IN THE PITS", detail, "checkpoint");
        }
        driver.pit_crossing = driver.crossings;
    }
}

} // namespace driver_form
