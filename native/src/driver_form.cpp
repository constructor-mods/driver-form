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
    if (!strength_of(option<std::string>("form_strength"), g_strength)) {
        log("[driver-form] form strength off, normal AI");
        return;
    }
    g_patches_on = option<bool>("bad_patches");
    g_cars_at_crossing.fill(0);
    g_race_running = false;
    g_armed = true;
}

// Checks whether a car is yours. Once yours, always yours: the game hands your car over in the pits.
void check_control(Driver &driver, size_t index)
{
    bool first_check = driver.human && !g_player_cars.count(driver.car) && !driver.seen;
    if (index != g_next_control_index && !first_check) {
        return;
    }
    driver.seen = true;
    auto control = cars::live::Car(driver.car).control();
    if (control && control->mode == sessions::Control::human) {
        g_player_cars.insert(driver.car);
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
    if (g_drivers.empty()) {
        if (!build()) {
            g_drivers.clear();
        }
        return;
    }
    // Guarded reads cost about 0.5 ms each here, so the tick does as few as it can: the game's driver system is
    // re-checked every 10 s, and control is read for one car per tick (it takes that car about 0.25 ms).
    if (now - g_last_system_check >= std::chrono::seconds(10)) {
        g_last_system_check = now;
        ai_driver::GameDrivers game;
        if (!ai_driver::find(game)) {
            return;
        }
        if (game.records != g_records) { // the game rebuilt its drivers: the cached pointers are gone
            g_drivers.clear();
            return;
        }
    }
    g_next_control_index = (g_next_control_index + 1) % g_drivers.size();
    for (size_t index = 0; index < g_drivers.size(); index++) {
        Driver &driver = g_drivers[index];
        check_control(driver, index);
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
        log("[driver-form] session.start: %zu entrants in the event context, session '%s', kind %d",
            g_context_entrants.size(), view ? view->session.c_str() : "?", g_session_kind);
        arm();
    });
    // A restart fires race.start again with no session.end before it.
    events::on("race.start").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_session_kind = 2;
        arm();
        g_race_running = true;
        g_race_started = Clock::now();
    });
    events::on<sessions::events::Lap>().after([](const sessions::events::Lap &event, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        on_lap(event.car.index());
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
