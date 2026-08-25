#include <CDP.h>
#include <queue>
#include "routing/RouteJSON.h"
#include "bridge/ClusterDuckBridge.h"
#include "payloads/DuckPayloads.h"
#include "utils/safe_base64.h"

// Forward declare C functions for MQTT publishing
extern "C" {
    void mqtt_publish_message(const char* topic, const char* message, int length);
}

// --- Global Objects ---
PapaDuck hub("PAPADUCK");

void processMessageFromDucks(std::vector<std::byte> packetBuffer);
void handleDuckData(CdpPacket receivedPacket);

void processMessageFromDucks(CdpPacket cdp_packet) {

    JsonDocument doc;

    int messageLength = cdp_packet.data.size();

    printf("Packet data size=%d\n", messageLength);

    std::string muid(cdp_packet.muid.begin(), cdp_packet.muid.end());
    std::string sduid(cdp_packet.sduid.begin(), cdp_packet.sduid.end());
    std::string cdpTopic = cdp_packet.topicToString();

    printf("[HUB] got topic: %s from %s\n",cdpTopic.c_str(), sduid.c_str());

    // Counter Message
    std::string payload(cdp_packet.data.begin(), cdp_packet.data.end());

    // Build JSON message matching official PapaDuck format
    // Reference: ClusterDuck-Protocol/examples/Basic-Ducks/PapaDuck/PapaDuck.ino
    doc["from"] = "hub";
    doc["to"] = "controller";
    doc["RE"] = false;
    doc["eventType"] = cdpTopic.c_str();
    doc["MessageID"] = muid.c_str();
    
    // Payload fields at root level (official PapaDuck format)
    doc["payload"]["hops"] = cdp_packet.hopCount;
    doc["payload"]["duckType"] = cdp_packet.duckType;
    doc["payload"]["DeviceID"] = sduid.c_str();
    
    // For RREQ/RREP packets, extract and include the routing path
    // Don't include raw Message field since we're parsing it into structured data
    if (cdp_packet.topic == reservedTopic::rreq || cdp_packet.topic == reservedTopic::rrep) {
        printf("[HUB] Processing route packet, data size: %zu\n", cdp_packet.data.size());
        
        try {
            RouteJSON routeDoc(cdp_packet.data);
            
            // Extract origin and destination as strings
            Duid originDuid = routeDoc.getOrigin();
            Duid destDuid = routeDoc.getDestination();
            std::string originStr = duckutils::toString(originDuid);
            std::string destStr = duckutils::toString(destDuid);
            
            doc["payload"]["origin"] = originStr.c_str();
            doc["payload"]["destination"] = destStr.c_str();
            
            // Add path as JSON array
            JsonArray pathArray = doc["payload"]["path"].to<JsonArray>();
            
            const auto& pathVec = routeDoc.getPath();
            printf("[HUB] Parsed RouteJSON, path vector size: %zu\n", pathVec.size());
            
            if (pathVec.empty()) {
                // If path is empty, use the source device ID as fallback
                pathArray.add(sduid.c_str());
                printf("[HUB] Route packet has empty path, using DeviceID '%s' as fallback\n", sduid.c_str());
            } else {
                for (const auto& node : pathVec) {
                    pathArray.add(node);
                    printf("[HUB] Added path node: %s\n", node.c_str());
                }
                printf("[HUB] Route packet with path size: %zu\n", pathVec.size());
            }
        } catch (const std::exception& e) {
            printf("[HUB] ERROR: Exception parsing RouteJSON: %s, using DeviceID as fallback\n", e.what());
            doc["payload"]["origin"] = sduid.c_str();
            doc["payload"]["destination"] = "UNKNOWN";
            JsonArray pathArray = doc["payload"]["path"].to<JsonArray>();
            pathArray.add(sduid.c_str());
        }
    } else {
        // For non-routing packets, include the raw message.
        // gps/alert/status/health payloads may be protobuf-encoded (see
        // duck_payloads.proto) instead of the legacy plain-text format;
        // detect that via the leading format-marker byte and reconstruct
        // the equivalent legacy text so downstream consumers of
        // payload.Message keep working unchanged.
        const uint8_t *rawData = reinterpret_cast<const uint8_t *>(cdp_packet.data.data());
        size_t rawLength = cdp_packet.data.size();
        std::string message = payload;

        // sealed_uplink/encrypted_data carry a
        // cleartext, AAD-authenticated real-topic byte as the first byte of
        // the data section (see Duck::sendSealedData()/sendEncryptedData(),
        // meshbeacon-firmware's src/Ducks/Duck.h), followed by
        // ephemeralPublicKey||nonce||ciphertext||tag (or
        // nonce||ciphertext||tag for encrypted_data). Recover that real
        // topic into eventType and record which encrypted transport this
        // came in on via payload.transport -- ProcessMqttMessage.php
        // (OpenDMS) needs both to rebuild the AAD and pick
        // unsealFromDuck() vs decryptFromDuck(). Without this,
        // every encrypted uplink (including an encrypted SOS) is reported
        // with eventType=="sealed_uplink"/"encrypted_data", payload.transport
        // is never set, decryption is never attempted, and the operator sees
        // the raw base64 ciphertext instead of the parsed SOS/alert/status
        // data.
        std::string realTopicName;
        if ((cdp_packet.topic == topics::sealed_uplink ||
             cdp_packet.topic == topics::encrypted_data) &&
            !payload.empty()) {
            CdpPacket realTopicPacket;
            realTopicPacket.topic = static_cast<uint8_t>(payload[0]);
            realTopicName = realTopicPacket.topicToString();
            doc["eventType"] = realTopicName.c_str();
            doc["payload"]["transport"] = cdpTopic.c_str();
            message = payload.substr(1);
            rawData = reinterpret_cast<const uint8_t *>(message.data());
            rawLength = message.size();
        }

        if (duckpayload::isProtobuf(rawData, rawLength)) {
            switch (cdp_packet.topic) {
                case topics::gps: {
                    duckcdp::GpsReading gps;
                    if (duckpayload::decodeGps(rawData, rawLength, gps)) {
                        message = duckpayload::gpsToLegacyText(gps);
                    }
                    break;
                }
                case topics::alert: {
                    duckcdp::SosAlert sos;
                    if (duckpayload::decodeSos(rawData, rawLength, sos)) {
                        message = duckpayload::sosToLegacyText(sos);
                    }
                    break;
                }
                case topics::status: {
                    // `status` carries a StatusReport envelope, which wraps
                    // either a SosAlert (phone-triggered SOS) or a StatusMsg
                    // (phone-composed message / device "Roger" ack) -- unlike
                    // `alert`, which always carries a bare SosAlert.
                    duckcdp::StatusReport report;
                    if (duckpayload::decodeStatusReport(rawData, rawLength, report)) {
                        message = duckpayload::statusReportToLegacyText(report);
                    }
                    break;
                }
                case topics::health: {
                    duckcdp::HealthStatus health;
                    if (duckpayload::decodeHealth(rawData, rawLength, health)) {
                        message = duckpayload::healthToLegacyText(health);
                    }
                    break;
                }
                case 26: {  // MamaDuck-to-MamaDuck (MTALK); example-sketch-only
                            // topic, not part of the core topics:: enum.
                    duckcdp::MTalk mtalk;
                    if (duckpayload::decodeMTalk(rawData, rawLength, mtalk)) {
                        message = duckpayload::mtalkToLegacyText(mtalk);
                    }
                    break;
                }
                case 22:
                case 23:
                case 24:
                case 25: {  // Operator/mesh free-text channels (message/dcmd,
                            // alert, emergency broadcast, personal message);
                            // example-sketch-only topics, not part of the
                            // core topics:: enum.
                    duckcdp::OpText opText;
                    if (duckpayload::decodeOpText(rawData, rawLength, opText)) {
                        message = opText.text();
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // `message` may still be an opaque binary blob here -- e.g. a
        // sealed_uplink/encrypted_data ciphertext or an identity_announce
        // raw public key, none of which are protobuf-decodable text. Only
        // the ciphertext/key bytes themselves are ever opaque; the CDP
        // topic byte is always sent in the clear by the firmware, so we
        // don't need to special-case any particular reserved topic here --
        // we just detect whether what we're about to embed in a JSON
        // string is safe printable text, and base64-encode it if not.
        const unsigned char *msgBytes = reinterpret_cast<const unsigned char *>(message.data());
        if (is_safe_json_text(msgBytes, message.size())) {
            doc["payload"]["Message"] = message.c_str();
        } else {
            std::vector<char> encoded(safe_b64_encoded_len(message.size()) + 1);
            int encodedLen = safe_b64_encode(msgBytes, message.size(), encoded.data(), encoded.size());
            if (encodedLen < 0) {
                printf("[HUB] ERROR: failed to base64-encode binary payload (topic=%d, size=%zu)\n",
                       cdp_packet.topic, message.size());
                doc["payload"]["Message"] = "";
            } else {
                doc["payload"]["Message"] = encoded.data();
                doc["payload"]["encoding"] = "base64";
            }
        }
    }

    std::string jsonstat;
    serializeJson(doc, jsonstat);
    printf("%s\n",jsonstat.c_str());
    
    // Publish to MQTT if enabled
    // Note: mqtt_publish_message() copies the message internally (either to queue or MQTT lib)
    // so it's safe to pass a pointer to local string
    mqtt_publish_message("", jsonstat.c_str(), jsonstat.length());
    
    printf("[HUB] processMessageFromDucks completed\n");
}

// The callback method simply takes the incoming packet and
// converts it to a JSON string, before sending it out over MQTT
void handleDuckData(CdpPacket receivedPacket) {
  printf("[HUB] got packet\n");
  processMessageFromDucks(receivedPacket);
}

// C wrapper for initialization and setup
extern "C" void* hub_init_and_setup(void) {
    printf("[HUB] Initializing ClusterDuck Protocol...\n");
    
    // Set up the LoRa radio (this will register callbacks)
    printf("[HUB] Calling hub.begin()...\n");
    int err = hub.begin();
    printf("[HUB] hub.begin() returned: %d\n", err);
    
    if (err != DUCK_ERR_NONE) {
        printf("[HUB] ERROR: Failed to setup LoRa radio, err=%d\n", err);
        return nullptr;
    }
    
    // Set the receive callback
    printf("[HUB] Setting receive callback...\n");
    hub.onReceiveDuckData(handleDuckData);
    
    printf("[HUB] ClusterDuck Protocol initialized successfully\n");
    return (void*)&hub;
}

// C wrapper to call the hub's main processing loop
extern "C" void hub_run_loop(void) {
    // PapaDuck in gateway mode uses processPackets() instead of main()
    // main() has network joining logic which PapaDucks don't need
    hub.processPackets();
}

// C wrapper to send data/commands from PapaDuck to MamaDucks
// topic: 0-19 are reserved, use 20+ for custom application messages
// targetDevice: Device ID as 8-byte array (use all 0xFF for broadcast)
// Returns 0 on success, error code otherwise
extern "C" int hub_send_data(uint8_t topic, const char* message, int length, const uint8_t* targetDevice) {
    if (message == NULL || length <= 0 || length > 256) {
        printf("[HUB] ERROR: Invalid message parameters\n");
        return -1;
    }
    
    // Convert target device to Duid (std::array)
    std::array<uint8_t, 8> target;
    if (targetDevice != NULL) {
        std::copy(targetDevice, targetDevice + 8, target.begin());
    } else {
        // Default to broadcast
        target.fill(0xFF);
    }
    
    printf("[HUB] Sending message: topic=%d, length=%d, target=", topic, length);
    for (int i = 0; i < 8; i++) {
        printf("%02X", target[i]);
    }
    printf("\n");
    
    // Send data using the hub's sendData method
    int err = hub.sendData(topic, (const uint8_t*)message, length, target);
    
    if (err != DUCK_ERR_NONE) {
        printf("[HUB] ERROR: Failed to send data, err=%d\n", err);
        return err;
    }
    
    printf("[HUB] Message sent successfully\n");
    return 0;
}
