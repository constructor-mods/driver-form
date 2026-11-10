// SPDX-License-Identifier: MIT
// Finds out who is on the grid: which cars, which drivers, what the game calls them, and what kind of session it is.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <swofty/db.hpp>
#include <swofty/registry.hpp>

#include "ai_driver.hpp"

#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace driver_form {

using namespace swofty;

// ---- session entrants (read only) ----
constexpr uintptr_t kGameManagerModeContext = 0xc0;
constexpr uintptr_t kModeSetup = 0x88;
constexpr uintptr_t kSetupModes = 0x260;
constexpr uintptr_t kSessionEntrantsBegin = 0x2e0;
constexpr uintptr_t kSessionEntrantsEnd = 0x2e8;
constexpr uintptr_t kEntrantStride = 0x88;

struct Entrant {
    uint16_t driver = 0; // matches entrants.drivers stat_id
    uint8_t number = 0;
    uint8_t car = 0xff;  // the game's vehicle id
    std::string label;   // the DB row, when the grid came from the event context
};

inline bool entrants_from_setup(uintptr_t setup, std::vector<Entrant> &out)
{
    uintptr_t begin = 0;
    uintptr_t end = 0;
    if (!setup || !mem::read(setup + kSessionEntrantsBegin, begin) || !mem::read(setup + kSessionEntrantsEnd, end) ||
        end <= begin) {
        return false;
    }
    if ((end - begin) % kEntrantStride != 0 || (end - begin) / kEntrantStride > 64) {
        return false;
    }
    for (uintptr_t address = begin; address < end; address += kEntrantStride) {
        Entrant entrant;
        mem::read(address + 0x52, entrant.driver);
        mem::read(address + 0x83, entrant.number);
        mem::read(address + 0x82, entrant.car);
        out.push_back(entrant);
    }
    return true;
}

// The entrants of a mode context: it points to the setup, which holds the list.
inline bool entrants_from_context(uintptr_t context, std::vector<Entrant> &out)
{
    uintptr_t holder = 0;
    uintptr_t setup = 0;
    if (!context || !mem::read(context + kModeSetup, holder) || !holder) {
        return false;
    }
    if (!mem::read(holder + kSetupModes, setup)) {
        return false;
    }
    return entrants_from_setup(setup, out);
}

inline uintptr_t race_state(const ai_driver::GameDrivers &game)
{
    uintptr_t system_data = 0;
    uintptr_t state = 0;
    if (!mem::read(game.system + 0x10, system_data) || !system_data) {
        return 0;
    }
    if (!mem::read(system_data + 0xa8, state)) {
        return 0;
    }
    return state;
}

// The name the game shows for a car, in capitals. Empty when it has none.
inline std::string game_name(const ai_driver::GameDrivers &game, uint32_t car)
{
    using NameFunction = const char *(*)(void *, uint8_t);
    static NameFunction name_function = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        if (api && api->resolve) {
            name_function = reinterpret_cast<NameFunction>(api->resolve("Sess_Get_CarDriverName"));
        }
    }
    uintptr_t state = race_state(game);
    if (!name_function || !state) {
        return {};
    }
    std::string name;
    const char *text = name_function(reinterpret_cast<void *>(state), uint8_t(car));
    if (!text || !mem::c_string(text, name, 64) || name.empty()) {
        return {};
    }
    for (char &character : name) {
        if (static_cast<unsigned char>(character) < 0x80) {
            character = char(std::toupper((unsigned char)character));
        }
    }
    return name;
}

// The kind of session: 0 practice, 1 qualifying, 2 race. -1 when it can't be read.
inline int session_kind(const ai_driver::GameDrivers &game)
{
    uintptr_t system_data = 0;
    uintptr_t session_data = 0;
    uintptr_t session_info = 0;
    uint32_t type = 0;
    if (!mem::read(game.system + 0x10, system_data) || !system_data) {
        return -1;
    }
    if (!mem::read(system_data + 0x68, session_data) || !session_data) {
        return -1;
    }
    if (!mem::read(session_data + 0x1a8, session_info) || !session_info) {
        return -1;
    }
    if (!mem::read(session_info + 4, type)) {
        return -1;
    }
    log("[driver-form] game session type %u", type);
    if (type >= 1 && type <= 4) {
        return 0;
    }
    if (type >= 5 && type <= 14) {
        return 1;
    }
    if (type >= 15 && type <= 17) {
        return 2;
    }
    return -1;
}

inline std::vector<Entrant> g_context_entrants; // from the event context; careers fill it, a stock Grand Prix does not

// Says once which way the grid was found.
inline void report_route(int route, size_t count)
{
    static int last_route = -1;
    if (route == last_route) {
        return;
    }
    last_route = route;
    const char *const kRoutes[] = {"game manager", "AI system", "career context", "AI records", "race state"};
    log("[driver-form] grid from %s: %zu cars", kRoutes[route], count);
}

// The grid. A Grand Prix holds it where the launcher's own trace reads it; a career does not (the trace shows no
// entrants there), so the routes are tried in turn. The last one needs no entrant data at all: it takes the cars the
// game is running its own driver system on, without names.
inline bool read_entrants(const ai_driver::GameDrivers &game, std::vector<Entrant> &out)
{
    out.clear();
    uintptr_t slot = 0;
    if (api && api->resolve) {
        slot = api->resolve("g_Game_Manager");
    }
    uintptr_t game_manager = 0;
    uintptr_t mode_context = 0;
    if (slot && mem::read(slot, game_manager) && game_manager &&
        mem::read(game_manager + kGameManagerModeContext, mode_context) && entrants_from_context(mode_context, out)) {
        report_route(0, out.size());
        return true;
    }
    out.clear();
    uintptr_t system_context = 0;
    if (mem::read(game.system + 0x60, system_context) && entrants_from_context(system_context, out)) {
        report_route(1, out.size());
        return true;
    }
    out.clear();
    // The same route the game uses to name a car.
    uintptr_t state = race_state(game);
    uintptr_t race_context = 0;
    if (state && mem::read(state + 0x190, race_context) && entrants_from_context(race_context, out)) {
        report_route(4, out.size());
        return true;
    }
    out.clear();
    if (!g_context_entrants.empty()) {
        out = g_context_entrants;
        report_route(2, out.size());
        return true;
    }
    // Every record but the last, which is the safety car. Your own car is dropped by the first tick.
    for (uint32_t car = 0; car + 1 < game.record_count; car++) {
        if (!ai_driver::state_of(game, car)) {
            continue;
        }
        Entrant entrant;
        entrant.car = uint8_t(car);
        entrant.driver = uint16_t(0xf000 + car);
        entrant.number = uint8_t(car);
        out.push_back(entrant);
    }
    if (out.empty()) {
        return false;
    }
    report_route(3, out.size());
    return true;
}

// stat_id -> initials from the game DB table entrants.drivers
// The table does not change while the game runs, so it is read once and kept.
inline const std::map<int64_t, std::string> &driver_initials()
{
    static std::map<int64_t, std::string> initials_by_id;
    if (!initials_by_id.empty() || !db::available()) {
        return initials_by_id;
    }
    db::Cursor cursor{};
    if (db::open("entrants.drivers", &cursor) != db::Result::ok) {
        return initials_by_id;
    }
    for (int guard = 0; guard < 4000; guard++) {
        db::Value id{};
        db::Value initials{};
        bool has_id = db::cell(&cursor, "stat_id", &id) == db::Result::ok;
        bool has_initials = db::cell(&cursor, "initials", &initials) == db::Result::ok;
        if (has_id && has_initials && initials.s && *initials.s) {
            initials_by_id[id.i] = initials.s;
        }
        if (db::step(&cursor) != db::Result::ok) {
            break;
        }
    }
    return initials_by_id;
}

} // namespace driver_form
