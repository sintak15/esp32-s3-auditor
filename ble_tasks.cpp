#include "ble_tasks.h"
#include "constants.h"
#include <Arduino.h>
#include <cstring>

extern lv_obj_t *lbl_ble_status, *btn_ble_flood, *btn_ble_sniff;

static AppContext* ble_context = nullptr;
static BLESniffCB* ble_sniffer_cb_instance = nullptr;

static void set_btn_label(lv_obj_t* btn, const char* text) {
    if (!btn) return;
    lv_obj_t* child = lv_obj_get_child(btn, 0);
    if (child) lv_label_set_text(child, text);
}

static void set_status(const char* text) {
    if (lbl_ble_status) lv_label_set_text(lbl_ble_status, text);
}

BLESniffCB::BLESniffCB(AppContext* context) : app_context(context) {}

void BLESniffCB::onResult(const NimBLEAdvertisedDevice* dev) {
    if (!app_context || !dev) return;

    BleState& ble = app_context->ble;
    ble.packet_count++;

    const std::string mac = dev->getAddress().toString();
    const uint8_t slot = ble.ring_head % BLE_RING_SIZE;

    strncpy(ble.ring_buf[slot].mac, mac.c_str(), sizeof(ble.ring_buf[slot].mac) - 1);
    ble.ring_buf[slot].mac[sizeof(ble.ring_buf[slot].mac) - 1] = '\0';
    ble.ring_buf[slot].rssi = dev->getRSSI();
    ble.ring_buf[slot].fresh = true;
    ble.ring_head++;

    ble.last_mac = ble.ring_buf[slot].mac;
}

void ble_tasks_init(AppContext* context) {
    ble_context = context;
    if (!ble_sniffer_cb_instance) {
        ble_sniffer_cb_instance = new BLESniffCB(context);
    }
    if (!context) return;

    context->ble.sniff_active = false;
    context->ble.flood_active = false;
    context->ble.busy = false;
    context->ble.nimble_ready = false;
    context->ble.scanner = nullptr;
    context->ble.packet_count = 0;
    context->ble.last_mac = "";
    context->ble.ring_head = 0;
    memset(context->ble.ring_buf, 0, sizeof(context->ble.ring_buf));
    context->ble.unique_macs.clear();
}

void stop_ble(AppContext* context) {
    if (!context) return;

    context->ble.sniff_active = false;
    context->ble.flood_active = false;
    context->ble.busy = false;

    if (context->ble.scanner) {
        context->ble.scanner->stop();
        context->ble.scanner->setScanCallbacks(nullptr);
        context->ble.scanner = nullptr;
    }

    if (context->ble.nimble_ready) {
        NimBLEDevice::deinit(true);
        context->ble.nimble_ready = false;
    }

    set_btn_label(btn_ble_sniff, "START BLE SNIFF");
    set_btn_label(btn_ble_flood, "START BLE FLOOD");
    set_status("#00FFCC BLE READY#\n\nBLE tasks are idle.");
}

void start_ble_sniff(AppContext* context) {
    if (!context || context->ble.busy) return;
    if (context->ble.flood_active) stop_ble(context);

    context->ble.busy = true;
    context->ble.packet_count = 0;
    context->ble.last_mac = "";
    context->ble.ring_head = 0;
    memset(context->ble.ring_buf, 0, sizeof(context->ble.ring_buf));

    if (!context->ble.nimble_ready) {
        NimBLEDevice::init("");
        context->ble.nimble_ready = true;
    }

    context->ble.scanner = NimBLEDevice::getScan();
    if (context->ble.scanner && ble_sniffer_cb_instance) {
        context->ble.scanner->setScanCallbacks(ble_sniffer_cb_instance, true);
        context->ble.scanner->setActiveScan(true);
        context->ble.scanner->setInterval(90);
        context->ble.scanner->setWindow(45);
        context->ble.scanner->start(0, false, true);
        context->ble.sniff_active = true;
        set_btn_label(btn_ble_sniff, "STOP BLE SNIFF");
        set_status("#00FFCC BLE SNIFFER#\n\nListening for nearby BLE advertisements.");
    } else {
        set_status("#FF4444 BLE ERROR#\n\nFailed to initialize BLE scanner.");
    }

    context->ble.busy = false;
}

void start_ble_flood(AppContext* context) {
    if (!context || context->ble.busy) return;

    context->ble.flood_active = false;
    set_btn_label(btn_ble_flood, "START BLE FLOOD");
    set_status("#FFAA00 BLE ACTION DISABLED#\n\nThis build keeps the project compile-safe and leaves BLE advertising disabled.");
}

void process_ble_sniff_ui(AppContext* context) {
    if (!context || !context->ble.sniff_active) return;

    static uint32_t last_ui = 0;
    if (millis() - last_ui < 1000) return;

    char buf[256];
    snprintf(buf, sizeof(buf),
             "#00FFCC BLE SNIFFER#\n\nPackets: %lu\nLast: %s",
             static_cast<unsigned long>(context->ble.packet_count),
             context->ble.last_mac.length() ? context->ble.last_mac.c_str() : "-");
    set_status(buf);
    last_ui = millis();
}

void process_ble_flood(AppContext* context) {
    (void)context;
    // Intentionally disabled in this compile-safe replacement.
}
