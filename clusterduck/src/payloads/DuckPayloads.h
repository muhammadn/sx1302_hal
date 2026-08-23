/**
 * @file DuckPayloads.h
 * @brief Decode helpers for protobuf-based CDP LoRa payloads, plus
 *  legacy-text reconstruction so downstream consumers of the gateway's JSON
 *  output (which still expect a comma-delimited "KEY:VALUE,..." Message
 *  string) keep working unchanged.
 *
 * Generated from the same duck_payloads.proto schema used by the duck
 * firmware (compiled there with the nanopb plugin; compiled here with
 * standard protoc --cpp_out).
 *
 * Every payload produced by the firmware starts with a one-byte format
 * marker (see `duckpayload::Format`) so this code can tell a new
 * protobuf-encoded payload apart from a legacy plain-text one during a
 * mixed-version rollout.
 *
 * @copyright
 */
#ifndef DUCKPAYLOADS_H
#define DUCKPAYLOADS_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "duck_payloads.pb.h"

namespace duckpayload {

/**
 * @brief First byte of every CdpPacket DATA payload produced by the
 * firmware for the gps/alert/status/health topics.
 */
enum class Format : uint8_t {
  kLegacyText = 0x00, ///< pre-existing plain-text AT-command-style payload
  kProtobuf = 0x01,   ///< payload encoded with duck_payloads.proto messages
};

/**
 * @brief Returns true if the payload starts with the protobuf format marker.
 */
bool isProtobuf(const uint8_t *data, size_t length);

/**
 * @brief Decode a GpsReading message, including the leading marker byte.
 */
bool decodeGps(const uint8_t *data, size_t length, duckcdp::GpsReading &out);

/**
 * @brief Decode a SosAlert message, including the leading marker byte.
 */
bool decodeSos(const uint8_t *data, size_t length, duckcdp::SosAlert &out);

/**
 * @brief Decode a HealthStatus message, including the leading marker byte.
 */
bool decodeHealth(const uint8_t *data, size_t length,
                   duckcdp::HealthStatus &out);

/**
 * @brief Decode an MTalk message (topic 26), including the leading marker
 * byte. The gateway only decodes/logs MTalk traffic -- it never originates
 * MTalk messages itself.
 */
bool decodeMTalk(const uint8_t *data, size_t length, duckcdp::MTalk &out);

/**
 * @brief Decode a StatusReport message (sent on the `status` topic),
 * including the leading marker byte.
 */
bool decodeStatusReport(const uint8_t *data, size_t length,
                         duckcdp::StatusReport &out);

/**
 * @brief Decode an OpText message (topics 22/23/24/25), including the
 * leading marker byte.
 */
bool decodeOpText(const uint8_t *data, size_t length, duckcdp::OpText &out);

/**
 * @brief Reconstruct a comma-delimited "KEY:VALUE,..." string from a decoded
 * GpsReading, using the same key names as the pre-migration plain-text
 * payload (SRC, FIX, REASON, LAT, LNG, ALT, SPD, HDG, BATT).
 */
std::string gpsToLegacyText(const duckcdp::GpsReading &reading);

/**
 * @brief Reconstruct a comma-delimited "KEY:VALUE,..." string from a decoded
 * SosAlert, using the same key names as the pre-migration plain-text
 * payload (SRC, LAT, LNG, ALT, SPD, HDG, BATT). The duck ID is intentionally
 * not included here -- it is already available as the CdpPacket source
 * device ID (`sduid`).
 */
std::string sosToLegacyText(const duckcdp::SosAlert &alert);

/**
 * @brief Reconstruct the pre-migration "C:<counter>|FM:<free_memory>" string
 * from a decoded HealthStatus.
 */
std::string healthToLegacyText(const duckcdp::HealthStatus &status);

/**
 * @brief Reconstruct the pre-migration MTalk wire text from a decoded MTalk
 * message: "<text>[,MID:<mid>]" for MTALK_MSG, or "[MACK:<mid>]" for
 * MTALK_ACK.
 */
std::string mtalkToLegacyText(const duckcdp::MTalk &msg);

/**
 * @brief Reconstruct the pre-migration `status` topic wire text from a
 * decoded StatusMsg: "MSG,SRC:DEVICE,TEXT:<text>" for a device-composed
 * message (e.g. "Roger"), or "MSG,URGENCY:<u>,LAT:<lat>,LNG:<lng>,TEXT:<text>"
 * for a phone-composed one.
 */
std::string statusMsgToLegacyText(const duckcdp::StatusMsg &msg);

/**
 * @brief Reconstruct the pre-migration `status` topic wire text from a
 * decoded StatusReport, dispatching to sosToLegacyText() or
 * statusMsgToLegacyText() depending on which oneof branch is set.
 */
std::string statusReportToLegacyText(const duckcdp::StatusReport &report);

} // namespace duckpayload

#endif // DUCKPAYLOADS_H
