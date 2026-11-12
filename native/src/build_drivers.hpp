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

#include <chrono>
#include <cmath>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace driver_form {

// The name the game shows for a driver. Most sessions have one for every car, so nothing slower is needed.
inline bool set_game_label(Driver &driver, const Entrant &entrant, const ai_driver::GameDrivers &game)
{
    std::string shown = game_name(game, entrant.car);
    if (shown.empty()) {
        return false;
    }
    driver.label = shown;
    return true;
}

// The name for a driver the game has no name for: their initials, otherwise the car number.
inline void set_label(Driver &driver, const Entrant &entrant, const std::map<int64_t, std::string> &initials_by_id)
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
        if (db::row("entrants.drivers", entrant.label, &cursor) == db::Result::ok) {
            if (db::cell(&cursor, "initials", &initials) == db::Result::ok && initials.s && *initials.s) {
                driver.label = initials.s;
            }
        }
    }
}

// The driver's id in the game's table, when the grid came from the event context.
inline void set_stat_id(Driver &driver, const Entrant &entrant)
{
    if (entrant.label.empty()) {
        return;
    }
    db::Cursor cursor{};
    db::Value id{};
    if (db::row("entrants.drivers", entrant.label, &cursor) == db::Result::ok &&
        db::cell(&cursor, "stat_id", &id) == db::Result::ok) {
        driver.stat_id = uint16_t(id.i);
    }
}

// Milliseconds since a point in time, for the build log.
inline double milliseconds_since(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
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
    auto start = Clock::now();
    ai_driver::GameDrivers game;
    std::vector<Entrant> entrants;
    if (!ai_driver::find(game) || !read_entrants(game, entrants)) {
        return false;
    }
    for (const Entrant &entrant : entrants) {
        Driver driver;
        driver.car = entrant.car;
        driver.stat_id = entrant.driver;
        if (entrant.car == 0xff) {
            if (g_build_failures == 0) {
                log("[driver-form] grid has no car numbers yet, waiting");
            }
            return false;
        }
        if (!ai_driver::read_stock(game, entrant.car, driver.stock) || driver.stock.characteristics[4] <= 0 ||
            driver.stock.base_lap_time <= 0) {
            if (g_build_failures < 3) {
                log("[driver-form] car %u not ready: speed_in %.3f base %.3f", unsigned(entrant.car),
                    double(driver.stock.characteristics[4]), double(driver.stock.base_lap_time));
            }
            return false;
        }
        driver.state = ai_driver::state_of(game, entrant.car);
        driver.master = ai_driver::master_of(game, entrant.car);
        driver.profile = ai_driver::profile_of(game, entrant.car);
        g_drivers.push_back(std::move(driver));
    }
    if (g_drivers.empty()) {
        return false;
    }
    auto cars_done = Clock::now();
    // The game's own names first. The table of initials is slow to walk, so it is only read for drivers without one.
    std::vector<size_t> unnamed;
    for (size_t index = 0; index < g_drivers.size(); index++) {
        set_stat_id(g_drivers[index], entrants[index]);
        if (!set_game_label(g_drivers[index], entrants[index], game)) {
            unnamed.push_back(index);
        }
    }
    if (!unnamed.empty()) {
        const auto &initials_by_id = driver_initials();
        for (size_t index : unnamed) {
            set_label(g_drivers[index], entrants[index], initials_by_id);
        }
    }
    auto names_done = Clock::now();
    g_records = game.records;
    give_weekend_forms();
    std::string level = set_message_level(game);
    log("[driver-form] %zu drivers, %.1f s a lap at full form, session kind %d, messages %s", g_drivers.size(),
        double(g_strength.seconds), g_session_kind, level.c_str());
    log("[driver-form] grid built in %.1f ms: cars %.1f, names %.1f (%zu without a game name), weekend %.1f",
        milliseconds_since(start), std::chrono::duration<double, std::milli>(cars_done - start).count(),
        std::chrono::duration<double, std::milli>(names_done - cars_done).count(), unnamed.size(),
        milliseconds_since(names_done));
    return true;
}

} // namespace driver_form
