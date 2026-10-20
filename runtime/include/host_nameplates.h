/* Optional title-owned, read-only nameplate provider. Registration happens
 * before emulation; configure/tick/observe run on the simulation thread.
 * snapshot runs on the GL thread and must return a synchronized copy. */
#ifndef PSX_HOST_NAMEPLATES_H
#define PSX_HOST_NAMEPLATES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct PsxHostNameplates {
    int visible;
    char names[2][64];
} PsxHostNameplates;
typedef struct PsxHostNameplateProvider {
    int (*configure)(int enabled);
    void (*tick)(uint64_t frame);
    void (*observe_gp0)(const uint32_t *words, unsigned count);
    void (*snapshot)(PsxHostNameplates *out);
    /* Session metadata only; null/empty names restore the slot's fallback. */
    void (*set_names)(const char *left, const char *right);
} PsxHostNameplateProvider;
extern const PsxHostNameplateProvider *g_psx_nameplate_provider;
void psx_host_nameplates_register(const PsxHostNameplateProvider *provider);
#ifdef __cplusplus
}
#endif
#endif
