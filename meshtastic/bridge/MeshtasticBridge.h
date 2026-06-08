/**
 * @file MeshtasticBridge.h
 * @brief Unified bridge between SX1302 HAL (C) and Meshtastic runtime
 *
 * This bridge provides:
 * 1. Uplink path: SX1302 concentrator -> Meshtastic runtime
 * 2. Downlink path: Meshtastic runtime -> SX1302 concentrator
 *
 * Thread-safe design allows concurrent access from multiple threads.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
#include <optional>
#include <vector>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mtk_rx_callback_t)(
    const uint8_t* payload, uint16_t size,
    int16_t rssi, float snr,
    uint32_t freq_hz, uint32_t tmst, uint8_t rf_chain,
    uint32_t bandwidth_hz, uint8_t datarate_sf, uint8_t coderate
);

void mtk_bridge_register_rx_callback(mtk_rx_callback_t cb);

void mtk_bridge_handle_uplink(
    const uint8_t* payload, uint16_t size,
    int16_t rssi, float snr,
    uint32_t freq_hz, uint32_t tmst, uint8_t rf_chain,
    uint32_t bandwidth_hz, uint8_t datarate_sf, uint8_t coderate
);

int mtk_bridge_enqueue_downlink(const uint8_t* payload, uint16_t size);

int mtk_bridge_enqueue_downlink_ext(
    const uint8_t* payload, uint16_t size,
    uint32_t freq_hz, uint32_t tmst, int16_t tx_power_dbm,
    uint32_t bw_hz, uint8_t sf, uint8_t cr, uint8_t rf_chain
);

int mtk_bridge_pop_downlink(
    uint8_t* out_buf, uint16_t* inout_capacity,
    uint32_t* freq_hz, uint32_t* tmst, int16_t* tx_power_dbm,
    uint32_t* bw_hz, uint8_t* sf, uint8_t* cr, uint8_t* rf_chain
);

void mtk_bridge_get_stats(
    uint32_t* uplink_count,
    uint32_t* downlink_count,
    uint32_t* downlink_queue_size
);

void mtk_bridge_reset_stats(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace mtk_bridge {

struct UplinkPacket {
    std::vector<uint8_t> payload;
    uint32_t freq_hz = 0;
    uint8_t datarate_sf = 0;
    uint8_t rf_chain = 0;
    int16_t rssi = 0;
    float snr = 0.0f;
};

bool has_uplink_packet();
std::optional<UplinkPacket> pop_uplink_packet();
void push_uplink_packet(const UplinkPacket& pkt);

} // namespace mtk_bridge
#endif
