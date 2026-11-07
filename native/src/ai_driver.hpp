// SPDX-License-Identifier: MIT
// Access to the game's own per-driver AI values. Read from the F1_25.exe AI code (build d873a1c8):
//   0x140659cc0, once per AI update, for each driver i (profile P = profiles + i*0x1a0, state S = record(i)+0x30):
//     effective characteristic P+0xa8+n*4 = S+0x7c+n*8 when the byte S+0x80+n*8 is set, else the stock value
//       (n: 0 aggression, 1 control, 2 mistakes, 3 reaction_speed, 4 speed_in, 5 speed_out)
//     difficulty P+0x14c = S+0x00 when the byte S+0x04 is set, else the game's own value
//     parameters P+0xc0..0xe8 = bilinear map (difficulty x characteristic), 11 floats, recomputed every update
//   So writing S changes the driver the same way the game's own systems do. Nothing is patched.
// Route: the global at 0x14604fb20 holds the AI manager M: M+0x38 AI system (M+0x70)[vehicle] = S,
//   system+0xa8/+0xb0 records (stride 0x508), [system+0x180]+0x20/+0x28 profiles (stride 0x1a0).
//   The global's address is read from the mov at Ai_DriverVariationInRaceSystem_Setup+0xa42, so a game update that
//   moves it just turns the addon off.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <cstdint>
#include <cstring>

namespace ai_driver {

using swofty::log;
namespace mem = swofty::mem;

constexpr uintptr_t kSetupMovOff = 0xa42;
constexpr uintptr_t kRecStride = 0x508, kProfStride = 0x1a0;

struct Ai {
    uintptr_t mgr = 0, sys = 0, recs = 0, profiles = 0;
    size_t nrec = 0, nprof = 0;
};

inline uintptr_t manager_slot()
{
    static uintptr_t slot = 0;
    static bool tried = false;
    if (tried)
        return slot;
    tried = true;
    uintptr_t fn = swofty::api && swofty::api->resolve ? swofty::api->resolve("Ai_DriverVariationInRaceSystem_Setup") : 0;
    uint8_t b[7]{};
    if (!fn || !mem::read(fn + kSetupMovOff, b)) {
        log("[driver-form] setup function not found, addon off");
        return 0;
    }
    if (b[0] != 0x48 || b[1] != 0x8b || b[2] != 0x05) {
        log("[driver-form] bytes at setup+0xa42 are not the expected mov (%02x %02x %02x), addon off", b[0], b[1], b[2]);
        return 0;
    }
    int32_t disp = 0;
    std::memcpy(&disp, b + 3, 4);
    slot = fn + kSetupMovOff + 7 + intptr_t(disp);
    log("[driver-form] AI manager slot %llx", (unsigned long long)slot);
    return slot;
}

inline bool find(Ai &a)
{
    uintptr_t slot = manager_slot(), bptr = 0, pb = 0, pe = 0, rb = 0, re = 0;
    if (!slot || !mem::read(slot, a.mgr) || !a.mgr || !mem::read(a.mgr + 0x38, a.sys) || !a.sys ||
        !mem::read(a.sys + 0xa8, rb) || !mem::read(a.sys + 0xb0, re) || !rb || re <= rb || (re - rb) % kRecStride ||
        (re - rb) / kRecStride > 32 || !mem::read(a.sys + 0x180, bptr) || !bptr || !mem::read(bptr + 0x20, pb) ||
        !mem::read(bptr + 0x28, pe) || !pb || pe <= pb || (pe - pb) % kProfStride || (pe - pb) / kProfStride > 64)
        return false;
    a.recs = rb;
    a.nrec = size_t((re - rb) / kRecStride);
    a.profiles = pb;
    a.nprof = size_t((pe - pb) / kProfStride);
    return true;
}

// The record of a vehicle: the AI code indexes records by vehicle id, checked against the byte at +0.
inline uintptr_t record_of(const Ai &a, uint32_t car)
{
    for (size_t i = 0; i < a.nrec; i++) {
        uint8_t veh = 0xff;
        uintptr_t rec = a.recs + i * kRecStride;
        if (mem::read(rec, veh) && veh == car)
            return rec;
    }
    return 0;
}

// The per-car state S, as the per-update code reads it: [record+0x30].
inline uintptr_t state_of(const Ai &a, uint32_t car)
{
    uintptr_t rec = record_of(a, car), s = 0;
    if (!rec || !mem::read(rec + 0x30, s) || !s || !mem::readable(s, 0xb0))
        return 0;
    return s;
}

// The manager's table holds a second object per car. The game's own writer of the added lap time and the
// characteristic re-apply (0x1406597b0) use this one; [record+0x30] looks like the per-frame copy the AI jobs read.
// Writing only to [record+0x30] has no effect. Both are written.
inline uintptr_t master_of(const Ai &a, uint32_t car)
{
    uintptr_t table = 0, s = 0;
    if (!mem::read(a.mgr + 0x70, table) || !table || !mem::read(table + car * 8ull, s) || !s || !mem::readable(s, 0xb0))
        return 0;
    return s;
}

template <class T> inline void poke(uintptr_t addr, T v)
{
    *reinterpret_cast<volatile T *>(addr) = v;
}

inline void set_char(uintptr_t s, int n, float v)
{
    poke<float>(s + 0x7c + n * 8, v);
    poke<uint8_t>(s + 0x80 + n * 8, 1);
}

inline void clear_char(uintptr_t s, int n)
{
    poke<uint8_t>(s + 0x80 + n * 8, 0);
}

inline void set_difficulty(uintptr_t s, float v)
{
    poke<float>(s + 0x00, v);
    poke<uint8_t>(s + 0x04, 1);
}

inline void clear_difficulty(uintptr_t s)
{
    poke<uint8_t>(s + 0x04, 0);
}

// The driver's target lap time, seconds: base P+0x148, plus the added time. 0x14065d860 turns it into two outputs:
// the difficulty (P+0x14c) and a car value that ends up at [car+0x558]+0x330. Overriding the difficulty (S+0x00) sets
// that second output to 0, so it is not the same thing as changing the target time.
inline uintptr_t profile_of(const Ai &a, uint32_t car)
{
    uintptr_t rec = record_of(a, car);
    if (!rec)
        return 0;
    size_t i = size_t((rec - a.recs) / kRecStride);
    return i < a.nprof ? a.profiles + i * kProfStride : 0;
}

struct Stock {
    float ch[6]{};  // effective aggression, control, mistakes, reaction, speed_in, speed_out
    float par[11]{}; // alongside_awareness .. wet_weather_ability, as the game evaluated them
    float diff = 0, base = 0;
};

// The profile of a vehicle: profile index = record index in the per-update code.
inline bool read_stock(const Ai &a, uint32_t car, Stock &o)
{
    uintptr_t rec = record_of(a, car);
    if (!rec)
        return false;
    size_t i = size_t((rec - a.recs) / kRecStride);
    if (i >= a.nprof)
        return false;
    uintptr_t p = a.profiles + i * kProfStride;
    return mem::read(p + 0xa8, o.ch) && mem::read(p + 0xc0, o.par) && mem::read(p + 0x14c, o.diff) &&
           mem::read(p + 0x148, o.base);
}

} // namespace ai_driver
