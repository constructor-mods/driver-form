// SPDX-License-Identifier: MIT
// Driver Form: every driver the computer controls gets a weekend form. The form sets their pace through the game's
// own target lap time, lowers their driving quality when they are out of form, and brings bad patches and hot
// spells. A board names the drivers in and out of form.
// This file wires the other files together: the grid, the form roll, the writes to the game, the patches and the
// messages.
// Include mixin.hpp first: registry.hpp redefines SWOFTY_ADDON so the DLL exports SwoftyAddonRegister.
#include <swofty/mixin.hpp>
#include <swofty/mem.hpp>

#include <swofty/db.hpp>
#include <swofty/events.hpp>
#include <swofty/hud.hpp>
#include <swofty/registry.hpp>
#include <swofty/storage.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/view.hpp>

#include "ai_driver.hpp"
#include "ai_writes.hpp"
#include "build_drivers.hpp"
#include "form_roll.hpp"
#include "form_state.hpp"
#include "grid.hpp"
#include "lap_events.hpp"
#include "messages.hpp"
#include "patches.hpp"

#include <chrono>
#include <string>
#include <vector>

using namespace swofty;
using namespace driver_form;

namespace {

// Gives every driver back to the game and forgets the list.
void stop()
{
    ai_driver::GameDrivers game;
    if (!g_drivers.empty() && ai_driver::find(game) && game.records == g_records) {
        for (const Driver &driver : g_drivers) {
            clear_driver(driver);
        }
    }
    g_drivers.clear(); // destroying a Layer puts the game's value back
    g_armed = false;
    g_board_done = false;
}

// Gets ready for a new session. The list is built later, when the game has its drivers.
void arm()
{
    stop();
    g_last_tick = Clock::now(); // leaves the game time to take the cleared values before they are read as stock
    if (g_time_trial) {
        log("[driver-form] time trial, left alone");
        return;
    }
    if (!strength_of(option<std::string>("form_strength"), g_strength)) {
        log("[driver-form] form strength off, normal AI");
        return;
    }
    g_patches_on = option<bool>("bad_patches");
    g_cars_at_crossing.fill(0);
    g_build_failures = 0;
    g_race_running = false;
    g_paused = false;
    g_check_control = true;
    g_armed = true;
}

// Finds the cars you drive. Once yours, always yours: the game hands your car over in the pits.
// Asking who drives a car is slow, so every car is asked in one go: when the grid is built, then every 10 s.
// In between, the game tells us when it hands a car to you.
void check_control()
{
    for (const Driver &driver : g_drivers) {
        auto control = cars::live::Car(driver.car).control();
        if (control && control->mode == sessions::Control::human) {
            g_player_cars.insert(driver.car);
        }
    }
}

// Switches a driver between "yours, left alone" and "ours, form applied".
void update_ownership(Driver &driver, bool human)
{
    driver.human = human;
    if (human) {
        driver.layers.clear();
        clear_driver(driver);
        log("[driver-form] %s is yours, left alone", driver.label.c_str());
        return;
    }
    start_layers(driver);
    log("[driver-form] car %u %s: form %+.2f, %+.2f s, quality %.2f", driver.car, driver.label.c_str(),
        double(driver.form), double(shift_of(driver)), double(quality_of(driver)));
}

// Holds the race clock while the game is paused: no patches start or end, and on resume every timer moves on by
// the length of the pause.
bool check_pause(Clock::time_point now)
{
    bool paused = view::paused();
    if (paused && !g_paused) {
        g_paused = true;
        g_paused_since = now;
        log("[driver-form] paused");
    } else if (!paused && g_paused) {
        g_paused = false;
        auto length = now - g_paused_since;
        g_race_started += length;
        for (Driver &driver : g_drivers) {
            driver.patch_until += length;
            driver.next_event += length;
        }
        log("[driver-form] resumed after %.0f s", std::chrono::duration<double>(length).count());
    }
    return paused;
}

// Runs twice a second: builds the grid when the session is ready, then keeps every driver's form written.
void frame(double, void *)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_armed) {
        return;
    }
    auto now = Clock::now();
    if (now - g_last_tick < std::chrono::milliseconds(500)) {
        return;
    }
    g_last_tick = now;
    // The game's big memory blocks stay where they are, so a check of one is trusted for two seconds instead of
    // being asked again for every read.
    mem::Trust trust(2000);
    if (check_pause(now)) {
        return;
    }
    if (g_drivers.empty()) {
        if (g_build_failures >= 10 && now - g_last_build_try < std::chrono::seconds(10)) {
            return;
        }
        g_last_build_try = now;
        if (build()) {
            g_build_failures = 0;
        } else {
            g_drivers.clear();
            g_build_failures++;
        }
        return;
    }
    // Two slow checks (about 5 ms each), each every 10 s and 5 s apart, never both in one tick: who drives each
    // car, and is the game's driver system still the one we found.
    if (g_check_control || now - g_last_control_check >= std::chrono::seconds(10)) {
        if (g_check_control) {
            g_last_system_check = now - std::chrono::seconds(5);
        }
        g_check_control = false;
        g_last_control_check = now;
        check_control();
    } else if (now - g_last_system_check >= std::chrono::seconds(10)) {
        g_last_system_check = now;
        uintptr_t records = ai_driver::records_start();
        if (!records) {
            return;
        }
        if (records != g_records) { // the game rebuilt its drivers: the cached pointers are gone
            g_drivers.clear();
            return;
        }
    }
    for (Driver &driver : g_drivers) {
        bool human = g_player_cars.count(driver.car) != 0;
        if (human != driver.human) {
            update_ownership(driver, human);
        }
        if (!human) {
            tick_patch(driver, now);
            write_driver(driver);
        }
    }
}

} // namespace

SWOFTY_ON_LOAD
{
    swofty::events::on_frame(frame);
}

SWOFTY_REGISTER
{
    events::on("session.start").after([](std::span<const std::byte>, const events::Context &context) {
        std::lock_guard<std::mutex> lock(g_mutex);
        // Your car is learned again each session: the game hands it over right after this, before the grid is built.
        g_player_cars.clear();
        g_context_entrants.clear();
        for (const auto &participant : context.participants()) {
            Entrant entrant;
            entrant.car = uint8_t(participant.car);
            std::string id(participant.driver.id());
            size_t slash = id.rfind('/');
            if (slash == std::string::npos) {
                entrant.label = id;
            } else {
                entrant.label = id.substr(slash + 1);
            }
            g_context_entrants.push_back(entrant);
        }
        const auto &view = context.session();
        if (!view) {
            g_session_kind = 2;
        } else if (view->type == events::SessionType::race || view->type == events::SessionType::sprint) {
            g_session_kind = 2;
        } else if (view->type == events::SessionType::qualifying) {
            g_session_kind = 1;
        } else {
            g_session_kind = 0;
        }
        g_time_trial = view && view->type == events::SessionType::time_trial;
        log("[driver-form] session.start: %zu entrants in the event context, session '%s', kind %d",
            g_context_entrants.size(), view ? view->session.c_str() : "?", g_session_kind);
        arm();
    });
    // A restart fires race.start again with no session.end before it.
    events::on("race.start").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_session_kind = 2;
        g_time_trial = false;
        arm();
        g_race_running = true;
        g_race_started = Clock::now();
    });
    events::on<sessions::events::Lap>().after([](const sessions::events::Lap &event, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        on_lap(event.car.index());
    });
    // The game hands a car to you: it's yours from now on, the next tick leaves it alone.
    events::on<sessions::events::PlayerControl>().after(
        [](const sessions::events::PlayerControl &event, const events::Context &) {
            if (event.to != sessions::Control::human) {
                return;
            }
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_player_cars.insert(event.car.index()).second) {
                log("[driver-form] car %u handed to you", event.car.index());
            }
        });
    events::on<sessions::events::PitStop>().after([](const sessions::events::PitStop &event, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        on_pit(event.car);
    });
    events::on("session.end").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        log("[driver-form] session.end: releasing %zu driver(s)", g_drivers.size());
        stop();
    });
}

SWOFTY_ADDON()
