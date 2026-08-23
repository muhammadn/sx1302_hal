/**
 * @file DuckPayloads.cpp
 * @brief Implementation of the gateway-side protobuf decode + legacy-text
 * reconstruction helpers.
 *
 * @copyright
 */
#include "DuckPayloads.h"

#include <cstdio>

namespace duckpayload {

namespace {

const char *gpsSourceToString(duckcdp::GpsSource source) {
  switch (source) {
    case duckcdp::GPS_SOURCE_DEVICE: return "DEVICE";
    case duckcdp::GPS_SOURCE_PHONE:  return "PHONE";
    case duckcdp::GPS_SOURCE_NONE:
    default:                         return "NONE";
  }
}

const char *noFixReasonToString(duckcdp::GpsNoFixReason reason) {
  switch (reason) {
    case duckcdp::GPS_REASON_NO_SIGNAL:   return "NO_SIGNAL";
    case duckcdp::GPS_REASON_NO_RESPONSE: return "NO_RESPONSE";
    case duckcdp::GPS_REASON_NONE:
    default:                              return "NONE";
  }
}

const char *sosOriginToString(duckcdp::SosOrigin origin) {
  switch (origin) {
    case duckcdp::SOS_ORIGIN_DEVICE: return "DEVICE";
    case duckcdp::SOS_ORIGIN_PHONE:  return "PHONE";
    case duckcdp::SOS_ORIGIN_UNKNOWN:
    default:                         return "UNKNOWN";
  }
}

template <typename T>
bool decodeWithMarker(const uint8_t *data, size_t length, T &out) {
  if (data == nullptr || length < 1 ||
      data[0] != static_cast<uint8_t>(Format::kProtobuf)) {
    return false;
  }
  return out.ParseFromArray(data + 1, static_cast<int>(length - 1));
}

} // namespace

bool isProtobuf(const uint8_t *data, size_t length) {
  return data != nullptr && length > 0 &&
         data[0] == static_cast<uint8_t>(Format::kProtobuf);
}

bool decodeGps(const uint8_t *data, size_t length, duckcdp::GpsReading &out) {
  return decodeWithMarker(data, length, out);
}

bool decodeSos(const uint8_t *data, size_t length, duckcdp::SosAlert &out) {
  return decodeWithMarker(data, length, out);
}

bool decodeHealth(const uint8_t *data, size_t length,
                   duckcdp::HealthStatus &out) {
  return decodeWithMarker(data, length, out);
}

bool decodeMTalk(const uint8_t *data, size_t length, duckcdp::MTalk &out) {
  return decodeWithMarker(data, length, out);
}

bool decodeStatusReport(const uint8_t *data, size_t length,
                         duckcdp::StatusReport &out) {
  return decodeWithMarker(data, length, out);
}

bool decodeOpText(const uint8_t *data, size_t length, duckcdp::OpText &out) {
  return decodeWithMarker(data, length, out);
}

std::string gpsToLegacyText(const duckcdp::GpsReading &reading) {
  char buf[160];
  if (!reading.has_fix()) {
    std::snprintf(buf, sizeof(buf), "GPS,FIX:0,SRC:%s,REASON:%s,BATT:%d",
                  gpsSourceToString(reading.source()),
                  noFixReasonToString(reading.no_fix_reason()),
                  reading.batt_pct());
    return std::string(buf);
  }

  std::string out = "GPS,SRC:" + std::string(gpsSourceToString(reading.source()));
  std::snprintf(buf, sizeof(buf), ",LAT:%.7f,LNG:%.7f",
                reading.lat_e7() / 1e7, reading.lng_e7() / 1e7);
  out += buf;
  if (reading.alt_m() != 0) {
    std::snprintf(buf, sizeof(buf), ",ALT:%d", reading.alt_m());
    out += buf;
  }
  if (reading.spd_dkmh() != 0) {
    std::snprintf(buf, sizeof(buf), ",SPD:%.1f", reading.spd_dkmh() / 10.0);
    out += buf;
  }
  if (reading.hdg_deg() != 0) {
    std::snprintf(buf, sizeof(buf), ",HDG:%u", reading.hdg_deg());
    out += buf;
  }
  std::snprintf(buf, sizeof(buf), ",BATT:%d", reading.batt_pct());
  out += buf;
  return out;
}

std::string sosToLegacyText(const duckcdp::SosAlert &alert) {
  char buf[160];
  std::string out = "SOS,SRC:" + std::string(sosOriginToString(alert.origin()));

  if (alert.has_gps()) {
    std::snprintf(buf, sizeof(buf), ",LAT:%.7f,LNG:%.7f",
                  alert.lat_e7() / 1e7, alert.lng_e7() / 1e7);
    out += buf;
    if (alert.alt_m() != 0) {
      std::snprintf(buf, sizeof(buf), ",ALT:%d", alert.alt_m());
      out += buf;
    }
    if (alert.spd_dkmh() != 0) {
      std::snprintf(buf, sizeof(buf), ",SPD:%.1f", alert.spd_dkmh() / 10.0);
      out += buf;
    }
    if (alert.hdg_deg() != 0) {
      std::snprintf(buf, sizeof(buf), ",HDG:%u", alert.hdg_deg());
      out += buf;
    }
    out += ",GPS:" + std::string(gpsSourceToString(alert.gps_source()));
  }

  std::snprintf(buf, sizeof(buf), ",BATT:%d", alert.batt_pct());
  out += buf;
  return out;
}

std::string healthToLegacyText(const duckcdp::HealthStatus &status) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "C:%u|FM:%d", status.counter(),
                status.free_memory());
  return std::string(buf);
}

std::string mtalkToLegacyText(const duckcdp::MTalk &msg) {
  if (msg.kind() == duckcdp::MTALK_ACK) {
    return "[MACK:" + msg.mid() + "]";
  }
  std::string out = msg.text();
  if (!msg.mid().empty()) {
    out += ",MID:" + msg.mid();
  }
  return out;
}

std::string statusMsgToLegacyText(const duckcdp::StatusMsg &msg) {
  if (msg.src() == duckcdp::STATUS_MSG_SRC_DEVICE) {
    return "MSG,SRC:DEVICE,TEXT:" + msg.text();
  }
  std::string lat, lng;
  if (msg.has_gps()) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.7f", msg.lat_e7() / 1e7);
    lat = buf;
    std::snprintf(buf, sizeof(buf), "%.7f", msg.lng_e7() / 1e7);
    lng = buf;
  }
  return "MSG,URGENCY:" + msg.urgency() + ",LAT:" + lat + ",LNG:" + lng +
         ",TEXT:" + msg.text();
}

std::string statusReportToLegacyText(const duckcdp::StatusReport &report) {
  if (report.has_sos()) {
    return sosToLegacyText(report.sos());
  }
  if (report.has_msg()) {
    return statusMsgToLegacyText(report.msg());
  }
  return "";
}

} // namespace duckpayload
