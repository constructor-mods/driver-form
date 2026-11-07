// SPDX-License-Identifier: MIT
// Driver Form: every AI driver gets a weekend form. The form sets their pace through the game's own target lap time,
// lowers their driving quality when they are out of form, and brings bad patches and hot spells. A board names the
// drivers in and out of form. How the levers work and what was measured: AI_NOTES.md.
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

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <array>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <set>
#include <vector>

using namespace swofty;
using cars::live::Cars;
using cars::live::Layer;
using Clock = std::chrono::steady_clock;

namespace {

constexpr const char *kBlocking = "ai.blocking.max_blocking_distance"; // stock 18

// ---- session entrants (read only, guarded) ----
constexpr uintptr_t kGmModeContext = 0xc0, kModeSetup = 0x88, kSetupModes = 0x260, kSessEntBegin = 0x2e0,
                    kSessEntEnd = 0x2e8, kRecord = 0x88;

struct Entrant {
    uint16_t driver = 0; // matches entrants.drivers stat_id
    uint8_t number = 0;
    uint8_t car = 0xff; // the game's vehicle id, +0x82 of the record
    std::string label;  // the DB row, when the grid came from the event context
};

bool entrants_from_setup(uintptr_t setup, std::vector<Entrant> &out)
{
    uintptr_t begin = 0, end = 0;
    if (!setup || !mem::read(setup + kSessEntBegin, begin) || !mem::read(setup + kSessEntEnd, end) || end <= begin ||
        (end - begin) % kRecord != 0 || (end - begin) / kRecord > 64)
        return false;
    for (uintptr_t r = begin; r < end; r += kRecord) {
        Entrant e;
        mem::read(r + 0x52, e.driver);
        mem::read(r + 0x83, e.number);
        mem::read(r + 0x82, e.car);
        out.push_back(e);
    }
    return true;
}

uintptr_t race_state(const ai_driver::Ai &a)
{
    uintptr_t x = 0, rs = 0;
    return mem::read(a.sys + 0x10, x) && x && mem::read(x + 0xa8, rs) ? rs : 0;
}

// The name the game shows for a car, in capitals. Empty when it has none.
std::string game_name(const ai_driver::Ai &a, uint32_t car)
{
    using Fn = const char *(*)(void *, uint8_t);
    static Fn fn = api && api->resolve ? reinterpret_cast<Fn>(api->resolve("Sess_Get_CarDriverName")) : nullptr;
    uintptr_t rs = race_state(a);
    if (!fn || !rs)
        return {};
    std::string name;
    const char *p = fn(reinterpret_cast<void *>(rs), uint8_t(car));
    if (!p || !mem::c_string(p, name, 64) || name.empty())
        return {};
    for (char &c : name)
        if (static_cast<unsigned char>(c) < 0x80)
            c = char(std::toupper((unsigned char)c));
    return name;
}

// The game's session type: the AI code reads it at [[[system+0x10]+0x68]+0x1a8]+4. 0 practice, 1 qualifying, 2 race.
int session_kind(const ai_driver::Ai &a)
{
    uintptr_t x = 0, y = 0, z = 0;
    uint32_t type = 0;
    if (!mem::read(a.sys + 0x10, x) || !x || !mem::read(x + 0x68, y) || !y || !mem::read(y + 0x1a8, z) || !z ||
        !mem::read(z + 4, type))
        return -1;
    log("[driver-form] game session type %u", type);
    return type >= 1 && type <= 4 ? 0 : type >= 5 && type <= 14 ? 1 : type >= 15 && type <= 17 ? 2 : -1;
}

std::vector<Entrant> g_ctx_entrants; // from the event context; careers fill it, a stock Grand Prix does not

// The grid. A Grand Prix holds it where the launcher's own trace reads it; a career does not (the trace shows no
// entrants there), so the routes are tried in turn. The last one needs no entrant data at all: the cars the game's own
// form system is running on are the AI racers, without names.
bool read_entrants(const ai_driver::Ai &a, std::vector<Entrant> &out)
{
    static int said = -1;
    auto report = [](int route, size_t n) {
        if (route != said) {
            said = route;
            const char *const kRoutes[] = {"game manager", "AI system", "career context", "AI records", "race state"};
            log("[driver-form] grid from %s: %zu cars", kRoutes[route], n);
        }
    };
    out.clear();
    uintptr_t slot = api && api->resolve ? api->resolve("g_Game_Manager") : 0;
    uintptr_t gm = 0, ctx = 0, holder = 0, setup = 0;
    if (slot && mem::read(slot, gm) && gm && mem::read(gm + kGmModeContext, ctx) && ctx &&
        mem::read(ctx + kModeSetup, holder) && holder && mem::read(holder + kSetupModes, setup) &&
        entrants_from_setup(setup, out)) {
        report(0, out.size());
        return true;
    }
    out.clear();
    uintptr_t c2 = 0, h2 = 0, s2 = 0;
    if (mem::read(a.sys + 0x60, c2) && c2 && mem::read(c2 + kModeSetup, h2) && h2 && mem::read(h2 + kSetupModes, s2) &&
        entrants_from_setup(s2, out)) {
        report(1, out.size());
        return true;
    }
    out.clear();
    // Sess_Get_CarDriverName's own route: AI system +0x10, +0xa8 race state, +0x190 context.
    uintptr_t rs = race_state(a), c3 = 0, h3 = 0, s3 = 0;
    if (rs && mem::read(rs + 0x190, c3) && c3 && mem::read(c3 + kModeSetup, h3) && h3 && mem::read(h3 + kSetupModes, s3) &&
        entrants_from_setup(s3, out)) {
        report(4, out.size());
        return true;
    }
    out.clear();
    if (!g_ctx_entrants.empty()) {
        out = g_ctx_entrants;
        report(2, out.size());
        return true;
    }
    // Every record but the last, which is the safety car. The game's form flag can't pick the AI cars: it is off in a
    // career race. The player's car is dropped by the first tick.
    for (uint32_t car = 0; car + 1 < a.nrec; car++) {
        if (!ai_driver::state_of(a, car))
            continue;
        Entrant e;
        e.car = uint8_t(car);
        e.driver = uint16_t(0xf000 + car);
        e.number = uint8_t(car);
        out.push_back(e);
    }
    if (out.empty())
        return false;
    report(3, out.size());
    return true;
}

// stat_id -> initials from the game DB table entrants.drivers
std::map<int64_t, std::string> driver_initials()
{
    std::map<int64_t, std::string> out;
    if (!db::available())
        return out;
    db::Cursor c{};
    if (db::open("entrants.drivers", &c) != db::Result::ok)
        return out;
    for (int guard = 0; guard < 4000; guard++) {
        db::Value id{}, ini{};
        if (db::cell(&c, "stat_id", &id) == db::Result::ok && db::cell(&c, "initials", &ini) == db::Result::ok && ini.s &&
            *ini.s)
            out[id.i] = ini.s;
        if (db::step(&c) != db::Result::ok)
            break;
    }
    return out;
}

// ---- strength ----
struct Strength {
    float seconds;  // pace swing at form +-1, seconds a lap
    float depth;    // how far driving quality falls at form -1
    float patch;    // size of bad patches and hot spells
    float blocking; // defending distance = stock x 2^(form x this)
};

bool strength_of(const std::string &s, Strength &o)
{
    if (s == "subtle")
        o = {0.8f, 0.3f, 0.7f, 0.5f};
    else if (s == "normal")
        o = {1.5f, 0.45f, 1.0f, 1.0f};
    else if (s == "wild")
        o = {2.2f, 0.6f, 1.5f, 1.6f};
    else if (s == "extreme")
        o = {3.5f, 0.7f, 2.5f, 2.5f};
    else
        return false;
    return true;
}

// ---- the form ----
constexpr const char *kSaveFile = "weekend.txt";
constexpr long long kWeekendLife = 24 * 3600; // a saved weekend older than this is a new weekend

enum class Patch { none, bad, hot, moment };

struct Driver {
    uint32_t car = 0;
    uint16_t stat = 0;
    std::string label;
    uintptr_t state = 0, master = 0, profile = 0; // found once in build(), the tick must stay cheap
    float form = 0; // -1 bad .. +1 good
    ai_driver::Stock stock;
    std::vector<Layer> layers;
    Patch patch = Patch::none;
    Clock::time_point patch_until, next_event;
    bool human = true; // the first tick decides
    bool seen = false;
    int crossings = 0, last_pos = 0, pit_at = -1;
};

std::vector<Driver> g_drivers;
uintptr_t g_records = 0; // the record table the cached pointers belong to
Clock::time_point g_last_check;
size_t g_control_next = 0;
std::map<uint16_t, float> g_week; // this weekend's rolls, by driver
float g_week_key = -1;            // the track's base target time tells one weekend from the next
std::mt19937 g_rng{std::random_device{}()};
Strength g_strength{};
bool g_patches = true, g_armed = false, g_board_done = false;
Clock::time_point g_last, g_last_toast, g_race_started;
bool g_race_running = false;
std::mutex g_m;
std::set<uint32_t> g_player;              // cars ever seen under human control, never touched again
long long g_week_time = 0;                // when this weekend's forms were rolled
std::array<int, 128> g_at_crossing{};     // how many cars have reached each line crossing
int g_session_kind = 2;                   // 0 practice, 1 qualifying, 2 race; each has its own Messages setting
int g_messages = 0;                       // 0 none, 1 low, 2 medium, 3 high, 4 ultra

// The race HUD's notification. The setting only sets how often one may show.
void say(const char *kind, const std::string &text, const std::string &detail, const char *icon = "generic")
{
    log("[driver-form] msg %s %s", text.c_str(), detail.c_str());
    if (g_messages == 0 || !option<bool>(kind) || !hud::available())
        return;
    auto now = Clock::now();
    constexpr int kGap[] = {0, 60, 30, 15, 4}; // seconds between toasts at each setting; every kind can show
    if (now - g_last_toast < std::chrono::seconds(kGap[g_messages]))
        return;
    g_last_toast = now;
    hud::toast(text, detail, 3, icon);
}

float uniform(float lo, float hi)
{
    return std::uniform_real_distribution<float>(lo, hi)(g_rng);
}

// A third of the grid clearly in form, a third clearly out, the rest neutral.
void roll_grid(std::vector<Driver> &ds)
{
    std::vector<size_t> ids(ds.size());
    for (size_t i = 0; i < ids.size(); i++)
        ids[i] = i;
    std::shuffle(ids.begin(), ids.end(), g_rng);
    size_t third = ds.size() / 3;
    for (size_t i = 0; i < ids.size(); i++) {
        float f = i < third ? uniform(0.6f, 1.0f) : i < third * 2 ? uniform(-1.0f, -0.6f) : uniform(-0.15f, 0.15f);
        g_week[ds[ids[i]].stat] = f;
    }
}

// Out-of-form drivers lose time through their driving as well, so the dial is smaller on that side.
float shift_of(const Driver &d)
{
    float s = d.form < 0 ? -d.form * g_strength.seconds * 0.6f : -d.form * g_strength.seconds;
    if (d.patch == Patch::bad)
        s += 1.0f * g_strength.patch;
    if (d.patch == Patch::hot)
        s -= 0.4f * g_strength.patch;
    return s;
}

float quality_of(const Driver &d)
{
    float q = d.form < 0 ? 1.0f + d.form * g_strength.depth : 1.0f;
    return d.patch == Patch::bad ? q * 0.6f : q;
}

void clear_driver(const Driver &d)
{
    for (uintptr_t s : {d.state, d.master})
        if (s) {
            for (int n = 0; n < 6; n++)
                ai_driver::clear_char(s, n);
            ai_driver::clear_difficulty(s);
        }
    if (d.profile)
        ai_driver::poke<float>(d.profile + 0x148, d.stock.base);
}

void write_driver(const Driver &d)
{
    constexpr int kChars[] = {0, 1, 4, 5}; // aggression, control, speed_in, speed_out
    float q = quality_of(d);
    for (uintptr_t s : {d.state, d.master}) {
        if (!s)
            continue;
        // A moment: for a few seconds the driver drives far below their level.
        if (d.patch == Patch::moment)
            ai_driver::set_difficulty(s, d.stock.diff * 0.35f);
        else
            ai_driver::clear_difficulty(s);
        for (int n : kChars) {
            if (q < 0.999f)
                ai_driver::set_char(s, n, d.stock.ch[n] * q);
            else
                ai_driver::clear_char(s, n);
        }
    }
    if (d.profile)
        ai_driver::poke<float>(d.profile + 0x148, d.stock.base + shift_of(d));
}

void stop()
{
    ai_driver::Ai a;
    if (!g_drivers.empty() && ai_driver::find(a) && a.recs == g_records)
        for (const Driver &d : g_drivers)
            clear_driver(d);
    g_drivers.clear(); // destroying a Layer puts the game's value back
    g_armed = false;
    g_board_done = false;
}

void arm()
{
    stop();
    g_last = Clock::now(); // leaves the game time to take the cleared values before they are read as stock
    if (!strength_of(option<std::string>("form_strength"), g_strength)) {
        log("[driver-form] form strength off, normal AI");
        return;
    }
    g_patches = option<bool>("bad_patches");
    g_at_crossing.fill(0);
    g_race_running = false;
    g_armed = true;
}

// The rolls survive closing the game, so a career weekend keeps its forms between sessions.
void load_week()
{
    static bool loaded = false;
    if (loaded)
        return;
    loaded = true;
    std::string text;
    if (!storage::available() || storage::read(kSaveFile, text) != storage::Result::ok)
        return;
    std::istringstream in(text);
    float key = 0;
    long long when = 0;
    if (!(in >> key >> when))
        return;
    int stat = 0;
    float form = 0;
    while (in >> stat >> form)
        g_week[uint16_t(stat)] = form;
    g_week_key = key;
    g_week_time = when;
    log("[driver-form] saved weekend loaded: %zu drivers, base lap %.3f", g_week.size(), double(key));
}

void save_week()
{
    if (!storage::available())
        return;
    std::ostringstream out;
    out << g_week_key << ' ' << g_week_time << '\n';
    for (const auto &[stat, form] : g_week)
        out << stat << ' ' << form << '\n';
    log("[driver-form] weekend saved: %s", storage::name(storage::write(kSaveFile, out.str())));
}

// Reads the grid and gives every AI driver their form. Returns false until the session is ready.
bool build()
{
    ai_driver::Ai a;
    std::vector<Entrant> ents;
    if (!ai_driver::find(a) || !read_entrants(a, ents))
        return false;
    auto initials = driver_initials();
    for (const Entrant &e : ents) {
        Driver d;
        d.car = e.car;
        d.stat = e.driver;
        if (!ai_driver::read_stock(a, e.car, d.stock) || d.stock.ch[4] <= 0 || d.stock.base <= 0) {
            log("[driver-form] car %u not ready: speed_in %.3f base %.3f", unsigned(e.car), double(d.stock.ch[4]),
                double(d.stock.base));
            return false;
        }
        d.state = ai_driver::state_of(a, e.car);
        d.master = ai_driver::master_of(a, e.car);
        d.profile = ai_driver::profile_of(a, e.car);
        auto it = initials.find(e.driver);
        d.label = it != initials.end() ? it->second : "#" + std::to_string(e.number);
        if (!e.label.empty()) {
            db::Cursor c{};
            db::Value ini{}, id{};
            if (db::row("entrants.drivers", e.label, &c) == db::Result::ok) {
                if (db::cell(&c, "initials", &ini) == db::Result::ok && ini.s && *ini.s)
                    d.label = ini.s;
                if (db::cell(&c, "stat_id", &id) == db::Result::ok)
                    d.stat = uint16_t(id.i);
            }
        }
        std::string shown = game_name(a, e.car);
        if (!shown.empty())
            d.label = shown;
        g_drivers.push_back(std::move(d));
    }
    if (g_drivers.empty())
        return false;
    g_records = a.recs;
    load_week();
    float key = g_drivers[0].stock.base;
    long long now = (long long)std::time(nullptr);
    bool known = true;
    for (const Driver &d : g_drivers)
        known = known && g_week.count(d.stat) != 0;
    if (std::fabs(key - g_week_key) > 0.01f || now - g_week_time > kWeekendLife || !known) {
        g_week.clear();
        g_week_key = key;
        g_week_time = now;
        g_player.clear();
        roll_grid(g_drivers);
        log("[driver-form] new weekend, base lap %.3f", double(key));
        save_week();
    }
    for (Driver &d : g_drivers) {
        auto it = g_week.find(d.stat);
        d.form = it != g_week.end() ? it->second : 0.0f;
    }
    int kind = session_kind(a);
    if (kind >= 0)
        g_session_kind = kind;
    std::string m = option<std::string>(g_session_kind == 2   ? "messages_race"
                                        : g_session_kind == 1 ? "messages_qualifying"
                                                              : "messages_practice");
    g_messages = m == "low" ? 1 : m == "medium" ? 2 : m == "high" ? 3 : m == "ultra" ? 4 : 0;
    log("[driver-form] %zu drivers, %.1f s a lap at full form, session kind %d, messages %s", g_drivers.size(),
        double(g_strength.seconds), g_session_kind, m.c_str());
    return true;
}

void start_layers(Driver &d)
{
    Layer l = Layer::scale(Cars::car(cars::live::Car(d.car)), kBlocking, std::exp2(d.form * g_strength.blocking));
    if (l.ok())
        d.layers.push_back(std::move(l));
}

void tick_patch(Driver &d, Clock::time_point now)
{
    if (d.patch != Patch::none) {
        if (now >= d.patch_until) {
            if (d.patch != Patch::moment)
                log("[driver-form] %s patch over", d.label.c_str());
            d.patch = Patch::none;
            d.next_event = now + std::chrono::seconds(45);
        }
        return;
    }
    // Nothing in the first minute of a race, when a sudden slow car would wreck the field.
    if (!g_patches || !g_race_running || now - g_race_started < std::chrono::seconds(60) || now < d.next_event)
        return;
    // Chances per minute; a tick is half a second.
    float cold = d.form < -0.3f ? 1.0f : 0.0f, warm = d.form > 0.3f ? 1.0f : 0.0f, mid = 1.0f - cold - warm;
    float moment = cold * 0.5f + mid * 0.06f + warm * 0.02f;
    float bad = cold * 0.25f + mid * 0.03f + warm * 0.01f;
    float hot = warm * 0.20f + mid * 0.03f;
    float r = uniform(0, 1) * 120.0f;
    float secs = 0;
    if (r < moment) {
        d.patch = Patch::moment;
        secs = uniform(3, 8);
        say("toast_mistakes", d.label + " MISTAKE", "Big time loss", "black");
    } else if (r < moment + bad) {
        d.patch = Patch::bad;
        secs = uniform(20, 60);
        say("toast_struggling", d.label + " STRUGGLING", "Losing time", "generic");
    } else if (r < moment + bad + hot) {
        d.patch = Patch::hot;
        secs = uniform(20, 60);
        say("toast_charge", d.label + " ON A CHARGE", "Finding time", "fast_lap");
    } else {
        return;
    }
    d.patch_until = now + std::chrono::milliseconds(int(secs * 1000));
    log("[driver-form] %s %s for %.0f s (form %+.2f)", d.label.c_str(),
        d.patch == Patch::moment ? "moment" : d.patch == Patch::bad ? "bad patch" : "hot patch", double(secs), double(d.form));
}

void frame(double, void *)
{
    std::lock_guard<std::mutex> lock(g_m);
    if (!g_armed)
        return;
    auto now = Clock::now();
    if (now - g_last < std::chrono::milliseconds(500))
        return;
    g_last = now;
    if (g_drivers.empty()) {
        if (!build())
            g_drivers.clear();
        return;
    }
    // Guarded reads cost about 0.5 ms each here, so the tick does as few as it can: the AI system is re-checked every
    // 10 s, and control is read for one car per tick (it takes that car about 0.25 ms).
    if (now - g_last_check >= std::chrono::seconds(10)) {
        g_last_check = now;
        ai_driver::Ai a;
        if (!ai_driver::find(a))
            return;
        if (a.recs != g_records) { // the game rebuilt the AI: the cached pointers are gone
            g_drivers.clear();
            return;
        }
    }
    g_control_next = (g_control_next + 1) % g_drivers.size();
    for (size_t i = 0; i < g_drivers.size(); i++) {
        Driver &d = g_drivers[i];
        bool first = d.human && !g_player.count(d.car) && !d.seen;
        if (i == g_control_next || first) {
            d.seen = true;
            auto ctl = cars::live::Car(d.car).control();
            if (ctl && ctl->mode == sessions::Control::human)
                g_player.insert(d.car);
        }
        bool human = g_player.count(d.car) != 0; // once yours, always yours: the game hands your car to the AI in the pits
        if (human != d.human) {
            d.human = human;
            if (human) {
                d.layers.clear();
                clear_driver(d);
                log("[driver-form] %s is yours, left alone", d.label.c_str());
            } else {
                start_layers(d);
                log("[driver-form] car %u %s: form %+.2f, %+.2f s, quality %.2f", d.car, d.label.c_str(), double(d.form),
                    double(shift_of(d)), double(quality_of(d)));
            }
        }
        if (!human) {
            tick_patch(d, now);
            write_driver(d);
        }
    }
}

void board()
{
    if (g_board_done || g_drivers.empty())
        return;
    g_board_done = true;
    if (!option<bool>("form_board") || !hud::available())
        return;
    std::vector<const Driver *> order;
    for (const Driver &d : g_drivers)
        if (!d.human)
            order.push_back(&d);
    std::sort(order.begin(), order.end(), [](const Driver *a, const Driver *b) { return a->form > b->form; });
    std::string hot, cold;
    for (size_t i = 0; i < order.size() && i < 3; i++)
        if (order[i]->form >= 0.3f)
            hot += (hot.empty() ? "" : "  ") + order[i]->label;
    for (size_t i = 0; i < order.size() && i < 3; i++) {
        const Driver *d = order[order.size() - 1 - i];
        if (d->form <= -0.3f)
            cold += (cold.empty() ? "" : "  ") + d->label;
    }
    if (!hot.empty())
        log("[driver-form] board: in form %s -> toast %s", hot.c_str(), hud::name(hud::toast("IN FORM", hot, 5, "fast_lap")));
    if (!cold.empty())
        log("[driver-form] board: out of form %s -> toast %s", cold.c_str(),
            hud::name(hud::toast("OUT OF FORM", cold, 5, "generic")));
}

// Line crossings give the running order: who gained or lost places over the last lap.
void on_lap(uint32_t car)
{
    board();
    if (!g_race_running) {
        g_race_running = true; // a benchmark race fires no race.start
        g_race_started = Clock::now();
    }
    for (Driver &d : g_drivers) {
        if (d.car != car)
            continue;
        d.crossings++;
        int n = std::min(d.crossings, int(g_at_crossing.size()) - 1);
        int pos = ++g_at_crossing[n];
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
        log("[driver-form] lap car %u %s form %+.2f P%d t=%lldms", d.car, d.label.c_str(), double(d.form), pos, (long long)ms);
        if (d.last_pos && !d.human && d.pit_at != n && d.pit_at != n - 1) {
            int gain = d.last_pos - pos;
            if (gain >= 3)
                say("toast_places", d.label + " UP " + std::to_string(gain) + " PLACES", "P" + std::to_string(pos), "checkpoint");
            else if (gain <= -3)
                say("toast_places", d.label + " DOWN " + std::to_string(-gain) + " PLACES", "P" + std::to_string(pos), "generic");
        }
        d.last_pos = pos;
    }
}

void on_pit(uint32_t car)
{
    for (Driver &d : g_drivers)
        if (d.car == car) {
            if (d.pit_at != d.crossings && !d.human)
                say("toast_pits", d.label + " IN THE PITS", d.form > 0.3f ? "In form" : d.form < -0.3f ? "Out of form" : "", "checkpoint");
            d.pit_at = d.crossings;
        }
}

} // namespace

SWOFTY_ON_LOAD
{
    swofty::events::on_frame(frame);
}

SWOFTY_REGISTER
{
    events::on("session.start").after([](std::span<const std::byte>, const events::Context &ctx) {
        std::lock_guard<std::mutex> lock(g_m);
        g_ctx_entrants.clear();
        for (const auto &p : ctx.participants()) {
            Entrant e;
            e.car = uint8_t(p.car);
            std::string id(p.driver.id());
            size_t cut = id.rfind('/');
            e.label = cut == std::string::npos ? id : id.substr(cut + 1);
            g_ctx_entrants.push_back(e);
        }
        const auto &view = ctx.session();
        g_session_kind = !view ? 2 : view->type == events::SessionType::race || view->type == events::SessionType::sprint ? 2 : view->type == events::SessionType::qualifying ? 1 : 0;
        log("[driver-form] session.start: %zu entrants in the event context, session '%s', kind %d", g_ctx_entrants.size(),
            view ? view->session.c_str() : "?", g_session_kind);
        arm();
    });
    // A restart fires race.start again with no session.end before it.
    events::on("race.start").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_m);
        g_session_kind = 2;
        arm();
        g_race_running = true;
        g_race_started = Clock::now();
    });
    events::on<sessions::events::Lap>().after([](const sessions::events::Lap &e, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_m);
        on_lap(e.car.index());
    });
    events::on<sessions::events::PitStop>().after([](const sessions::events::PitStop &e, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_m);
        on_pit(e.car);
    });
    events::on("session.end").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_m);
        log("[driver-form] session.end: releasing %zu driver(s)", g_drivers.size());
        stop();
    });
}

SWOFTY_ADDON()
