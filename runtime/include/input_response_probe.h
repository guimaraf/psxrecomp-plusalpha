#ifndef PSX_INPUT_RESPONSE_PROBE_H
#define PSX_INPUT_RESPONSE_PROBE_H
#include <stddef.h>
#include <stdint.h>
typedef struct InputResponseConfig {
    int discovery, port;
    uint32_t warmup_ms, capture_ms, button_mask, lo, hi;
    uint32_t state_addr, state_width, idle_value, attack_value, state_pc;
    uint32_t input_addr, input_width, input_mask, input_value, input_pc;
} InputResponseConfig;
#ifndef PSX_NO_DEBUG_TOOLS
extern int g_input_response_active;
int input_response_arm(const InputResponseConfig *cfg);
void input_response_stop(void);
void input_response_frame(void);
void input_response_pad(int port, uint16_t buttons);
void input_response_write(uint32_t addr, uint32_t old_value, uint32_t value,
                          unsigned width, uint32_t pc);
void input_response_status(char *out, size_t cap);
void input_response_page(char *out, size_t cap, unsigned cursor, int events_only);
#endif
#endif
