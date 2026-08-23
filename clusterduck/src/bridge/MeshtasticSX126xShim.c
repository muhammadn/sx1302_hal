#include "MeshtasticSX126xShim.h"

#include "MeshtasticBridge.h"

#include <stdio.h>
#include <string.h>

#ifndef MESHTASTIC_ALLOW_WEAK_RUNTIME_STUBS
#define MESHTASTIC_ALLOW_WEAK_RUNTIME_STUBS 1
#endif

static uint8_t bounded_strlen16(const uint8_t* s) {
    uint8_t n = 0;

    if (s == NULL) {
        return 0;
    }

    while ((n < 16) && (s[n] != '\0')) {
        n++;
    }
    return n;
}

int __attribute__((weak)) meshtastic_sx126x_runtime_init(void) {
#if MESHTASTIC_ALLOW_WEAK_RUNTIME_STUBS
    printf("[MTK_SX126X] WARNING: meshtastic_sx126x_runtime_init() weak fallback in use\n");
    return 0;
#else
    printf("[MTK_SX126X] ERROR: no strong meshtastic_sx126x_runtime_init() implementation\n");
    return -1;
#endif
}

void __attribute__((weak)) meshtastic_sx126x_runtime_loop(void) {
#if MESHTASTIC_ALLOW_WEAK_RUNTIME_STUBS
    /* Deliberate no-op: real runtime should provide a strong implementation. */
#endif
}

int __attribute__((weak)) meshtastic_sx126x_runtime_send(
    const uint8_t* payload,
    uint16_t size,
    uint8_t topic,
    const uint8_t* target_device,
    uint8_t target_len
) {
#if MESHTASTIC_ALLOW_WEAK_RUNTIME_STUBS
    (void)topic;
    (void)target_device;
    (void)target_len;
    return mtk_bridge_enqueue_downlink(payload, size);
#else
    (void)payload;
    (void)size;
    (void)topic;
    (void)target_device;
    (void)target_len;
    printf("[MTK_SX126X] ERROR: no strong meshtastic_sx126x_runtime_send() implementation\n");
    return -1;
#endif
}

void* meshtastic_init_and_setup(void) {
    int rc = meshtastic_sx126x_runtime_init();
    return (rc == 0) ? (void*)0x1 : NULL;
}

void meshtastic_run_loop(void) {
    meshtastic_sx126x_runtime_loop();
}

int meshtastic_send_data(uint8_t topic, const char* message, int length, const uint8_t* targetDevice) {
    uint8_t target_len = 0;

    if ((message == NULL) || (length <= 0)) {
        return -1;
    }

    if (targetDevice != NULL) {
        target_len = bounded_strlen16(targetDevice);
    }

    return meshtastic_sx126x_runtime_send(
        (const uint8_t*)message,
        (uint16_t)length,
        topic,
        targetDevice,
        target_len
    );
}
