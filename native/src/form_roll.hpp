// SPDX-License-Identifier: MIT
// Rolls each driver's form for the weekend, and saves it so a career weekend keeps its forms between sessions.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/storage.hpp>

#include "form_state.hpp"

#include <algorithm>
#include <sstream>

namespace driver_form {

constexpr const char *kSaveFile = "weekend.txt";
constexpr long long kWeekendLife = 24 * 3600; // a saved weekend older than this is a new weekend

// A third of the grid clearly in form, a third clearly out, the rest neutral.
inline void roll_grid(std::vector<Driver> &drivers)
{
    std::vector<size_t> order(drivers.size());
    for (size_t i = 0; i < order.size(); i++) {
        order[i] = i;
    }
    std::shuffle(order.begin(), order.end(), g_random);
    size_t third = drivers.size() / 3;
    for (size_t i = 0; i < order.size(); i++) {
        float form = 0;
        if (i < third) {
            form = random_between(0.6f, 1.0f);
        } else if (i < third * 2) {
            form = random_between(-1.0f, -0.6f);
        } else {
            form = random_between(-0.15f, 0.15f);
        }
        g_week[drivers[order[i]].stat_id] = form;
    }
}

// The rolls survive closing the game, so a career weekend keeps its forms between sessions.
inline void load_week()
{
    static bool loaded = false;
    if (loaded) {
        return;
    }
    loaded = true;
    std::string text;
    if (!storage::available() || storage::read(kSaveFile, text) != storage::Result::ok) {
        return;
    }
    std::istringstream input(text);
    float key = 0;
    long long saved_time = 0;
    if (!(input >> key >> saved_time)) {
        return;
    }
    int stat_id = 0;
    float form = 0;
    while (input >> stat_id >> form) {
        g_week[uint16_t(stat_id)] = form;
    }
    g_week_key = key;
    g_week_time = saved_time;
    log("[driver-form] saved weekend loaded: %zu drivers, base lap %.3f", g_week.size(), double(key));
}

inline void save_week()
{
    if (!storage::available()) {
        return;
    }
    std::ostringstream output;
    output << g_week_key << ' ' << g_week_time << '\n';
    for (const auto &[stat_id, form] : g_week) {
        output << stat_id << ' ' << form << '\n';
    }
    log("[driver-form] weekend saved: %s", storage::name(storage::write(kSaveFile, output.str())));
}

} // namespace driver_form
