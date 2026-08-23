/**
 * @file MeshtasticBridge.cpp
 * @brief Implementation of unified bridge between SX1302 HAL and Meshtastic runtime
 */

#include "MeshtasticBridge.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

struct DownlinkPacket {
    std::vector<uint8_t> payload;
    uint32_t freq_hz = 0;
    uint32_t tmst = 0;
    int16_t tx_power_dbm = 0;
    uint32_t bw_hz = 0;
    uint8_t sf = 0;
    uint8_t cr = 0;
    uint8_t rf_chain = 0;
};

static struct {
    std::mutex uplink_mutex;
    mtk_rx_callback_t rx_callback = nullptr;
    std::deque<mtk_bridge::UplinkPacket> uplink_queue;

    std::mutex downlink_mutex;
    std::deque<DownlinkPacket> downlink_queue;

    std::mutex stats_mutex;
    uint32_t uplink_count = 0;
    uint32_t downlink_count = 0;
} g_bridge;

extern "C" {

void mtk_bridge_register_rx_callback(mtk_rx_callback_t cb) {
    std::lock_guard<std::mutex> lock(g_bridge.uplink_mutex);
    g_bridge.rx_callback = cb;
    printf("[MTK_BRIDGE] RX callback %s (cb=%p)\n", cb ? "REGISTERED" : "UNREGISTERED", (void*)cb);
}

void mtk_bridge_handle_uplink(
    const uint8_t* payload, uint16_t size,
    int16_t rssi, float snr,
    uint32_t freq_hz, uint32_t tmst, uint8_t rf_chain,
    uint32_t bandwidth_hz, uint8_t datarate_sf, uint8_t coderate) {
    if (!payload || size == 0) {
        printf("[MTK_BRIDGE] WARNING: Invalid uplink packet (payload=%p, size=%u)\n", (void*)payload, size);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_bridge.stats_mutex);
        g_bridge.uplink_count++;
    }

    if (g_bridge.rx_callback) {
        g_bridge.rx_callback(payload, size, rssi, snr, freq_hz, tmst, rf_chain,
                             bandwidth_hz, datarate_sf, coderate);
        return;
    }

    std::lock_guard<std::mutex> lock(g_bridge.uplink_mutex);
    const size_t MAX_QUEUE_SIZE = 50;
    if (g_bridge.uplink_queue.size() >= MAX_QUEUE_SIZE) {
        g_bridge.uplink_queue.pop_front();
    }

    mtk_bridge::UplinkPacket pkt;
    pkt.payload = std::vector<uint8_t>(payload, payload + size);
    pkt.freq_hz = freq_hz;
    pkt.datarate_sf = datarate_sf;
    pkt.rf_chain = rf_chain;
    pkt.rssi = rssi;
    pkt.snr = snr;
    g_bridge.uplink_queue.push_back(std::move(pkt));
}

int mtk_bridge_enqueue_downlink(const uint8_t* payload, uint16_t size) {
    if (!payload || size == 0) {
        return -1;
    }

    DownlinkPacket pkt;
    pkt.payload.assign(payload, payload + size);

    std::lock_guard<std::mutex> lock(g_bridge.downlink_mutex);
    g_bridge.downlink_queue.emplace_back(std::move(pkt));

    {
        std::lock_guard<std::mutex> stats_lock(g_bridge.stats_mutex);
        g_bridge.downlink_count++;
    }

    return 0;
}

int mtk_bridge_enqueue_downlink_ext(
    const uint8_t* payload, uint16_t size,
    uint32_t freq_hz, uint32_t tmst, int16_t tx_power_dbm,
    uint32_t bw_hz, uint8_t sf, uint8_t cr, uint8_t rf_chain) {
    if (!payload || size == 0) {
        return -1;
    }

    DownlinkPacket pkt;
    pkt.payload.assign(payload, payload + size);
    pkt.freq_hz = freq_hz;
    pkt.tmst = tmst;
    pkt.tx_power_dbm = tx_power_dbm;
    pkt.bw_hz = bw_hz;
    pkt.sf = sf;
    pkt.cr = cr;
    pkt.rf_chain = rf_chain;

    std::lock_guard<std::mutex> lock(g_bridge.downlink_mutex);
    g_bridge.downlink_queue.emplace_back(std::move(pkt));

    {
        std::lock_guard<std::mutex> stats_lock(g_bridge.stats_mutex);
        g_bridge.downlink_count++;
    }

    return 0;
}

int mtk_bridge_pop_downlink(
    uint8_t* out_buf, uint16_t* inout_capacity,
    uint32_t* freq_hz, uint32_t* tmst, int16_t* tx_power_dbm,
    uint32_t* bw_hz, uint8_t* sf, uint8_t* cr, uint8_t* rf_chain) {
    if (!inout_capacity) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(g_bridge.downlink_mutex);

    if (g_bridge.downlink_queue.empty()) {
        return 1;
    }

    DownlinkPacket pkt = std::move(g_bridge.downlink_queue.front());
    g_bridge.downlink_queue.pop_front();

    if (!out_buf || *inout_capacity < pkt.payload.size()) {
        *inout_capacity = 0;
        return -2;
    }

    std::memcpy(out_buf, pkt.payload.data(), pkt.payload.size());
    *inout_capacity = static_cast<uint16_t>(pkt.payload.size());

    if (freq_hz) {
        *freq_hz = pkt.freq_hz;
    }
    if (tmst) {
        *tmst = pkt.tmst;
    }
    if (tx_power_dbm) {
        *tx_power_dbm = pkt.tx_power_dbm;
    }
    if (bw_hz) {
        *bw_hz = pkt.bw_hz;
    }
    if (sf) {
        *sf = pkt.sf;
    }
    if (cr) {
        *cr = pkt.cr;
    }
    if (rf_chain) {
        *rf_chain = pkt.rf_chain;
    }

    return 0;
}

void mtk_bridge_get_stats(
    uint32_t* uplink_count,
    uint32_t* downlink_count,
    uint32_t* downlink_queue_size) {
    std::lock_guard<std::mutex> stats_lock(g_bridge.stats_mutex);
    std::lock_guard<std::mutex> downlink_lock(g_bridge.downlink_mutex);

    if (uplink_count) {
        *uplink_count = g_bridge.uplink_count;
    }
    if (downlink_count) {
        *downlink_count = g_bridge.downlink_count;
    }
    if (downlink_queue_size) {
        *downlink_queue_size = static_cast<uint32_t>(g_bridge.downlink_queue.size());
    }
}

void mtk_bridge_reset_stats(void) {
    std::lock_guard<std::mutex> lock(g_bridge.stats_mutex);
    g_bridge.uplink_count = 0;
    g_bridge.downlink_count = 0;
}

} // extern "C"

namespace mtk_bridge {

bool has_uplink_packet() {
    std::lock_guard<std::mutex> lock(g_bridge.uplink_mutex);
    return !g_bridge.uplink_queue.empty();
}

std::optional<UplinkPacket> pop_uplink_packet() {
    std::lock_guard<std::mutex> lock(g_bridge.uplink_mutex);
    if (g_bridge.uplink_queue.empty()) {
        return std::nullopt;
    }

    auto packet = std::move(g_bridge.uplink_queue.front());
    g_bridge.uplink_queue.pop_front();
    return packet;
}

void push_uplink_packet(const UplinkPacket& pkt) {
    std::lock_guard<std::mutex> lock(g_bridge.uplink_mutex);

    const size_t MAX_QUEUE_SIZE = 50;
    if (g_bridge.uplink_queue.size() >= MAX_QUEUE_SIZE) {
        g_bridge.uplink_queue.pop_front();
    }

    g_bridge.uplink_queue.push_back(pkt);
}

} // namespace mtk_bridge
