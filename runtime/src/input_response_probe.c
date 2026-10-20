/* Offline causal probe. No guest writes, input injection, disk IO, or pacing.
 * Discovery is intentionally heavier and must NEVER be used as a latency ruler.
 * All calls run on the emulation thread; TCP drains only a stopped capture. */
#include "input_response_probe.h"
#ifndef PSX_NO_DEBUG_TOOLS
#include "psx_sdl.h"
#include "psx_cycles.h"
#include "psx_memory.h"
#include "psx_netplay.h"
#include "sio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CANDIDATE_CAP 262144u
#define EVENT_CAP 1024u
extern uint64_t s_frame_count;
extern int g_dma_exec_depth;
extern uint8_t *g_psx_ram;
typedef struct ProbeEvent {
    uint64_t qpc, frame, cycle;
    uint32_t kind, addr, pc, old_value, value;
} ProbeEvent;
typedef struct Candidate {
    uint32_t key, pre_writes, pre_changes;
    uint32_t return_pc;
    uint64_t return_frame;
    ProbeEvent first;
} Candidate;
static Candidate *candidates;
static ProbeEvent events[EVENT_CAP];
static InputResponseConfig config;
static unsigned event_count, candidate_count, overflow, presses, unexpected;
static int phase, initial_idle, trigger, cancelled;
static uint16_t previous_buttons;
static uint64_t frequency, armed_qpc, start_qpc, end_qpc, stop_qpc;
static uint64_t start_frame, stop_frame;
int g_input_response_active;

static ProbeEvent event(unsigned kind, uint32_t addr, uint32_t pc,
                         uint32_t old_value, uint32_t value) {
    ProbeEvent e;
    memset(&e, 0, sizeof(e));
    e.kind = kind; e.addr = addr; e.pc = pc;
    e.old_value = old_value; e.value = value;
    e.qpc = SDL_GetPerformanceCounter();
    e.frame = s_frame_count; e.cycle = psx_get_cycle_count();
    return e;
}
static void append(ProbeEvent e) {
    if (event_count < EVENT_CAP) events[event_count++] = e;
    else overflow++;
}
static int width_ok(uint32_t n) { return n == 1 || n == 2 || n == 4; }
static uint32_t read_value(uint32_t addr, uint32_t width) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < width; ++i) v |= (uint32_t)g_psx_ram[addr + i] << (8u * i);
    return v;
}
int input_response_arm(const InputResponseConfig *cfg) {
    if (g_input_response_active || psx_netplay_active()) return 0;
    if (!cfg || cfg->port < 0 || cfg->port > 1 || !cfg->button_mask ||
        (cfg->button_mask & ~0xFFFFu) || cfg->warmup_ms > 30000u ||
        cfg->capture_ms < 1000u || cfg->capture_ms > 30000u ||
        cfg->lo >= cfg->hi || cfg->hi > psx_ram_live_bytes()) return 0;
    if (!cfg->discovery &&
        (!width_ok(cfg->state_width) || !width_ok(cfg->input_width) ||
         cfg->state_addr >= psx_ram_live_bytes() ||
         cfg->state_width > psx_ram_live_bytes() - cfg->state_addr ||
         cfg->input_addr >= psx_ram_live_bytes() ||
         cfg->input_width > psx_ram_live_bytes() - cfg->input_addr ||
         cfg->idle_value == cfg->attack_value || !cfg->input_mask ||
         !cfg->input_pc || !cfg->state_pc)) return 0;
    Candidate *next_candidates = NULL;
    if (cfg->discovery) {
        next_candidates = (Candidate *)calloc(CANDIDATE_CAP, sizeof(Candidate));
        if (!next_candidates) return 0;
    }
    free(candidates); candidates = next_candidates;
    config = *cfg;
    event_count = candidate_count = overflow = presses = unexpected = 0;
    cancelled = trigger = 0;
    initial_idle = cfg->discovery ? 0 :
        read_value(cfg->state_addr, cfg->state_width) == cfg->idle_value;
    previous_buttons = sio_get_pad_buttons_slot(cfg->port);
    if (previous_buttons != 0xFFFFu) unexpected++;
    frequency = SDL_GetPerformanceFrequency();
    if (!frequency) { free(candidates); candidates = NULL; return 0; }
    armed_qpc = SDL_GetPerformanceCounter();
    start_qpc = end_qpc = stop_qpc = 0;
    start_frame = stop_frame = 0;
    phase = 1; g_input_response_active = 1;
    return 1;
}
void input_response_stop(void) {
    if (g_input_response_active) {
        cancelled = 1; stop_qpc = SDL_GetPerformanceCounter();
        stop_frame = s_frame_count;
        phase = 3; g_input_response_active = 0;
    }
}
void input_response_frame(void) {
    if (!g_input_response_active) return;
    const uint64_t now = SDL_GetPerformanceCounter();
    if (phase == 1 && now - armed_qpc >= frequency * config.warmup_ms / 1000u) {
        phase = 2; start_qpc = now;
        start_frame = s_frame_count;
        end_qpc = now + frequency * config.capture_ms / 1000u;
        if (!config.discovery)
            initial_idle = read_value(config.state_addr, config.state_width) == config.idle_value;
    }
    if (phase == 2 && now >= end_qpc) {
        stop_qpc = now; stop_frame = s_frame_count; phase = 3; g_input_response_active = 0;
    }
    if (psx_netplay_active()) input_response_stop();
}
void input_response_pad(int port, uint16_t buttons) {
    if (!g_input_response_active) return;
    if (port != config.port) {
        if (buttons != 0xFFFFu) unexpected++;
        return;
    }
    const unsigned down = (unsigned)(~buttons) & 0xFFFFu;
    const unsigned old_down = (unsigned)(~previous_buttons) & 0xFFFFu;
    previous_buttons = buttons;
    if (down & ~config.button_mask) unexpected++;
    if (phase == 1) {
        if (down & config.button_mask) unexpected++;
        return;
    }
    if (phase != 2) return;
    if ((down & config.button_mask) == config.button_mask &&
        (old_down & config.button_mask) != config.button_mask) {
        presses++; trigger = 1;
        append(event(1, 0, 0, old_down, down)); /* host pad latched into SIO */
    }
}
void input_response_write(uint32_t addr, uint32_t old_value, uint32_t value,
                          unsigned width, uint32_t pc) {
    if (!g_input_response_active || g_dma_exec_depth || addr >= psx_ram_live_bytes()) return;
    if (!config.discovery) {
        if (phase != 2 || !trigger || old_value == value) return;
        if (addr == config.input_addr && width == config.input_width &&
            pc == config.input_pc && (value & config.input_mask) == config.input_value &&
            (old_value & config.input_mask) != config.input_value)
            append(event(2, addr, pc, old_value, value));
        if (addr == config.state_addr && width == config.state_width && pc == config.state_pc &&
            old_value == config.idle_value && value == config.attack_value)
            append(event(3, addr, pc, old_value, value));
        return;
    }
    if (addr < config.lo || addr >= config.hi) return;
    /* Key includes width. Addresses with overlapping writes remain candidates,
     * not proof of an animation field; they must be reviewed in the game code. */
    uint32_t key = (addr + 1u) | ((uint32_t)width << 24);
    unsigned slot = (key * 2654435761u) & (CANDIDATE_CAP - 1u);
    Candidate *c = NULL;
    for (unsigned i = 0; i < 32u; ++i) {
        Candidate *at = &candidates[(slot + i) & (CANDIDATE_CAP - 1u)];
        if (!at->key) { at->key = key; candidate_count++; c = at; break; }
        if (at->key == key) { c = at; break; }
    }
    if (!c) { overflow++; return; }
    if (phase == 1 || !trigger) {
        c->pre_writes++;
        if (old_value != value) c->pre_changes++;
    } else if (phase == 2 && !c->first.kind && old_value != value && pc) {
        c->first = event(width, addr, pc, old_value, value);
    } else if (phase == 2 && c->first.kind && !c->return_pc &&
               value == c->first.old_value && old_value != value && pc) {
        c->return_pc = pc; c->return_frame = s_frame_count;
    }
}
void input_response_status(char *out, size_t cap) {
    snprintf(out, cap,
        "\"probe_version\":1,\"phase\":%d,\"discovery\":%d,\"frequency\":%llu,"
        "\"armed_qpc\":%llu,\"start_qpc\":%llu,\"stop_qpc\":%llu,"
        "\"events\":%u,\"candidates\":%u,\"overflow\":%u,\"presses\":%u,"
        "\"unexpected_inputs\":%u,\"initial_idle\":%d,\"cancelled\":%d,"
        "\"port\":%d,\"button_mask\":%u,\"frame\":%llu",
        phase, config.discovery, (unsigned long long)frequency,
        (unsigned long long)armed_qpc, (unsigned long long)start_qpc,
        (unsigned long long)stop_qpc, event_count, candidate_count, overflow,
        presses, unexpected, initial_idle, cancelled, config.port, config.button_mask,
        (unsigned long long)s_frame_count);
    const size_t used = strlen(out);
    if (used < cap)
        snprintf(out + used, cap - used, ",\"start_frame\":%llu,\"stop_frame\":%llu",
                 (unsigned long long)start_frame, (unsigned long long)stop_frame);
}
static size_t format_event(char *out, size_t cap, const ProbeEvent *e,
                            unsigned pre_writes, unsigned pre_changes,
                            uint32_t return_pc, uint64_t return_frame) {
    return (size_t)snprintf(out, cap,
        "{\"kind\":%u,\"qpc\":%llu,\"frame\":%llu,\"cycle\":%llu,"
        "\"addr\":%u,\"pc\":%u,\"old\":%u,\"value\":%u,"
        "\"pre_writes\":%u,\"pre_changes\":%u,\"return_pc\":%u,\"return_frame\":%llu}",
        e->kind, (unsigned long long)e->qpc, (unsigned long long)e->frame,
        (unsigned long long)e->cycle, e->addr, e->pc, e->old_value, e->value,
        pre_writes, pre_changes, return_pc, (unsigned long long)return_frame);
}
void input_response_page(char *out, size_t cap, unsigned cursor, int events_only) {
    const int discovery = config.discovery && !events_only;
    const unsigned limit = discovery ? CANDIDATE_CAP : event_count;
    if (phase != 3 || cursor > limit || cap < 1024 || (discovery && !candidates)) {
        snprintf(out, cap, "\"error\":\"capture must be stopped before export\""); return;
    }
    size_t used = (size_t)snprintf(out, cap, "\"records\":[");
    unsigned emitted = 0;
    while (cursor < limit && emitted < 64 && used + 512 < cap) {
        const ProbeEvent *e;
        unsigned pre_writes = 0, pre_changes = 0;
        uint32_t return_pc = 0;
        uint64_t return_frame = 0;
        if (discovery) {
            const Candidate *c = &candidates[cursor++];
            if (!c->first.kind) continue;
            e = &c->first; pre_writes = c->pre_writes; pre_changes = c->pre_changes;
            return_pc = c->return_pc; return_frame = c->return_frame;
        } else e = &events[cursor++];
        if (emitted++) out[used++] = ',';
        used += format_event(out + used, cap - used, e, pre_writes, pre_changes,
                               return_pc, return_frame);
    }
    snprintf(out + used, cap - used, "],\"next\":%u,\"done\":%s",
             cursor, cursor == limit ? "true" : "false");
}
#endif
