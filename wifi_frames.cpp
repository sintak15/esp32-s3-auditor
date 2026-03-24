#include "wifi_frames.h"
#include <string.h>

void build_deauth_frame(const uint8_t* bssid, const uint8_t* client_mac, uint8_t* frame_buffer) {
    struct WifiMgmtHdr* hdr = (struct WifiMgmtHdr*)frame_buffer;
    hdr->frame_control = 0xC000; // Deauthentication
    hdr->duration = 0;
    memcpy(hdr->dest_addr, client_mac, 6);
    memcpy(hdr->source_addr, bssid, 6);
    memcpy(hdr->bssid, bssid, 6);
    hdr->seq_ctrl = 0;

    // Reason code (2 bytes)
    frame_buffer[sizeof(struct WifiMgmtHdr)] = 7; // Class 3 frame received from nonassociated STA
    frame_buffer[sizeof(struct WifiMgmtHdr) + 1] = 0;
}

void build_beacon_frame(const uint8_t* bssid, const char* ssid, uint8_t channel, uint8_t* frame_buffer, uint16_t* frame_len) {
    struct WifiMgmtHdr* hdr = (struct WifiMgmtHdr*)frame_buffer;
    hdr->frame_control = 0x8000; // Beacon
    hdr->duration = 0;
    memset(hdr->dest_addr, 0xFF, 6); // Broadcast
    memcpy(hdr->source_addr, bssid, 6);
    memcpy(hdr->bssid, bssid, 6);
    hdr->seq_ctrl = 0;

    uint8_t* body = frame_buffer + sizeof(struct WifiMgmtHdr);
    uint16_t body_len = 0;

    // Timestamp (8 bytes)
    memset(body, 0, 8);
    body += 8;
    body_len += 8;

    // Beacon interval (2 bytes)
    body[0] = 0x64; // 100 TU
    body[1] = 0x00;
    body += 2;
    body_len += 2;

    // Capability info (2 bytes)
    body[0] = 0x01;
    body[1] = 0x04;
    body += 2;
    body_len += 2;

    // SSID parameter set
    uint8_t ssid_len = strlen(ssid);
    body[0] = 0; // Tag Number: SSID
    body[1] = ssid_len; // Tag Length
    memcpy(body + 2, ssid, ssid_len);
    body += 2 + ssid_len;
    body_len += 2 + ssid_len;

    // Supported rates
    body[0] = 1; // Tag Number: Supported Rates
    body[1] = 8; // Tag Length
    body[2] = 0x82; // 1 Mbps (B)
    body[3] = 0x84; // 2 Mbps (B)
    body[4] = 0x8B; // 5.5 Mbps (B)
    body[5] = 0x96; // 11 Mbps (B)
    body[6] = 0x0C; // 6 Mbps
    body[7] = 0x12; // 9 Mbps
    body[8] = 0x18; // 12 Mbps
    body[9] = 0x24; // 18 Mbps
    body += 10;
    body_len += 10;

    // DS parameter set
    body[0] = 3; // Tag Number: DS Parameter Set
    body[1] = 1; // Tag Length
    body[2] = channel;
    body += 3;
    body_len += 3;

    *frame_len = sizeof(struct WifiMgmtHdr) + body_len;
}
