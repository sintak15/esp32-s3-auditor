#include "ble_tasks.h"
#include "constants.h"
#include "sd_logger.h"
#include <NimBLEDevice.h>
#include <NimBLEAddress.h> // Explicitly include NimBLEAddress header
#include <esp_task_wdt.h>
#include <WiFi.h> // For WiFi.disconnect()
#include <esp_wifi.h> // For esp_wifi_get_mode
#include <esp_random.h>
#include <esp_bt.h>
#include "ui_events.h"

extern lv_obj_t *lbl_ble_status, *btn_ble_adv_test, *btn_ble_scan;

static AppContext* ble_context = nullptr;
static BLEScanCB* ble_scan_cb_instance = nullptr;
static SemaphoreHandle_t ble_mutex = nullptr;
extern volatile bool req_stop_ble; // Allow memory bailouts to trigger UI cleanup

// Worker Task Queue definitions
enum BleCmdType {
    BLE_CMD_NONE,
    BLE_CMD_START_SCAN,
    BLE_CMD_START_ADV_TEST,
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

// FIX 3: Harden BLE startup against null-pointer crashes
static bool g_bleHostInit = false;

static bool ensure_ble_host_ready() {
    if (g_bleHostInit) return true;
    if (!ble_mutex) return false;
    if (xSemaphoreTake(ble_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return false;
    if (!g_bleHostInit) {
        try {
            NimBLEDevice::init("");
            NimBLEDevice::setPower(ESP_PWR_LVL_P9);
            NimBLEDevice::setMTU(23);                // minimum MTU
            g_bleHostInit = true;
            Serial.printf("[BLE] host init ok, heap=%u\n", ESP.getFreeHeap());
        } catch (...) {
            Serial.println("[BLE] host init exception");
            g_bleHostInit = false;
        }
    }
    xSemaphoreGive(ble_mutex);
    return g_bleHostInit;
}

// FIX 4: Low Memory Bailout Helper
static bool ble_low_mem() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 20000;
}

static void diag_ble_state(const char* where, AppContext* context) {
    Serial.printf("[BLE] %s scan=%d advTest=%d busy=%d ready=%d scanner=%p adv=%p heap=%u\n",
                  where,
                  context->ble.scan_active,
                  context->ble.adv_test_active,
                  context->ble.busy,
                  context->ble.nimble_ready,
                  context->ble.scanner,
                  NimBLEDevice::getAdvertising(),
                  ESP.getFreeHeap());
}

// Implementation of the callback class
BLEScanCB::BLEScanCB(AppContext* context) : app_context(context) {}

void BLEScanCB::onResult(const NimBLEAdvertisedDevice *dev) {
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

    if (context->ble.scan_active) {
        context->ble.scan_active = false;
        if (context->ble.scanner) {
            context->ble.scanner->setScanCallbacks(nullptr);
            context->ble.scanner->stop();
            context->ble.scanner->clearResults(); // FIX 1: Clear results on stop
        }
    }

    if (context->ble.adv_test_active) {
        context->ble.adv_test_active = false;
        // Stop advertising safely before deinit
        NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
        if (adv) adv->stop();
    }

    context->ble.busy = false;
    diag_ble_state("internal_stop_ble:exit", context);
}

static void internal_start_ble_scan(AppContext* context) {
    diag_ble_state("internal_start_ble_scan:enter", context);
    
    Serial.printf("[BLE] start request: scanner=%p adv=%p cb=%p heap=%u scan=%d advTest=%d busy=%d ready=%d\n",
                  (void*)context->ble.scanner,
                  (void*)NimBLEDevice::getAdvertising(),
                  (void*)ble_scan_cb_instance,
                  ESP.getFreeHeap(),
                  context->ble.scan_active,
                  context->ble.adv_test_active,
                  context->ble.busy,
                  context->ble.nimble_ready);
                  
    context->ble.busy = true;

    if (context->ble.adv_test_active) internal_stop_ble(context);

    if (!ensure_ble_host_ready()) {
        Serial.println("[BLE] host not ready");
        context->ble.busy = false;
        return;
    }

    context->wifi_scan.paused = true;
    esp_wifi_set_promiscuous(false);
    // Only call disconnect if WiFi was actually initialized
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        WiFi.disconnect(true);
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    NimBLEScan* scanner = NimBLEDevice::getScan();
    if (scanner == nullptr) {
        Serial.println("[BLE] getScan returned null");
        context->ble.busy = false;
        return;
    }
    context->ble.scanner = scanner;

    scanner->stop();
    scanner->clearResults();

    if (ble_scan_cb_instance) {
        scanner->setScanCallbacks(ble_scan_cb_instance, true);
    }
    scanner->setActiveScan(false); // Reduce memory & CPU load
    scanner->setInterval(400);     // Soften scan duty
    scanner->setWindow(40);
    scanner->start(0, false, true);

    if (ble_mutex && xSemaphoreTake(ble_mutex, portMAX_DELAY) == pdTRUE) {
        context->ble.unique_macs.clear();
        xSemaphoreGive(ble_mutex);
    }
    context->ble.packet_count = 0;
    context->ble.scan_active = true;
    context->ble.busy = false;

    // FIX 7: Add heap debug
    Serial.printf("[BLE] heap=%u min=%u\n",
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

    diag_ble_state("internal_start_ble_scan:exit", context);
}

static void internal_start_ble_adv_test(AppContext* context) {
    diag_ble_state("internal_start_ble_adv_test:enter", context);
    context->ble.busy = true;

    if (context->ble.scan_active) internal_stop_ble(context);

    context->wifi_scan.paused = true;
    esp_wifi_set_promiscuous(false);
    // Only call disconnect if WiFi was actually initialized
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        WiFi.disconnect(true);
    }

    if (!ensure_ble_host_ready()) {
        Serial.println("[BLE] host not ready");
        context->ble.busy = false;
        return;
    }

    context->ble.adv_test_active = true;
    context->ble.busy = false;
    
    // FIX 7: Add heap debug
    Serial.printf("[BLE] heap=%u min=%u\n",
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

    diag_ble_state("internal_start_ble_adv_test:exit", context);
}

static void internal_process_ble_adv_test(AppContext* context) {
    if (!context->ble.adv_test_active || context->ble.busy) return;
    
    if (ble_low_mem()) {
        Serial.println("[BLE] stopping adv test: low memory");
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
                    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}; // Reduced to 24 bytes to strictly respect 31-byte BLE packet limit
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
                case BLE_CMD_START_SCAN:     internal_start_ble_scan(context); break;
                case BLE_CMD_START_ADV_TEST: internal_start_ble_adv_test(context); break;
                case BLE_CMD_STOP:        internal_stop_ble(context); break;
                default: break;
            }
        }

        // Background periodic advertising test runs independent of UI
        if (context->ble.adv_test_active && !context->ble.busy) {
            internal_process_ble_adv_test(context);
        }
    }
}

// --- EXTERNAL API WRAPPERS (Safe for main_app_task & LVGL rendering) ---

void ble_tasks_init(AppContext* context) {
    ble_context = context;
    ble_scan_cb_instance = new BLEScanCB(context);
    ble_mutex = xSemaphoreCreateMutex();
    
    ble_cmd_q = xQueueCreate(10, sizeof(BleCmd));
    // Spin up BLE worker on core 1 to reduce contention with WiFi/system on core 0
    xTaskCreatePinnedToCore(ble_worker_task, "ble_task", 6144, context, 1, &ble_task_handle, 1); 
}

void stop_ble(AppContext* context) {
    if (!context->ble.scan_active && !context->ble.adv_test_active) return;
    
    BleCmd cmd = {BLE_CMD_STOP};
    xQueueSend(ble_cmd_q, &cmd, 0);

    queue_local_ui_text(UI_EVT_SET_BLE_SCAN_BUTTON, "START BLE SCAN");
    queue_local_ui_text(UI_EVT_SET_BLE_ADV_TEST_BUTTON, "START BLE ADV TEST");
    queue_local_ui_text(UI_EVT_SET_BLE_STATUS, "#00FFCC BLE READY#\n\nSelect an action.");
}

void start_ble_scan(AppContext* context) {
    if (!ble_switch_allowed()) {
        Serial.println("[BLE] switch blocked (cooldown)");
        return;
    }

    if (context->ble.busy) return;
    
    if (context->ble.adv_test_active) {
        Serial.println("[BLE] refusing scan while adv test is active");
        queue_local_ui_text(UI_EVT_SET_BLE_SCAN_BUTTON, "START BLE SCAN");
        queue_local_ui_text(UI_EVT_SET_BLE_STATUS, "#FF4444 ERROR#\n\nStop the adv test first.");
        return;
    }
    
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 60000) {
        Serial.println("[BLE] start denied: low internal heap");
        queue_local_ui_text(UI_EVT_SET_BLE_SCAN_BUTTON, "START BLE SCAN");
        queue_local_ui_text(UI_EVT_SET_BLE_STATUS, "#FF4444 LOW MEMORY#\n\nCannot start BLE.");
        return;
    }

    BleCmd cmd = {BLE_CMD_START_SCAN};
    xQueueSend(ble_cmd_q, &cmd, 0);
}

void start_ble_adv_test(AppContext* context) {
    if (!ble_switch_allowed()) {
        Serial.println("[BLE] switch blocked (cooldown)");
        return;
    }

    if (context->ble.busy) return;
    
    if (context->ble.scan_active) {
        Serial.println("[BLE] refusing adv test while scan is active");
        queue_local_ui_text(UI_EVT_SET_BLE_ADV_TEST_BUTTON, "START BLE ADV TEST");
        queue_local_ui_text(UI_EVT_SET_BLE_STATUS, "#FF4444 ERROR#\n\nStop the scan first.");
        return;
    }
    
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 60000) {
        Serial.println("[BLE] start denied: low internal heap");
        queue_local_ui_text(UI_EVT_SET_BLE_ADV_TEST_BUTTON, "START BLE ADV TEST");
        queue_local_ui_text(UI_EVT_SET_BLE_STATUS, "#FF4444 LOW MEMORY#\n\nCannot start BLE.");
        return;
    }

    BleCmd cmd = {BLE_CMD_START_ADV_TEST};
    xQueueSend(ble_cmd_q, &cmd, 0);
}

void process_ble_scan_ui(AppContext* context) {
    if (!context->ble.scan_active) return;
    
    if (ble_low_mem()) {
        Serial.println("[BLE] stopping scan: low memory");
        stop_ble(context);
        return;
    }

    BleState& ble = context->ble;
    uint32_t unique_size = 0;
    uint32_t pkt_count = 0;

    if (ble_mutex && xSemaphoreTake(ble_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // FIX 5: Proper ring buffer consumer
        uint8_t processed = 0;
        while (ble.ring_tail != ble.ring_head && processed < 8) {
            uint8_t i = ble.ring_tail;
            strncpy(ble.last_mac, ble.ring_buf[i].mac, 17);
            ble.last_mac[17] = '\0';
            if (sd_card_ready()) sd_log_ble_scan(millis(), ble.ring_buf[i].mac, ble.ring_buf[i].rssi);
            ble.ring_tail = (ble.ring_tail + 1) % BLE_RING_SIZE;
            processed++;
        }
        unique_size = ble.unique_macs.size();
        pkt_count = ble.packet_count;
        xSemaphoreGive(ble_mutex);
    }
    
    static uint32_t last_ui = 0;
    if (millis() - last_ui < 1000) return;
    char buf[128];
    snprintf(buf, sizeof(buf), "#00FFCC BLE SCAN#\n\nPackets: %lu\nUnique: %lu\nLast: %s",
             pkt_count, unique_size, ble.last_mac);
    queue_local_ui_text(UI_EVT_SET_BLE_STATUS, buf);
    last_ui = millis();
}
