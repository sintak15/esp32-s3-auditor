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
extern volatile bool req_stop_ble; // Allow memory bailouts to trigger UI cleanup

// Worker Task Queue definitions
enum BleCmdType {
    BLE_CMD_NONE,
    BLE_CMD_START_SNIFF,
    BLE_CMD_START_FLOOD,
    BLE_CMD_STOP
};
struct BleCmd {
    BleCmdType type;
};
static QueueHandle_t ble_cmd_q = nullptr;
static TaskHandle_t ble_task_handle = nullptr;

// FIX 2: BLE Cooldown Tracker
static uint32_t ble_last_switch = 0;
static bool ble_switch_allowed(uint32_t cooldown = 500) {
    uint32_t now = millis();
    if (now - ble_last_switch < cooldown) return false;
    ble_last_switch = now;
    return true;
}

// FIX 3: Reusable Advertisement object to avoid fragmentation
static NimBLEAdvertisementData g_adv_data;
static bool g_adv_initialized = false;

// FIX 4: Low Memory Bailout Helper
static bool ble_low_mem() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 20000;
}

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
        
        // FIX 5: Prevent ring buffer overwrite race
        uint8_t next = (ble.ring_head + 1) % BLE_RING_SIZE;
        if (next == ble.ring_tail) {
            xSemaphoreGive(ble_mutex); // buffer full, drop packet
            return;
        }
        
        uint8_t slot = ble.ring_head % BLE_RING_SIZE;
        strncpy(ble.ring_buf[slot].mac, dev->getAddress().toString().c_str(), 17);
        ble.ring_buf[slot].mac[17] = 0;
        ble.ring_buf[slot].rssi = dev->getRSSI();
        ble.ring_head = next;
        ble.unique_macs.insert(current_mac); // Insert the MacAddress struct safely
        xSemaphoreGive(ble_mutex);
    }
}

// --- INTERNAL HARDWARE FUNCTIONS (Runs on Core 0) ---

static void internal_stop_ble(AppContext* context) {
    diag_ble_state("internal_stop_ble:enter", context);
    context->ble.busy = true;

    if (context->ble.sniff_active) {
        context->ble.sniff_active = false;
        if (context->ble.scanner) {
            context->ble.scanner->setScanCallbacks(nullptr);
            context->ble.scanner->stop();
            context->ble.scanner->clearResults(); // FIX 1: Clear results on stop
        }
    }

    if (context->ble.flood_active) {
        context->ble.flood_active = false;
        // Stop advertising safely before deinit
        NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
        if (adv) adv->stop();
    }

    context->ble.busy = false;
    diag_ble_state("internal_stop_ble:exit", context);
}

static void internal_start_ble_sniff(AppContext* context) {
    diag_ble_state("internal_start_ble_sniff:enter", context);
    context->ble.busy = true;

    if (context->ble.flood_active) internal_stop_ble(context);

    context->wifi_scan.paused = true;
    esp_wifi_set_promiscuous(false);
    // Only call disconnect if WiFi was actually initialized
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        WiFi.disconnect(true);
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    if (!context->ble.nimble_ready) {
        NimBLEDevice::init("");
        context->ble.nimble_ready = true;
    }
    context->ble.scanner = NimBLEDevice::getScan();
    context->ble.scanner->setScanCallbacks(ble_sniffer_cb_instance, true);
    context->ble.scanner->setActiveScan(true);
    context->ble.scanner->setInterval(160); // FIX 6: Reduce scan pressure
    context->ble.scanner->setWindow(80);
    context->ble.scanner->start(0, false, true);

    if (ble_mutex && xSemaphoreTake(ble_mutex, portMAX_DELAY) == pdTRUE) {
        context->ble.unique_macs.clear();
        xSemaphoreGive(ble_mutex);
    }
    context->ble.packet_count = 0;
    context->ble.sniff_active = true;
    context->ble.busy = false;

    // FIX 7: Add heap debug
    Serial.printf("[BLE] heap=%u min=%u\n",
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

    diag_ble_state("internal_start_ble_sniff:exit", context);
}

static void internal_start_ble_flood(AppContext* context) {
    diag_ble_state("internal_start_ble_flood:enter", context);
    context->ble.busy = true;

    if (context->ble.sniff_active) internal_stop_ble(context);

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
    
    // FIX 7: Add heap debug
    Serial.printf("[BLE] heap=%u min=%u\n",
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

    diag_ble_state("internal_start_ble_flood:exit", context);
}

static void internal_process_ble_flood(AppContext* context) {
    if (!context->ble.flood_active || context->ble.busy) return;
    
    if (ble_low_mem()) {
        Serial.println("[BLE] stopping flood: low memory");
        req_stop_ble = true; // Signal main loop to push UI teardown safely
        return;
    }

    static uint32_t last_ble = 0;
    if (millis() - last_ble < 300) return;

    if (!context->ble.nimble_ready) return;

    context->ble.busy = true;
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv) {
        context->ble.busy = false;
        return;
    }

    adv->stop();

    uint8_t ap[] = {0x4C,0x00,0x07,0x19,0x01,0x0A,0x20,0x55,0x14,0x58,0x6E,0x42,
                    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    esp_fill_random(&ap[12], 6); 

    if (!g_adv_initialized) {
        g_adv_data = NimBLEAdvertisementData();
        g_adv_initialized = true;
    }
    g_adv_data.setManufacturerData(std::string((char*)ap, sizeof(ap)));
    adv->setAdvertisementData(g_adv_data);
    adv->start(0);              

    static uint32_t last_debug = 0;
    if (millis() - last_debug > 5000) {
        Serial.printf("[BLE] heap=%u min=%u\n",
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
        last_debug = millis();
    }

    last_ble = millis();
    context->ble.busy = false;
}

static void ble_worker_task(void* arg) {
    AppContext* context = (AppContext*)arg;
    BleCmd cmd;

    for (;;) {
        // Rapid response command polling
        if (xQueueReceive(ble_cmd_q, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
            switch (cmd.type) {
                case BLE_CMD_START_SNIFF: internal_start_ble_sniff(context); break;
                case BLE_CMD_START_FLOOD: internal_start_ble_flood(context); break;
                case BLE_CMD_STOP:        internal_stop_ble(context); break;
                default: break;
            }
        }

        // Background periodic flooding runs completely independent of UI
        if (context->ble.flood_active && !context->ble.busy) {
            internal_process_ble_flood(context);
        }
    }
}

// --- EXTERNAL API WRAPPERS (Safe for main_app_task & LVGL rendering) ---

void ble_tasks_init(AppContext* context) {
    ble_context = context;
    ble_sniffer_cb_instance = new BLESniffCB(context);
    ble_mutex = xSemaphoreCreateMutex();
    
    ble_cmd_q = xQueueCreate(10, sizeof(BleCmd));
    // Spin up BLE worker off the main rendering core 1, placing it entirely on core 0
    xTaskCreatePinnedToCore(ble_worker_task, "ble_task", 6144, context, 1, &ble_task_handle, 0); 
}

void stop_ble(AppContext* context) {
    if (!context->ble.sniff_active && !context->ble.flood_active) return;
    
    BleCmd cmd = {BLE_CMD_STOP};
    xQueueSend(ble_cmd_q, &cmd, 0);

    queue_local_ui_text(UI_EVT_SET_BLE_SNIFF_BUTTON, "START BLE SNIFF");
    queue_local_ui_text(UI_EVT_SET_BLE_FLOOD_BUTTON, "START BLE FLOOD");
    queue_local_ui_text(UI_EVT_SET_BLE_STATUS, "#00FFCC BLE READY#\n\nSelect an action.");
}

void start_ble_sniff(AppContext* context) {
    if (!ble_switch_allowed()) {
        Serial.println("[BLE] switch blocked (cooldown)");
        return;
    }

    if (context->ble.busy) return;
    
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 50000) {
        Serial.println("[BLE] start denied: low internal heap");
        queue_local_ui_text(UI_EVT_SET_BLE_SNIFF_BUTTON, "START BLE SNIFF");
        queue_local_ui_text(UI_EVT_SET_BLE_STATUS, "#FF4444 LOW MEMORY#\n\nCannot start BLE.");
        return;
    }

    BleCmd cmd = {BLE_CMD_START_SNIFF};
    xQueueSend(ble_cmd_q, &cmd, 0);
}

void start_ble_flood(AppContext* context) {
    if (!ble_switch_allowed()) {
        Serial.println("[BLE] switch blocked (cooldown)");
        return;
    }

    if (context->ble.busy) return;
    
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 50000) {
        Serial.println("[BLE] start denied: low internal heap");
        queue_local_ui_text(UI_EVT_SET_BLE_FLOOD_BUTTON, "START BLE FLOOD");
        queue_local_ui_text(UI_EVT_SET_BLE_STATUS, "#FF4444 LOW MEMORY#\n\nCannot start BLE.");
        return;
    }

    BleCmd cmd = {BLE_CMD_START_FLOOD};
    xQueueSend(ble_cmd_q, &cmd, 0);
}

void process_ble_sniff_ui(AppContext* context) {
    if (!context->ble.sniff_active) return;
    
    if (ble_low_mem()) {
        Serial.println("[BLE] stopping sniff: low memory");
        stop_ble(context);
        return;
    }

    BleState& ble = context->ble;
    uint32_t unique_size = 0;
    uint32_t pkt_count = 0;
    String last_mac_str = "";

    if (ble_mutex && xSemaphoreTake(ble_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // FIX 5: Proper ring buffer consumer
        while (ble.ring_tail != ble.ring_head) {
            uint8_t i = ble.ring_tail;
            ble.last_mac = ble.ring_buf[i].mac; 
            if (sd_card_ready()) sd_log_ble_sniff(millis(), ble.ring_buf[i].mac, ble.ring_buf[i].rssi);
            ble.ring_tail = (ble.ring_tail + 1) % BLE_RING_SIZE;
        }
        unique_size = ble.unique_macs.size();
        pkt_count = ble.packet_count;
        last_mac_str = ble.last_mac;
        xSemaphoreGive(ble_mutex);
    }
    
    static uint32_t last_ui = 0;
    if (millis() - last_ui < 1000) return;
    char buf[128];
    snprintf(buf, sizeof(buf), "#00FFCC BLE SNIFFER#\n\nPackets: %lu\nUnique: %lu\nLast: %s",
             pkt_count, unique_size, last_mac_str.c_str());
    queue_local_ui_text(UI_EVT_SET_BLE_STATUS, buf);
    last_ui = millis();
}
