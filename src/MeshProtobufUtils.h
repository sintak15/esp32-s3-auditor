#ifndef MESH_PROTOBUF_UTILS_H
#define MESH_PROTOBUF_UTILS_H

#include <pb_encode.h>
#include <pb_decode.h>
#include <vector>
#include "meshtastic/mesh.pb.h"

/**
 * @brief Quality of Life utilities for interacting with Meshtastic Nanopb structures
 */
namespace MeshUtils {

    /**
     * @brief Encodes a Nanopb message into a std::vector
     * 
     * @tparam T The Nanopb struct type
     * @param fields The field definition (e.g., meshtastic_MeshPacket_fields)
     * @param src The source message struct
     * @return std::vector<uint8_t> The encoded bytes, empty on failure
     */
    template <typename T>
    std::vector<uint8_t> encode(const pb_msgdesc_t* fields, const T& src) {
        size_t size;
        if (!pb_get_encoded_size(&size, fields, &src)) return {};

        std::vector<uint8_t> buffer(size);
        pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.size());

        if (!pb_encode(&stream, fields, &src)) return {};
        return buffer;
    }

    /**
     * @brief Decodes a buffer into a Nanopb struct
     */
    template <typename T>
    bool decode(const pb_msgdesc_t* fields, const uint8_t* buffer, size_t len, T& dest) {
        pb_istream_t stream = pb_istream_from_buffer(buffer, len);
        return pb_decode(&stream, fields, &dest);
    }

    /**
     * @brief Returns a human-readable string for a PortNum (useful for logging)
     */
    inline const char* getPortName(meshtastic_PortNum port) {
        switch (port) {
            case meshtastic_PortNum_TEXT_MESSAGE_APP: return "TEXT";
            case meshtastic_PortNum_POSITION_APP:     return "POS";
            case meshtastic_PortNum_NODEINFO_APP:     return "INFO";
            case meshtastic_PortNum_TELEMETRY_APP:    return "TELEMETRY";
            case meshtastic_PortNum_ADMIN_APP:        return "ADMIN";
            case meshtastic_PortNum_ROUTING_APP:      return "ROUTING";
            case meshtastic_PortNum_RANGE_TEST_APP:   return "RANGE_TEST";
            case meshtastic_PortNum_PRIVATE_APP:      return "PRIVATE";
            default: return "UNKNOWN";
        }
    }

    /**
     * @brief Returns a short string for common Hardware Models
     */
    inline const char* getHardwareName(meshtastic_HardwareModel hw) {
        switch (hw) {
            case meshtastic_HardwareModel_LILYGO_TBEAM_S3_CORE: return "T-Beam S3";
            case meshtastic_HardwareModel_HELTEC_V3:            return "Heltec V3";
            case meshtastic_HardwareModel_T_DECK:               return "T-Deck";
            case meshtastic_HardwareModel_T_WATCH_S3:           return "T-Watch S3";
            case meshtastic_HardwareModel_STATION_G2:           return "Station G2";
            default: return "Generic/Other";
        }
    }

    /**
     * @brief QoL Helper to create a Data message without manual flag management
     */
    inline meshtastic_Data createData(meshtastic_PortNum port, const uint8_t* payload, size_t len) {
        meshtastic_Data d = meshtastic_Data_init_default;
        d.portnum = port;
        
        size_t copyLen = (len > sizeof(d.payload.bytes)) ? sizeof(d.payload.bytes) : len;
        memcpy(d.payload.bytes, payload, copyLen);
        d.payload.size = (pb_size_t)copyLen;
        
        return d;
    }

    /**
     * @brief Simple helper to check if battery is charging based on Meshtastic logic
     */
    inline bool isCharging(uint32_t batteryLevel) { return batteryLevel > 100; }

    /**
     * @brief Converts Meshtastic fixed-point coordinate to double
     */
    inline double fixedToDouble(int32_t fixed) {
        return fixed / 1e7;
    }

    /**
     * @brief Converts double coordinate to Meshtastic fixed-point
     */
    inline int32_t doubleToFixed(double val) {
        return static_cast<int32_t>(val * 1e7);
    }

    /**
     * @brief QoL Helper to initialize a Position message with standard values
     */
    inline meshtastic_Position createPosition(double lat, double lon, int32_t alt) {
        meshtastic_Position p = meshtastic_Position_init_default;
        p.latitude_i = doubleToFixed(lat);
        p.has_latitude_i = true;
        p.longitude_i = doubleToFixed(lon);
        p.has_longitude_i = true;
        if (alt != 0) {
            p.altitude = alt;
            p.has_altitude = true;
        }
        p.time = 0; // Should be set by RTC/GPS provider
        return p;
    }

    /**
     * @brief Quality of Life helper to calculate battery percentage from millivolts.
     * Li-Po discharge curves are non-linear; this uses a multi-point approximation
     * suitable for the ESP32-S3 hardware.
     */
    inline uint32_t mvoltsToPct(uint32_t mvolts) {
        if (mvolts >= 4050) return 100; // Pad 100% to include full resting voltage (e.g., 4.05V)
        if (mvolts >= 4000) return 90;  // 4.00V
        if (mvolts >= 3900) return 80;  // 3.90V
        if (mvolts >= 3800) return 60;
        if (mvolts >= 3700) return 40;
        if (mvolts >= 3600) return 20;
        if (mvolts >= 3500) return 10;
        return 0;
    }

    /**
     * @brief Detects charging status based on voltage threshold for CYD/S3 hardware.
     * USB VBUS typically pulls the measured battery voltage node above 4.3V.
     */
    inline bool isHardwareCharging(uint32_t mvolts) {
        return mvolts > 4130;
    }
}

#endif // MESH_PROTOBUF_UTILS_H
