#ifndef PSX_INPUT_RESPONSE_PROBE_H
#define PSX_INPUT_RESPONSE_PROBE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct InputResponseConfig {
    int discovery, port;
    uint32_t warmup_ms, capture_ms, button_mask, lo, hi;
    uint32_t state_addr, state_width, idle_value, attack_value, state_pc;
    uint32_t input_addr, input_width, input_mask, input_value, input_pc;
    int timing; /* optional Ryu P1 observational stream; v1 events unchanged */
} InputResponseConfig;
enum InputResponseTimingKind {
    IR_HOST_BEGIN = 10, IR_HOST_END, IR_PAD_DELIVERY,
    IR_PACER_BEGIN, IR_PACER_END, IR_PRESENT_BEGIN, IR_PRESENT_END,
    IR_SWAP_BEGIN, IR_SWAP_END, IR_VBLANK_EDGE, IR_IRQ_DELIVER,
    IR_CALLBACK_ENTRY, IR_BUFFER_READ, IR_BUFFER_WRITE,
    IR_SIO_SELECT, IR_SIO_DESELECT, IR_SIO_SHIFT_BEGIN, IR_SIO_RX_READY,
    IR_SIO_RX_READ, IR_SIO_SNAPSHOT, IR_SIO_ACK, IR_GAME_WRITE,
    IR_PACER_DEFER, IR_PACER_RESUME, IR_PACER_CANCEL
};
#ifndef PSX_NO_DEBUG_TOOLS
extern int g_input_response_active;
extern int g_input_response_timing_active;
void input_response_swap(unsigned kind);
void input_response_timing(unsigned kind, uint32_t addr, uint32_t pc,
                           uint32_t old_value, uint32_t value, uint32_t aux);
void input_response_sio(unsigned kind, int port, uint8_t tx, uint8_t rx,
                        uint32_t index, uint16_t ctrl, uint16_t stat,
                        uint16_t mode, uint16_t baud);
#define INPUT_RESPONSE_TRACE(...) do { if (g_input_response_timing_active) \
    input_response_timing(__VA_ARGS__); } while (0)
int input_response_arm(const InputResponseConfig *cfg);
void input_response_stop(void);
void input_response_frame(void);
void input_response_pad(int port, uint16_t buttons);
void input_response_write(uint32_t addr, uint32_t old_value, uint32_t value,
                          unsigned width, uint32_t pc);
void input_response_status(char *out, size_t cap);
void input_response_page(char *out, size_t cap, unsigned cursor, int events_only);
#else
#define INPUT_RESPONSE_TRACE(...) ((void)0)
#endif
#ifdef __cplusplus
}
#endif
#endif
