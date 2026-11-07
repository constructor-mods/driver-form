// SPDX-License-Identifier: MIT
// The messages shown on the race screen: the in and out of form board, and the toasts.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/hud.hpp>
#include <swofty/registry.hpp>

#include "form_state.hpp"

#include <algorithm>

namespace driver_form {

// The race HUD's notification. The setting only sets how often one may show.
inline void say(const char *kind, const std::string &text, const std::string &detail, const char *icon = "generic")
{
    log("[driver-form] msg %s %s", text.c_str(), detail.c_str());
    if (g_message_level == 0 || !option<bool>(kind) || !hud::available()) {
        return;
    }
    auto now = Clock::now();
    constexpr int kSecondsBetweenToasts[] = {0, 60, 30, 15, 4}; // at each setting; every kind can show
    if (now - g_last_toast < std::chrono::seconds(kSecondsBetweenToasts[g_message_level])) {
        return;
    }
    g_last_toast = now;
    hud::toast(text, detail, 3, icon);
}

// Names the drivers most in form and most out of form, once at the start.
inline void board()
{
    if (g_board_done || g_drivers.empty()) {
        return;
    }
    g_board_done = true;
    if (!option<bool>("form_board") || !hud::available()) {
        return;
    }
    std::vector<const Driver *> order;
    for (const Driver &driver : g_drivers) {
        if (!driver.human) {
            order.push_back(&driver);
        }
    }
    std::sort(order.begin(), order.end(),
              [](const Driver *left, const Driver *right) { return left->form > right->form; });
    std::string in_form;
    std::string out_of_form;
    for (size_t i = 0; i < order.size() && i < 3; i++) {
        if (order[i]->form >= 0.3f) {
            if (!in_form.empty()) {
                in_form += "  ";
            }
            in_form += order[i]->label;
        }
    }
    for (size_t i = 0; i < order.size() && i < 3; i++) {
        const Driver *driver = order[order.size() - 1 - i];
        if (driver->form <= -0.3f) {
            if (!out_of_form.empty()) {
                out_of_form += "  ";
            }
            out_of_form += driver->label;
        }
    }
    if (!in_form.empty()) {
        log("[driver-form] board: in form %s -> toast %s", in_form.c_str(),
            hud::name(hud::toast("IN FORM", in_form, 5, "fast_lap")));
    }
    if (!out_of_form.empty()) {
        log("[driver-form] board: out of form %s -> toast %s", out_of_form.c_str(),
            hud::name(hud::toast("OUT OF FORM", out_of_form, 5, "generic")));
    }
}

} // namespace driver_form
