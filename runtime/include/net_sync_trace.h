#ifndef PSX_NET_SYNC_TRACE_H
#define PSX_NET_SYNC_TRACE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Main-thread safe points only, like debug_server_poll. No disk I/O in guest. */
typedef struct NetSyncTraceEntry {
    uint64_t seq, qpc, deadline, sample_ticks;
    uint32_t tick, generation, highest_wire, packets_rx;
    uint64_t rx_age_ms;
    int stage, host, slot, delay, lead, running, media, xa, resim, tip_hold;
    int timesync_enabled, auto_enabled;
    uint32_t debt_ms, debt_added_ms, slice_requested_ms, mispredicts;
    uint32_t tick_ema_ms, ahead_streak, stall_until_ms;
    uint32_t window_ticks, window_misses, window_late;
    int window_lead_min;
    uint32_t transit_x16, auto_eval_ms, auto_change_ms;
    int auto_target;
    char stall[48];
} NetSyncTraceEntry;
/* stage: 1=barrier enter, 2=barrier wait (100ms), 3=barrier exit,
 * 4=present begin, 5=present end, 6=pacer begin, 7=pacer end, 8=session start returned.
 * Tick is the current admission cursor, not necessarily the displayed frame. */
int psx_net_sync_trace_snapshot(uint64_t after, NetSyncTraceEntry *out, int cap,
                               uint64_t *total, uint64_t *lost, uint64_t *frequency);
#ifdef __cplusplus
}
#endif
#endif
