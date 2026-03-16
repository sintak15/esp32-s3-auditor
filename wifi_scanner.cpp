#include "wifi_scanner.h"
#include "constants.h"

static AppContext *context = nullptr;

void wifi_scanner_init(AppContext *ctx) {
    context = ctx;
}

const char* enc_str(uint8_t enc) {
    switch (enc) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK: return "WAPI";
        default: return "UNKNOWN";
    }
}

void wifi_scanner_task(void *param) {

    while (true) {

        int n = WiFi.scanNetworks();

        if (!context) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        context->scan_count = 0;

        for (int i = 0; i < n && i < MAX_SCAN_RESULTS; i++) {

            ScanResult &r = context->scan_results[i];

            r.ssid = WiFi.SSID(i);
            r.rssi = WiFi.RSSI(i);
            r.channel = WiFi.channel(i);
            r.encryption = WiFi.encryptionType(i);

            context->scan_count++;
        }

        WiFi.scanDelete();

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}