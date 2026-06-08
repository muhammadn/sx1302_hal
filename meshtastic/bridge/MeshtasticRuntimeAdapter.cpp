#include "MeshtasticBridge.h"
#include "MeshtasticSX126xShim.h"

#include "mesh/MeshTypes.h"
#include "mesh/RadioInterface.h"
#include "mesh/Router.h"
#include "main.h"

#include <cstring>
#include <memory>

extern void setup();
extern void loop();

namespace {

class SX1302BridgeRadio final : public RadioInterface {
  public:
    bool init() override
    {
        RadioInterface::init();
        return true;
    }

    bool reconfigure() override
    {
        RadioInterface::reconfigure();
        return true;
    }

    bool canSleep() override { return true; }

    bool sleep() override { return true; }

    uint32_t getPacketTime(uint32_t totalPacketLen, bool received) override
    {
        (void)received;
        /* Conservative placeholder airtime estimate for scheduling/accounting paths. */
        return (totalPacketLen * 2U) + 20U;
    }

    float getFreq() override
    {
        return savedFreq > 0.0f ? savedFreq : 915.0f;
    }

    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        if (p == nullptr) {
            return ERRNO_UNKNOWN;
        }

        const size_t frame_len = beginSending(p);
        if ((frame_len == 0) || (frame_len > MAX_LORA_PAYLOAD_LEN)) {
            packetPool.release(p);
            return ERRNO_UNKNOWN;
        }

        uint16_t out_size = static_cast<uint16_t>(frame_len);
        uint32_t freq_hz = static_cast<uint32_t>(getFreq() * 1000000.0f);
        uint32_t bw_hz = static_cast<uint32_t>(bw * 1000.0f);

        int rc = mtk_bridge_enqueue_downlink_ext(
            reinterpret_cast<const uint8_t*>(&radioBuffer),
            out_size,
            freq_hz,
            0,
            power,
            bw_hz,
            sf,
            cr,
            0
        );

        packetPool.release(p);

        if (rc == 0) {
            return ERRNO_OK;
        }
        return ERRNO_UNKNOWN;
    }

    void pumpUplink()
    {
        for (int i = 0; i < 32; ++i) {
            auto pkt = mtk_bridge::pop_uplink_packet();
            if (!pkt.has_value()) {
                return;
            }

            const auto &raw = pkt->payload;
            if (raw.size() < sizeof(PacketHeader)) {
                continue;
            }

            const size_t payload_len = raw.size() - sizeof(PacketHeader);
            meshtastic_MeshPacket *mp = packetPool.allocZeroed();
            if (mp == nullptr) {
                return;
            }

            if (payload_len > sizeof(mp->encrypted.bytes)) {
                packetPool.release(mp);
                continue;
            }

            PacketHeader header;
            std::memcpy(&header, raw.data(), sizeof(PacketHeader));

            if (header.from == 0) {
                packetPool.release(mp);
                continue;
            }

            mp->from = header.from;
            mp->to = header.to;
            mp->id = header.id;
            mp->channel = header.channel;
            mp->hop_limit = header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
            mp->hop_start = (header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
            mp->want_ack = !!(header.flags & PACKET_FLAGS_WANT_ACK_MASK);
            mp->via_mqtt = !!(header.flags & PACKET_FLAGS_VIA_MQTT_MASK);
            mp->next_hop = (mp->hop_start == 0) ? NO_NEXT_HOP_PREFERENCE : header.next_hop;
            mp->relay_node = (mp->hop_start == 0) ? NO_RELAY_NODE : header.relay_node;
            mp->rx_snr = pkt->snr;
            mp->rx_rssi = pkt->rssi;
            mp->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;

            std::memcpy(mp->encrypted.bytes, raw.data() + sizeof(PacketHeader), payload_len);
            mp->encrypted.size = payload_len;

            deliverToReceiver(mp);
        }
    }
};

static SX1302BridgeRadio *g_bridge_radio = nullptr;
static bool g_runtime_ready = false;

} // namespace

extern "C" int meshtastic_sx126x_runtime_init(void)
{
    if (!g_runtime_ready) {
        setup();

        if (router == nullptr) {
            return -1;
        }

        std::unique_ptr<RadioInterface> bridge_iface(new SX1302BridgeRadio());
        g_bridge_radio = static_cast<SX1302BridgeRadio*>(bridge_iface.get());
        router->addInterface(std::move(bridge_iface));
        g_runtime_ready = true;
    }

    return 0;
}

extern "C" void meshtastic_sx126x_runtime_loop(void)
{
    if (!g_runtime_ready) {
        return;
    }

    if (g_bridge_radio != nullptr) {
        g_bridge_radio->pumpUplink();
    }

    loop();
}

extern "C" int meshtastic_sx126x_runtime_send(
    const uint8_t *payload,
    uint16_t size,
    uint8_t topic,
    const uint8_t *target_device,
    uint8_t target_len)
{
    (void)topic;
    (void)target_device;
    (void)target_len;

    if ((!g_runtime_ready) || (router == nullptr) || (payload == nullptr) || (size == 0)) {
        return -1;
    }

    meshtastic_MeshPacket *p = router->allocForSending();
    if (p == nullptr) {
        return -1;
    }

    p->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    if (size > sizeof(p->decoded.payload.bytes)) {
        packetPool.release(p);
        return -1;
    }

    std::memcpy(p->decoded.payload.bytes, payload, size);
    p->decoded.payload.size = size;

    const ErrorCode rc = router->sendLocal(p, RX_SRC_LOCAL);
    return (rc == ERRNO_OK || rc == ERRNO_SHOULD_RELEASE) ? 0 : -1;
}
