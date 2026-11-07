// SPDX-License-Identifier: MIT
// Builds the list of drivers when a session is ready: who they are, what the game had set, their form for the weekend.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/db.hpp>
#include <swofty/registry.hpp>

#include "ai_driver.hpp"
#include "form_roll.hpp"
#include "form_state.hpp"
#include "grid.hpp"

#include <cmath>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace driver_form {

// The name shown for a driver: the game's own name if it has one, otherwise their initials, otherwise the car number.
inline void set_label(Driver &driver, const Entrant &entrant, const ai_driver::GameDrivers &game,
                      const std::map<int64_t, std::string> &initials_by_id)
{
    auto found = initials_by_id.find(entrant.driver);
    if (found != initials_by_id.end()) {
        driver.label = found->second;
    } else {
        driver.label = "#" + std::to_string(entrant.number);
    }
    if (!entrant.label.empty()) {
        db::Cursor cursor{};
        db::Value initials{};
        db::Value id{};
        if (db::row("entrants.drivers", entrant.label, &cursor) == db::Result::ok) {
            if (db::cell(&cursor, "initials", &initials) == db::Result::ok && initials.s && *initials.s) {
                driver.label = initials.s;
            }
            if (db::cell(&cursor, "stat_id", &id) == db::Result::ok) {
                driver.stat_id = uint16_t(id.i);
            }
        }
    }
    std::string shown = game_name(game, entrant.car);
    if (!shown.empty()) {
        driver.label = shown;
    }
}

// Rolls a new weekend's forms, or keeps the saved ones, and gives every driver theirs.
inline void give_weekend_forms()
{
    load_week();
    float key = g_drivers[0].stock.base_lap_time;
    long long now = (long long)std::time(nullptr);
    bool known = true;
    for (const Driver &driver : g_drivers) {
        known = known && g_week.count(driver.stat_id) != 0;
    }
    if (std::fabs(key - g_week_key) > 0.01f || now - g_week_time > kWeekendLife || !known) {
        g_week.clear();
        g_week_key = key;
        g_week_time = now;
        g_player_cars.clear();
        roll_grid(g_drivers);
        log("[driver-form] new weekend, base lap %.3f", double(key));
        save_week();
    }
    for (Driver &driver : g_drivers) {
        auto found = g_week.find(driver.stat_id);
        if (found != g_week.end()) {
            driver.form = found->second;
        } else {
            driver.form = 0.0f;
        }
    }
}

// Reads the "Messages" setting for this kind of session.
inline std::string set_message_level(const ai_driver::GameDrivers &game)
{
    int kind = session_kind(game);
    if (kind >= 0) {
        g_session_kind = kind;
    }
    const char *setting = "messages_practice";
    if (g_session_kind == 2) {
        setting = "messages_race";
    } else if (g_session_kind == 1) {
        setting = "messages_qualifying";
    }
    std::string level = option<std::string>(setting);
    g_message_level = 0;
    if (level == "low") {
        g_message_level = 1;
    } else if (level == "medium") {
        g_message_level = 2;
    } else if (level == "high") {
        g_message_level = 3;
    } else if (level == "ultra") {
        g_message_level = 4;
    }
    return level;
}

// Reads the grid and gives every driver their form. Returns false until the session is ready.
inline bool build()
{
    ai_driver::GameDrivers game;
    std::vector<Entrant> entrants;
    if (!ai_driver::find(game) || !read_entrants(game, entrants)) {
        return false;
    }
    auto initials_by_id = driver_initials();
    for (const Entrant &entrant : entrants) {
        Driver driver;
        driver.car = entrant.car;
        driver.stat_id = entrant.driver;
        if (!ai_driver::read_stock(game, entrant.car, driver.stock) || driver.stock.characteristics[4] <= 0 ||
            driver.stock.base_lap_time <= 0) {
            log("[driver-form] car %u not ready: speed_in %.3f base %.3f", unsigned(entrant.car),
                double(driver.stock.characteristics[4]), double(driver.stock.base_lap_time));
            return false;
        }
        driver.state = ai_driver::state_of(game, entrant.car);
        driver.master = ai_driver::master_of(game, entrant.car);
        driver.profile = ai_driver::profile_of(game, entrant.car);
        set_label(driver, entrant, game, initials_by_id);
        g_drivers.push_back(std::move(driver));
    }
    if (g_drivers.empty()) {
        return false;
    }
    g_records = game.records;
    give_weekend_forms();
    std::string level = set_message_level(game);
    log("[driver-form] %zu drivers, %.1f s a lap at full form, session kind %d, messages %s", g_drivers.size(),
        double(g_strength.seconds), g_session_kind, level.c_str());
    return true;
}

} // namespace driver_form
