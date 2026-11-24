/* Optional title-owned HUD provider; never modifies guest RAM. Registration happens
 * before emulation; configure/tick/observe run on the simulation thread.
 * snapshot runs on the GL thread and must return a synchronized copy.
 * Simulation/save/load/reset callbacks run on the simulation thread even
 * with the HUD disabled. State callbacks form one complete optional group;
 * the 16-byte little-endian payload belongs to the registered title. */
#ifndef PSX_HOST_NAMEPLATES_H
#define PSX_HOST_NAMEPLATES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct PsxHostNameplates {
    int visible;
    char names[2][64];
    uint32_t wins[2];
} PsxHostNameplates;
typedef struct PsxHostNameplateProvider {
    int (*configure)(int enabled);
    void (*tick)(uint64_t frame);
    void (*observe_gp0)(const uint32_t *words, unsigned count);
    void (*snapshot)(PsxHostNameplates *out);
    /* Session metadata only; null/empty names restore the slot's fallback. */
    void (*set_names)(const char *left, const char *right);
    /* Simulation thread, before frame snapshots, including rollback replay. */
    void (*simulation_tick)(void);
    void (*reset_score)(void);
    void (*begin_session)(void);
    void (*save_state)(uint8_t out[16]);
    int (*validate_state)(const uint8_t *data, uint32_t len);
    void (*load_state)(const uint8_t *data);
} PsxHostNameplateProvider;
extern const PsxHostNameplateProvider *g_psx_nameplate_provider;
void psx_host_nameplates_register(const PsxHostNameplateProvider *provider);
#ifdef __cplusplus
}
#endif
#endif
