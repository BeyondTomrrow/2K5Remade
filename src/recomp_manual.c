#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
/* Host yields in guest context must give up the guest CPU (RECOMP_GGL). */
#include "xbox_ggl.h"
#include "recomp_funcs.h"
#include "nfl2k5_broadcast.h"

/* FOX samples the same current-match player accessor as Gamecast, but from
 * normal guest execution rather than the Gamecast renderer. The scope-zero
 * descriptor at 543B38 calls 003216C0 -> 000CB240; scope 11 is season data.
 * Guest callbacks must never run on the presenter or a Windows timer thread. */
static int nfl2k5_qb_read(uint32_t address, void *out, size_t size)
{
    uint64_t end = (uint64_t)address + size;
    if (!((address >= 0x10000u && end <= 0x10000000u) ||
          (address >= 0x80000000u && end <= 0x90000000u)))
        return 0;
    __try {
        memcpy(out, (const void *)((uintptr_t)address + g_xbox_mem_offset), size);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static uint32_t nfl2k5_qb_rd32(uint32_t address)
{
    uint32_t value = 0;
    nfl2k5_qb_read(address, &value, sizeof value);
    return value;
}

static int nfl2k5_guest_player_stat(uint32_t player, uint32_t field, int *out)
{
    uint32_t save_eax = g_eax, save_ebx = g_ebx, save_ecx = g_ecx, save_edx = g_edx;
    uint32_t save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp, save_esp = g_esp;
    uint32_t save_seh_ebp = g_seh_ebp;
    int save_fp_top = g_fp_top, save_fp_cmp = g_fp_cmp, save_df = g_df;
    uint16_t save_fp_cc = g_fp_cc, save_fp_control = g_fp_control_word;
    double save_fp_stack[8];
    RecompXmm save_xmm[8] = { g_xmm0, g_xmm1, g_xmm2, g_xmm3,
                            g_xmm4, g_xmm5, g_xmm6, g_xmm7 };
    double value;
    int valid;
    static unsigned abi_errors;

    memcpy(save_fp_stack, g_fp_stack, sizeof save_fp_stack);
    g_ecx = player;
    g_edx = field;
    PUSH32(g_esp, 0u);          /* Gamecast's actual current-match scope. */
    PUSH32(g_esp, 0x003216D9u); /* Synthetic call frame, not a stack argument. */
    RECOMP_ABI_CALL(0x000CB240u, sub_000CB240);
    value = g_fp_stack[g_fp_top & 7];
    valid = g_esp == save_esp && g_fp_top == ((save_fp_top + 7) & 7) &&
            isfinite(value) && value >= -10000.0 && value <= 10000.0;
    if (!valid && abi_errors++ < 4)
        fprintf(stderr, "[LIVE-QB-ABI] field=%X esp=%08X expected=%08X fp=%d expected=%d value=%.3f\n",
                field, g_esp, save_esp, g_fp_top, (save_fp_top + 7) & 7, value);
    g_eax = save_eax; g_ebx = save_ebx; g_ecx = save_ecx; g_edx = save_edx;
    g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp; g_esp = save_esp;
    g_seh_ebp = save_seh_ebp; g_df = save_df;
    memcpy(g_fp_stack, save_fp_stack, sizeof save_fp_stack);
    g_fp_top = save_fp_top; g_fp_cmp = save_fp_cmp; g_fp_cc = save_fp_cc;
    g_fp_control_word = save_fp_control;
    g_xmm0 = save_xmm[0]; g_xmm1 = save_xmm[1]; g_xmm2 = save_xmm[2]; g_xmm3 = save_xmm[3];
    g_xmm4 = save_xmm[4]; g_xmm5 = save_xmm[5]; g_xmm6 = save_xmm[6]; g_xmm7 = save_xmm[7];
    if (valid) *out = (int)(value >= 0 ? value + 0.5 : value - 0.5);
    return valid;
}

/* Ask the title's current depth chart for the first QB slot. 61CB0/61CD0
 * feed the home/away substitution state into E7990 -> E7810, so a substituted
 * QB is selected before he has thrown his first pass. Roster order and total
 * passing attempts do not identify the player currently in that slot. */
static int nfl2k5_guest_current_qb(int side, uint32_t table, unsigned count,
                                  uint32_t *out)
{
    uint32_t chart = side == 0 ? 0x00B33A28u : 0x00B336F4u;
    uint32_t slots = nfl2k5_qb_rd32(chart + 0x9Cu);
    unsigned char index = 0xFFu, position;
    uint32_t save_eax = g_eax, save_ebx = g_ebx, save_ecx = g_ecx, save_edx = g_edx;
    uint32_t save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp, save_esp = g_esp;
    uint32_t save_seh_ebp = g_seh_ebp;
    int save_fp_top = g_fp_top, save_fp_cmp = g_fp_cmp, save_df = g_df;
    uint16_t save_fp_cc = g_fp_cc, save_fp_control = g_fp_control_word;
    double save_fp_stack[8];
    RecompXmm save_xmm[8] = { g_xmm0, g_xmm1, g_xmm2, g_xmm3,
                            g_xmm4, g_xmm5, g_xmm6, g_xmm7 };
    uint32_t player;
    unsigned i;
    int valid, found = 0;
    static unsigned abi_errors;

    /* E80D0 initializes chart+0 from the side's match roster. A missing chart
     * or depth slot during initialization is retried on the next sample. */
    if (!count || nfl2k5_qb_rd32(chart) != table || !slots ||
        !nfl2k5_qb_read(slots, &index, sizeof index) || index >= count)
        return 0;
    memcpy(save_fp_stack, g_fp_stack, sizeof save_fp_stack);
    g_ecx = 0u; /* QB position: 221EE0/510208 maps this to depth category 0. */
    g_edx = 0u; /* First slot, with live substitutions applied by E7810. */
    if (side == 0) {
        PUSH32(g_esp, 0x0005A22Au);
        RECOMP_ABI_CALL(0x00061CD0u, sub_00061CD0);
    } else {
        PUSH32(g_esp, 0x0005A231u);
        RECOMP_ABI_CALL(0x00061CB0u, sub_00061CB0);
    }
    player = g_eax;
    valid = g_esp == save_esp && g_fp_top == save_fp_top;
    if (!valid && abi_errors++ < 4)
        fprintf(stderr, "[LIVE-QB-SELECT-ABI] side=%d esp=%08X expected=%08X fp=%d expected=%d\n",
                side, g_esp, save_esp, g_fp_top, save_fp_top);
    g_eax = save_eax; g_ebx = save_ebx; g_ecx = save_ecx; g_edx = save_edx;
    g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp; g_esp = save_esp;
    g_seh_ebp = save_seh_ebp; g_df = save_df;
    memcpy(g_fp_stack, save_fp_stack, sizeof save_fp_stack);
    g_fp_top = save_fp_top; g_fp_cmp = save_fp_cmp; g_fp_cc = save_fp_cc;
    g_fp_control_word = save_fp_control;
    g_xmm0 = save_xmm[0]; g_xmm1 = save_xmm[1]; g_xmm2 = save_xmm[2]; g_xmm3 = save_xmm[3];
    g_xmm4 = save_xmm[4]; g_xmm5 = save_xmm[5]; g_xmm6 = save_xmm[6]; g_xmm7 = save_xmm[7];
    if (!valid || !player ||
        !nfl2k5_qb_read(player + 0x35u, &position, sizeof position) || position != 0)
        return 0;
    for (i = 0; i < count; ++i) {
        if (nfl2k5_qb_rd32(table + i * 4u) == player) { found = 1; break; }
    }
    if (!found || !nfl2k5_qb_rd32(nfl2k5_qb_rd32(player + 0x30u))) return 0;
    *out = player;
    return 1;
}

static int nfl2k5_guest_text(uint32_t address, char *out, size_t capacity)
{
    size_t i;
    unsigned char first[2];
    int wide;
    if (!out || capacity < 2 || !nfl2k5_qb_read(address, first, sizeof first))
        return 0;
    wide = first[1] == 0;
    for (i = 0; i + 1 < capacity; ++i) {
        unsigned char ch;
        if (!nfl2k5_qb_read(address + (uint32_t)i * (wide ? 2u : 1u), &ch, 1))
            return 0;
        if (!ch) break;
        if (ch < 32 || ch >= 127) return 0;
        out[i] = (char)ch;
    }
    out[i] = 0;
    return i != 0;
}

static int nfl2k5_guest_player_name(uint32_t player, char *out, size_t capacity)
{
    char first[32] = {0}, last[40] = {0};
    if (!nfl2k5_guest_text(nfl2k5_qb_rd32(player + 0x10u), first, sizeof first) ||
        !nfl2k5_guest_text(nfl2k5_qb_rd32(player + 0x14u), last, sizeof last))
        return 0;
    _snprintf_s(out, capacity, _TRUNCATE, "%c. %s", first[0], last);
    return 1;
}

void nfl2k5_live_qb_guest_tick(void)
{
    static DWORD last_sample;
    static volatile LONG sampling;
    static int was_match;
    static unsigned diagnostics;
    DWORD now = GetTickCount();
    int side;
    uint32_t period;
    if (now - last_sample < 500u || InterlockedCompareExchange(&sampling, 1, 0))
        return;
    last_sample = now;
    period = nfl2k5_qb_rd32(0x00E602C4u);
    if (period == 0 || period > 5) {
        if (was_match) {
            nfl2k5_broadcast_live_qb_reset();
        }
        was_match = 0;
        InterlockedExchange(&sampling, 0);
        return;
    }
    was_match = 1;
    for (side = 0; side < 2; ++side) {
        /* Marker 1 selects E5FC20 (home); marker 2 selects E5FC60 (away)
         * in sub_0007E0A6. These tables are NOT in broadcast side order. */
        uint32_t table = side == 0 ? 0x00B30A58u : 0x00B30864u;
        uint32_t count_address = side == 0 ? 0x00B30B74u : 0x00B30980u;
        unsigned char count = 0;
        uint32_t player = 0;
        Nfl2k5PlayerStat stat;
        char name[48];
        nfl2k5_qb_read(count_address, &count, sizeof count);
        if (count > 71u || !nfl2k5_guest_current_qb(side, table, count, &player)) continue;
        memset(&stat, 0, sizeof stat);
        if (!nfl2k5_guest_player_name(player, name, sizeof name) ||
            !nfl2k5_guest_player_stat(player, 0x04u, &stat.completions) ||
            !nfl2k5_guest_player_stat(player, 0x23u, &stat.attempts) ||
            !nfl2k5_guest_player_stat(player, 0x4Cu, &stat.passing_yards) ||
            !nfl2k5_guest_player_stat(player, 0x40u, &stat.passing_touchdowns) ||
            !nfl2k5_guest_player_stat(player, 0x16u, &stat.interceptions))
            continue;
        if (stat.completions < 0 || stat.attempts < stat.completions || stat.attempts > 200 ||
            stat.passing_yards < -1000 || stat.passing_yards > 3000 ||
            stat.passing_touchdowns < 0 || stat.passing_touchdowns > 50 ||
            stat.interceptions < 0 || stat.interceptions > 50)
            continue;
        stat.kind = NFL2K5_STAT_QB;
        stat.player_name = name;
        stat.display_seconds = 5.0f;
        nfl2k5_broadcast_live_qb_sample(&stat, side);
        if (diagnostics++ < 2)
            fprintf(stderr, "[LIVE-QB-SOURCE] side=%d player=%08X %s %d/%d %d YDS TD=%d INT=%d\n",
                    side, player, name, stat.completions, stat.attempts, stat.passing_yards,
                    stat.passing_touchdowns, stat.interceptions);
    }
    InterlockedExchange(&sampling, 0);
}

/* Boot-only AC97 channel reset acknowledgement. This does not synthesize audio. */
void nfl2k5_ack_ac97_reset(uint32_t address)
{
    if (address >= 0xFEC00100u && address < 0xFEC00200u) {
        volatile uint8_t *control=(volatile uint8_t *)((uintptr_t)xbox_GetMemoryOffset()+address);
        *control &= (uint8_t)~2u;
        static unsigned count;
        if(count++<8) fprintf(stderr,"[BOOT-BYPASS] AC97 reset acknowledged at %08X\n",address);
    }
}

/* Boot trace only: B04EC8 is a reference count, so record both directions
 * with the guest return address instead of treating a later zero as a reset. */
void nfl2k5_trace_phase_gate(uint32_t value, uint32_t caller)
{
    static unsigned count;
    if (count++ < 64)
        fprintf(stderr, "[PHASE] B04EC8=%u caller=%08X\n", value, caller);
}

/* Low-overhead Gamecast Live row probe. sub_00171910 renders many list types,
 * so record one row for each callback/style pair.  Passing columns share a
 * callback but use separate style records; keying only on the callback hid
 * YDS, TD, and INT after the first three numeric columns.  This remains
 * bounded and never emits per-frame render-path output. */
void nfl2k5_gamecast_row_probe(uint32_t list, uint32_t row,
                               uint32_t index, uint32_t callback)
{
    enum { PROBE_SLOTS = 128 };
    static volatile LONG seen[PROBE_SLOTS];
    uint32_t style = MEM32(row + 0x00);
    LONG key = (LONG)(callback ^ (style * 0x9E3779B9u));
    unsigned slot = ((unsigned)key >> 1) & (PROBE_SLOTS - 1);

    /* The renderer can invoke this concurrently.  One lock-free slot claims
     * each observed callback/style pair; a collision merely loses optional
     * diagnostic output and can never stall a frame. */
    if (key == 0 || InterlockedCompareExchange(&seen[slot], key, 0) != 0)
        return;

    fprintf(stderr,
            "[GAMECAST-ROW] cb=%08X list=%08X row=%08X index=%u "
            "w=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X "
            "method=%08X/%08X/%08X/%08X\n",
            callback, list, row, index,
            MEM32(row + 0x00), MEM32(row + 0x04), MEM32(row + 0x08),
            MEM32(row + 0x0C), MEM32(row + 0x10), MEM32(row + 0x14),
            MEM32(row + 0x18), MEM32(row + 0x1C), MEM32(row + 0x20),
            MEM32(row + 0x24), MEM32(row + 0x28),
            MEM32(MEM32(row + 0x00) + 0x08), MEM32(MEM32(row + 0x00) + 0x0C),
            MEM32(MEM32(row + 0x00) + 0x10), MEM32(MEM32(row + 0x00) + 0x14));
}

void nfl2k5_gamecast_text_probe(uint32_t widget, uint32_t value)
{
    enum { MAX_VALUES = 32 };
    static uint32_t seen[MAX_VALUES];
    static LONG seen_count;
    LONG count = seen_count;
    LONG i;
    int j;
    char ascii[33];

    for (i = 0; i < count && i < MAX_VALUES; ++i)
        if (seen[i] == value)
            return;
    i = InterlockedIncrement(&seen_count) - 1;
    if (i < 0 || i >= MAX_VALUES)
        return;
    seen[i] = value;
    for (j = 0; j < 32; ++j) {
        unsigned char ch = MEM8(value + (uint32_t)j);
        ascii[j] = (ch >= 32 && ch < 127) ? (char)ch : '.';
    }
    ascii[32] = 0;
    fprintf(stderr,
            "[GAMECAST-TEXT] widget=%08X value=%08X "
            "w=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X ascii='%s'\n",
            widget, value,
            MEM32(value + 0x00), MEM32(value + 0x04),
            MEM32(value + 0x08), MEM32(value + 0x0C),
            MEM32(value + 0x10), MEM32(value + 0x14),
            MEM32(value + 0x18), MEM32(value + 0x1C), ascii);
}

/* 003639D0 is the Gamecast text adapter behind the player-name widget.
 * Capture a bounded sample of its model context and selector so the source
 * stat record can be mapped without observing D3D text output. */
void nfl2k5_gamecast_stat_context(uint32_t context, uint32_t selector)
{
    enum { MAX_SAMPLES = 24 };
    static LONG samples;
    LONG n = InterlockedIncrement(&samples);
    if (n > MAX_SAMPLES)
        return;
    {
        uint32_t player = MEM32(0x00B30A58u + ((uint32_t)(n - 1) * 4u));
        fprintf(stderr,
                "[GAMECAST-STATCTX] idx=%ld ctx=%08X sel=%08X player=%08X "
                "p=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,"
                "%08X,%08X,%08X,%08X\n",
                n - 1, context, selector, player,
                MEM32(player + 0x00), MEM32(player + 0x04),
                MEM32(player + 0x08), MEM32(player + 0x0C),
                MEM32(player + 0x10), MEM32(player + 0x14),
                MEM32(player + 0x18), MEM32(player + 0x1C),
                MEM32(player + 0x20), MEM32(player + 0x24),
                MEM32(player + 0x28), MEM32(player + 0x2C));
    }
}

void nfl2k5_gamecast_stat_value(uint32_t table_slot, uint32_t field,
                                 uint32_t value_bits)
{
    enum { MAX_SAMPLES = 24 };
    static LONG samples;
    LONG n = InterlockedIncrement(&samples);
    uint32_t index;
    uint32_t player;
    if (n > MAX_SAMPLES || table_slot < 0x00AED668u ||
        table_slot >= 0x00AED668u + MAX_SAMPLES * 12u)
        return;
    index = (table_slot - 0x00AED668u) / 12u;
    player = MEM32(0x00B30A58u + index * 4u);
    fprintf(stderr,
            "[GAMECAST-STATVALUE] idx=%u player=%08X field=%08X fmt=%08X raw=%08X "
            "player=%08X,%08X,%08X,%08X,%08X,%08X\n",
            index, player, field, MEM32(table_slot + 4), value_bits,
            MEM32(player + 0x00), MEM32(player + 0x04), MEM32(player + 0x08),
            MEM32(player + 0x0C), MEM32(player + 0x10), MEM32(player + 0x14));
}

/* The frontend scheduler runs on a worker thread.  Keep its trace entirely
 * in memory: writing stderr here changes scheduling enough to hide races. */
volatile LONG nfl2k5_frontend_samples;
volatile uint32_t nfl2k5_frontend_queue;
volatile uint32_t nfl2k5_frontend_pending;
volatile uint32_t nfl2k5_frontend_head;
volatile uint32_t nfl2k5_frontend_tail;
volatile uint32_t nfl2k5_frontend_state;
volatile uint32_t nfl2k5_frontend_request;
volatile uint32_t nfl2k5_frontend_active;
volatile LONG nfl2k5_frontend_nonempty_samples;
volatile LONG nfl2k5_frontend_queue_pump_calls;
volatile LONG nfl2k5_worker_task_dispatches;
volatile uint32_t nfl2k5_worker_task_target;
volatile LONG nfl2k5_worker_loop_entries;
volatile LONG nfl2k5_worker_drain_calls;
volatile LONG nfl2k5_worker_entry_calls;
volatile uint32_t nfl2k5_worker_entry_target;
volatile LONG nfl2k5_worker_entry_359b0;
volatile LONG nfl2k5_worker_entry_4d810;
volatile LONG nfl2k5_worker_entry_other;
volatile LONG nfl2k5_worker_bridge_starts;
volatile uint32_t nfl2k5_worker_bridge_context;
/* Windows can schedule a resumed guest worker before its caller reaches the
 * following readiness store. Capture that narrow handoff without logging from
 * either thread. */
volatile LONG nfl2k5_worker_handoff_parent_ready;
volatile LONG nfl2k5_worker_handoff_worker_ready;
volatile LONG nfl2k5_worker_handoff_worker_missed;
volatile uint32_t nfl2k5_worker_handoff_last_active;
volatile uint32_t nfl2k5_worker_handoff_last_ready;
volatile LONG nfl2k5_worker_idle_yields;
volatile LONG nfl2k5_worker_completion_calls;
volatile LONG nfl2k5_worker_completion_idle_calls;
volatile LONG nfl2k5_worker_completion_active_calls;
volatile LONG nfl2k5_worker_counter_releases;
volatile LONG nfl2k5_worker_counter_acquires;
volatile uint32_t nfl2k5_worker_counter_acquire_caller;
volatile uint32_t nfl2k5_worker_counter_release_caller;
volatile uint32_t nfl2k5_worker_completion_last_mode;
/* sub_000359C0 zeroes B0284C/B02848/B02680 every time it (re)creates the
 * worker thread. If it ever runs again while another host thread is inside
 * the active-wait branch of sub_00035CE0 (which has already bumped B0284C
 * and set B02848=1), this reset silently drops the pairing: B02848 goes back
 * to 0 while B0284C is left non-zero by the still-pending waiter, which is
 * exactly the stuck state observed in native diagnostic samples. */
volatile LONG nfl2k5_worker_reset_calls;
volatile uint32_t nfl2k5_worker_reset_caller;

/* sub_00420810 stores a guest function pointer into the D3D device struct at
 * +0x1DB8 (device_ptr = MEM32(0x4409A8)). No generated code anywhere reads
 * that field back -- grep across src/recomp/gen confirms the single MEM32
 * reference to +0x1DB8 is this write. That makes it a candidate resource/GPU
 * completion notify callback that only fires if something on the native
 * NV2A/D3D bridge side invokes it; this trace exists to confirm the
 * registration itself actually runs before any invocation is added. */
volatile LONG nfl2k5_gpu_notify_register_calls;
volatile uint32_t nfl2k5_gpu_notify_register_callback;
volatile uint32_t nfl2k5_gpu_notify_register_device;

/* sub_00028DE0 seeds MEM32(0xA6A9B0) from MEM32(0xA6A9AC) then spins in
 * sub_000341A0 until it drains back to <= 0. The only decrement site,
 * sub_00026EE0, is reached exclusively through the callback registered above
 * (imm_ref_target in the static xref database, never a direct call). */
volatile LONG nfl2k5_gpu_wait_seed_calls;
volatile uint32_t nfl2k5_gpu_wait_seed_value;
volatile LONG nfl2k5_gpu_notify_delivered;
volatile LONG nfl2k5_gpu_notify_service_calls;
volatile LONG nfl2k5_gpu_wait_entry_calls;
volatile uint32_t nfl2k5_gpu_wait_entry_value;
volatile LONG nfl2k5_gpu_wait_exit_calls;
volatile LONG nfl2k5_frontend_prepare_calls;
volatile LONG nfl2k5_frontend_prepare_ok;
volatile uint32_t nfl2k5_frontend_prepare_result;
volatile LONG nfl2k5_frontend_submit_calls;
volatile LONG nfl2k5_frontend_submit_ok;
volatile uint32_t nfl2k5_frontend_submit_result;
volatile uint32_t nfl2k5_frontend_submit_status;
volatile uint32_t nfl2k5_frontend_range_source;
volatile uint32_t nfl2k5_frontend_range_size;
volatile uint32_t nfl2k5_frontend_range_low;
volatile uint32_t nfl2k5_frontend_range_high;
volatile uint32_t nfl2k5_frontend_range_end_low;
volatile uint32_t nfl2k5_frontend_range_end_high;
volatile uint32_t nfl2k5_frontend_range_request;
volatile uint32_t nfl2k5_frontend_range_provider;
volatile LONG nfl2k5_frontend_state_counts[9];
volatile LONG nfl2k5_title_init_74bf0_calls;
volatile LONG nfl2k5_title_init_38fc0_calls;
volatile LONG nfl2k5_title_init_network_calls;
volatile LONG nfl2k5_title_init_checkpoint;
volatile LONG nfl2k5_async_items;
volatile uint32_t nfl2k5_async_item;
volatile uint32_t nfl2k5_async_item_type;
volatile uint32_t nfl2k5_async_provider;
volatile LONG nfl2k5_async_dispatches;
volatile uint32_t nfl2k5_async_dispatch_type;
volatile uint32_t nfl2k5_async_dispatch_target;
volatile LONG nfl2k5_archive_init_44c10_calls;
volatile uint32_t nfl2k5_archive_init_44c10_context;
volatile uint32_t nfl2k5_archive_init_44c10_source;
volatile LONG nfl2k5_archive_setup_44d00_calls;
volatile uint32_t nfl2k5_archive_setup_44d00_counter;
volatile LONG nfl2k5_boot_task_3be40_calls;
volatile uint32_t nfl2k5_boot_task_3be40_owner;
volatile uint32_t nfl2k5_boot_task_3be40_record;
volatile uint32_t nfl2k5_boot_task_3be40_completion;
volatile LONG nfl2k5_boot_task_42440_calls;
volatile uint32_t nfl2k5_boot_task_42440_result_slot;
volatile uint32_t nfl2k5_boot_task_3be40_state;
volatile uint32_t nfl2k5_boot_task_3be40_type;
volatile uint32_t nfl2k5_boot_task_3be40_target;
volatile uint32_t nfl2k5_boot_task_3be40_argument;
volatile uint32_t nfl2k5_boot_task_3be40_dispatch_lock_before;
volatile uint32_t nfl2k5_boot_task_3be40_dispatch_lock_after;
volatile LONG nfl2k5_archive_dispatch_438d0_calls;
volatile uint32_t nfl2k5_archive_dispatch_438d0_callback;
volatile uint32_t nfl2k5_archive_dispatch_438d0_tag;
volatile uint32_t nfl2k5_archive_dispatch_438d0_result;
volatile LONG nfl2k5_archive_completion_44df0_calls;
volatile uint32_t nfl2k5_archive_completion_44df0_context;
volatile uint32_t nfl2k5_async_dispatch_branch;
volatile LONG nfl2k5_frontend_state_dispatch_calls;
volatile uint32_t nfl2k5_frontend_state_dispatch_this;
volatile uint32_t nfl2k5_frontend_state_dispatch_value;
volatile uint32_t nfl2k5_frontend_state_dispatch_tick;
volatile uint32_t nfl2k5_frontend_state_dispatch_limit;
volatile uint32_t nfl2k5_frontend_state_dispatch_flags;
/* In-memory branch trace for the one-shot frontend bootstrap.  Stderr output
 * here changes which side of the worker race wins, so main.c samples these
 * fields after the run instead. */
volatile LONG nfl2k5_frontend_enqueue_calls;
volatile uint32_t nfl2k5_frontend_enqueue_caller;
volatile uint32_t nfl2k5_frontend_enqueue_node;
volatile uint32_t nfl2k5_frontend_enqueue_descriptor;
volatile LONG nfl2k5_frontend_producer_178150;
volatile LONG nfl2k5_frontend_producer_272a60;
volatile LONG nfl2k5_frontend_event_4953c4;
volatile LONG nfl2k5_frontend_event_492e9b;
volatile LONG nfl2k5_frontend_event_49582b;
volatile LONG nfl2k5_frontend_event_492414;
volatile LONG nfl2k5_frontend_event_48bb78;
volatile uint32_t nfl2k5_frontend_packet_alloc;
volatile uint32_t nfl2k5_frontend_packet_submit;
volatile uint32_t nfl2k5_frontend_packet_route;
volatile uint32_t nfl2k5_frontend_packet_token;
volatile uint32_t nfl2k5_frontend_packet_flags;
volatile LONG nfl2k5_frontend_packet_route_counts[5];
volatile LONG nfl2k5_packet_pump_49516f;
volatile LONG nfl2k5_packet_processor_48e0a6;
/* Packet completion runs in a guest DPC.  These fields show whether the
 * DPC's resource-manager gate accepts work before the frontend queue can be
 * populated.  They are memory-only to avoid changing worker timing. */
volatile LONG nfl2k5_packet_dpc_entries;
volatile LONG nfl2k5_packet_dpc_gate_open;
volatile uint32_t nfl2k5_packet_dpc_context;
volatile uint32_t nfl2k5_packet_dpc_pending;
volatile uint32_t nfl2k5_packet_dpc_limit;
volatile uint32_t nfl2k5_packet_dpc_gate_result;
volatile uint32_t nfl2k5_packet_dpc_list_head;
volatile LONG nfl2k5_packet_dpc_handler_calls;
volatile uint32_t nfl2k5_packet_dpc_handler_item;
volatile uint32_t nfl2k5_packet_dpc_handler_flags;
volatile uint32_t nfl2k5_packet_dpc_handler_target;
volatile LONG nfl2k5_packet_dpc_dequeue_calls;
volatile uint32_t nfl2k5_packet_dpc_dequeue_item;
volatile uint32_t nfl2k5_packet_dpc_dequeue_flags;
volatile uint32_t nfl2k5_packet_dpc_dequeue_target;
volatile uint32_t nfl2k5_packet_dpc_dequeue_link;
volatile uint32_t nfl2k5_packet_dpc_dequeue_metadata;
volatile uint32_t nfl2k5_packet_dpc_owner_flags;
volatile LONG nfl2k5_packet_buffer_install_calls;
volatile uint32_t nfl2k5_packet_buffer_install_context;
volatile uint32_t nfl2k5_packet_buffer_install_buffer;
/* The packet DPC reaches 0048ECD0 after a completion is decoded.  Record
 * which of its two delivery routes is selected and the target on that route.
 * This is deliberately memory-only: the DPC is timing-sensitive. */
volatile LONG nfl2k5_packet_delivery_calls;
volatile uint32_t nfl2k5_packet_delivery_context;
volatile uint32_t nfl2k5_packet_delivery_item;
volatile uint32_t nfl2k5_packet_delivery_route;
volatile uint32_t nfl2k5_packet_delivery_target;
volatile LONG nfl2k5_packet_fallback_calls;
volatile uint32_t nfl2k5_packet_fallback_context;
volatile uint32_t nfl2k5_packet_fallback_item;
volatile uint32_t nfl2k5_packet_fallback_length;
volatile uint32_t nfl2k5_packet_fallback_alloc_fn;
volatile uint32_t nfl2k5_packet_fallback_free_fn;
volatile uint32_t nfl2k5_packet_fallback_block;
volatile uint32_t nfl2k5_packet_fallback_after_alloc;
volatile LONG nfl2k5_frontend_boot_steps;
volatile uint32_t nfl2k5_frontend_boot_stage;
volatile uint32_t nfl2k5_frontend_boot_result;
static CRITICAL_SECTION nfl2k5_scheduler_table_cs;
static INIT_ONCE nfl2k5_scheduler_table_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK nfl2k5_init_scheduler_table_cs(PINIT_ONCE once,
                                                     PVOID parameter,
                                                     PVOID *context)
{
    (void)once; (void)parameter; (void)context;
    InitializeCriticalSection(&nfl2k5_scheduler_table_cs);
    return TRUE;
}

void nfl2k5_scheduler_table_lock(void)
{
    InitOnceExecuteOnce(&nfl2k5_scheduler_table_once,
                        nfl2k5_init_scheduler_table_cs, NULL, NULL);
    EnterCriticalSection(&nfl2k5_scheduler_table_cs);
}

void nfl2k5_scheduler_table_unlock(void)
{
    LeaveCriticalSection(&nfl2k5_scheduler_table_cs);
}
volatile LONG nfl2k5_scheduler_samples;
volatile uint32_t nfl2k5_scheduler_callback;
volatile uint32_t nfl2k5_scheduler_callback_entered;
volatile uint32_t nfl2k5_scheduler_callback_returned;
volatile LONG nfl2k5_scheduler_callback_entries;
volatile LONG nfl2k5_scheduler_callback_returns;
volatile LONG nfl2k5_scheduler_registration_attempts;
volatile uint32_t nfl2k5_scheduler_registration_callback;
volatile uint32_t nfl2k5_scheduler_registration_caller;
/* Keep a small history: the final value alone cannot distinguish a title
 * that never requested callbacks 8/9 from one whose requests were rejected. */
volatile LONG nfl2k5_scheduler_registration_history_index;
volatile uint32_t nfl2k5_scheduler_registration_history_count[16];
volatile uint32_t nfl2k5_scheduler_registration_history_callback[16];
volatile uint32_t nfl2k5_scheduler_registration_history_caller[16];
volatile LONG nfl2k5_input_init_stage;
volatile LONG nfl2k5_network_init_stage;
volatile uint32_t nfl2k5_network_init_status;
volatile uint32_t nfl2k5_network_init_version;
volatile LONG nfl2k5_invalid_queue_objects;
volatile uint32_t nfl2k5_invalid_queue_object;
volatile uint32_t nfl2k5_invalid_queue_record;

void nfl2k5_trace_worker_handoff(uint32_t phase, uint32_t active,
                                 uint32_t ready)
{
    nfl2k5_worker_handoff_last_active = active;
    nfl2k5_worker_handoff_last_ready = ready;
    if (phase == 2) {
        InterlockedIncrement(&nfl2k5_worker_handoff_parent_ready);
    } else if (active) {
        if (ready)
            InterlockedIncrement(&nfl2k5_worker_handoff_worker_ready);
        else
            InterlockedIncrement(&nfl2k5_worker_handoff_worker_missed);
    }
}

/* The original worker's idle loop relies on the Xbox scheduler to hand time
 * back to the thread that fills its queue. A native host thread can otherwise
 * reacquire the guest critical section continuously before that producer runs. */
void nfl2k5_yield_worker_idle(void)
{
    InterlockedIncrement(&nfl2k5_worker_idle_yields);
    if (!SwitchToThread())
        Sleep(0);
}

/* sub_00035CE0 pairs the worker's resume counter with its completion wait.
 * Record which side of that pairing the title takes without printing from a
 * timing-sensitive guest thread. */
void nfl2k5_trace_worker_completion(uint32_t mode)
{
    nfl2k5_worker_completion_last_mode = mode;
    InterlockedIncrement(&nfl2k5_worker_completion_calls);
    if (mode)
        InterlockedIncrement(&nfl2k5_worker_completion_active_calls);
    else
        InterlockedIncrement(&nfl2k5_worker_completion_idle_calls);
}

void nfl2k5_trace_worker_counter_release(void)
{
    nfl2k5_worker_counter_release_caller = MEM32(g_esp);
    InterlockedIncrement(&nfl2k5_worker_counter_releases);
}

/* sub_000359C0 entry: distinguishes the first worker-thread launch from a
 * later re-launch that could stomp an in-flight B0284C/B02848 pairing. */
void nfl2k5_trace_worker_reset(void)
{
    nfl2k5_worker_reset_caller = MEM32(g_esp);
    InterlockedIncrement(&nfl2k5_worker_reset_calls);
}

/* sub_00420810 entry: eax holds the callback pointer being stored at
 * device+0x1DB8 (device = MEM32(0x4409A8)) before the store executes. */
void nfl2k5_trace_gpu_notify_register(uint32_t callback)
{
    nfl2k5_gpu_notify_register_callback = callback;
    nfl2k5_gpu_notify_register_device = MEM32(0x4409A8u);
    InterlockedIncrement(&nfl2k5_gpu_notify_register_calls);
}

/* sub_00028DE0 loc_00028EE5: records the value MEM32(0xA6A9B0) is reseeded
 * to right before the drain-wait loop can block on it again. */
void nfl2k5_trace_gpu_wait_seed(uint32_t value)
{
    nfl2k5_gpu_wait_seed_value = value;
    InterlockedIncrement(&nfl2k5_gpu_wait_seed_calls);
    InterlockedExchange(&nfl2k5_gpu_notify_delivered, 0);
}

/* sub_00028DE0's wait loop blocks on MEM32(0xA6A9B0) draining to <= 0. The
 * only path that decrements it, sub_00026EE0, is never reached by a direct
 * call anywhere in the recompiled code (confirmed by grepping every
 * generated .c file): its address is only ever taken as data and stored into
 * the D3D device struct at device+0x1DB8 by sub_00420810 (device =
 * MEM32(0x4409A8)), which is a resource/GPU-completion notify-callback
 * register with no reader anywhere in the recompiled code, guest or native.
 * Real hardware would deliver this through a GPU interrupt that the native
 * NV2A bridge does not yet model. It is delivered here instead, at the one
 * point verified to need it: from inside the guest thread that is itself
 * blocked waiting for it, immediately before it enters the retry loop. This
 * runs on that thread's own TLS g_esp/register context (RECOMP_TLS, per
 * recomp_types.h), matching every other example of native code invoking
 * guest code in this project -- compare xbox_bridge_drain_guest_dpcs,
 * invoked from inside xbox_KeDelayExecutionThread on the blocked thread's
 * own stack, not from an unrelated thread.
 * Delivered at most once per reseed (the flag above is cleared each time
 * this function records a fresh seed), matching sub_00026EE0's own
 * semantics: it is a one-shot completion signal, not a level-triggered
 * condition, and invoking it on every retry iteration would keep
 * incrementing its companion MEM32(0xA6A9B4) completion tally forever. */
/* sub_00028DE0 entry: records how many times this function has been called
 * and the current wait counter value it sees at loc_00028E61 (before any
 * reseed in this call), to distinguish "still inside an earlier invocation"
 * from "reached a later invocation whose wait was not serviced". */
void nfl2k5_trace_gpu_wait_entry(uint32_t value)
{
    nfl2k5_gpu_wait_entry_value = value;
    InterlockedIncrement(&nfl2k5_gpu_wait_entry_calls);
}

/* sub_00028DE0's every return path (early-exit gate and normal completion
 * both land at loc_00028F3A). Comparing this against entry calls shows
 * whether the current invocation ever finishes. */
void nfl2k5_trace_gpu_wait_exit(void)
{
    InterlockedIncrement(&nfl2k5_gpu_wait_exit_calls);
}

/* 2026-09-22: sub_00033660 (called from inside sub_00028DE0's GPU wait,
 * loc_00028DFF) spins "while (MEM32(esi+8) != 0) sub_000341A0();" where
 * esi = MEM32(MEM32(0xA6AA70)+4). Live cdb attaches during a hang
 * consistently showed the main thread parked deep inside this exact loop
 * (sub_000341A0 -> sub_0001B79F -> sub_0001B601 -> a real
 * KeDelayExecutionThread call), distinct from the already-handled
 * MEM32(0xA6A9B0) wait in the same function. Tracks the context pointer and
 * the field's value at loop entry and at each retry to show whether it ever
 * drains or is permanently stuck. */
volatile LONG nfl2k5_drain33660_entry_calls;
volatile uint32_t nfl2k5_drain33660_entry_context;
volatile uint32_t nfl2k5_drain33660_entry_value;
volatile LONG nfl2k5_drain33660_retry_calls;
volatile uint32_t nfl2k5_drain33660_retry_value;
volatile uint32_t nfl2k5_drain33660_first_value;

volatile LONG nfl2k5_state27_resource_list_calls;
volatile uint32_t nfl2k5_state27_resource_list_context;
volatile uint32_t nfl2k5_state27_resource_list_head;

void nfl2k5_trace_state27_resource_list(uint32_t context, uint32_t head)
{
    nfl2k5_state27_resource_list_context = context;
    nfl2k5_state27_resource_list_head = head;
    InterlockedIncrement(&nfl2k5_state27_resource_list_calls);
}

void nfl2k5_trace_drain33660(uint32_t context, uint32_t value, int is_retry)
{
    if (!is_retry) {
        nfl2k5_drain33660_entry_context = context;
        nfl2k5_drain33660_entry_value = value;
        if (InterlockedIncrement(&nfl2k5_drain33660_entry_calls) == 1)
            nfl2k5_drain33660_first_value = value;
    } else {
        nfl2k5_drain33660_retry_value = value;
        InterlockedIncrement(&nfl2k5_drain33660_retry_calls);
    }
}

void nfl2k5_gpu_notify_service(void)
{
    uint32_t device, callback, saved_esp;
    /* RECOMP_ICALL_SAFE follows generated-code convention and assigns its
     * local eax alias on a lookup failure; this call ignores the result
     * either way, but the macro still needs somewhere to write it. */
    uint32_t eax = 0;
    if (InterlockedCompareExchange(&nfl2k5_gpu_notify_delivered, 1, 0) != 0)
        return;
    device = MEM32(0x4409A8u);
    callback = device ? MEM32(device + 0x1DB8u) : 0;
    if (!callback)
        return;
    InterlockedIncrement(&nfl2k5_gpu_notify_service_calls);
    saved_esp = g_esp;
    PUSH32(g_esp, 0);
    RECOMP_ICALL_SAFE(callback, saved_esp);
    g_esp = saved_esp;
    (void)eax;
}

void nfl2k5_trace_worker_counter_acquire(void)
{
    nfl2k5_worker_counter_acquire_caller = MEM32(g_esp);
    InterlockedIncrement(&nfl2k5_worker_counter_acquires);
}

void nfl2k5_trace_frontend(uint32_t queue, uint32_t pending,
                           uint32_t head, uint32_t tail)
{
    nfl2k5_frontend_queue = queue;
    nfl2k5_frontend_pending = pending;
    nfl2k5_frontend_head = head;
    nfl2k5_frontend_tail = tail;
    if (queue >= 0x00010000u && queue < 0x04000000u &&
        queue != 0x00AF57C8u) {
        InterlockedIncrement(&nfl2k5_frontend_nonempty_samples);
        nfl2k5_frontend_request = MEM32(queue + 0x78u);
        nfl2k5_frontend_active = MEM32(queue + 0x7Cu);
        nfl2k5_frontend_state = MEM32(queue + 0x8Cu);
    }
    InterlockedIncrement(&nfl2k5_frontend_samples);
}

void nfl2k5_trace_frontend_prepare(uint32_t result)
{
    nfl2k5_frontend_prepare_result = result;
    InterlockedIncrement(&nfl2k5_frontend_prepare_calls);
    if (result)
        InterlockedIncrement(&nfl2k5_frontend_prepare_ok);
}

void nfl2k5_trace_frontend_submit(uint32_t result, uint32_t status)
{
    nfl2k5_frontend_submit_result = result;
    nfl2k5_frontend_submit_status = status;
    InterlockedIncrement(&nfl2k5_frontend_submit_calls);
    if (result)
        InterlockedIncrement(&nfl2k5_frontend_submit_ok);
}

void nfl2k5_trace_frontend_range(uint32_t source, uint32_t size,
                                 uint32_t low, uint32_t high,
                                 uint32_t end_low, uint32_t end_high,
                                 uint32_t request, uint32_t provider)
{
    nfl2k5_frontend_range_source = source;
    nfl2k5_frontend_range_size = size;
    nfl2k5_frontend_range_low = low;
    nfl2k5_frontend_range_high = high;
    nfl2k5_frontend_range_end_low = end_low;
    nfl2k5_frontend_range_end_high = end_high;
    nfl2k5_frontend_range_request = request;
    nfl2k5_frontend_range_provider = provider;
}

void nfl2k5_trace_frontend_state(uint32_t state)
{
    if (state < 9u)
        InterlockedIncrement(&nfl2k5_frontend_state_counts[state]);
}

void nfl2k5_trace_title_init(uint32_t stage)
{
    InterlockedExchange(&nfl2k5_title_init_checkpoint, (LONG)stage);
    if (stage == 1u) InterlockedIncrement(&nfl2k5_title_init_74bf0_calls);
    if (stage == 2u) InterlockedIncrement(&nfl2k5_title_init_38fc0_calls);
    if (stage == 3u) InterlockedIncrement(&nfl2k5_title_init_network_calls);
}

void nfl2k5_trace_async_item(uint32_t item, uint32_t type, uint32_t provider)
{
    nfl2k5_async_item = item;
    nfl2k5_async_item_type = type;
    nfl2k5_async_provider = provider;
    InterlockedIncrement(&nfl2k5_async_items);
}

void nfl2k5_trace_async_dispatch(uint32_t type, uint32_t target)
{
    nfl2k5_async_dispatch_type = type;
    nfl2k5_async_dispatch_target = target;
    InterlockedIncrement(&nfl2k5_async_dispatches);
}

void nfl2k5_trace_async_dispatch_branch(uint32_t branch)
{
    nfl2k5_async_dispatch_branch = branch;
}

void nfl2k5_trace_archive_init_44c10(uint32_t context, uint32_t source)
{
    nfl2k5_archive_init_44c10_context = context;
    nfl2k5_archive_init_44c10_source = source;
    InterlockedIncrement(&nfl2k5_archive_init_44c10_calls);
}

void nfl2k5_trace_archive_setup_44d00(uint32_t counter)
{
    nfl2k5_archive_setup_44d00_counter = counter;
    InterlockedIncrement(&nfl2k5_archive_setup_44d00_calls);
}

void nfl2k5_trace_boot_task_3be40(uint32_t owner, uint32_t record, uint32_t completion)
{
    nfl2k5_boot_task_3be40_owner = owner;
    nfl2k5_boot_task_3be40_record = record;
    nfl2k5_boot_task_3be40_completion = completion;
    InterlockedIncrement(&nfl2k5_boot_task_3be40_calls);
}

void nfl2k5_trace_boot_task_42440(uint32_t result_slot)
{
    nfl2k5_boot_task_42440_result_slot = result_slot;
    InterlockedIncrement(&nfl2k5_boot_task_42440_calls);
}

void nfl2k5_trace_boot_task_3be40_ready(uint32_t record)
{
    nfl2k5_boot_task_3be40_state = MEM32(record + 8u);
    nfl2k5_boot_task_3be40_type = MEM32(record + 0xCu);
    nfl2k5_boot_task_3be40_target = MEM32(record + 0x10u);
    nfl2k5_boot_task_3be40_argument = MEM32(record + 0x14u);
    nfl2k5_boot_task_3be40_dispatch_lock_before = MEM32(0x00B057D0u);
}

void nfl2k5_trace_boot_task_3be40_return(void)
{
    nfl2k5_boot_task_3be40_dispatch_lock_after = MEM32(0x00B057D0u);
}

void nfl2k5_trace_archive_dispatch_438d0(uint32_t callback, uint32_t tag)
{
    nfl2k5_archive_dispatch_438d0_callback = callback;
    nfl2k5_archive_dispatch_438d0_tag = tag;
    InterlockedIncrement(&nfl2k5_archive_dispatch_438d0_calls);
}

void nfl2k5_trace_archive_dispatch_438d0_result(uint32_t result)
{
    nfl2k5_archive_dispatch_438d0_result = result;
}

/* The frontend's first asynchronous request is an offset-based descriptor,
 * not a host pointer range.  On Xbox the provider accepts this zero-aperture
 * form; the bridge's partially initialized provider advertises range checks
 * and rejects it with status 0x13 before the title worker sees it. */
int nfl2k5_allow_zero_frontend_range(uint32_t request, uint32_t provider)
{
    if (request < 0x01000000u || request >= 0x04000000u)
        return 0;
    if (provider != 0x00A77788u)
        return 0;
    if (MEM32(request + 8) != 0 || MEM32(request + 0xC) != 0)
        return 0;
    if (MEM32(request + 0x18) > 0x10000u)
        return 0;
    return 1;
}

void nfl2k5_trace_scheduler(void)
{
    InterlockedIncrement(&nfl2k5_scheduler_samples);
}

void nfl2k5_trace_scheduler_callback(uint32_t callback)
{
    nfl2k5_scheduler_callback = callback;
}

void nfl2k5_trace_frontend_event_4953c4(void)
{
    InterlockedIncrement(&nfl2k5_frontend_event_4953c4);
}

void nfl2k5_trace_frontend_event_492e9b(void)
{
    InterlockedIncrement(&nfl2k5_frontend_event_492e9b);
}

void nfl2k5_trace_frontend_event_49582b(void)
{
    InterlockedIncrement(&nfl2k5_frontend_event_49582b);
}
void nfl2k5_trace_frontend_event_492414(void)
{
    InterlockedIncrement(&nfl2k5_frontend_event_492414);
}

void nfl2k5_trace_frontend_event_48bb78(void)
{
    InterlockedIncrement(&nfl2k5_frontend_event_48bb78);
}
void nfl2k5_trace_frontend_packet_alloc(uint32_t value)
{
    nfl2k5_frontend_packet_alloc = value;
}

void nfl2k5_trace_frontend_packet_submit(uint32_t value)
{
    nfl2k5_frontend_packet_submit = value;
}


void nfl2k5_trace_packet_pump_49516f(void)
{
    InterlockedIncrement(&nfl2k5_packet_pump_49516f);
}

void nfl2k5_trace_packet_processor_48e0a6(void)
{
    InterlockedIncrement(&nfl2k5_packet_processor_48e0a6);
}

void nfl2k5_trace_packet_dpc_enter(uint32_t context, uint32_t pending,
                                   uint32_t limit, uint32_t list_head)
{
    nfl2k5_packet_dpc_context = context;
    nfl2k5_packet_dpc_pending = pending;
    nfl2k5_packet_dpc_limit = limit;
    nfl2k5_packet_dpc_list_head = list_head;
    InterlockedIncrement(&nfl2k5_packet_dpc_entries);
}

void nfl2k5_trace_packet_dpc_gate(uint32_t result)
{
    nfl2k5_packet_dpc_gate_result = result;
    if (result)
        InterlockedIncrement(&nfl2k5_packet_dpc_gate_open);
}

void nfl2k5_trace_packet_dpc_handler(uint32_t item, uint32_t flags,
                                     uint32_t target)
{
    nfl2k5_packet_dpc_handler_item = item;
    nfl2k5_packet_dpc_handler_flags = flags;
    nfl2k5_packet_dpc_handler_target = target;
    InterlockedIncrement(&nfl2k5_packet_dpc_handler_calls);
}

void nfl2k5_trace_packet_dpc_dequeue(uint32_t item, uint32_t flags,
                                     uint32_t target, uint32_t link,
                                     uint32_t metadata, uint32_t owner_flags)
{
    nfl2k5_packet_dpc_dequeue_item = item;
    nfl2k5_packet_dpc_dequeue_flags = flags;
    nfl2k5_packet_dpc_dequeue_target = target;
    nfl2k5_packet_dpc_dequeue_link = link;
    nfl2k5_packet_dpc_dequeue_metadata = metadata;
    nfl2k5_packet_dpc_owner_flags = owner_flags;
    InterlockedIncrement(&nfl2k5_packet_dpc_dequeue_calls);
}

void nfl2k5_trace_packet_buffer_install(uint32_t context, uint32_t buffer)
{
    nfl2k5_packet_buffer_install_context = context;
    nfl2k5_packet_buffer_install_buffer = buffer;
    InterlockedIncrement(&nfl2k5_packet_buffer_install_calls);
}

void nfl2k5_trace_packet_delivery(uint32_t context, uint32_t item,
                                  uint32_t route, uint32_t target)
{
    nfl2k5_packet_delivery_context = context;
    nfl2k5_packet_delivery_item = item;
    nfl2k5_packet_delivery_route = route;
    nfl2k5_packet_delivery_target = target;
    InterlockedIncrement(&nfl2k5_packet_delivery_calls);
}

void nfl2k5_trace_packet_fallback(uint32_t context, uint32_t item,
                                  uint32_t length, uint32_t alloc_fn,
                                  uint32_t free_fn, uint32_t block,
                                  uint32_t after_alloc)
{
    nfl2k5_packet_fallback_context = context;
    nfl2k5_packet_fallback_item = item;
    nfl2k5_packet_fallback_length = length;
    nfl2k5_packet_fallback_alloc_fn = alloc_fn;
    nfl2k5_packet_fallback_free_fn = free_fn;
    nfl2k5_packet_fallback_block = block;
    nfl2k5_packet_fallback_after_alloc = after_alloc;
    InterlockedIncrement(&nfl2k5_packet_fallback_calls);
}

void nfl2k5_trace_frontend_packet_route(uint32_t route, uint32_t token, uint32_t flags)
{
    nfl2k5_frontend_packet_route = route;
    nfl2k5_frontend_packet_token = token;
    nfl2k5_frontend_packet_flags = flags;
    if (route < 5) {
        InterlockedIncrement(&nfl2k5_frontend_packet_route_counts[route]);
    }
}
void nfl2k5_trace_frontend_producer(uint32_t producer)
{
    if (producer == 0x00178150u)
        InterlockedIncrement(&nfl2k5_frontend_producer_178150);
    else if (producer == 0x00272A60u)
        InterlockedIncrement(&nfl2k5_frontend_producer_272a60);
}

void nfl2k5_trace_frontend_enqueue(uint32_t caller, uint32_t node, uint32_t descriptor)
{
    nfl2k5_frontend_enqueue_caller = caller;
    nfl2k5_frontend_enqueue_node = node;
    nfl2k5_frontend_enqueue_descriptor = descriptor;
    InterlockedIncrement(&nfl2k5_frontend_enqueue_calls);
}
void nfl2k5_trace_frontend_boot(uint32_t stage, uint32_t result)
{
    nfl2k5_frontend_boot_stage = stage;
    nfl2k5_frontend_boot_result = result;
    InterlockedIncrement(&nfl2k5_frontend_boot_steps);
}

/* These are memory-only markers around the translated indirect call.  They
 * distinguish a dispatcher that cannot enter a callback from one whose
 * callback never returns, without altering the guest callback's ABI. */
void nfl2k5_trace_scheduler_callback_enter(uint32_t callback)
{
    nfl2k5_scheduler_callback_entered = callback;
    InterlockedIncrement(&nfl2k5_scheduler_callback_entries);
}

void nfl2k5_trace_scheduler_callback_return(uint32_t callback)
{
    nfl2k5_scheduler_callback_returned = callback;
    InterlockedIncrement(&nfl2k5_scheduler_callback_returns);
}

void nfl2k5_trace_scheduler_registration_attempt(uint32_t count,
                                                 uint32_t callback,
                                                 uint32_t caller)
{
    LONG slot = InterlockedIncrement(&nfl2k5_scheduler_registration_history_index) - 1;
    unsigned index = (unsigned)slot & 15u;
    nfl2k5_scheduler_registration_history_count[index] = count;
    nfl2k5_scheduler_registration_history_callback[index] = callback;
    nfl2k5_scheduler_registration_history_caller[index] = caller;
    nfl2k5_scheduler_registration_callback = callback;
    nfl2k5_scheduler_registration_caller = caller;
    InterlockedIncrement(&nfl2k5_scheduler_registration_attempts);
}

void nfl2k5_trace_input_init(uint32_t stage)
{
    InterlockedExchange(&nfl2k5_input_init_stage, (LONG)stage);
}

void nfl2k5_trace_network_init(uint32_t stage, uint32_t status,
                               uint32_t version)
{
    nfl2k5_network_init_status = status;
    nfl2k5_network_init_version = version;
    InterlockedExchange(&nfl2k5_network_init_stage, (LONG)stage);
}

void nfl2k5_trace_invalid_queue_object(uint32_t object, uint32_t record)
{
    nfl2k5_invalid_queue_object = object;
    nfl2k5_invalid_queue_record = record;
    InterlockedIncrement(&nfl2k5_invalid_queue_objects);
}
typedef void (*recomp_func_t)(void);
extern RECOMP_TLS uint32_t g_eax, g_esp, g_ecx, g_edx, g_ebx, g_esi, g_edi;
extern void xbox_bridge_signal_worker_completion(void);
extern void xbox_bridge_wait_worker_completion(unsigned long timeout_ms);
extern void xbox_bridge_service_scheduler_now(void);
extern void xbox_bridge_note_scheduler_ready(void);
extern void xbox_bridge_drive_scheduler_until(uint32_t target_count,
                                               unsigned long timeout_ms);

void nfl2k5_signal_worker_completion(void)
{
    xbox_bridge_signal_worker_completion();
}

void nfl2k5_wait_worker_completion(void)
{
    xbox_bridge_wait_worker_completion(4);
}

void nfl2k5_trace_worker_task(uint32_t target)
{
    nfl2k5_worker_task_target = target;
    InterlockedIncrement(&nfl2k5_worker_task_dispatches);
}

/* Ad hoc diagnostic for the KeDelayExecutionThread hot-retry loop found via
 * kernel_bridge.c's [KERNEL] summary (2026-09-17): thousands of ordinal-99
 * calls per second, always returning to guest address 0x0001B632, which is
 * inside sub_0001B601. That function only calls the delay once per normal
 * invocation, so the fast repetition means *something* keeps re-invoking it
 * (or the retry branch inside it) very quickly. This logs each entry's
 * this-pointer, both stack params, and the caller's return address, capped
 * at 20 lines, gated behind RECOMP_WAIT_TRACE so it costs nothing normally. */
void nfl2k5_trace_sub_0001B601_entry(uint32_t ecx_this, uint32_t param1,
                                      uint32_t param2, uint32_t ret_addr)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20)
        fprintf(stderr, "  [WAIT] sub_0001B601 call #%ld ecx(this)=0x%08X "
                "param1=0x%08X param2=0x%08X caller_ret=0x%08X\n",
                n, ecx_this, param1, param2, ret_addr);
}

/* Same technique as nfl2k5_trace_sub_0001B601_entry, for the second hot
 * spin observed at a different run (sub_00016CFF -> ObReferenceObjectByHandle
 * at 0x16D15). Logs the handle param and the caller's return address, capped
 * at 20 lines, gated behind RECOMP_WAIT_TRACE. */
void nfl2k5_trace_sub_00016CFF_entry(uint32_t param1, uint32_t ret_addr)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20)
        fprintf(stderr, "  [WAIT] sub_00016CFF call #%ld handle_param=0x%08X "
                "caller_ret=0x%08X\n", n, param1, ret_addr);
}

/* One more hop up from nfl2k5_trace_sub_00016CFF_entry: who keeps calling
 * sub_003CAED0 (the guarded "boost my own thread's priority" routine) at
 * over a thousand times a second. */
void nfl2k5_trace_sub_003CAED0_entry(uint32_t ret_addr)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20)
        fprintf(stderr, "  [WAIT] sub_003CAED0 call #%ld caller_ret=0x%08X\n",
                n, ret_addr);
}

/* One more hop up from nfl2k5_boot_registration: who keeps calling
 * sub_003D58B0 (the generic "if a callback is registered at 0xCC7A9C, call
 * it" pump) so fast. */
void nfl2k5_trace_sub_003D58B0_entry(uint32_t ret_addr)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20)
        fprintf(stderr, "  [WAIT] sub_003D58B0 call #%ld caller_ret=0x%08X\n",
                n, ret_addr);
}

/* One more hop: who calls sub_003D58F0 ("event 1" -> sub_003D58B0 pump) so
 * fast. 12 call sites in the generated code, so trace the callee's own
 * entry instead of each site individually. */
void nfl2k5_trace_sub_003D58F0_entry(uint32_t ret_addr)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20)
        fprintf(stderr, "  [WAIT] sub_003D58F0 call #%ld caller_ret=0x%08X\n",
                n, ret_addr);
}

/* Traces the source side of the rep-movsd-style vertex copy in
 * sub_004251A0 (recomp_0028.c:279534), right before the copy loop reads
 * from it. This session traced the *destination* (the pushbuffer's
 * INLINE_ARRAY words) to exactly this copy via a write-watchpoint
 * (RECOMP_WATCH_PB_WRITE) and found every destination word was zero; this
 * answers the next question directly -- is the source itself zero (a data
 * problem further upstream, e.g. an unfilled vertex buffer), or does the
 * source hold real data that the copy somehow fails to transfer (a bug in
 * this copy or its address computation)? device_field_bcc/bd0 are
 * MEM32(0x4409A8+0xBCC)/(+0xBD0) -- the two device-struct fields this
 * function's source-address arithmetic is built from (see recomp_0028.c
 * around line 279445), dumped raw so a zero/non-zero source pointer itself
 * is visible even if the dereferenced words are misleading. */
/* Gates sub_004308C0's whole vertex-source-setup body (recomp_0029.c
 * loc_004308DC): bit 0x20 of MEM32(0x440508) must be set or the function
 * no-ops and device+0xBCC (traced separately, nfl2k5_trace_vertex_copy_
 * source) never gets computed -- which this session found stays 0, causing
 * the zero-filled INLINE_ARRAY vertex data. This answers whether the flag
 * itself is the problem. */
/* Per-slot vertex-stream table check inside sub_004308C0's 16-iteration
 * loop (recomp_0029.c loc_00430931). If type_value==2 for a slot, that
 * slot is skipped entirely; the *first* slot where it is not 2 is what
 * ends up initializing MEM32(esp+0x20) -> device+0xBCC (see
 * nfl2k5_trace_vertex_copy_source). If every slot reads 2, that field
 * never gets set at all, and device+0xBCC keeps whatever was already on
 * the stack -- which this session already confirmed comes out as 0. */
/* The descriptor whose +0x18 field becomes device+0xBCC (the vertex data
 * source pointer) for the first non-skipped stream slot. If data_ptr is 0
 * here, that's the actual, final root: this specific descriptor record's
 * own data pointer was never set to a real vertex buffer address, by
 * whatever guest code is supposed to bind one (not yet identified -- this
 * is as far as this session traced). */
/* sub_004290C0's own single parameter. Bit 0 decides everything: clear ->
 * calls sub_00428010 (binds the vertex stream descriptor, the path that
 * fills device+0xBCC with real data -- see nfl2k5_trace_stream_descriptor);
 * set -> calls sub_00428C90/sub_00428D60 instead and returns without ever
 * binding anything. This traces which one actually happens and with what
 * value, at the root of the whole zero-vertex-data chain this session
 * followed down from the pushbuffer. */
/* sub_0042F9A0's own gate (recomp_0029.c:20711): reads MEM8(device+0x794's
 * table + 4) and skips its entire main body (the part that walks the
 * stream table and emits real per-stream pushbuffer setup) if that byte
 * has any bit in mask 0x12 set. sub_00428010(ebx=4)'s own branch (traced
 * earlier, ebx=esi=4 case) ORs bit 0x2 into this exact field. If that's
 * what's happening, this function's real work is being skipped by a flag
 * its own sibling call sets -- the same "flag says skip real setup" shape
 * as several other things found tonight, just one level further out. */
/* sub_00427890's own entry: the ~800-line device/graphics init function
 * this whole night's investigation traces back to. Retail's own repeated
 * sub_004290C0(param=4) hits (15 in a row, one call site) proved this
 * function's own caller must invoke it more than once on retail; native's
 * every capture ever taken shows it running exactly once. This traces
 * how many times it's actually entered in native, and from where. */
/* The actual HRESULT-style return value of sub_00427890 (device init),
 * captured at its caller (sub_00420160, recomp_0028.c:267178) right where
 * the success/fail branch happens. Negative = failure -> the caller wipes
 * the entire device struct (2344 dwords at 0x4409B0) and nulls
 * MEM32(0x4409A8). This is the most direct possible check of whether
 * device creation itself is failing in native. */
/* sub_00420160's own caller -- the level above where this session's
 * device-init thread currently stops. Vblank synthesis (tested, ruled
 * out) doesn't change this function's own call count, so whatever paces
 * it is something else entirely; this is the next hop to find it. */
void nfl2k5_trace_device_setup_entry(uint32_t caller_ret)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20)
        fprintf(stderr, "  [VSETUP] sub_00420160 call #%ld caller_ret=0x%08X\n",
                n, caller_ret);
}

void nfl2k5_trace_device_init_result(uint32_t hresult)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20)
        fprintf(stderr, "  [VDEVRESULT] sub_00427890 call #%ld returned 0x%08X (%s)\n",
                n, hresult, ((int32_t)hresult < 0) ? "FAILED" : "succeeded");
}

void nfl2k5_trace_device_init_entry(uint32_t caller_ret, uint32_t ecx_val)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20)
        fprintf(stderr, "  [VDEVINIT] sub_00427890 call #%ld caller_ret=0x%08X ecx(this)=0x%08X\n",
                n, caller_ret, ecx_val);
}

void nfl2k5_trace_stream_process_gate(uint32_t table_addr, uint32_t flags_byte)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20)
        fprintf(stderr, "  [VPROC] sub_0042F9A0 call #%ld table=0x%08X flags_byte=0x%02X "
                "mask0x12_test=%s -> %s\n",
                n, table_addr, flags_byte, (flags_byte & 0x12) ? "NONZERO" : "zero",
                (flags_byte & 0x12) ? "SKIPS main body" : "runs main body");
}

void nfl2k5_trace_stream_bind_param(uint32_t ebx_val)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20)
        fprintf(stderr, "  [VBIND] sub_004290C0 param=0x%08X bit0=%s -> %s\n",
                ebx_val, (ebx_val & 1) ? "SET" : "clear",
                (ebx_val & 1) ? "skips stream bind (sub_00428C90/D60)"
                              : "BINDS stream (sub_00428010)");
}

/* 2026-09-21: sub_00074790 (recomp_0003.c) computes its "keep looping" flag
 * from sub_00038F50()'s result into `edi` near its own top, then reads it
 * back ~30 calls later right before returning -- and `edi` is thread-local
 * storage (g_edi). This codebase uses real OS threads for cooperative guest
 * scheduling (see the worker-entry/bridge counters), so if anything in
 * between causes a switch to a different host thread before the function
 * returns, the final read would see that OTHER thread's own (likely zero/
 * stale) g_edi, not the value this call chain actually computed -- with no
 * memory write involved anywhere, which is why RECOMP_DATA_WATCH/HW_WATCH
 * on the quit flag found nothing: the bug isn't in memory, it's in which
 * thread's register state gets read. Confirmed/refuted by comparing the
 * thread ID at both points, opt-in via RECOMP_FRAME_LOOP_TRACE=1. */
/* 2026-09-21: sub_00028F70 has 17 static call sites total across the
 * generated code (recomp_0003/0007/0008/0010/0011/0016.c). The live
 * hybrid-xemu reference showed one specific site (0x0007481B, inside
 * sub_00074790) as the 100%-hot caller during continuous gameplay, but
 * that was never independently verified on native -- if native reaches
 * sub_00028F70 through a DIFFERENT one of those 17 sites, everything
 * downstream traced from "it's called via sub_00074790" would be moot. */
void nfl2k5_trace_28f70_caller(uint32_t return_addr)
{
    fprintf(stderr, "  [28F70CALLER] return_addr=0x%08X\n", return_addr);
    fflush(stderr);
}

void nfl2k5_trace_frame_loop_flag_set(uint32_t edi_value)
{
    /* Env-var gate temporarily removed for a reachability diagnostic --
     * 2026-09-21, see PROJECT_STATUS.md. */
    fprintf(stderr, "  [FRAMELOOP] flag set: tid=%lu edi=0x%08X\n",
            (unsigned long)GetCurrentThreadId(), edi_value);
    fflush(stderr);
}

void nfl2k5_trace_frame_loop_flag_check(uint32_t edi_value)
{
    fprintf(stderr, "  [FRAMELOOP] flag check: tid=%lu edi=0x%08X\n",
            (unsigned long)GetCurrentThreadId(), edi_value);
    fflush(stderr);
}

void nfl2k5_trace_stream_descriptor(uint32_t descriptor_addr, uint32_t data_ptr)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20)
        fprintf(stderr, "  [VDESC] descriptor@0x%08X data_ptr(+0x18)=0x%08X%s\n",
                descriptor_addr, data_ptr, data_ptr == 0 ? "  <-- NULL" : "");
}

void nfl2k5_trace_stream_slot_type(uint32_t slot_index, uint32_t type_value)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 40)
        fprintf(stderr, "  [VSLOT] slot=%u type=0x%08X%s\n", slot_index, type_value,
                type_value == 2 ? "  (skipped)" : "  <-- NOT skipped");
}

void nfl2k5_trace_vertex_setup_gate(uint32_t flags_value, uint32_t esi_val)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20)
        fprintf(stderr, "  [VGATE] call #%ld flags=0x%08X bit0x20=%s esi(device?)=0x%08X\n",
                n, flags_value, (flags_value & 0x20) ? "SET" : "clear", esi_val);
}

void nfl2k5_trace_vertex_copy_source(uint32_t esi_val, uint32_t edi_val,
                                      uint32_t edx_count, uint32_t device_field_bcc,
                                      uint32_t device_field_bd0)
{
    static volatile LONG trace_count;
    LONG n;
    if (!getenv("RECOMP_WAIT_TRACE"))
        return;
    n = InterlockedIncrement(&trace_count);
    if (n <= 20) {
        unsigned i;
        fprintf(stderr, "  [VCOPY] call #%ld esi(src)=0x%08X edi(dst)=0x%08X "
                "edx(outer_count)=0x%08X device+0xBCC=0x%08X device+0xBD0=0x%08X\n",
                n, esi_val, edi_val, edx_count, device_field_bcc, device_field_bd0);
        fprintf(stderr, "  [VCOPY] source words:");
        for (i = 0; i < 8; i++)
            fprintf(stderr, " %08X", MEM32(esi_val + i * 4));
        fprintf(stderr, "\n");
        fflush(stderr);
    }
}

void nfl2k5_trace_worker_loop(int draining)
{
    if (draining)
        InterlockedIncrement(&nfl2k5_worker_drain_calls);
    else
        InterlockedIncrement(&nfl2k5_worker_loop_entries);
}

void nfl2k5_trace_worker_entry(uint32_t target)
{
    nfl2k5_worker_entry_target = target;
    InterlockedIncrement(&nfl2k5_worker_entry_calls);
    if (target == 0x000359B0u)
        InterlockedIncrement(&nfl2k5_worker_entry_359b0);
    else if (target == 0x0004D810u)
        InterlockedIncrement(&nfl2k5_worker_entry_4d810);
    else
        InterlockedIncrement(&nfl2k5_worker_entry_other);
}

void nfl2k5_trace_worker_bridge_start(uint32_t context)
{
    nfl2k5_worker_bridge_context = context;
    InterlockedIncrement(&nfl2k5_worker_bridge_starts);
}

/* XBE 00044DF0 is an asynchronous archive-completion callback that the
 * function finder missed because it tail-jumps into the existing resource
 * handlers.  Preserve those tail calls: the handlers consume the return
 * address and argument already on the guest stack. */
static void nfl2k5_archive_completion_44df0(void)
{
    nfl2k5_archive_completion_44df0_context = g_ecx;
    InterlockedIncrement(&nfl2k5_archive_completion_44df0_calls);
    /* This is invoked only through the manual-override lookup table (an
     * indirect call), so its caller has to be read off the guest stack the
     * same way as every other return-address trace this session -- same
     * technique that found nfl2k5_boot_registration's and sub_003D58B0's
     * callers. Targeting the specific, long-standing "~23 calls" plateau
     * directly, with RECOMP_SYNTHESIZE_VBLANK active as a control (this
     * session's earlier experiment already showed a working vblank/DPC
     * heartbeat does not by itself move this counter). */
    if (getenv("RECOMP_WAIT_TRACE")) {
        static volatile LONG trace_count;
        LONG n = InterlockedIncrement(&trace_count);
        if (n <= 30)
            fprintf(stderr, "  [WAIT] archive_completion_44df0 call #%ld "
                    "ecx=0x%08X caller_ret=0x%08X\n",
                    n, g_ecx, MEM32(g_esp));
    }
    g_eax = MEM32(0x00B120E0u);
    if (g_eax != 0) {
        g_edx = MEM32(0x00B120D0u);
        g_ecx = MEM32(0x00B120E4u);
        PUSH32(g_esp, 0x00044E0Au);
        sub_0004DC00();
    }

    g_eax = MEM32(0x00B120ECu);
    if ((int32_t)g_eax > 0) {
        g_ecx = MEM32(0x00B120D4u);
        g_eax = MEM32(0x00B120DCu);
        g_edx = g_eax + g_ecx;
        g_ecx = MEM32(0x00B120D0u);
        PUSH32(g_esp, 0x00044E2Cu);
        sub_00048760();
    }

    /* Both terminal paths use this completion pair.  The special HITX path
     * only selects the flag-setting wrapper; it does not own the callback
     * setup.  Leaving these writes inside that branch made the ordinary
     * archive path reuse the event argument (B09598) as a code address. */
    g_ecx = MEM32(0x00B120D0u);
    g_edx = 0x00044DA0u;
    MEM32(g_esp + 4u) = 0x00044DC0u;
    if (MEM32(g_ecx + 0xCu) == 0x58544948u) { /* "HITX" */
        sub_00043E50();
    } else {
        sub_00043E10();
    }
}

/* Companion completion callback for the second archive queue.  Like 44DF0,
 * it was missed because its final instruction is a tail jump to 43E10. */
static void nfl2k5_archive_completion_44bb0(void)
{
    g_eax = MEM32(0x00B12084u);
    if (g_eax != 0) {
        g_edx = MEM32(0x00B1206Cu);
        g_ecx = MEM32(0x00B1207Cu);
        PUSH32(g_esp, 0x00044BCAu);
        sub_0004DC00();
    }

    g_eax = MEM32(0x00B12088u);
    if ((int32_t)g_eax > 0) {
        g_ecx = MEM32(0x00B12078u);
        g_eax = MEM32(0x00B12070u);
        g_edx = g_ecx + g_eax;
        g_ecx = MEM32(0x00B1206Cu);
        PUSH32(g_esp, 0x00044BECu);
        sub_00048760();
    }

    g_ecx = MEM32(0x00B1206Cu);
    MEM32(g_esp + 4u) = 0x00044B80u;
    g_edx = 0x00044B60u;
    sub_00043E10();
}

/* XBE 00045A20 is the completion callback for the third archive queue.  It
 * has the same shape as 44DF0, but chooses the HSiN handler on its terminal
 * record.  The generated-function finder did not emit it because both final
 * branches are tail jumps into existing resource handlers. */
static void nfl2k5_archive_completion_45a20(void)
{
    g_eax = MEM32(0x00B12250u);
    if (g_eax != 0) {
        g_edx = MEM32(0x00B12238u);
        g_ecx = MEM32(0x00B12248u);
        PUSH32(g_esp, 0x00045A3Au);
        sub_0004DC00();
    }

    g_eax = MEM32(0x00B12254u);
    if ((int32_t)g_eax > 0) {
        g_ecx = MEM32(0x00B12240u);
        g_eax = MEM32(0x00B12244u);
        g_edx = g_eax + g_ecx;
        g_ecx = MEM32(0x00B12238u);
        PUSH32(g_esp, 0x00045A5Cu);
        sub_00048760();
    }

    g_ecx = MEM32(0x00B12238u);
    g_edx = 0x000459D0u;
    MEM32(g_esp + 4u) = 0x000459E0u;
    if (MEM32(g_ecx + 0xCu) == 0x6E536948u) /* "HSiN" */
        sub_00043E50();
    else
        sub_00043E10();
}

/* 45A20 installs this as the completion predicate for its terminal archive
 * record.  It is a three-instruction tail-call thunk, omitted from generated
 * output because it begins in padding between detected functions. */
static void nfl2k5_archive_completion_459d0(void)
{
    g_ecx = MEM32(g_ecx + 0x14u);
    g_edx = MEM32(0x00B1223Cu);
    sub_0002F140();
}

/* Terminal callback for the MRKS archive records.  This starts inside an
 * undetected gap immediately before 168C90 and tail-jumps to the existing
 * marker-resource completion handler. */
static void nfl2k5_marker_completion_168c70(void)
{
    MEM32(g_ecx + 0xCu) = 0x534B524Du; /* "MRKS" */
    g_edx = MEM32(0x00BDB934u);
    g_ecx = MEM32(g_ecx + 0x14u);
    sub_00151650();
}

/* Completion callback for the archive range queued by 45650.  It advances
 * the original 64-bit stream cursor then tail-dispatches the record through
 * the shared resource handler. */
static void nfl2k5_archive_completion_45600(void)
{
    uint32_t cursor = g_edx;
    uint64_t value = (uint64_t)MEM32(cursor) |
                     ((uint64_t)MEM32(cursor + 4u) << 32);

    g_eax = MEM32(0x00B121B4u);
    g_ecx = MEM32(0x00B121A4u);
    value += g_eax;
    MEM32(cursor) = (uint32_t)value;
    MEM32(cursor + 4u) = (uint32_t)(value >> 32);

    MEM32(g_esp + 4u) = 0x000455D0u;
    g_edx = 0x000455A0u;
    sub_00043E10();
}

/* XBE 00045100 is the completion fan-out for the HSET archive queue.  The
 * automatic finder omitted it because all four record paths terminate in
 * resource handlers.  It is important that every record uses the original
 * handler/flag combination: those handlers own the queue wakeup that moves
 * the frontend past its loading state. */
static void nfl2k5_archive_completion_45100(void)
{
    uint32_t queue;
    uint32_t index;
    uint32_t count;

    /* This function's own comment says its handlers "own the queue wakeup
     * that moves the frontend past its loading state" -- the most direct
     * lead this session has found for what actually needs to happen after
     * the 44DF0 completion hand-off. Trace entry/loop-bounds/exit to see
     * whether it's even reached, and how many records it processes. */
    if (getenv("RECOMP_WAIT_TRACE")) {
        static volatile LONG entry_count;
        LONG n = InterlockedIncrement(&entry_count);
        if (n <= 10)
            fprintf(stderr, "  [WAIT] archive_completion_45100 call #%ld "
                    "caller_ret=0x%08X queue_ptr_field=0x%08X\n",
                    n, MEM32(g_esp), MEM32(0x00B1211Cu));
    }

    g_eax = MEM32(0x00B1212Cu);
    if (g_eax != 0) {
        g_edx = MEM32(0x00B1211Cu);
        g_ecx = MEM32(0x00B12130u);
        PUSH32(g_esp, 0x0004511Au);
        sub_0004DC00();
    }

    g_eax = MEM32(0x00B12138u);
    if ((int32_t)g_eax > 0) {
        g_ecx = MEM32(0x00B12128u);
        g_edx = g_ecx + MEM32(0x00B12120u);
        g_ecx = MEM32(0x00B1211Cu);
        PUSH32(g_esp, 0x0004513Cu);
        sub_00048760();
    }

    queue = MEM32(0x00B1211Cu);
    PUSH32(g_esp, g_esi);
    PUSH32(g_esp, g_edi);
    g_edi = queue;
    g_ecx = MEM32(queue);
    if (g_ecx != 0)
        MEM32(queue) = g_ecx + queue - 1u;

    PUSH32(g_esp, 0x00045156u);
    sub_000430E0();
    index = g_eax != 0 ? MEM32(queue + 4u) - 1u : 0u;
    count = MEM32(queue + 4u);

    if (getenv("RECOMP_WAIT_TRACE")) {
        static volatile LONG loop_count;
        LONG n = InterlockedIncrement(&loop_count);
        if (n <= 10)
            fprintf(stderr, "  [WAIT] archive_completion_45100 loop #%ld "
                    "queue=0x%08X index=%d count=%d (sub_430E0 eax=%08X)\n",
                    n, queue, (int32_t)index, (int32_t)count, g_eax);
    }

    for (; (int32_t)index < (int32_t)count; ++index) {
        uint32_t record;
        uint32_t tag;

        PUSH32(g_esp, g_ebx);
        PUSH32(g_esp, 0); /* saved EBP; the bridge has no guest EBP TLS. */
        record = MEM32(queue) + index * 0x24u;
        g_ecx = record;
        g_eax = MEM32(record + 0x20u);
        if (g_eax != 0)
            MEM32(record + 0x20u) = g_eax + record + 0x1Fu;

        tag = MEM32(record + 0xCu);
        g_edx = 0x000450B0u;   /* load callback for every record (was 0x450D0 for all but the last) */
        PUSH32(g_esp, 0x000450D0u);

        if (index < count - 1u) {
            PUSH32(g_esp, 0x000451A5u);
            if (tag == 0x54455348u)
                sub_00043E70();
            else
                sub_00043E30();
        } else {
            PUSH32(g_esp, 0x000451B7u);
            if (tag == 0x54455348u)
                sub_00043E50();
            else
                sub_00043E10();
        }

        /* The stdcall handler consumed its return address and 450D0
         * argument.  The two caller-saved stack slots remain. */
        g_esp += 8;
        count = MEM32(queue + 4u);
    }

    POP32(g_esp, g_edi);
    POP32(g_esp, g_esi);
    g_esp += 8; /* ret 4 */
}

void nfl2k5_service_post_queue(void)
{
    /* Called after sub_00035D50 returns, after sub_00035C70 released the
     * queue lock, and before the title unregisters its frontend callback. */
    xbox_bridge_drain_guest_dpcs();
    xbox_bridge_service_scheduler_now();
}

void nfl2k5_scheduler_registered(uint32_t count)
{
    if (count == 5)
        xbox_bridge_note_scheduler_ready();
}

/* XBE 145B0 is cdecl memmove(dst, src, length). The generated range
 * truncates its reverse-copy jump table, dropping overlapping copies. */
void nfl2k5_guest_memmove(void)
{
    uint32_t dst = MEM32(g_esp + 4), src = MEM32(g_esp + 8);
    uint32_t length = MEM32(g_esp + 12);
    if (length) memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), length);
    g_eax = dst;
    g_esp += 4;
}

int nfl2k5_test_memmove(uint32_t buffer)
{
    const unsigned lengths[] = {0, 1, 3, 7, 31, 64};
    unsigned char expected[128];
    uint32_t initial_esp = g_esp;
    for (unsigned direction = 0; direction < 2; ++direction)
        for (unsigned n = 0; n < sizeof lengths / sizeof lengths[0]; ++n) {
            unsigned src = direction ? 8 : 0, dst = direction ? 0 : 8;
            for (unsigned i = 0; i < 128; ++i) expected[i] = (unsigned char)i;
            memcpy((void *)XBOX_PTR(buffer), expected, sizeof expected);
            memmove(expected + dst, expected + src, lengths[n]);
            PUSH32(g_esp, lengths[n]); PUSH32(g_esp, buffer + src);
            PUSH32(g_esp, buffer + dst); PUSH32(g_esp, 0);
            sub_000145B0();
            if (g_esp != initial_esp - 12 || g_eax != buffer + dst ||
                memcmp((const void *)XBOX_PTR(buffer), expected, sizeof expected)) return 0;
            g_esp += 12;
        }
    return 1;
}

/* These are executable gap/tail targets present in the XBE but omitted from
 * the generated dispatch table. The boot path uses their simple return forms. */
static void nfl2k5_gap_return_zero(void)
{
    g_eax = 0;
    g_esp += 4;
}

/* 004C3207 is the virtual state dispatcher installed by 004C32AA.  It sits
 * immediately after a generated function, so the function finder treated it
 * as that function's end address and omitted it from recomp_dispatch.c.
 * Leaving the vtable entry unresolved makes every state-machine completion
 * silently disappear.  This is a direct translation of 004C3207..004C325B:
 * ECX is the state object, +0x0C selects one of its nine handlers, and the
 * original returns with one stack argument consumed. */
static void nfl2k5_network_state_dispatch_4c3207(void)
{
    uint32_t object = MEM32(g_esp + 4);
    uint32_t state;

    g_ecx = object;
    state = MEM32(object + 0x0Cu);
    g_edx = state;
    g_eax = 0;
    switch (state) {
    case 0: PUSH32(g_esp, 0x004C3221u); sub_004C2EC5(); break;
    case 1: PUSH32(g_esp, 0x004C3228u); sub_004C2F62(); break;
    case 2: PUSH32(g_esp, 0x004C322Fu); sub_004C1B39(); break;
    case 3: PUSH32(g_esp, 0x004C3236u); sub_004C2A44(); break;
    case 4: PUSH32(g_esp, 0x004C323Du); sub_004C1B87(); break;
    case 5: PUSH32(g_esp, 0x004C3244u); sub_004C1C63(); break;
    case 6: PUSH32(g_esp, 0x004C324Bu); sub_004C30FA(); break;
    case 7: PUSH32(g_esp, 0x004C3252u); sub_004C1D0D(); break;
    case 8: PUSH32(g_esp, 0x004C3259u); sub_004C1CBD(); break;
    default: break;
    }
    g_esp += 8; /* ret 4 */
}

/* 00041FF0 is a tiny completion adapter used by the audio/resource worker.
 * Its address is pushed as a callback by 00042010, but its eight instruction
 * body was also skipped by function discovery. */
static void nfl2k5_resource_completion_41ff0(void)
{
    uint32_t completion = g_ecx;
    g_eax = g_edx;
    g_ecx = MEM32(g_eax + 0x14u);
    g_edx = completion ? 0u : 8u;
    sub_0003A4F0(); /* original tail jump consumes our caller return */
}

/* The asynchronous bootstrap probes this compact helper at 001C1FA0.  The
 * scanner skipped it because it is a short chain of tail calls between two
 * aligned functions.  Returning zero for its unresolved target caused the
 * bootstrap to take the wrong ready path and eventually feed an uninitialized
 * renderer work item to 0003F580. */
static void nfl2k5_bootstrap_probe_1c1fa0(void)
{
    PUSH32(g_esp, 0x001C1FA5u); sub_00122EE0();
    if (g_eax) {
        g_eax = MEM32(0x00A9C1FCu); /* tail target 00122FC0 */
        g_esp += 4;
        return;
    }
    PUSH32(g_esp, 0x001C1FB3u); sub_00064BF0();
    if (g_eax) {
        g_ecx = 0;
        sub_0005E7E0(); /* tail jump */
        return;
    }
    PUSH32(g_esp, 0x001C1FBCu); sub_00270BA0();
    if (g_eax) {
        sub_00275F20(); /* tail jump */
        return;
    }
    g_eax = 0x8000u;
    g_esp += 4;
}

/* 003CC280 is the frontend queue pump.  It appears as a callback target in
 * the XBE but was not emitted by the function scanner.  It moves completed
 * resource chunks into the queue that feeds the command writer; without it
 * the writer emits its zero-filled bootstrap primitive forever. */
static void nfl2k5_frontend_queue_pump_3cc280(void)
{
    /* 2026-09-21: empirically checking this function's own stack balance --
     * suspected (not yet confirmed) source of an intermittent esi/edi
     * corruption reaching sub_00038CD0's scheduler dispatch loop, several
     * frames up. See PROJECT_STATUS.md. entry_esp is the guest ESP value
     * right after the `call` pushed the return address (i.e. what this
     * function sees as its own "esp+0" on entry); every exit path below
     * must leave g_esp == entry_esp + 4 (the final `ret`'s own pop) before
     * returning, or something this function calls left the stack
     * unbalanced. */
    uint32_t entry_esp = g_esp;
    InterlockedIncrement(&nfl2k5_frontend_queue_pump_calls);
    PUSH32(g_esp, g_ecx);
    MEM32(0x00AF5890u) = 1;
    if (MEM32(0x00AF588Cu))
        goto out_ecx;

    g_ebx = MEM32(0x00AF5884u);
    PUSH32(g_esp, g_ebx);
    if (g_ebx == 0x00AF57C8u) {
        /* The Xbox scheduler timeslices this permanent worker even while its
         * completed-resource list is empty.  A native Win32 thread otherwise
         * executes the no-work path millions of times per second and starves
         * the producer that will populate the list.  Yield only on the empty
         * sentinel; real records still drain without delay. */
        Sleep(1);
        goto out_ebx;
    }
    PUSH32(g_esp, g_esi);
    PUSH32(g_esp, g_edi);

    while (!MEM32(0x00AF588Cu)) {
        g_esi = MEM32(g_ebx + 0xA4u);
        if (g_esi && !MEM32(g_esi + 0xF4u)) {
            uint32_t result_slot = g_esp + 12u;
            PUSH32(g_esp, result_slot);
            PUSH32(g_esp, 0x003CC2DFu); sub_003CB220();
            if (g_eax) {
                g_ecx = MEM32(result_slot);
                MEM32(g_esi + 0xF8u) = g_ecx;
                MEM32(g_esi + 0xF4u) = 1;
            }
        }
        if (g_esi) {
            g_edi = MEM32(g_esi + 0xCCu);
            if (!MEM32(g_edi)) {
                int32_t delta = (int32_t)(g_edi - g_esi - 0x64u);
                g_ecx = (uint32_t)(delta / 100);
                PUSH32(g_esp, 0x003CC320u); sub_003CB260();
                if (g_eax) {
                    uint32_t ready = MEM32(g_esi + 0x5Cu);
                    uint32_t pending = MEM32(g_esi + 0xF8u);
                    if (!ready || (int32_t)ready < (int32_t)pending) {
                        uint32_t next = MEM32(g_edi + 0x10u);
                        MEM32(g_esi + 0xCCu) = next;
                        MEM32(g_esi + 0x5Cu) = pending;
                        MEM32(g_edi + 4u) = pending;
                        MEM32(g_edi) = 1;
                    }
                    MEM32(g_esi + 0xF4u) = 0;
                }
            }
        }
        g_ebx = MEM32(g_ebx + 0xBCu);
        if (g_ebx == 0x00AF57C8u)
            break;
    }
    POP32(g_esp, g_edi);
    POP32(g_esp, g_esi);
out_ebx:
    POP32(g_esp, g_ebx);
out_ecx:
    MEM32(0x00AF5890u) = 0;
    POP32(g_esp, g_ecx);
    if (g_esp != entry_esp) {
        static volatile LONG warned;
        if (InterlockedIncrement(&warned) <= 5)
            fprintf(stderr, "  [STACKCHECK] 3CC280: g_esp=0x%08X entry_esp=0x%08X (delta=%d)\n",
                    g_esp, entry_esp, (int32_t)(g_esp - entry_esp));
    }
    g_esp += 4;
}

/* The first active 004945A3 state observed in the retail title is state 6.
 * Its original branch at 0049464F submits action 7 when flag 0x10 is set and
 * action 9 otherwise.  Keep the explicit ret 4 because this is a vtable
 * method, not a cdecl callback. */
static void nfl2k5_frontend_state_probe_4945a3(void)
{
    uint32_t state = MEM8(g_ecx + 0x8C8u);
    nfl2k5_frontend_state_dispatch_this = g_ecx;
    nfl2k5_frontend_state_dispatch_value = state;
    nfl2k5_frontend_state_dispatch_tick = MEM8(g_ecx + 0x8C9u);
    nfl2k5_frontend_state_dispatch_limit = MEM8(g_ecx + 0x19u);
    nfl2k5_frontend_state_dispatch_flags = MEM8(g_ecx + 0x8CCu);
    InterlockedIncrement(&nfl2k5_frontend_state_dispatch_calls);
    if (state == 6u) {
        uint32_t action = (MEM8(g_ecx + 0x8CCu) & 0x10u) ? 7u : 9u;
        PUSH32(g_esp, 0);
        PUSH32(g_esp, action);
        PUSH32(g_esp, 0x00494661u); sub_00492E9B();
        g_esp += 8; /* original ret 4 after the helper's ret 8 */
        return;
    }
    if (state == 9u) {
        uint32_t action, arg;
#ifdef NFL2K5_FORCE_UNBLOCK_STATE9_READY
        /* EXPERIMENTAL, 2026-09-21: after tracing this exact readiness bit
         * all the way down through archive registration (works),
         * indirect-callback registration (works), disc-read submission
         * (sub_000439B8, confirmed via exec_watch: NEVER CALLED --
         * unreachable via any static call site, only via an .rdata vtable
         * slot at guest VA 0x00903728 whose actual trigger was not found
         * this session), and a fully-active but unrelated worker dispatch
         * loop -- force this readiness bit as if the resource had loaded,
         * once the tick-based wait has run its course. Not a real fix:
         * the archive/font data itself still never arrives, so whatever
         * consumes it downstream may find it missing/blank. Gated
         * off-by-default; see PROJECT_STATUS.md. */
        if (MEM8(g_ecx + 0x8C9u) >= MEM8(g_ecx + 0x19u))
            MEM8(g_ecx + 5u) |= 4u;
#endif
        if (MEM8(g_ecx + 0x8C9u) < MEM8(g_ecx + 0x19u)) {
            action = state;
            arg = 1u;
        } else if (MEM8(g_ecx + 0x8CCu) & 0x10u) {
            MEM8(g_ecx + 0xA79u) |= 8u;
            action = 12u;
            arg = 0u;
        } else if (MEM8(g_ecx + 5u) & 4u) {
            action = 23u;
            arg = 0u;
        } else {
            MEM8(g_ecx + 0xA7Bu) |= 0xC0u;
            g_eax = 0;
            g_esp += 8;
            {
                static volatile long logged = 0;
                if (++logged <= 5)
                    fprintf(stderr, "  [STATE9] tick>=limit, took give-up branch (bit5=%u) -- MEM8(ecx+0xA7B)|=0xC0\n",
                            MEM8(g_ecx + 5u) & 4u);
            }
            return;
        }
        {
            static volatile long logged2 = 0;
            if (++logged2 <= 10)
                fprintf(stderr, "  [STATE9] action=%u arg=%u (tick=%u limit=%u flags=0x%02X)\n",
                        action, arg, MEM8(g_ecx + 0x8C9u), MEM8(g_ecx + 0x19u), MEM8(g_ecx + 0x8CCu));
        }
        PUSH32(g_esp, arg);
        PUSH32(g_esp, action);
        PUSH32(g_esp, 0x004946FEu); sub_00492E9B();
        g_esp += 8; /* ret 4 */
        return;
    }
    /* 2026-09-22: states 23/24/25 reconstructed directly from the real
     * jump table at guest VA 0x00494855 (26 entries, states 1..26) after
     * NFL2K5_FORCE_UNBLOCK_STATE9_READY let the machine reach state 23 for
     * the first time. Unlike state 9, none of these three depend on an
     * external readiness bit -- 23 is an unconditional reset+advance, and
     * 24/25 are plain tick-vs-limit waits (limit byte at ecx+0x2e for both),
     * so no force-unblock is needed here; they should resolve on their own
     * once enough ticks elapse. State 25's "ready" branch submits action 27,
     * which is outside this table's range (max 26) -- falls through to the
     * generic default below, presumably handled by a different subsystem. */
    if (state == 23u) {
        MEM32(g_ecx + 0x908u) = 0;
        MEM32(g_ecx + 0x930u) = 0;
        MEM8(g_ecx + 0x8CAu) = 0;
        MEM32(g_ecx + 0x8F4u) = 0xFFFFu;
        PUSH32(g_esp, 0);
        PUSH32(g_esp, 24u);
        PUSH32(g_esp, 0x00494852u); sub_00492E9B();
        g_esp += 8; /* ret 4 */
        return;
    }
    if (state == 24u) {
        uint32_t action = (MEM8(g_ecx + 0x8C9u) < MEM8(g_ecx + 0x2Eu)) ? 24u : 25u;
        uint32_t arg = (action == 24u) ? 1u : 0u;
        PUSH32(g_esp, arg);
        PUSH32(g_esp, action);
        PUSH32(g_esp, 0x00494852u); sub_00492E9B();
        g_esp += 8; /* ret 4 */
        return;
    }
    if (state == 25u) {
        uint32_t action = (MEM8(g_ecx + 0x8C9u) < MEM8(g_ecx + 0x2Eu)) ? 25u : 27u;
        uint32_t arg = (action == 25u) ? 1u : 0u;
        PUSH32(g_esp, arg);
        PUSH32(g_esp, action);
        PUSH32(g_esp, 0x00494852u); sub_00492E9B();
        g_esp += 8; /* ret 4 */
        return;
    }
    g_eax = 0;
    g_esp += 8; /* ret 4 */
}

/* 00498EF6 is an indirect continuation in the checksum worker at 00498E54.
 * It arrives with EBX/ESI and the remaining word count already saved on the
 * guest stack.  The generic dispatcher only indexes function starts, so this
 * valid jump-table tail needs an explicit entry. */
static void nfl2k5_checksum_tail_498ef6(void)
{
    uint64_t sum = (uint64_t)g_eax + g_edx;
    uint32_t count = g_ecx, flags, prior;
    uint32_t i;
    for (i = 12u; i < 16u; ++i)
        sum += MEM32(g_esi + i * 4u);
    g_esi += 0x40u;
    while (count > 1u) {
        for (i = 0; i < 16u; ++i)
            sum += MEM32(g_esi + i * 4u);
        g_esi += 0x40u;
        --count;
    }
    g_eax = (uint32_t)sum + (uint32_t)(sum >> 32);
    POP32(g_esp, g_ecx);
    if (g_ecx & 1u) {
        prior = g_eax;
        g_eax += MEM16(g_esi);
        if (g_eax < prior) ++g_eax;
    }
    g_ecx = ROR32(g_eax, 16);
    g_eax += g_ecx;
    flags = MEM32(g_esp + 0x10u);
    g_eax >>= 16;
    if (flags & 1u) SET_LO16(g_eax, ROR32(LO16(g_eax), 8));
    prior = g_eax;
    SET_LO16(g_eax, LO16(g_eax) + MEM16(g_esp + 0xCu));
    if (g_eax < prior) ++g_eax;
    POP32(g_esp, g_esi);
    POP32(g_esp, g_ebx);
    g_esp += 16; /* ret 12 */
}

/* 003CB1C0 is a real unprologued function in the XBE, not a no-op callback.
 * It was missed by the function detector. This reproduces its 10 instructions
 * and preserves the original call/return stack layout. */
static void nfl2k5_boot_registration(void)
{
    /* 2026-09-21: same empirical stack-balance check as 3CC280 above --
     * this function trusts sub_003784D0/sub_00016CFF/sub_00016CAD to each
     * clean up their own stdcall arguments via their own generated `ret N`.
     * If any of those three has a mismatched N, it surfaces here first. */
    uint32_t entry_esp = g_esp;
    if (getenv("RECOMP_WAIT_TRACE")) {
        static volatile LONG trace_count;
        LONG n = InterlockedIncrement(&trace_count);
        if (n <= 20)
            fprintf(stderr, "  [WAIT] nfl2k5_boot_registration call #%ld "
                    "caller_ret=0x%08X\n", n, MEM32(g_esp));
    }
    PUSH32(g_esp, 0x00AF5888u);
    PUSH32(g_esp, 0x003CB1CAu);
    sub_003784D0();
    if (g_esp != entry_esp) {
        static volatile LONG warned0;
        if (InterlockedIncrement(&warned0) <= 5)
            fprintf(stderr, "  [STACKCHECK] 3CB1C0/after-3784D0: g_esp=0x%08X entry_esp=0x%08X (delta=%d)\n",
                    g_esp, entry_esp, (int32_t)(g_esp - entry_esp));
    }
    if (g_eax == 1) {
        PUSH32(g_esp, 0xFFFFFFFEu);
        PUSH32(g_esp, 0x003CB1D6u);
        sub_00016CFF();
        if (g_esp != entry_esp) {
            static volatile LONG warned1;
            if (InterlockedIncrement(&warned1) <= 5)
                fprintf(stderr, "  [STACKCHECK] 3CB1C0/after-16CFF: g_esp=0x%08X entry_esp=0x%08X (delta=%d)\n",
                        g_esp, entry_esp, (int32_t)(g_esp - entry_esp));
        }
        MEM32(0x00CC7934u) = g_eax;
        PUSH32(g_esp, 0x0000000Fu);
        PUSH32(g_esp, 0xFFFFFFFEu);
        PUSH32(g_esp, 0x003CB1E4u);
        sub_00016CAD();
        if (g_esp != entry_esp) {
            static volatile LONG warned2;
            if (InterlockedIncrement(&warned2) <= 5)
                fprintf(stderr, "  [STACKCHECK] 3CB1C0/after-16CAD: g_esp=0x%08X entry_esp=0x%08X (delta=%d)\n",
                        g_esp, entry_esp, (int32_t)(g_esp - entry_esp));
        }
        g_esp += 4; /* ret */
    } else {
        sub_00016EAC(); /* original tail jump; this consumes caller return */
    }
}

/* 00041810 is the title's per-frame callback. It is referenced as data by
 * the scheduler, which is why the function scanner did not emit it. This is
 * a direct translation of 00041810..00041870; keeping it real matters because
 * it advances the renderer's update queues after AvSetDisplayMode. */
static void nfl2k5_frame_callback(void)
{
    static unsigned callback_count;
    ++callback_count;
    PUSH32(g_esp, g_esi);
    g_esi = MEM32(0x00B068F4u);
    while (g_esi != 0x00B057D8u) {
        g_eax = MEM32(g_esi + 0x1130u);
        if (g_eax || MEM32(g_esi + 0x112Cu)) {
            g_ecx = MEM32(g_esi + 0x1C24u);
            PUSH32(g_esp, 0xBF800000u);
            PUSH32(g_esp, 0x00041844u);
            sub_0003D0D0();
        }
        g_esi = MEM32(g_esi + 0x111Cu);
    }
    PUSH32(g_esp, 0x00041857u); sub_0003EF30();
    PUSH32(g_esp, 0x0004185Cu); sub_0003F140();
    PUSH32(g_esp, 0x00041861u); sub_00041790();
    PUSH32(g_esp, 0x00041866u); sub_00040870();
    PUSH32(g_esp, 0x0004186Bu); sub_00040A10();
    /* The retail boot loop treats this as an edge-triggered "work pending"
     * notification.  On the native bridge it is raised by the XPP path but
     * no interrupt completion clears it, leaving startup polling forever.
     * Keep this strictly opt-in until the completion source is modeled. */
    if (getenv("RECOMP_BOOT_UNSTICK"))
        MEM32(0x00B04EC0u) = 0;
    POP32(g_esp, g_esi);
    sub_00040D20(); /* original tail jump consumes the caller return */
}

/* D3D command emitters immediately preceding the first display mode. Like the
 * frame callback, these routine starts fall just after a scanner range end. */
static void nfl2k5_d3d_set_state_42a260(void)
{
    uint32_t arg;
    g_edx = MEM32(0x00440508u) | 0x200u;
    PUSH32(g_esp, g_esi);
    g_esi = MEM32(0x004409A8u);
    MEM32(0x00440508u) = g_edx;
    g_eax = MEM32(g_esi);
    if (g_eax >= MEM32(g_esi + 4u)) { PUSH32(g_esp, 0x0042A285u); sub_004265A0(); }
    arg = MEM32(g_esp + 8u);
    MEM32(g_eax) = 0x00040328u;
    MEM32(g_eax + 4u) = arg;
    MEM32(g_esi) = g_eax + 8u;
    MEM32(0x00440934u) = arg;
    POP32(g_esp, g_esi);
    g_esp += 8; /* ret 4 */
}

static void nfl2k5_d3d_set_state_42b220(void)
{
    uint32_t arg = MEM32(g_esp + 4u), command_buffer;
    PUSH32(g_esp, g_esi);
    PUSH32(g_esp, g_edi);
    g_esi = MEM32(0x00440974u) << 16;
    command_buffer = MEM32(0x004409A8u);
    g_esi |= arg;
    MEM32(0x00440988u) = arg;
    g_eax = MEM32(command_buffer + 8u);
    if (((g_eax >> 8) & 0x80u) && MEM32(0x00440970u)) g_esi |= 1u;
    g_eax = MEM32(command_buffer);
    if (g_eax >= MEM32(command_buffer + 4u)) { PUSH32(g_esp, 0x0042B25Bu); sub_004265A0(); }
    MEM32(g_eax) = 0x00041D7Cu;
    MEM32(g_eax + 4u) = g_esi;
    MEM32(command_buffer) = g_eax + 8u;
    POP32(g_esp, g_edi);
    POP32(g_esp, g_esi);
    g_esp += 8; /* ret 4 */
}

static void nfl2k5_d3d_set_state_42ade0(void)
{
    uint32_t arg;
    PUSH32(g_esp, g_esi);
    g_esi = MEM32(0x004409A8u);
    g_eax = MEM32(g_esi);
    if (g_eax >= MEM32(g_esi + 4u)) { PUSH32(g_esp, 0x0042ADF3u); sub_004265A0(); }
    arg = MEM32(g_esp + 8u);
    g_edx = arg;
    g_ecx = (arg && MEM32(g_esi + 0x1A08u)) ? 1u : 0u;
    MEM32(g_eax) = 0x0004030Cu;
    MEM32(g_eax + 4u) = g_ecx;
    MEM32(g_eax + 8u) = 0x00041D78u;
    MEM32(g_eax + 12u) = MEM32(0x00440858u);
    MEM32(g_esi) = g_eax + 16u;
    if (MEM32(0x0044094Cu) == 2u || g_edx == 2u) {
        MEM32(0x0044094Cu) = g_edx;
        PUSH32(g_esp, 0x0042AE46u); sub_004299B0();
        g_ecx = g_esi;
        PUSH32(g_esp, 0x0042AE4Du); sub_00428FD0();
        g_eax = MEM32(g_esi);
        if (g_eax >= MEM32(g_esi + 4u)) { PUSH32(g_esp, 0x0042AE59u); sub_004265A0(); }
        g_edx = g_eax;
        g_ecx = g_esi;
        PUSH32(g_esp, 0x0042AE62u); sub_0042A810();
        g_edx = g_eax;
        PUSH32(g_esp, 0x0042AE69u); sub_0042A620();
        MEM32(g_esi) = g_eax;
    } else {
        MEM32(0x0044094Cu) = g_edx;
    }
    POP32(g_esp, g_esi);
    g_esp += 8; /* ret 4 */
}
/* Worker omitted by function discovery. Dispatch the XBE's actual jump-table
 * destinations, preserving its state transitions and guest stack. */
static void nfl2k5_io_worker(void)
{
    PUSH32(g_esp, g_esi);
    if (MEM32(0x00A7D300u) != 13u) {
        g_esi = 1;
        do {
            g_eax = MEM32(0x00A7D300u);
            if (g_eax > 13u) {
                fprintf(stderr, "[WORKER] State outside jump table: %08X\n", g_eax);
                abort();
            }
            uint32_t target = MEM32(0x0004D8C0u + g_eax * 4u);
            int complete = 1;
            switch (target) {
            case 0x0004D82Fu: MEM32(0x00A7D300u) = g_esi; complete = 0; break;
            case 0x0004D837u: PUSH32(g_esp, 0x0004D83Cu); sub_0004B910(); complete = 0; break;
            case 0x0004D83Eu: PUSH32(g_esp, 0x0004D843u); sub_0004D630(); break;
            case 0x0004D845u: PUSH32(g_esp, 0x0004D84Au); sub_0004D720(); break;
            case 0x0004D84Cu: PUSH32(g_esp, 0x0004D851u); sub_0004CF30(); break;
            case 0x0004D853u: PUSH32(g_esp, 0x0004D858u); sub_0004D210(); break;
            case 0x0004D85Au: PUSH32(g_esp, 0x0004D85Fu); sub_0004CFE0(); break;
            case 0x0004D861u: PUSH32(g_esp, 0x0004D866u); sub_0004D2E0(); break;
            case 0x0004D868u: PUSH32(g_esp, 0x0004D86Du); sub_0004D350(); break;
            case 0x0004D86Fu: PUSH32(g_esp, 0x0004D874u); sub_0004D3C0(); break;
            case 0x0004D876u: PUSH32(g_esp, 0x0004D87Bu); sub_0004D430(); break;
            case 0x0004D87Du: PUSH32(g_esp, 0x0004D882u); sub_0004D480(); break;
            case 0x0004D884u: PUSH32(g_esp, 0x0004D889u); sub_0004D4C0(); break;
            default:
                fprintf(stderr, "[WORKER] Invalid state %08X target %08X\n", g_eax, target);
                abort();
            }
            if (complete) {
                g_edx = MEM32(0x00A7D368u);
                g_ecx = 0x00A7D370u;
                MEM32(0x00A7D300u) = g_esi;
                PUSH32(g_esp, 0x0004D89Fu); sub_0003A4F0();
            }
        } while (MEM32(0x00A7D300u) != 13u);
    }
    MEM32(0x00A7D300u) = 0;
    PUSH32(g_esp, 0x0004D8BBu); sub_0004B920();
    POP32(g_esp, g_esi);
    g_esp += 8;
}
static void nfl2k5_io_completion(void)
{
    PUSH32(g_esp, g_esi);
    g_esi = g_edx;
    PUSH32(g_esp, 0x0004C3C8u); sub_0003A750();
    g_ecx = g_esi;
    g_edx = g_eax;
    POP32(g_esp, g_esi);
    sub_0003A4F0();
}

/* XPP timer setup at 004DB047 fell in the byte range immediately following
 * the generated 004DAFCA function.  It initializes the game-input timer and
 * its backing queue; without it the XPP startup path enters an unresolved
 * indirect call before it can register controller devices. */
static void nfl2k5_xpp_timer_init(void)
{
    uint32_t bytes;
    /* RECOMP_ICALL_SAFE follows generated-code convention and assigns its
     * local eax alias on a lookup failure.  Successful callees update g_eax. */
    uint32_t eax = 0;
    PUSH32(g_esp, g_edi);
    g_eax = MEM8(0x004DA590u) ? 1u : 0u;
    MEM16(0x00CC72A2u) = 0;
    PUSH32(g_esp, 0x48425355u);
    g_eax = g_eax * 8u + 6u;
    MEM16(0x00CC72A0u) = (uint16_t)g_eax;
    bytes = g_eax << 6;
    PUSH32(g_esp, bytes);
    PUSH32(g_esp, 0x004DB079u); sub_004DD0D8();
    bytes = (uint32_t)MEM16(0x00CC72A0u) << 6;
    g_edi = g_eax;
    MEM32(0x00CC72A4u) = g_edi;
    memset((void *)XBOX_PTR(g_edi), 0, bytes);
    PUSH32(g_esp, 0);
    PUSH32(g_esp, 0x00CC7250u);
    {
        uint32_t saved_esp = g_esp;
        uint32_t target = MEM32(0x004E3C44u);
        PUSH32(g_esp, 0x004DB0A8u);
        RECOMP_ICALL_SAFE(target, saved_esp);
        if (g_esp == saved_esp) g_eax = eax;
    }
    MEM32(0x00CC7298u) = 0;
    POP32(g_esp, g_edi);
    g_esp += 8; /* ret 4 */
}
extern recomp_func_t nfl2k5_video_menu_lookup(uint32_t address);

recomp_func_t recomp_lookup_manual(uint32_t address)
{
    /* Rows of the Video Settings screen (src/nfl2k5_video_menu.c). */
    if ((address & 0xFFFF0000u) == 0xFEC00000u)
        return nfl2k5_video_menu_lookup(address);
    switch(address) {
    case 0x0004C3C0u:
        return nfl2k5_io_completion;
    /* 0x44DF0, 0x44BB0, 0x45A20, 0x459D0, 0x168C70, 0x45600: no overrides
     * either (2026-09-26) -- the analysis translates them now, and the
     * hand-written versions were how 0x45100 broke. */
    /* 0x00045100: no override any more (2026-09-25). The analysis now
     * translates it, and nfl2k5_archive_completion_45100 passed the release
     * callback (0x450D0) as the load callback for every record but the
     * last -- the original loads with 0x450B0 for all of them -- so each
     * texture set was released as it loaded: black uniforms and field. */
    case 0x0004D810u:
        return nfl2k5_io_worker;
    case 0x004DB047u:
        return nfl2k5_xpp_timer_init;
    case 0x004C3207u:
        return nfl2k5_network_state_dispatch_4c3207;
    case 0x00041FF0u:
        return nfl2k5_resource_completion_41ff0;
    case 0x001C1FA0u:
        return nfl2k5_bootstrap_probe_1c1fa0;
    case 0x003CC280u:
        return nfl2k5_frontend_queue_pump_3cc280;
    case 0x004945A3u:
        return nfl2k5_frontend_state_probe_4945a3;
    case 0x00498EF6u:
        return nfl2k5_checksum_tail_498ef6;
    /* Every entry below belongs to the same unrolled checksum tail table at
     * 00498F50.  They share the saved-register and ret-12 ABI. */
    case 0x00498EBFu: case 0x00498EC4u: case 0x00498EC9u:
    case 0x00498ECEu: case 0x00498ED3u: case 0x00498ED8u:
    case 0x00498EDDu: case 0x00498EE2u: case 0x00498EE7u:
    case 0x00498EECu: case 0x00498EF1u: case 0x00498EFBu:
    case 0x00498F00u: case 0x00498F05u:
        return nfl2k5_checksum_tail_498ef6;
    case 0x0003448Eu: /* xor eax,eax; ret */
        return nfl2k5_gap_return_zero;
    case 0x003CB1C0u:
        return nfl2k5_boot_registration;
    case 0x00041810u:
        return nfl2k5_frame_callback;
    case 0x0042A260u:
        return nfl2k5_d3d_set_state_42a260;
    case 0x0042B220u:
        return nfl2k5_d3d_set_state_42b220;
    case 0x0042ADE0u:
        return nfl2k5_d3d_set_state_42ade0;
    default: break;
    }
    return NULL;
}
void recomp_icall_fail_log(uint32_t address)
{
    static unsigned count;
    if (count++ < 32) {
        /* Every generated indirect-call macro has already pushed the guest
         * return address when it reaches this hook.  Recording that word
         * identifies the source instruction without altering its ABI. */
        uint32_t caller = MEM32(g_esp);
        fprintf(stderr, "[ICALL] Unresolved guest target %08X from %08X esp=%08X\n",
                address, caller, g_esp);
    }
}
void recomp_icall_not_code_log(uint32_t address)
{
    static unsigned count;
    if (count++ < 32) {
        uint32_t caller = MEM32(g_esp);
        fprintf(stderr, "[ICALL] Target outside guest code %08X from %08X esp=%08X\n",
                address, caller, g_esp);
    }
}


static void sub_004952F8(void)
{
    uint32_t arg1 = MEM32(g_esp + 4);
    uint32_t arg2 = MEM32(g_esp + 8);
    uint32_t arg3 = MEM32(g_esp + 12);
    uint32_t arg4 = MEM32(g_esp + 16);
    uint32_t arg5 = MEM32(g_esp + 20);
    uint32_t arg6 = MEM32(g_esp + 24);

    // Perform operations with the arguments
    // ...

    // Return from the function
    g_esp += 28; // Adjust stack pointer to account for all arguments
    return;
}
