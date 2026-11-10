// SPDX-License-Identifier: MIT
// Reads and writes the game's own values for each driver (how they drive and how fast they are).
// Nothing is patched. If a game update moves what we look for, the addon turns itself off.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <cstdint>
#include <cstring>

namespace ai_driver {

using swofty::log;
namespace mem = swofty::mem;

constexpr uintptr_t kSetupMovOffset = 0xa42;
constexpr uintptr_t kRecordStride = 0x508;
constexpr uintptr_t kProfileStride = 0x1a0;

// Where the game keeps its drivers.
struct GameDrivers {
    uintptr_t manager = 0;
    uintptr_t system = 0;
    uintptr_t records = 0;
    uintptr_t profiles = 0;
    size_t record_count = 0;
    size_t profile_count = 0;
    uint8_t vehicles[32]{}; // the car each record belongs to, read once by find()
};

inline uintptr_t manager_slot()
{
    static uintptr_t slot = 0;
    static bool tried = false;
    if (tried) {
        return slot;
    }
    tried = true;
    uintptr_t setup_function = 0;
    if (swofty::api && swofty::api->resolve) {
        setup_function = swofty::api->resolve("Ai_DriverVariationInRaceSystem_Setup");
    }
    uint8_t bytes[7]{};
    if (!setup_function || !mem::read(setup_function + kSetupMovOffset, bytes)) {
        log("[driver-form] setup function not found, addon off");
        return 0;
    }
    if (bytes[0] != 0x48 || bytes[1] != 0x8b || bytes[2] != 0x05) {
        log("[driver-form] bytes at setup+0xa42 are not the expected mov (%02x %02x %02x), addon off", bytes[0],
            bytes[1], bytes[2]);
        return 0;
    }
    int32_t displacement = 0;
    std::memcpy(&displacement, bytes + 3, 4);
    slot = setup_function + kSetupMovOffset + 7 + intptr_t(displacement);
    log("[driver-form] AI manager slot %llx", (unsigned long long)slot);
    return slot;
}

inline bool find(GameDrivers &game)
{
    uintptr_t slot = manager_slot();
    if (!slot) {
        return false;
    }
    if (!mem::read(slot, game.manager) || !game.manager) {
        return false;
    }
    if (!mem::read(game.manager + 0x38, game.system) || !game.system) {
        return false;
    }
    uintptr_t records_begin = 0;
    uintptr_t records_end = 0;
    if (!mem::read(game.system + 0xa8, records_begin) || !mem::read(game.system + 0xb0, records_end)) {
        return false;
    }
    if (!records_begin || records_end <= records_begin || (records_end - records_begin) % kRecordStride) {
        return false;
    }
    if ((records_end - records_begin) / kRecordStride > 32) {
        return false;
    }
    uintptr_t profile_block = 0;
    uintptr_t profiles_begin = 0;
    uintptr_t profiles_end = 0;
    if (!mem::read(game.system + 0x180, profile_block) || !profile_block) {
        return false;
    }
    if (!mem::read(profile_block + 0x20, profiles_begin) || !mem::read(profile_block + 0x28, profiles_end)) {
        return false;
    }
    if (!profiles_begin || profiles_end <= profiles_begin || (profiles_end - profiles_begin) % kProfileStride) {
        return false;
    }
    if ((profiles_end - profiles_begin) / kProfileStride > 64) {
        return false;
    }
    game.records = records_begin;
    game.record_count = size_t((records_end - records_begin) / kRecordStride);
    game.profiles = profiles_begin;
    game.profile_count = size_t((profiles_end - profiles_begin) / kProfileStride);
    // One check for the whole block, then every record's car is read straight away.
    if (!mem::readable(game.records, game.record_count * kRecordStride)) {
        return false;
    }
    for (size_t index = 0; index < game.record_count; index++) {
        game.vehicles[index] = *reinterpret_cast<const uint8_t *>(game.records + index * kRecordStride);
    }
    return true;
}

// Where the game's records start. Three reads instead of find(): enough to see that the game rebuilt its drivers.
inline uintptr_t records_start()
{
    uintptr_t slot = manager_slot();
    uintptr_t manager = 0;
    uintptr_t system = 0;
    uintptr_t records_begin = 0;
    if (!slot || !mem::read(slot, manager) || !manager || !mem::read(manager + 0x38, system) || !system ||
        !mem::read(system + 0xa8, records_begin)) {
        return 0;
    }
    return records_begin;
}

// The record of a car.
inline uintptr_t record_of(const GameDrivers &game, uint32_t car)
{
    for (size_t index = 0; index < game.record_count; index++) {
        if (game.vehicles[index] == car) {
            return game.records + index * kRecordStride;
        }
    }
    return 0;
}

// The per-frame copy of a car's values.
inline uintptr_t state_of(const GameDrivers &game, uint32_t car)
{
    uintptr_t record = record_of(game, car);
    uintptr_t state = 0;
    if (!record || !mem::read(record + 0x30, state) || !state || !mem::readable(state, 0xb0)) {
        return 0;
    }
    return state;
}

// The main copy of a car's values. Writing only the per-frame copy has no effect, so both are written.
inline uintptr_t master_of(const GameDrivers &game, uint32_t car)
{
    uintptr_t table = 0;
    uintptr_t state = 0;
    if (!mem::read(game.manager + 0x70, table) || !table || !mem::read(table + car * 8ull, state) || !state ||
        !mem::readable(state, 0xb0)) {
        return 0;
    }
    return state;
}

template <class Value> inline void poke(uintptr_t address, Value value)
{
    *reinterpret_cast<volatile Value *>(address) = value;
}

inline void set_characteristic(uintptr_t state, int characteristic, float value)
{
    poke<float>(state + 0x7c + characteristic * 8, value);
    poke<uint8_t>(state + 0x80 + characteristic * 8, 1);
}

inline void clear_characteristic(uintptr_t state, int characteristic)
{
    poke<uint8_t>(state + 0x80 + characteristic * 8, 0);
}

inline void set_difficulty(uintptr_t state, float value)
{
    poke<float>(state + 0x00, value);
    poke<uint8_t>(state + 0x04, 1);
}

inline void clear_difficulty(uintptr_t state)
{
    poke<uint8_t>(state + 0x04, 0);
}

// The driver's profile, where the target lap time lives.
inline uintptr_t profile_of(const GameDrivers &game, uint32_t car)
{
    uintptr_t record = record_of(game, car);
    if (!record) {
        return 0;
    }
    size_t index = size_t((record - game.records) / kRecordStride);
    if (index >= game.profile_count) {
        return 0;
    }
    return game.profiles + index * kProfileStride;
}

struct Stock {
    float characteristics[6]{}; // aggression, control, mistakes, reaction, speed in, speed out
    float parameters[11]{};     // the game's eleven derived driving values
    float difficulty = 0;
    float base_lap_time = 0;
};

// What the game had set for a car before we changed anything.
inline bool read_stock(const GameDrivers &game, uint32_t car, Stock &stock)
{
    uintptr_t record = record_of(game, car);
    if (!record) {
        return false;
    }
    size_t index = size_t((record - game.records) / kRecordStride);
    if (index >= game.profile_count) {
        return false;
    }
    uintptr_t profile = game.profiles + index * kProfileStride;
    return mem::read(profile + 0xa8, stock.characteristics) && mem::read(profile + 0xc0, stock.parameters) &&
           mem::read(profile + 0x14c, stock.difficulty) && mem::read(profile + 0x148, stock.base_lap_time);
}

} // namespace ai_driver
