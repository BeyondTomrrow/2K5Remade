/* Native player mapping verified against the original constructors/callers:
 * 61730 copies match rosters; 75B30 initialises E60268's primary nodes;
 * 17B010/918B0 consume node+3C as the compact match roster record.
 * 75D90 samples matrix 12 of node+4 as a world-space player centre.
 * E60274 uses a different 0x38-byte node layout and is deliberately excluded.
 * Ratings edits are restricted to the match's two copied roster arrays.
 * No source roster, profile, appearance or save pointer is changed. */
#include "nfl2k5_sandbox_actors.h"
#include "xbox_memory_layout.h"
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct MatchRoster {
    uint32_t record;
    int side, index;
} MatchRoster;
static uint32_t property_record(uint32_t clock, uint32_t id);

static int read_guest(uint32_t at, void *out, size_t size)
{
    uint64_t end = (uint64_t)at + size;
    if (!out || !size || at < 0x10000u || end > 0x08000000u) return 0;
    __try {
        memcpy(out, (const void *)((uintptr_t)xbox_GetMemoryOffset()+at), size);
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static uint32_t u32(uint32_t at)
{
    uint32_t v = 0; read_guest(at, &v, sizeof v); return v;
}
static int current(uint32_t clock)
{
    uint32_t period = u32(0xE602C4u);
    return clock && u32(0xA83A18u) == 3u && u32(0xE6028Cu) == clock &&
           period >= 1u && period <= 8u;
}
static void name_part(uint32_t at, char *out, int capacity)
{
    WCHAR wide[64];
    if (!out || capacity < 1) return;
    out[0] = 0;
    memset(wide, 0, sizeof wide);
    /* The final code unit is kept zero, including for unterminated data. */
    if (!read_guest(at, wide, sizeof wide - sizeof wide[0])) return;
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, capacity, NULL, NULL))
        out[0] = 0;
}
static int rosters(MatchRoster *out)
{
    int n = 0;
    for (int side = 0; side < 2; ++side) {
        uint32_t team = side ? 0xB30A58u : 0xB30864u;
        unsigned char count = 0;
        if (!read_guest(team + 0x11Cu, &count, 1) || count > 71u) return 0;
        for (unsigned i = 0; i < count; ++i) {
            uint32_t record = u32(team + 4u * i);
            if (!record) continue;
            out[n].record = record; out[n].side = side; out[n].index = (int)i;
            ++n;
        }
    }
    return n;
}
static int actor_snapshot(uint32_t node, const uint32_t fields[20],
                           const MatchRoster *owner, Nfl2k5SandboxActor *actor)
{
    uint32_t record = owner->record, descriptor;
    unsigned char side_tag = 0, position = 0xFFu;
    float centre[3];
    char first[100], last[100], full_name[201];
    size_t length;
    if (fields[0x3C / 4] != record || fields[0x48 / 4] != 0 ||
        !read_guest(record + 0x34u, &side_tag, 1) || side_tag != owner->side + 1u ||
        fields[0x38 / 4] != (owner->side ? 0xE5FC60u : 0xE5FC20u) ||
        !(descriptor = u32(record + 0x30u)) ||
        descriptor > 0x07FFFFECu || u32(descriptor + 0x10u) != node ||
        fields[1] < 0x10000u || fields[1] > 0x07FFFCC4u ||
        !read_guest(fields[1] + 0x330u, centre, sizeof centre) ||
        !isfinite(centre[0]) || !isfinite(centre[1]) || !isfinite(centre[2]) ||
        fabsf(centre[0]) > 100000 || fabsf(centre[2]) > 100000 ||
        centre[1] < -10000 || centre[1] > 50000) return 0;
    memset(actor, 0, sizeof *actor);
    actor->record = record; actor->node = node;
    actor->geometry = fields[0x18 / 4]; actor->payload = fields[1];
    actor->side = owner->side; actor->index = owner->index;
    actor->jersey = (int)((u32(record + 0x20u) >> 3) & 0x7Fu);
    read_guest(record + 0x35u, &position, 1); actor->position = position;
    actor->x = centre[0]; actor->y = centre[1]; actor->z = centre[2];
    name_part(u32(record + 0x10u), first, sizeof first);
    name_part(u32(record + 0x14u), last, sizeof last);
    snprintf(full_name, sizeof full_name, "%s%s%s", first,
             first[0] && last[0] ? " " : "", last);
    length = strlen(full_name);
    if (length >= sizeof actor->name) length = sizeof actor->name - 1;
    /* Do not leave a split UTF-8 character at the browser's string limit. */
    while (length && ((unsigned char)full_name[length] & 0xC0u) == 0x80u) --length;
    memcpy(actor->name, full_name, length); actor->name[length] = 0;
    if (!actor->name[0])
        snprintf(actor->name, sizeof actor->name, "Player #%d", actor->jersey);
    return 1;
}
int nfl2k5_sandbox_actors_list(uint32_t expected_clock,
                             Nfl2k5SandboxActor *out, int capacity)
{
    MatchRoster roster[142];
    uint32_t seen[NFL2K5_SANDBOX_MAX_ACTORS];
    int count = 0, visited = 0, roster_count;
    uint32_t node;
    if (!out || capacity <= 0 || !current(expected_clock)) return 0;
    if (capacity > NFL2K5_SANDBOX_MAX_ACTORS) capacity = NFL2K5_SANDBOX_MAX_ACTORS;
    roster_count = rosters(roster);
    if (!roster_count) return 0;
    node = u32(0xE60268u);
    while (node && visited < NFL2K5_SANDBOX_MAX_ACTORS && count < capacity) {
        uint32_t fields[20], record;
        int duplicate = 0, owner = -1;
        for (int i = 0; i < visited; ++i) if (seen[i] == node) duplicate = 1;
        if (duplicate || !read_guest(node, fields, sizeof fields)) break;
        seen[visited++] = node;
        record = fields[0x3C / 4];
        /* Native loops exclude node+48 != 0. The exact roster identity and
         * the side ownership prevent a referee/prop/replay slot being
         * mistaken for a current on-field roster player. */
        if (fields[0x48 / 4] == 0) {
            for (int i = 0; i < roster_count; ++i)
                if (roster[i].record == record) { owner = i; break; }
        }
        if (owner >= 0 && actor_snapshot(node, fields, &roster[owner], &out[count]))
            ++count;
        node = fields[0x30 / 4];
    }
    /* Never publish a snapshot spanning a native scene teardown. */
    if (!current(expected_clock)) return 0;
    return count;
}
static int record_id(uint32_t record)
{
    static const uint32_t bases[2] = { 0xB30C4Cu, 0xB321A0u };
    for (int side = 0; side < 2; ++side) {
        uint32_t base = bases[side];
        if (record >= base && record < base + 65u * 0x54u &&
            (record - base) % 0x54u == 0)
            return side * NFL2K5_SANDBOX_PLAYER_ID_STRIDE + (int)((record - base) / 0x54u);
    }
    return -1;
}
static int primary_node(uint32_t node)
{
    /* 75B30 clears 1B8 dwords = 22 nodes * 50 bytes at B3EF60. */
    return node >= 0xB3EF60u && node < 0xB3F640u &&
           (node - 0xB3EF60u) % 0x50u == 0;
}
int nfl2k5_sandbox_actor_get(uint32_t expected_clock, uint32_t roster_record,
                            Nfl2k5SandboxActor *out)
{
    Nfl2k5SandboxActor value;
    MatchRoster owner;
    uint32_t descriptor, node, fields[20];
    int id = record_id(roster_record);
    if (!out || id < 0 || property_record(expected_clock, (uint32_t)id) != roster_record)
        return 0;
    descriptor = u32(roster_record + 0x30u);
    if (!descriptor || descriptor > 0x07FFFFECu) return 0;
    node = u32(descriptor + 0x10u);
    if (!primary_node(node) || !read_guest(node, fields, sizeof fields)) return 0;
    owner.record = roster_record;
    owner.side = id / NFL2K5_SANDBOX_PLAYER_ID_STRIDE;
    owner.index = id % NFL2K5_SANDBOX_PLAYER_ID_STRIDE;
    if (!actor_snapshot(node, fields, &owner, &value) ||
        property_record(expected_clock, (uint32_t)id) != roster_record) return 0;
    *out = value;
    return 1;
}

/* These arrays each contain 65 compact records (B321A0-B30C4C == 65*54),
 * although the team header reserves 71 pointer slots before its count byte.
 * A corrupt count or pointer must never turn a match edit into a source/save
 * edit or let home slot 65 spill into the away array. */
static int property_id_valid(uint32_t id)
{
    return id < 2u * NFL2K5_SANDBOX_PLAYER_ID_STRIDE &&
           id % NFL2K5_SANDBOX_PLAYER_ID_STRIDE < 65u;
}
static uint32_t property_record(uint32_t clock, uint32_t id)
{
    uint32_t side, index, team, base, record;
    unsigned char count = 0, side_tag = 0;
    if (!property_id_valid(id) || !current(clock)) return 0;
    side = id / NFL2K5_SANDBOX_PLAYER_ID_STRIDE;
    index = id % NFL2K5_SANDBOX_PLAYER_ID_STRIDE;
    team = side ? 0xB30A58u : 0xB30864u;
    base = side ? 0xB321A0u : 0xB30C4Cu;
    if (!read_guest(team + 0x11Cu, &count, 1) || count > 65u || index >= count)
        return 0;
    record = u32(team + index * 4u);
    if (record != base + index * 0x54u ||
        !read_guest(record + 0x34u, &side_tag, 1) || side_tag != side + 1u)
        return 0;
    return record;
}
static uint32_t hash_byte(uint32_t value, unsigned char byte)
{
    return (value ^ byte) * 16777619u;
}
static uint32_t hash_names(const uint16_t *first, const uint16_t *last,
                           unsigned jersey)
{
    const uint16_t *parts[2] = { first, last };
    uint32_t hash = 2166136261u;
    if (!first || !last || jersey > 127u) return 0;
    for (unsigned part = 0; part < 2; ++part) {
        unsigned i;
        for (i = 0; i < 64u; ++i) {
            uint16_t unit = parts[part][i];
            hash = hash_byte(hash, (unsigned char)unit);
            hash = hash_byte(hash, (unsigned char)(unit >> 8));
            if (!unit) break;
        }
        if (i == 64u) return 0;
    }
    hash = hash_byte(hash, (unsigned char)jersey);
    return hash ? hash : 1u;
}
static int read_identity_name(uint32_t at, uint16_t out[64])
{
    if (!at) return 0;
    for (unsigned i = 0; i < 64; ++i) {
        uint64_t address = (uint64_t)at + i * 2u;
        if (address > 0xFFFFFFFFu ||
            !read_guest((uint32_t)address, &out[i], sizeof out[i])) return 0;
        if (!out[i]) return 1;
    }
    return 0; /* An unterminated name cannot be a stable scene identity. */
}
static uint32_t record_identity(uint32_t record)
{
    uint16_t first[64], last[64];
    if (!read_identity_name(u32(record + 0x10u), first) ||
        !read_identity_name(u32(record + 0x14u), last) ||
        (!first[0] && !last[0])) return 0;
    return hash_names(first, last, (u32(record + 0x20u) >> 3) & 127u);
}
int nfl2k5_sandbox_actor_valid_properties(const Nfl2k5SandboxActorProperties *in)
{
    /* This checks snapshot structure only. Byte changes are checked against
     * the actual live values by the normal setter; a snapshot may contain
     * unchanged >99 values from an optional mod. */
    return in && in->identity && property_id_valid(in->id);
}
int nfl2k5_sandbox_actor_get_properties(uint32_t expected_clock, int id,
                                      Nfl2k5SandboxActorProperties *out)
{
    Nfl2k5SandboxActorProperties value;
    uint32_t record;
    if (!out || id < 0 || !(record = property_record(expected_clock, (uint32_t)id)))
        return 0;
    memset(&value, 0, sizeof value);
    value.id = (uint32_t)id;
    value.identity = record_identity(record);
    if (!value.identity ||
        !read_guest(record + 0x36u, value.ratings, sizeof value.ratings) ||
        property_record(expected_clock, value.id) != record) return 0;
    *out = value;
    return 1;
}
static int rating_changes_allowed(const uint8_t *old, const uint8_t *next)
{
    for (unsigned i = 0; i < NFL2K5_SANDBOX_RATINGS; ++i) {
        if ((i == 21 || i == 23 || i == 25) && next[i] != old[i]) return 0;
        if (next[i] > 99u && next[i] != old[i]) return 0;
    }
    return 1;
}
static int set_properties(uint32_t clock, const Nfl2k5SandboxActorProperties *in,
                          int raw_restore)
{
    Nfl2k5SandboxActorProperties next;
    uint8_t previous[NFL2K5_SANDBOX_RATINGS];
    uint32_t record;
    int ok = 0;
    if (!nfl2k5_sandbox_actor_valid_properties(in)) return 0;
    next = *in;
    record = property_record(clock, next.id);
    if (!record || record_identity(record) != next.identity ||
        !read_guest(record + 0x36u, previous, sizeof previous) ||
        (!raw_restore && !rating_changes_allowed(previous, next.ratings)) ||
        property_record(clock, next.id) != record) return 0;
    /* The guarded pre-read is a complete backup before the first write. Only
     * these 28 attribute bytes are touched; neighbouring pointer fields and
     * appearance/style metadata outside the rating span remain intact. */
    __try {
        void *dest = (void *)((uintptr_t)xbox_GetMemoryOffset() + record + 0x36u);
        memcpy(dest, next.ratings, sizeof next.ratings);
        ok = memcmp(dest, next.ratings, sizeof next.ratings) == 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok = 0; }
    if (!ok) {
        int restored = 0;
        if (property_record(clock, next.id) == record &&
            record_identity(record) == next.identity) {
            __try {
                void *dest = (void *)((uintptr_t)xbox_GetMemoryOffset() + record + 0x36u);
                memcpy(dest, previous, sizeof previous);
                restored = memcmp(dest, previous, sizeof previous) == 0;
            } __except(EXCEPTION_EXECUTE_HANDLER) { restored = 0; }
        }
        fprintf(stderr, "[SANDBOX] ratings write failed id=%u; backup %s\n",
                next.id, restored ? "restored" : "could not be restored");
    }
    return ok;
}
int nfl2k5_sandbox_actor_set_properties(uint32_t clock,
                                       const Nfl2k5SandboxActorProperties *in)
{
    return set_properties(clock, in, 0);
}
int nfl2k5_sandbox_actor_restore_properties(uint32_t clock,
                                           const Nfl2k5SandboxActorProperties *in)
{
    return set_properties(clock, in, 1);
}
const char *nfl2k5_sandbox_actor_rating_name(int index)
{
    static const char *const names[NFL2K5_SANDBOX_RATINGS] = {
        "Speed", "Agility", "Arm strength", "Stamina", "Kick power",
        "Durability", "Strength", "Jumping", "Coverage", "Route running",
        "Tackling", "Break tackle", "Pass accuracy", "Read coverage",
        "Catching", "Run blocking", "Pass blocking", "Ball security",
        "Pass rush", "Run coverage", "Kick accuracy", "Kicking style",
        "Leadership", "Power run style", "Composure", "Scramble",
        "Consistency", "Aggressiveness"
    };
    return index >= 0 && index < NFL2K5_SANDBOX_RATINGS ? names[index] : "";
}
int nfl2k5_sandbox_actor_properties_selftest(void)
{
    static const uint16_t ab[] = { 'A','B',0 }, c[] = { 'C',0 };
    static const uint16_t a[] = { 'A',0 }, bc[] = { 'B','C',0 };
    Nfl2k5SandboxActorProperties value;
    uint8_t old[NFL2K5_SANDBOX_RATINGS], next[NFL2K5_SANDBOX_RATINGS];
    uint16_t unterminated[64];
    if (record_id(0xB30C4Cu) != 0 || record_id(0xB3214Cu) != 64 ||
        record_id(0xB321A0u) != 71 || record_id(0xB336A0u) != 135 ||
        record_id(0xB336F4u) != -1 || record_id(0xB30C4Du) != -1 ||
        !primary_node(0xB3EF60u) || !primary_node(0xB3F5F0u) ||
        primary_node(0xB3EF61u) || primary_node(0xB3F640u)) return 0;
    memset(&value, 0, sizeof value); value.identity = hash_names(ab,c,12);
    if (!value.identity || value.identity != hash_names(ab,c,12) ||
        value.identity == hash_names(a,bc,12) ||
        value.identity == hash_names(ab,c,13)) return 0;
    for (unsigned i = 0; i < 64; ++i) unterminated[i] = 'A';
    if (hash_names(unterminated,c,12)) return 0;
    value.id = 64; if (!nfl2k5_sandbox_actor_valid_properties(&value)) return 0;
    value.id = 65; if (nfl2k5_sandbox_actor_valid_properties(&value)) return 0;
    value.id = 71; if (!nfl2k5_sandbox_actor_valid_properties(&value)) return 0;
    value.id = 135; if (!nfl2k5_sandbox_actor_valid_properties(&value)) return 0;
    value.id = 136; if (nfl2k5_sandbox_actor_valid_properties(&value)) return 0;
    value.id = 142; if (nfl2k5_sandbox_actor_valid_properties(&value)) return 0;
    memset(old, 50, sizeof old); memcpy(next, old, sizeof old);
    next[0] = 99; if (!rating_changes_allowed(old,next)) return 0;
    next[0] = 100; if (rating_changes_allowed(old,next)) return 0;
    next[0] = 50; next[21] = 51;
    if (rating_changes_allowed(old,next)) return 0;
    next[21] = 50; next[23] = 49;
    if (rating_changes_allowed(old,next)) return 0;
    next[23] = 50; next[25] = 99;
    if (rating_changes_allowed(old,next)) return 0;
    next[25] = 50;
    old[21] = next[21] = 255; next[0] = 50;
    if (!rating_changes_allowed(old,next)) return 0;
    next[21] = 127; if (rating_changes_allowed(old,next)) return 0;
    return 1;
}
