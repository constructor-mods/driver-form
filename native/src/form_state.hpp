// SPDX-License-Identifier: MIT
// What the addon remembers while it runs: each driver's form and the settings that are shared by every file.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>

#include "ai_driver.hpp"

#include <array>
#include <chrono>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace driver_form {

using namespace swofty;
using cars::live::Layer;
using Clock = std::chrono::steady_clock;

// How strong the form is, from the "Form strength" setting.
struct Strength {
    float seconds;  // pace swing at form +-1, seconds a lap
    float depth;    // how far driving quality falls at form -1
    float patch;    // size of bad patches and hot spells
    float blocking; // defending distance = stock x 2^(form x this)
};

inline bool strength_of(const std::string &name, Strength &strength)
{
    if (name == "subtle") {
        strength = {0.8f, 0.3f, 0.7f, 0.5f};
    } else if (name == "normal") {
        strength = {1.5f, 0.45f, 1.0f, 1.0f};
    } else if (name == "wild") {
        strength = {2.2f, 0.6f, 1.5f, 1.6f};
    } else if (name == "extreme") {
        strength = {3.5f, 0.7f, 2.5f, 2.5f};
    } else {
        return false;
    }
    return true;
}

enum class Patch { none, bad, hot, moment };

struct Driver {
    uint32_t car = 0;
    uint16_t stat_id = 0;
    std::string label;
    uintptr_t state = 0;   // the game's values for this car, found once so the tick stays cheap
    uintptr_t master = 0;
    uintptr_t profile = 0;
    float form = 0;        // -1 bad .. +1 good
    float gap = 0;         // seconds a lap from the real driver gaps
    ai_driver::Stock stock;
    std::vector<Layer> layers;
    Patch patch = Patch::none;
    Clock::time_point patch_until;
    Clock::time_point next_event;
    bool human = true;     // the first tick decides
    int crossings = 0;
    int last_position = 0;
    int pit_crossing = -1;
};

inline std::vector<Driver> g_drivers;
inline uintptr_t g_records = 0; // the record table the cached pointers belong to
inline Clock::time_point g_last_system_check;
inline Clock::time_point g_last_control_check;
inline bool g_check_control = true; // read who drives every car on the next tick
inline bool g_time_trial = false;   // a Time Trial is left alone: its cars are your own ghosts
inline std::map<uint16_t, float> g_week; // this weekend's rolls, by driver
inline float g_week_key = -1;            // the track's base target time tells one weekend from the next
inline std::mt19937 g_random{std::random_device{}()};
inline Strength g_strength{};
inline bool g_patches_on = true;
inline bool g_armed = false;
inline bool g_board_done = false;
inline Clock::time_point g_last_tick;
inline Clock::time_point g_last_build_try;
inline int g_build_failures = 0; // after a few, the grid is only tried every 10 s
inline Clock::time_point g_last_toast;
inline Clock::time_point g_race_started;
inline bool g_race_running = false;
inline bool g_paused = false;
inline Clock::time_point g_paused_since;
inline std::mutex g_mutex;
inline std::set<uint32_t> g_player_cars;          // cars ever seen under human control, never touched again
inline long long g_week_time = 0;                 // when this weekend's forms were rolled
inline std::array<int, 128> g_cars_at_crossing{}; // how many cars have reached each line crossing
inline int g_session_kind = 2;                    // 0 practice, 1 qualifying, 2 race; each has its own Messages setting
inline int g_message_level = 0;                   // 0 none, 1 low, 2 medium, 3 high, 4 ultra

inline float random_between(float low, float high)
{
    return std::uniform_real_distribution<float>(low, high)(g_random);
}

} // namespace driver_form
