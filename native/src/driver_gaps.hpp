// SPDX-License-Identifier: MIT
// Real gaps between team-mates: how much faster or slower each driver is than their team's average, from the
// real qualifying averages. The car's pace stays the game's; only team-mates move apart.
#pragma once
#include <swofty/mixin.hpp>

#include "form_state.hpp"

#include <string>
#include <vector>

namespace driver_form {

struct DriverGap {
    const char *name;
    float seconds; // minus is faster than the team-mate, on a 90 second lap
};

// The 2026 line-up.
constexpr DriverGap kGaps2026[] = {
    {"NORRIS", -0.038f},     {"PIASTRI", 0.038f},  {"ANTONELLI", -0.045f}, {"RUSSELL", 0.045f},
    {"VERSTAPPEN", -0.072f}, {"HADJAR", 0.072f},   {"LECLERC", -0.020f},   {"HAMILTON", 0.020f},
    {"SAINZ", -0.116f},      {"ALBON", 0.116f},    {"ALONSO", -0.176f},    {"STROLL", 0.176f},
    {"BEARMAN", -0.125f},    {"OCON", 0.125f},     {"GASLY", -0.155f},     {"COLAPINTO", 0.155f},
    {"LAWSON", -0.069f},     {"LINDBLAD", 0.069f}, {"BORTOLETO", -0.031f}, {"HULKENBERG", 0.031f},
    {"PEREZ", -0.118f},      {"BOTTAS", 0.118f},
};

// The 2025 line-up, for a grid without the 2026 newcomers.
constexpr DriverGap kGaps2025[] = {
    {"VERSTAPPEN", -0.340f}, {"TSUNODA", 0.340f},  {"RUSSELL", -0.215f},   {"ANTONELLI", 0.215f},
    {"ALONSO", -0.205f},     {"STROLL", 0.205f},   {"LECLERC", -0.130f},   {"HAMILTON", 0.130f},
    {"HADJAR", -0.095f},     {"LAWSON", 0.095f},   {"SAINZ", -0.065f},     {"ALBON", 0.065f},
    {"BEARMAN", -0.045f},    {"OCON", 0.045f},     {"HULKENBERG", -0.035f}, {"BORTOLETO", 0.035f},
    {"NORRIS", -0.035f},     {"PIASTRI", 0.035f},  {"GASLY", -0.155f},     {"COLAPINTO", 0.155f},
};

constexpr float kSheetLapSeconds = 90.0f;

inline bool g_gaps_on = false;

// Game names can carry accents ("PÉREZ"): compare the plain letters only.
inline std::string plain_name(const std::string &label)
{
    std::string plain;
    for (size_t i = 0; i < label.size(); i++) {
        unsigned char letter = label[i];
        if (letter < 0x80) {
            plain += char(letter);
        } else if (letter == 0xC3 && i + 1 < label.size()) {
            unsigned char accented = label[i + 1] & 0x1F;
            if (accented <= 0x06) {
                plain += 'A';
            } else if (accented >= 0x08 && accented <= 0x0B) {
                plain += 'E';
            } else if (accented >= 0x0C && accented <= 0x0F) {
                plain += 'I';
            } else if (accented == 0x11) {
                plain += 'N';
            } else if (accented >= 0x12 && accented <= 0x16) {
                plain += 'O';
            } else if (accented >= 0x19 && accented <= 0x1C) {
                plain += 'U';
            }
            i++;
        }
    }
    return plain;
}

inline bool grid_has(const std::vector<std::string> &names, const char *wanted)
{
    for (const std::string &name : names) {
        if (name == wanted) {
            return true;
        }
    }
    return false;
}

// Gives every driver on the sheet their gap, scaled to this track's lap length. Others keep the game's pace.
inline void give_gaps()
{
    for (Driver &driver : g_drivers) {
        driver.gap = 0;
    }
    if (!g_gaps_on) {
        return;
    }
    std::vector<std::string> names;
    for (const Driver &driver : g_drivers) {
        names.push_back(plain_name(driver.label));
    }
    bool line_up_2026 = grid_has(names, "LINDBLAD") || grid_has(names, "BOTTAS") || grid_has(names, "PEREZ");
    int found = 0;
    for (size_t index = 0; index < g_drivers.size(); index++) {
        Driver &driver = g_drivers[index];
        float scale = driver.stock.base_lap_time / kSheetLapSeconds;
        if (line_up_2026) {
            for (const DriverGap &gap : kGaps2026) {
                if (names[index] == gap.name) {
                    driver.gap = gap.seconds * scale;
                }
            }
        } else {
            for (const DriverGap &gap : kGaps2025) {
                if (names[index] == gap.name) {
                    driver.gap = gap.seconds * scale;
                }
            }
        }
        if (driver.gap != 0) {
            found++;
            log("[driver-form] gap car %u %s %+.3f s", driver.car, driver.label.c_str(), double(driver.gap));
        }
    }
    log("[driver-form] real driver gaps: %s line-up, %d of %zu drivers", line_up_2026 ? "2026" : "2025", found,
        g_drivers.size());
}

} // namespace driver_form
