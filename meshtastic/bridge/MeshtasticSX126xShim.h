#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SX126x-oriented integration contract.
 * Real Meshtastic runtime code can override these weak symbols.
 */
int meshtastic_sx126x_runtime_init(void);
void meshtastic_sx126x_runtime_loop(void);
int meshtastic_sx126x_runtime_send(
    const uint8_t* payload,
    uint16_t size,
    uint8_t topic,
    const uint8_t* target_device,
    uint8_t target_len
);

/*
 * Forwarder-facing API consumed by meshtasticd.c.
 */
void* meshtastic_init_and_setup(void);
void meshtastic_run_loop(void);
int meshtastic_send_data(uint8_t topic, const char* message, int length, const uint8_t* targetDevice);

#ifdef __cplusplus
}
#endif
