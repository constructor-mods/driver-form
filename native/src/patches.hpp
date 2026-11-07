// SPDX-License-Identifier: MIT
// Bad patches, hot spells and single mistakes during a race.
#pragma once
#include <swofty/mixin.hpp>

#include "form_state.hpp"
#include "messages.hpp"

namespace driver_form {

// Starts and ends a driver's patch. Called twice a second for each driver the computer controls.
inline void tick_patch(Driver &driver, Clock::time_point now)
{
    if (driver.patch != Patch::none) {
        if (now >= driver.patch_until) {
            if (driver.patch != Patch::moment) {
                log("[driver-form] %s patch over", driver.label.c_str());
            }
            driver.patch = Patch::none;
            driver.next_event = now + std::chrono::seconds(45);
        }
        return;
    }
    // Nothing in the first minute of a race, when a sudden slow car would wreck the field.
    if (!g_patches_on || !g_race_running || now - g_race_started < std::chrono::seconds(60) ||
        now < driver.next_event) {
        return;
    }
    // Chances per minute; a tick is half a second.
    float cold = 0.0f;
    if (driver.form < -0.3f) {
        cold = 1.0f;
    }
    float warm = 0.0f;
    if (driver.form > 0.3f) {
        warm = 1.0f;
    }
    float middle = 1.0f - cold - warm;
    float moment_chance = cold * 0.5f + middle * 0.06f + warm * 0.02f;
    float bad_chance = cold * 0.25f + middle * 0.03f + warm * 0.01f;
    float hot_chance = warm * 0.20f + middle * 0.03f;
    float roll = random_between(0, 1) * 120.0f;
    float seconds = 0;
    if (roll < moment_chance) {
        driver.patch = Patch::moment;
        seconds = random_between(3, 8);
        say("toast_mistakes", driver.label + " MISTAKE", "Big time loss", "black");
    } else if (roll < moment_chance + bad_chance) {
        driver.patch = Patch::bad;
        seconds = random_between(20, 60);
        say("toast_struggling", driver.label + " STRUGGLING", "Losing time", "generic");
    } else if (roll < moment_chance + bad_chance + hot_chance) {
        driver.patch = Patch::hot;
        seconds = random_between(20, 60);
        say("toast_charge", driver.label + " ON A CHARGE", "Finding time", "fast_lap");
    } else {
        return;
    }
    driver.patch_until = now + std::chrono::milliseconds(int(seconds * 1000));
    const char *kind = "hot patch";
    if (driver.patch == Patch::moment) {
        kind = "moment";
    } else if (driver.patch == Patch::bad) {
        kind = "bad patch";
    }
    log("[driver-form] %s %s for %.0f s (form %+.2f)", driver.label.c_str(), kind, double(seconds),
        double(driver.form));
}

} // namespace driver_form
