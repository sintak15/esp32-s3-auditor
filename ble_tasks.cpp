#include "ble_tasks.h"
#include "constants.h"
#include "sd_logger.h"
#include <NimBLEDevice.h>
#include <NimBLEAddress.h> // Explicitly include NimBLEAddress header
#include <esp_task_wdt.h>
#include <WiFi.h> // For WiFi.disconnect()
#include <esp_wifi.h> // For esp_wifi_get_mode
#include <esp_random.h>
#include "ui_events.h"

extern lv_obj_t *lbl_ble_status, *btn_ble_flood, *btn_ble_sniff;

static AppContext* ble_context = nullptr;
static BLESniffCB* ble_sniffer_cb_instance = nullptr;
static SemaphoreHandle_t ble_mutex = nullptr;

static void diag_ble_state(const char* where, AppContext* context) {
    Serial.printf("[BLE] %s sniff=%d flood=%d busy=%d ready=%d scanner=%p adv=%p heap=%u\n",
                  where,
                  context->ble.sniff_active,
                  context->ble.flood_active,
                  context->ble.busy,
                  context->ble.nimble_ready,
                  context->ble.scanner,
                  NimBLEDevice::getAdvertising(),
                  ESP.getFreeHeap());
}

// Implementation of the callback class
BLESniffCB::BLESniffCB(AppContext* context) : app_context(context) {}

void BLESniffCB::onResult(const NimBLEAdvertisedDevice *dev) {
    if (!app_context || !dev) return;
    BleState& ble = app_context->ble;

    MacAddress current_mac;
    memcpy(current_mac.data, dev->getAddress().getBase(), 6); // Explicitly get raw MAC address bytes

    if (ble_mutex && xSemaphoreTake(ble_mutex, portMAX_DELAY) == pdTRUE) {
        ble.packet_count++;
        uint8_t slot = ble.ring_head % BLE_RING_SIZE;
        strncpy(ble.ring_buf[slot].mac, dev->getAddress().toString().c_str(), 17);
        ble.ring_buf[slot].mac[17] = 0;
        ble.ring_buf[slot].rssi = dev->getRSSI();
        ble.ring_buf[slot].fresh = true;
        ble.ring_head++;
        ble.unique_macs.insert(current_mac); // Insert the MacAddress struct safely
        xSemaphoreGive(ble_mutex);
    }
}

void ble_tasks_init(AppContext* context) {
    ble_context = context;
    ble_sniffer_cb_instance = new BLESniffCB(context);
    ble_mutex = xSemaphoreCreateMutex();
}

void stop_ble(AppContext* context) {
    if (!context->ble.sniff_active && !context->ble.flood_active) return;

    diag_ble_state("stop_ble:enter", context);
    context->ble.busy = true;

    if (context->ble.sniff_active) {
        context->ble.sniff_active = false;
        if (context->ble.scanner) {
            context->ble.scanner->setScanCallbacks(nullptr);
            context->ble.scanner->stop();
        }
        if (btn_ble_sniff) lv_label_set_text(lv_obj_get_child(btn_ble_sniff, 0), "START BLE SNIFF");
    }

    if (context->ble.flood_active) {
        context->ble.flood_active = false;
        // Stop advertising safely before deinit
        NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
        if (adv) adv->stop();
        if (btn_ble_flood) lv_label_set_text(lv_obj_get_child(btn_ble_flood, 0), "START BLE FLOOD");
    }

        // Avoid calling NimBLEDevice::deinit(true) here!
        // It contains internal blocking loops that, combined with the delays above, stall the UI task
        // for 350ms+, starving lv_timer_handler and crashing the physics engine during navigation.
        /*
    if (context->ble.nimble_ready) {
        NimBLEDevice::deinit(true);
        context->ble.nimble_ready = false;
        context->ble.scanner = nullptr; // Ensure scanner is nulled after deinit
    } else {
        // If nimble_ready was false but sniff/flood active, it means something went wrong.
        // Log an error or handle it. For now, just ensure UI is reset.
        if (btn_ble_flood) lv_label_set_text(lv_obj_get_child(btn_ble_flood, 0), "START BLE FLOOD");
        if (btn_ble_sniff) lv_label_set_text(lv_obj_get_child(btn_ble_sniff, 0), "START BLE SNIFF");
        lv_label_set_text(lbl_ble_status, "#FF4444 BLE ERROR — NimBLE state inconsistent#");
    }
        */

    lv_label_set_text(lbl_ble_status, "#00FFCC BLE READY#\n\nSelect an action.");
    context->ble.busy = false;
    diag_ble_state("stop_ble:exit", context);
}

void start_ble_sniff(AppContext* context) {
    if (context->ble.busy) return;
    
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 50000) {
        Serial.println("[BLE] start denied: low internal heap");
        if (btn_ble_sniff) lv_label_set_text(lv_obj_get_child(btn_ble_sniff, 0), "START BLE SNIFF");
        lv_label_set_text(lbl_ble_status, "#FF4444 LOW MEMORY#\n\nCannot start BLE.");
        return;
    }
    
    diag_ble_state("start_ble_sniff:enter", context);
    context->ble.busy = true;

    if (context->ble.flood_active) stop_ble(context);

    context->wifi_scan.paused = true;
    esp_wifi_set_promiscuous(false);
    // Only call disconnect if WiFi was actually initialized
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        WiFi.disconnect(true);
    }
    delay(50);

    if (!context->ble.nimble_ready) {
        NimBLEDevice::init("");
        context->ble.nimble_ready = true;
    }
    context->ble.scanner = NimBLEDevice::getScan();
    context->ble.scanner->setScanCallbacks(ble_sniffer_cb_instance, true);
    context->ble.scanner->setActiveScan(true);
    context->ble.scanner->setInterval(100);
    context->ble.scanner->setWindow(99);
    context->ble.scanner->start(0, false, true);

    if (ble_mutex && xSemaphoreTake(ble_mutex, portMAX_DELAY) == pdTRUE) {
        context->ble.unique_macs.clear();
        xSemaphoreGive(ble_mutex);
    }
    context->ble.packet_count = 0;
    context->ble.sniff_active = true;
    context->ble.busy = false;
    diag_ble_state("start_ble_sniff:exit", context);
}

void start_ble_flood(AppContext* context) {
    if (context->ble.busy) return;
    
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 50000) {
        Serial.println("[BLE] start denied: low internal heap");
        if (btn_ble_flood) lv_label_set_text(lv_obj_get_child(btn_ble_flood, 0), "START BLE FLOOD");
        lv_label_set_text(lbl_ble_status, "#FF4444 LOW MEMORY#\n\nCannot start BLE.");
        return;
    }

    diag_ble_state("start_ble_flood:enter", context);
    context->ble.busy = true;

    if (context->ble.sniff_active) stop_ble(context);

    context->wifi_scan.paused = true;
    esp_wifi_set_promiscuous(false);
    // Only call disconnect if WiFi was actually initialized
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        WiFi.disconnect(true);
    }

    // Init NimBLE once here — process_ble_flood will reuse it each cycle
    // rather than deinit/reinit every 300ms (which causes PC=0x0 crash)
    if (!context->ble.nimble_ready) {
        NimBLEDevice::init("");
        context->ble.nimble_ready = true;
    }

    context->ble.flood_active = true;
    context->ble.busy = false;
    diag_ble_state("start_ble_flood:exit", context);
}

void process_ble_sniff_ui(AppContext* context) {
    if (!context->ble.sniff_active) return;
    BleState& ble = context->ble;
    uint32_t unique_size = 0;
    uint32_t pkt_count = 0;
    String last_mac_str = "";

    if (ble_mutex && xSemaphoreTake(ble_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        for (int i = 0; i < BLE_RING_SIZE; i++) {
            if (!ble.ring_buf[i].fresh) continue;
            ble.ring_buf[i].fresh = false;
            ble.last_mac = ble.ring_buf[i].mac; 
            if (sd_card_ready()) sd_log_ble_sniff(millis(), ble.ring_buf[i].mac, ble.ring_buf[i].rssi);
        }
        unique_size = ble.unique_macs.size();
        pkt_count = ble.packet_count;
        last_mac_str = ble.last_mac;
        xSemaphoreGive(ble_mutex);
    }
    
    static uint32_t last_ui = 0;
    if (millis() - last_ui < 1000) return;
    UiEvent* e = ui_queue.get_write_slot();
    if (e) {
        e->type = UiEvent::SET_BLE_STATUS;
        snprintf(e->text, sizeof(e->text), "#00FFCC BLE SNIFFER#\n\nPackets: %lu\nUnique: %lu\nLast: %s",
                 pkt_count, unique_size, last_mac_str.c_str());
        ui_queue.commit_write();
    }
    last_ui = millis();
}

void process_ble_flood(AppContext* context) {
    if (!context->ble.flood_active || context->ble.busy) return;
    static uint32_t last_ble = 0;
    if (millis() - last_ble < 300) return;

    uint32_t start = millis();

    // Guard: NimBLE must be initialized (done once in start_ble_flood)
    if (!context->ble.nimble_ready) return;

    context->ble.busy = true;

    // Reuse the existing NimBLE instance — just stop, update payload, restart.
    // Do NOT deinit/reinit every cycle: after deinit the host task is destroyed
    // and getAdvertising() returns a pointer with a NULL vtable, causing PC=0x0 crash.
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv) {
        Serial.println("[BLE] process_ble_flood: adv=null");
        context->ble.busy = false;
        return;
    }

    uint32_t t = millis();
    adv->stop();
    Serial.printf("[BLE] adv->stop dt=%lu\n", (unsigned long)(millis() - t));

    // Randomize the 6 filler bytes in the Apple proximity payload so each
    // beacon looks like a different device
    uint8_t ap[] = {0x4C,0x00,0x07,0x19,0x01,0x0A,0x20,0x55,0x14,0x58,0x6E,0x42,
                    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    esp_fill_random(&ap[12], 6); // randomize trailing bytes

    NimBLEAdvertisementData d;
    d.setManufacturerData(std::string((char*)ap, sizeof(ap)));
    
    t = millis();
    adv->setAdvertisementData(d);
    Serial.printf("[BLE] setAdvertisementData dt=%lu\n", (unsigned long)(millis() - t));
    
    // Configure for non-connectable, non-scannable advertisements
    // adv->setConnectable(false); // Set advertisement to non-connectable
    // adv->setScannable(false);   // Set advertisement to non-scannable
    t = millis();
    adv->start(0);              // Start advertising indefinitely (duration 0)
    Serial.printf("[BLE] adv->start dt=%lu total=%lu\n", (unsigned long)(millis() - t), (unsigned long)(millis() - start));

    last_ble = millis();
    context->ble.busy = false;
}
