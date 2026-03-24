#pragma once
#include <stdint.h>

// All structs are packed to ensure they map directly to raw packet data
// without any compiler padding.

// Basic 802.11 management frame header
struct __attribute__((packed)) WifiMgmtHdr {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t  dest_addr[6];
    uint8_t  source_addr[6];
    uint8_t  bssid[6];
    uint16_t seq_ctrl;
};

// Frame Control field breakdown
// To use: auto* fc = (FrameControlField*)&hdr->frame_control;
struct __attribute__((packed)) FrameControlField {
    uint8_t protocol_version : 2;
    uint8_t type             : 2;
    uint8_t subtype          : 4;
    uint8_t flags;
};

// Probe Request frame body (after management header)
struct __attribute__((packed)) ProbeReqBody {
    uint8_t tag_number; // 0x00 for SSID
    uint8_t tag_length;
    char    ssid[0];    // Flexible array member for SSID
};

// EAPOL-Key frame structure for PMKID monitoring
// This is a simplified view focusing on the Key Information field
// and finding the Vendor Specific IE for PMKID.
struct __attribute__((packed)) EapolKeyFrame {
    uint8_t  llc_dsap;
    uint8_t  llc_ssap;
    uint8_t  llc_ctrl;
    uint8_t  org_code[3];
    uint16_t type; // 888e
    uint8_t  version;
    uint8_t  key_type;
    uint16_t key_length;
    uint64_t key_replay_counter;
    uint8_t  key_nonce[32];
    uint8_t  key_iv[16];
    uint8_t  key_rsc[8];
    uint8_t  key_id[8];
    uint8_t  key_mic[16];
    uint16_t key_data_length;
    uint8_t  key_data[0]; // Flexible array member
};

// Key Information field inside EAPOL-Key frame
struct __attribute__((packed)) KeyInfoField {
    uint16_t key_descriptor_version : 3;
    uint16_t key_type             : 1; // 1 = Pairwise
    uint16_t reserved1            : 2;
    uint16_t install              : 1;
    uint16_t key_ack              : 1;
    uint16_t key_mic              : 1;
    uint16_t secure               : 1;
    uint16_t error                : 1;
    uint16_t request              : 1;
    uint16_t encrypted_key_data   : 1;
    uint16_t smk_message          : 1;
    uint16_t reserved2            : 2;
};

// Vendor Specific IEEE 802.11 Tag for WPA
struct __attribute__((packed)) WpaVendorIe {
    uint8_t element_id; // 0xDD
    uint8_t length;
    uint8_t oui[3];     // 00-0F-AC for WPA
    uint8_t type;       // 0x04 for PMKID
    // Data follows
    uint8_t data[0]; // Flexible array member for PMKID data
};

void build_deauth_frame(const uint8_t* bssid, const uint8_t* client_mac, uint8_t* frame_buffer);
void build_beacon_frame(const uint8_t* bssid, const char* ssid, uint8_t channel, uint8_t* frame_buffer, uint16_t* frame_len);
