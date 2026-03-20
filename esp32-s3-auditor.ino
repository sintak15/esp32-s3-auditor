/**
 * ESP32-S3 Auditor Suite — Refactored
 *
 * This is the main entry point for the application. It initializes all modules,
 * sets up the tasks and timers, and runs the main processing loop.
 */

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <lvgl.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <SD_MMC.h>
#include <Update.h>
#include <nvs_flash.h> // For NVS initialization
#include <esp_heap_caps.h> // For heap diagnostics
#include <freertos/semphr.h>
#include <mbedtls/sha256.h>
#include <driver/temperature_sensor.h>
#include <sys/time.h> // For settimeofday()

#include <pb_encode.h>
#include <pb_decode.h>
#include "src/meshtastic/mesh.pb.h"
#include "src/meshtastic/telemetry.pb.h"
#include "src/meshtastic/portnums.pb.h"
#include "src/MeshProtobufUtils.h"

#include "constants.h"
// #include "state.h" // REMOVED: Redundant, use types.h
#include "types.h"
#include "ui_module.h"
#include "display.h"
#include "sd_logger.h"
#include "wifi_scanner.h"
#include "audit_actions.h"
#include "ble_tasks.h"
#include "pcap_and_probes.h"
#include "companion_link.h"
#include "ui_events.h"
#include "src/UIConfig.h" // For UI::Colors

// ──────────────────────────────────────────────
//               Global Context & Objects
// ──────────────────────────────────────────────
AppContext g_app_context;
HardwareSerial SerialLoRa(2);

WebServer server(80);

UiEventQueue ui_queue;

extern lv_obj_t *probe_list;
extern lv_obj_t *lbl_ble_status;
extern lv_obj_t *lbl_pcap_status;
extern lv_obj_t *btn_ble_adv_test;
extern lv_obj_t *btn_ble_scan;
extern lv_obj_t *btn_pcap_start;
extern lv_obj_t *btn_probe_start;
extern lv_obj_t *lbl_scan_pause;
extern lv_obj_t *lbl_audit_target;
extern lv_obj_t *lbl_audit_bssid;
extern lv_obj_t *lbl_audit_status;
extern lv_obj_t *btn_stop_audit;
extern lv_obj_t *lbl_firmware_version;
extern lv_obj_t *lbl_build_date;
extern lv_obj_t *lbl_device_id;
extern lv_obj_t *tabview;

// ──────────────────────────────────────────────
//             Diagnostics & Tracing
// ──────────────────────────────────────────────
static SemaphoreHandle_t g_diagMutex = nullptr;
SemaphoreHandle_t g_i2cMutex = nullptr;

static char g_lastStateCore0[96] = "boot";
static char g_lastStateCore1[96] = "boot";

static inline int current_core_id() {
#if CONFIG_FREERTOS_UNICORE
    return 0;
#else
    return xPortGetCoreID();
#endif
}

static void set_last_state_fmt(const char* fmt, ...) {
    if (!g_diagMutex) return;

    char tmp[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if (xSemaphoreTake(g_diagMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        char* dst = (current_core_id() == 0) ? g_lastStateCore0 : g_lastStateCore1;
        strlcpy(dst, tmp, 96);
        xSemaphoreGive(g_diagMutex);
    }
}

static void print_crash_recovery_banner() {
    char c0[96];
    char c1[96];

    if (!g_diagMutex) return;

    if (xSemaphoreTake(g_diagMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        strlcpy(c0, g_lastStateCore0, sizeof(c0));
        strlcpy(c1, g_lastStateCore1, sizeof(c1));
        xSemaphoreGive(g_diagMutex);
    } else {
        strlcpy(c0, "unknown", sizeof(c0));
        strlcpy(c1, "unknown", sizeof(c1));
    }

    Serial.println();
    Serial.println("========================================");
    Serial.println("         ⚠️ CRASH RECOVERY ⚠️           ");
    Serial.printf (" Last state on Core 0: %s\n", c0);
    Serial.printf (" Last state on Core 1: %s\n", c1);
    Serial.println("========================================");
    Serial.println();
}

void get_last_crash_states(char* c0, char* c1, size_t max_len) {
    if (g_diagMutex && xSemaphoreTake(g_diagMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        strlcpy(c0, g_lastStateCore0, max_len);
        strlcpy(c1, g_lastStateCore1, max_len);
        xSemaphoreGive(g_diagMutex);
    } else {
        strlcpy(c0, "unknown", max_len);
        strlcpy(c1, "unknown", max_len);
    }
}

// ──────────────────────────────────────────────
//             Performance Tracking
// ──────────────────────────────────────────────
struct PerfTracker {
    uint32_t call_count = 0;
    uint32_t total_time = 0;
    uint32_t max_time = 0;
    uint32_t min_time = 0xFFFFFFFF;
    
    void record(uint32_t dt) {
        call_count++;
        total_time += dt;
        if (dt > max_time) max_time = dt;
        if (dt < min_time) min_time = dt;
    }
    
    void print_and_reset(const char* name) {
        if (call_count == 0) return;
        uint32_t avg = total_time / call_count;
        Serial.printf("[PERF] %-22s | calls: %-5lu | avg: %-3lu ms | min: %-3lu ms | max: %-3lu ms\n", 
                      name, call_count, avg, min_time, max_time);
        call_count = 0;
        total_time = 0;
        max_time = 0;
        min_time = 0xFFFFFFFF;
    }
};

PerfTracker perf_ui_timer;
PerfTracker perf_ui_queue;
PerfTracker perf_ble_ui;
PerfTracker perf_pcap;
PerfTracker perf_probe;
PerfTracker perf_chan_hop;
PerfTracker perf_lora;
PerfTracker perf_web;

void get_perf_stats(char* buf, size_t max_len) {
    uint32_t totalTime;
    UBaseType_t taskCount = uxTaskGetNumberOfTasks();
    TaskStatus_t *taskArray = (TaskStatus_t *)pvPortMalloc(taskCount * sizeof(TaskStatus_t));
    
    String out = "--- OS TASK MONITOR ---\n";
    out += "Task Name      CPU%  Stack\n";

    if (taskArray != nullptr) {
        taskCount = uxTaskGetSystemState(taskArray, taskCount, &totalTime);
        if (totalTime > 0) {
            for (UBaseType_t i = 0; i < taskCount; i++) {
                // Only show tasks consuming > 0.1% or our critical suite tasks
                int pct = (taskArray[i].ulRunTimeCounter * 100) / totalTime;
                if (pct > 0 || strstr(taskArray[i].pcTaskName, "lora") || strstr(taskArray[i].pcTaskName, "wifi")) {
                    char line[64];
                    snprintf(line, sizeof(line), "%-14s %-3d%%  %u\n", 
                             taskArray[i].pcTaskName, pct, taskArray[i].usStackHighWaterMark);
                    out += line;
                }
            }
        }
        vPortFree(taskArray);
    }

    out += "\n--- STAGE TIMINGS (ms) ---\n";
    auto add_p = [&](const char* n, PerfTracker& p) {
        char l[64];
        uint32_t avg = p.call_count ? p.total_time / p.call_count : 0;
        snprintf(l, sizeof(l), "%-12s Avg:%-2lu Max:%-2lu\n", n, avg, (p.max_time == 0xFFFFFFFF ? 0 : p.max_time));
        out += l;
    };

    add_p("UI Timer", perf_ui_timer);
    add_p("WiFi Work", perf_pcap);
    add_p("LoRa RX", perf_lora);
    add_p("Web Serv", perf_web);

    strlcpy(buf, out.c_str(), max_len);
}

// ──────────────────────────────────────────────
//               Local UI Event Queue
// ──────────────────────────────────────────────

QueueHandle_t g_local_ui_queue = nullptr;

// ──────────────────────────────────────────────
//             Watchdog Management
// ──────────────────────────────────────────────
static bool g_mainTaskWdtAdded = false;
static bool g_uiTaskWdtAdded   = false;
static bool g_loraTaskWdtAdded = false;
static bool g_wifiTaskWdtAdded = false;

static inline void safe_wdt_reset(bool registered) {
    if (registered) {
        esp_task_wdt_reset();
    }
}

void queue_local_ui_text(LocalUiEventType type, const char *text) {
    if (!g_local_ui_queue) return;
    LocalUiEvent ev = {};
    ev.type = type;
    if (text) strlcpy(ev.text, text, sizeof(ev.text));
    xQueueSend(g_local_ui_queue, &ev, 0);
}

void process_ui_queue() {
    static char probe_text_buf[2048] = "Waiting for probes...\n";
    static size_t probe_len = 22;
    bool probe_updated = false;
    static bool probe_needs_render = false;

    uint32_t t0 = millis();
    const uint32_t budgetMs = 6;

    int processed = 0;
    UiEvent* e;
    
    while ((e = ui_queue.get_read_slot()) != nullptr) {
        switch (e->type) {
            case UiEvent::ADD_PROBE: {
                size_t tlen = strlen(e->text);
                if (probe_len + tlen + 2 > sizeof(probe_text_buf)) {
                    strcpy(probe_text_buf, "--- Buffer Cleared ---\n");
                    probe_len = 23;
                }
                strcpy(probe_text_buf + probe_len, e->text);
                probe_len += tlen;
                strcpy(probe_text_buf + probe_len, "\n");
                probe_len += 1;
                probe_updated = true;
                break;
            }
            case UiEvent::CLEAR_PROBES:
                strcpy(probe_text_buf, "Waiting for probes...\n");
                probe_len = 22;
                probe_updated = true;
                break;
            case UiEvent::SET_BLE_STATUS:
                if (lbl_ble_status) lv_label_set_text(lbl_ble_status, e->text);
                break;
            case UiEvent::SET_PCAP_STATUS:
                if (lbl_pcap_status) lv_label_set_text(lbl_pcap_status, e->text);
                break;
        }
        ui_queue.commit_read();
        processed++;
        
        if ((millis() - t0) >= budgetMs || processed >= 8) {
            break;
        }
    }

    LocalUiEvent lev;
    int local_processed = 0;
    while (g_local_ui_queue && xQueueReceive(g_local_ui_queue, &lev, 0) == pdTRUE) {
        switch (lev.type) {
            case UI_EVT_SET_PCAP_BUTTON:
                if (btn_pcap_start) {
                    lv_obj_t *child = lv_obj_get_child(btn_pcap_start, 0);
                    if (child) lv_label_set_text(child, lev.text);
                }
                break;
            case UI_EVT_SET_BLE_ADV_TEST_BUTTON:
                if (btn_ble_adv_test) {
                    lv_obj_t *child = lv_obj_get_child(btn_ble_adv_test, 0);
                    if (child) lv_label_set_text(child, lev.text);
                }
                break;
            case UI_EVT_SET_BLE_SCAN_BUTTON:
                if (btn_ble_scan) {
                    lv_obj_t *child = lv_obj_get_child(btn_ble_scan, 0);
                    if (child) lv_label_set_text(child, lev.text);
                }
                break;
            case UI_EVT_SET_PROBE_BUTTON:
                if (btn_probe_start) {
                    lv_obj_t *child = lv_obj_get_child(btn_probe_start, 0);
                    if (child) lv_label_set_text(child, lev.text);
                }
                break;
            case UI_EVT_SET_SCAN_PAUSE:
                if (lbl_scan_pause) lv_label_set_text(lbl_scan_pause, lev.text);
                break;
            case UI_EVT_SET_PCAP_STATUS:
                if (lbl_pcap_status) lv_label_set_text(lbl_pcap_status, lev.text);
                break;
            case UI_EVT_SET_BLE_STATUS:
                if (lbl_ble_status) lv_label_set_text(lbl_ble_status, lev.text);
                break;
            case UI_EVT_ADD_PROBE_TEXT: {
                size_t tlen = strlen(lev.text);
                if (probe_len + tlen + 2 > sizeof(probe_text_buf)) {
                    strcpy(probe_text_buf, "--- Buffer Cleared ---\n");
                    probe_len = 23;
                }
                strcpy(probe_text_buf + probe_len, lev.text);
                probe_len += tlen;
                strcpy(probe_text_buf + probe_len, "\n");
                probe_len += 1;
                probe_updated = true;
                break;
            }
            case UI_EVT_CLEAR_PROBE_LIST:
                strcpy(probe_text_buf, "Waiting for probes...\n");
                probe_len = 22;
                probe_updated = true;
                break;
            case UI_EVT_NAVIGATE:
                if (tabview) lv_tabview_set_act(tabview, lev.tab_id, LV_ANIM_OFF);
                break;
        }
        local_processed++;
        if ((millis() - t0) >= budgetMs || local_processed >= 8) {
            break;
        }
    }

    if (probe_updated) {
        probe_needs_render = true;
    }

    if (probe_needs_render && probe_list && tabview && lv_tabview_get_tab_act(tabview) == 5) {
        static uint32_t last_probe_draw = 0;
        if (millis() - last_probe_draw > 250) {
            lv_textarea_set_text(probe_list, probe_text_buf);
            lv_obj_scroll_to_y(probe_list, LV_COORD_MAX, LV_ANIM_OFF);
            last_probe_draw = millis();
            probe_needs_render = false;
        }
    }
}

// ──────────────────────────────────────────────
//             Benign Service Tasks
// ──────────────────────────────────────────────

static void write_status_snapshot(bool sdMounted, int batteryPct, bool isCharging, uint32_t batteryMv, int batteryTempC) {
    if (g_app_context.status.mutex && xSemaphoreTake(g_app_context.status.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        g_app_context.status.snap.sdMounted = sdMounted;
        g_app_context.status.snap.batteryPct = batteryPct;
        g_app_context.status.snap.isCharging = isCharging;
        g_app_context.status.snap.batteryMv = batteryMv;
        g_app_context.status.snap.batteryTempC = batteryTempC;
        xSemaphoreGive(g_app_context.status.mutex);
    }
}

void reset_battery_calibration() {
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, "batt_max");
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    g_app_context.status.calibrated_max_mv = 4050; // Reset to safe default
}

void load_battery_calibration() {
    nvs_handle_t nvs;
    g_app_context.status.calibrated_max_mv = 4050; // default
    if (nvs_open("storage", NVS_READONLY, &nvs) == ESP_OK) {
        uint32_t val;
        if (nvs_get_u32(nvs, "batt_max", &val) == ESP_OK) g_app_context.status.calibrated_max_mv = val;
        nvs_close(nvs);
    }
}

void status_service_task(void *pv) {
    uint32_t sd_retry_ms = 0;
    static float filtered_mv = 0.0f;
    const float alpha = 0.10f; // Increased damping for more stable percentage

    // Configure ADC attenuation for the full 0-3.1V range
    // On S3-CYD models, the battery pin is GPIO 1 (GPIO 4 is LCD_DC)
    analogSetAttenuation(ADC_11db);

    // Initialize internal temperature sensor
    temperature_sensor_handle_t temp_handle = NULL;
    temperature_sensor_config_t temp_conf = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    if (temperature_sensor_install(&temp_conf, &temp_handle) == ESP_OK) {
        temperature_sensor_enable(temp_handle);
    }
    
    load_battery_calibration();

    for (;;) {
        // Use factory-calibrated millivolts reading with multi-sampling pre-filter
        uint32_t raw_sum = 0;
        for(int i=0; i<10; i++) raw_sum += analogReadMilliVolts(BATT_ADC);
        uint32_t raw_mv = (raw_sum / 10) * 2; // Standard 1:2 divider calculation

        // Exponential Moving Average (EMA) low-pass filter to prevent flickering
        // Increased threshold to 130mV to allow natural voltage relaxation (sag) 
        // when unplugging to be smoothed by the filter instead of jumping instantly.
        if (filtered_mv == 0.0f || abs((int32_t)raw_mv - (int32_t)filtered_mv) > 130) {
            filtered_mv = (float)raw_mv; 
        } else {
            filtered_mv = (alpha * (float)raw_mv) + ((1.0f - alpha) * filtered_mv);
        }

        uint32_t mv = (uint32_t)filtered_mv;

        // Learned Accuracy Logic:
        // If we are charging and seeing a stable voltage higher than our current 100% mark, 
        // but within sane Li-ion limits (4.0V - 4.4V), update the calibration.
        if (mv > 4130 && mv < 4400) {
            if (mv > g_app_context.status.calibrated_max_mv + 10) {
                g_app_context.status.calibrated_max_mv = mv;
                // Save to NVS
                nvs_handle_t nvs;
                if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
                    nvs_set_u32(nvs, "batt_max", mv);
                    nvs_commit(nvs);
                    nvs_close(nvs);
                }
            }
        }
        
        // Calculate percentage locally with strict clamping to 0-100
        // Now uses the learned calibrated_max_mv for the 100% point.
        uint32_t max_v = g_app_context.status.calibrated_max_mv;
        int newBattPct = map(constrain(mv, 3300, max_v), 3300, max_v, 0, 100);
        
        // Heuristic: If reading is above 4130mV on a 1S battery, it's physically 
        // impossible unless a charger is applying 5V to the rail.
        bool charging = (mv > 4130);

        if (!sd_card_ready() && millis() - sd_retry_ms > 5000) {
            if (!g_app_context.capture.pcap_active && 
                !g_app_context.capture.probe_active && 
                g_app_context.audit.current_mode == AUDIT_NONE && 
                !g_app_context.ble.scan_active) {
                sd_retry_ms = millis();
                sd_reinit();
            }
        }

        float tsens_out = 0;
        if (temp_handle) temperature_sensor_get_celsius(temp_handle, &tsens_out);

        write_status_snapshot(sd_card_ready(), newBattPct, charging, mv, (int)tsens_out);
        vTaskDelay(pdMS_TO_TICKS(500)); // Sample twice as fast for better responsiveness
    }
}

void send_meshtastic_text(const char* text) {
    meshtastic_ToRadio to_radio = meshtastic_ToRadio_init_zero;
    to_radio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    
    meshtastic_MeshPacket &packet = to_radio.packet;
    packet.to = 0xFFFFFFFF; // Broadcast to all nodes
    packet.want_ack = false;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    
    meshtastic_Data &data = packet.decoded;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    
    size_t text_len = strlen(text);
    if (text_len > 233) text_len = 233; // Max payload constraint
    data.payload.size = text_len;
    memcpy(data.payload.bytes, text, text_len);
    
    uint8_t tx_buf[512];
    pb_ostream_t stream = pb_ostream_from_buffer(tx_buf, sizeof(tx_buf));
    if (pb_encode(&stream, meshtastic_ToRadio_fields, &to_radio)) {
        size_t len = stream.bytes_written;
        SerialLoRa.write(0x94); // StreamAPI Magic Byte 1
        SerialLoRa.write(0xC3); // StreamAPI Magic Byte 2
        SerialLoRa.write((len >> 8) & 0xFF); // Length MSB
        SerialLoRa.write(len & 0xFF);        // Length LSB
        SerialLoRa.write(tx_buf, len);       // Protobuf Payload
    } else {
        Serial.println("Protobuf encode failed");
    }
}

// Helper to safely append to a buffer, clearing it if it's about to overflow
void safe_append(char* buffer, size_t size, const char* append) {
    if (!buffer || !append || size == 0) return;
    size_t current_len = strlen(buffer);
    size_t append_len = strlen(append);
    
    if (current_len + append_len >= size - 1) { 
        strcpy(buffer, "--- Buffer Cleared ---\n");
        current_len = strlen(buffer);
    }
    strncat(buffer, append, size - current_len - 1);
}

void lora_service_task(void *pv) {
    enum FrameState { FIND_MAGIC1, FIND_MAGIC2, READ_LEN1, READ_LEN2, READ_PAYLOAD };
    FrameState state = FIND_MAGIC1;
    uint16_t payload_len = 0;
    
    static uint8_t* rx_buf = nullptr;
    if (!rx_buf) rx_buf = (uint8_t*)ps_malloc(512);
    if (!rx_buf) {
        Serial.println("[FATAL] Could not allocate LoRa RX buffer in PSRAM");
        vTaskDelete(NULL);
    }
    int rx_idx = 0;
    
    esp_task_wdt_add(nullptr);
    g_loraTaskWdtAdded = true;

    for (;;) {
        safe_wdt_reset(g_loraTaskWdtAdded);

        int bytes_processed = 0;
        uint32_t loop_start = millis();
        
        while (SerialLoRa.available() > 0 && bytes_processed < 256 && (millis() - loop_start < 20)) {
            uint8_t c = SerialLoRa.read();
            bytes_processed++;

            switch (state) {
                case FIND_MAGIC1:
                    if (c == 0x94) state = FIND_MAGIC2;
                    break;
                case FIND_MAGIC2:
                    state = (c == 0xC3) ? READ_LEN1 : FIND_MAGIC1;
                    break;
                case READ_LEN1:
                    payload_len = c << 8;
                    state = READ_LEN2;
                    break;
                case READ_LEN2:
                    payload_len |= c;
                    if (payload_len == 0 || payload_len > 512) {
                        state = FIND_MAGIC1; // Safety reset on garbage length
                        rx_idx = 0;
                    } else {
                        rx_idx = 0;
                        state = READ_PAYLOAD;
                    }
                    break;
                case READ_PAYLOAD:
                    rx_buf[rx_idx++] = c;
                    if (rx_idx >= payload_len) {
                        static uint32_t last_lora_log_ms = 0;
                        static uint32_t decoded_frames = 0;
                        
                        decoded_frames++;
                        if (millis() - last_lora_log_ms >= 1000) {
                            last_lora_log_ms = millis();
                            Serial.printf("[TRACE] lora decoded frames=%lu\n", (unsigned long)decoded_frames);
                            decoded_frames = 0;
                        }

                        pb_istream_t stream = pb_istream_from_buffer(rx_buf, payload_len);
                        static meshtastic_FromRadio from_radio;
                        memset(&from_radio, 0, sizeof(from_radio)); // CRITICAL: Fully wipe the union to prevent Nanopb crashes
                        static char* log_msg = nullptr;
                        static char* chat_msg = nullptr;
                        if (!log_msg) log_msg = (char*)ps_calloc(256, 1);
                        if (!chat_msg) chat_msg = (char*)ps_calloc(256, 1);
                        memset(log_msg, 0, 256); // Clear local buffer
                        memset(chat_msg, 0, 256); // Clear local buffer
                        
                        if (pb_decode(&stream, meshtastic_FromRadio_fields, &from_radio)) {
                            if (from_radio.which_payload_variant == meshtastic_FromRadio_packet_tag) {
                                meshtastic_MeshPacket &packet = from_radio.packet;
                                
                                if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
                                    meshtastic_Data &data = packet.decoded;
                                    
                                    if (data.portnum == meshtastic_PortNum_TELEMETRY_APP) {
                                        meshtastic_Telemetry tel = meshtastic_Telemetry_init_zero;
                                        pb_istream_t tel_stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
                                        if (pb_decode(&tel_stream, meshtastic_Telemetry_fields, &tel)) {
                                            if (tel.which_variant == meshtastic_Telemetry_device_metrics_tag) {
                                                if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                                                    g_app_context.lora.stats.battery_level = tel.variant.device_metrics.battery_level;
                                                    g_app_context.lora.stats.voltage = tel.variant.device_metrics.voltage;
                                                    g_app_context.lora.stats.channel_utilization = tel.variant.device_metrics.channel_utilization;
                                                    g_app_context.lora.stats.air_util_tx = tel.variant.device_metrics.air_util_tx;
                                                    g_app_context.lora.stats.uptime_seconds = tel.variant.device_metrics.uptime_seconds;
                                                    g_app_context.lora.stats.updated = true;
                                                    xSemaphoreGive(g_app_context.lora.mutex);
                                                }
                                            snprintf(log_msg, 256, "[%08lX] Batt: %lu%% %.2fV\n", 
                                                    (unsigned long)packet.from, 
                                                    (unsigned long)tel.variant.device_metrics.battery_level, 
                                                    tel.variant.device_metrics.voltage);
                                            } else if (tel.which_variant == meshtastic_Telemetry_environment_metrics_tag) {
                                            snprintf(log_msg, 256, "[%08lX] Env: %.1fC %.1f%%\n", 
                                                    (unsigned long)packet.from, 
                                                    tel.variant.environment_metrics.temperature, 
                                                    tel.variant.environment_metrics.relative_humidity);
                                            }
                                            else if (tel.which_variant == meshtastic_Telemetry_local_stats_tag) {
                                                if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                                                    g_app_context.lora.stats.num_packets_rx = tel.variant.local_stats.num_packets_rx;
                                                    g_app_context.lora.stats.num_packets_tx = tel.variant.local_stats.num_packets_tx;
                                                    g_app_context.lora.stats.num_online_nodes = tel.variant.local_stats.num_online_nodes;
                                                    g_app_context.lora.stats.num_total_nodes = tel.variant.local_stats.num_total_nodes;
                                                    g_app_context.lora.stats.updated = true;
                                                    xSemaphoreGive(g_app_context.lora.mutex);
                                                }
                                                snprintf(log_msg, 256, "[%08lX] LocalStats updated\n", (unsigned long)packet.from);
                                            }
                                        }
                                    } else if (data.portnum == meshtastic_PortNum_POSITION_APP) {
                                        meshtastic_Position pos = meshtastic_Position_init_zero;
                                        pb_istream_t pos_stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
                                        if (pb_decode(&pos_stream, meshtastic_Position_fields, &pos)) {
                                            snprintf(log_msg, 256, "[%08lX] GPS: %.4f, %.4f\n", 
                                                (unsigned long)packet.from, 
                                                pos.latitude_i / 1e7, pos.longitude_i / 1e7);
                                            
                                            // Sync internal RTC if we have a valid GPS time
                                            uint32_t gps_time = pos.time > 0 ? pos.time : pos.timestamp;
                                            if (gps_time > 1700000000) { // Basic sanity check (> Nov 2023)
                                                struct timeval tv;
                                                tv.tv_sec = gps_time;
                                                tv.tv_usec = 0;
                                                settimeofday(&tv, NULL);
                                            }
                                        }
                                    } else if (data.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
                                        // Safely print up to payload.size using %.*s to prevent buffer overflows on max-length messages
                                        snprintf(log_msg, 256, "[%08lX] Msg: %.*s\n", (unsigned long)packet.from, (int)data.payload.size, data.payload.bytes);
                                        
                                        // Try to map sender ID to known name for the chat interface
                                        char sender_name[40];
                                        snprintf(sender_name, sizeof(sender_name), "%08lX", (unsigned long)packet.from);
                                        if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                                            for (const auto& n : g_app_context.lora.known_nodes) {
                                                if (n.num == packet.from && strlen(n.long_name) > 0) {
                                                    strncpy(sender_name, n.long_name, 39);
                                                    sender_name[39] = '\0';
                                                    break;
                                                }
                                            }
                                            xSemaphoreGive(g_app_context.lora.mutex);
                                        }
                                    snprintf(chat_msg, 256, "[%s]: %.*s\n", sender_name, (int)data.payload.size, data.payload.bytes);
                                    } else if (data.portnum == meshtastic_PortNum_NODEINFO_APP) {
                                        meshtastic_User user = meshtastic_User_init_zero;
                                        pb_istream_t user_stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
                                        if (pb_decode(&user_stream, meshtastic_User_fields, &user)) {
                                    if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                                        bool found = false;
                                        for (auto& n : g_app_context.lora.known_nodes) {
                                            if (n.num == packet.from) {
                                                strncpy(n.long_name, user.long_name, 39);
                                                n.long_name[39] = '\0';
                                                n.last_heard = millis();
                                                found = true;
                                                break;
                                            }
                                        }
                                        if (!found && g_app_context.lora.known_nodes.size() < 100) {
                                            NodeRecord nr = {0};
                                            nr.num = packet.from;
                                            strncpy(nr.long_name, user.long_name, 39);
                                            nr.long_name[39] = '\0';
                                            nr.last_heard = millis();
                                            g_app_context.lora.known_nodes.push_back(nr);
                                        }
                                            g_app_context.lora.nodedb_updated = true;
                                        xSemaphoreGive(g_app_context.lora.mutex);
                                    }
                                    snprintf(log_msg, 256, "[%08lX] Node: %.39s\n", (unsigned long)packet.from, user.long_name);
                                        }
                                    } else if (data.portnum == meshtastic_PortNum_WAYPOINT_APP) {
                                        meshtastic_Waypoint wp = meshtastic_Waypoint_init_zero;
                                        pb_istream_t wp_stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
                                        if (pb_decode(&wp_stream, meshtastic_Waypoint_fields, &wp)) {
                                    snprintf(log_msg, 256, "[%08lX] Waypoint: %.29s\n", 
                                            (unsigned long)packet.from, wp.name);
                                        }
                                    } else {
                                    snprintf(log_msg, 256, "[%08lX] App PortNum: %d\n", (unsigned long)packet.from, data.portnum);
                                    }
                                } else if (packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
                                snprintf(log_msg, 256, "[%08lX] Encrypted Packet\n", (unsigned long)packet.from);
                                }
                            } else if (from_radio.which_payload_variant == meshtastic_FromRadio_my_info_tag) {
                                if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                                    g_app_context.lora.stats.my_node_num = from_radio.my_info.my_node_num;
                                    g_app_context.lora.stats.updated = true;
                                    xSemaphoreGive(g_app_context.lora.mutex);
                                }
                            snprintf(log_msg, 256, "[LOCAL] Connected! NodeNum: %08lX\n", (unsigned long)from_radio.my_info.my_node_num);
                            } else if (from_radio.which_payload_variant == meshtastic_FromRadio_log_record_tag) {
                        snprintf(log_msg, 256, "[NODE_LOG] %.383s\n", from_radio.log_record.message);
                            } else if (from_radio.which_payload_variant == meshtastic_FromRadio_node_info_tag) {
                            snprintf(log_msg, 256, "[MESH] Heard Node: %.39s\n", from_radio.node_info.user.long_name);
                        if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                            bool found = false;
                            for (auto& n : g_app_context.lora.known_nodes) {
                                if (n.num == from_radio.node_info.num) {
                                    strncpy(n.long_name, from_radio.node_info.user.long_name, sizeof(n.long_name) - 1);
                                    n.long_name[sizeof(n.long_name) - 1] = '\0'; // Strictly enforce null-termination
                                    n.last_heard = millis();
                                    n.snr = from_radio.node_info.snr;
                                    found = true;
                                    break;
                                }
                            }
                            if (!found && g_app_context.lora.known_nodes.size() < 100) {
                                NodeRecord nr = {0};
                                nr.num = from_radio.node_info.num;
                                strncpy(nr.long_name, from_radio.node_info.user.long_name, sizeof(nr.long_name) - 1);
                                nr.long_name[sizeof(nr.long_name) - 1] = '\0'; // Strictly enforce null-termination
                                nr.last_heard = millis();
                                nr.snr = from_radio.node_info.snr;
                                g_app_context.lora.known_nodes.push_back(nr);
                            }
                                g_app_context.lora.nodedb_updated = true;
                            xSemaphoreGive(g_app_context.lora.mutex);
                        }
                            }
                        } else {
                            // Print protobuf decode errors to the USB Serial Monitor so we can debug
                            Serial.printf("[LoRa] PB Decode Failed: %s\n", PB_GET_ERROR(&stream));
                        }
                        
                        // If we successfully built a log message, push it to the UI
                        if (strlen(log_msg) > 0 && g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                            safe_append(g_app_context.lora.log_data, 2048, log_msg);
                            g_app_context.lora.log_updated = true;
                            xSemaphoreGive(g_app_context.lora.mutex);
                        }

                        // If we successfully built a chat message, push it to the Chat UI
                        if (strlen(chat_msg) > 0 && g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                            safe_append(g_app_context.lora.chat_data, 2048, chat_msg);
                            g_app_context.lora.chat_updated = true;
                            g_app_context.lora.unread_chat = true; // Trigger notification
                            xSemaphoreGive(g_app_context.lora.mutex);
                        }
                        
                        rx_idx = 0;
                        payload_len = 0;
                        state = FIND_MAGIC1;
                    }
                    break;
            }
        }
        
        // Diagnostic log: check for stack near-overflow
        static uint32_t last_watermark_log = 0;
        if (millis() - last_watermark_log > 5000) {
            Serial.printf("[LoRa] Stack High Water Mark: %u bytes free\n", uxTaskGetStackHighWaterMark(NULL));
            last_watermark_log = millis();
        }
        
        uint32_t dt = millis() - loop_start;
        perf_lora.record(dt);
        
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}


// ──────────────────────────────────────────────
//           UI Navigation & Callbacks
// ──────────────────────────────────────────────

static volatile bool pending_stop_all = false;
static volatile int pending_nav_tab = -1;
static volatile bool pending_stop_audit = true;
static volatile bool pending_pause_scan = true;
static volatile bool pending_stop_ble = true;
static volatile bool pending_stop_pcap = true;
static volatile bool pending_stop_probe = true;
static volatile bool req_start_ble_adv_test = false;
volatile bool req_stop_ble = false; // Make globally accessible to BLE background thread
static volatile bool req_start_ble_scan = false;
static volatile bool req_start_pcap = false;
static volatile bool req_stop_pcap = false;
static volatile bool req_start_probe = false;
static volatile bool req_stop_probe = false;

void wait_for_idle() {
    // Deprecated: Teardown is now handled safely by the main loop state machine
    // to prevent LVGL Use-After-Free memory crashes during button clicks.
}

void navigate_to(int tab) {
    // Request the state machine to safely tear down operations and navigate
    pending_nav_tab = tab;

    // Default behavior: only stop active/non-passive operations when navigating.
    pending_stop_audit = true;
    pending_pause_scan = false;
    pending_stop_ble = false;
    pending_stop_pcap = false;
    pending_stop_probe = false;

    // If a PMKID monitor is running, keep it active when leaving the Audit tab.
    // The monitor is driven by an LVGL timer + WiFi promiscuous callback (or companion),
    // and should not be coupled to which tab is currently visible.
    if (g_app_context.audit.current_mode == AUDIT_PMKID) {
        pending_stop_audit = false;
        // When using the companion, allow the main scan loop to remain active.
        if (g_app_context.audit.pmkid_via_companion) {
            pending_pause_scan = false;
        }
    } else if (g_app_context.audit.current_mode == AUDIT_RECONNECT) {
        pending_stop_audit = false;
    }

    // BLE advertisement test is not passive; stop it on navigation.
    if (g_app_context.ble.adv_test_active) {
        pending_stop_ble = true;
    }

    pending_stop_all = true;
}

void cb_nav_home(lv_event_t*) { navigate_to(0); }
void cb_nav_scan(lv_event_t*) { navigate_to(1); }

void cb_net_selected(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_current_target(e); // Corrected to use g_app_context
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (idx < 0 || idx >= g_app_context.wifi_scan.ap_count || !g_app_context.wifi_scan.ap_list[idx].active) return;
    
    g_app_context.wifi_scan.selected_net = idx;
    g_app_context.wifi_scan.reconnect_sta_target = -1;
    
    char bss[18]; // Corrected to use g_app_context
    sprintf(bss, "%02X:%02X:%02X:%02X:%02X:%02X", g_app_context.wifi_scan.ap_list[idx].bssid[0], g_app_context.wifi_scan.ap_list[idx].bssid[1], g_app_context.wifi_scan.ap_list[idx].bssid[2], g_app_context.wifi_scan.ap_list[idx].bssid[3], g_app_context.wifi_scan.ap_list[idx].bssid[4], g_app_context.wifi_scan.ap_list[idx].bssid[5]);

    lv_label_set_text_fmt(lbl_audit_target, "#00FFCC SELECTED:#  %s", g_app_context.wifi_scan.ap_list[idx].ssid);
    lv_label_set_text_fmt(lbl_audit_bssid, "BSSID: %s  CH:%d", bss, g_app_context.wifi_scan.ap_list[idx].channel);
    lv_label_set_text(lbl_audit_status, "#444444 IDLE - choose an audit action#");
    
    lv_obj_clear_flag(btn_reconnect, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_beacon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_pmkid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_stop_audit, LV_OBJ_FLAG_HIDDEN);

    navigate_to(2);
}

void cb_pause_scan(lv_event_t*) {
    if (!g_app_context.wifi_scan.started) {
        g_app_context.wifi_scan.started = true; // Initialize new member
        g_app_context.wifi_scan.paused = false; // Initialize new member
        run_ap_scan(&g_app_context); // Initialize new member
        render_scan_list(&g_app_context); // Initialize new member
        g_app_context.wifi_scan.last_scan_ms = millis(); // Initialize new member
        lv_label_set_text(lbl_scan_pause, LV_SYMBOL_PAUSE " PAUSE"); // Initialize new member
        return;
    }
    g_app_context.wifi_scan.paused = !g_app_context.wifi_scan.paused;
    if (g_app_context.wifi_scan.paused) {
        esp_wifi_set_promiscuous(false);
    } else {
        run_ap_scan(&g_app_context);
        render_scan_list(&g_app_context);
        g_app_context.wifi_scan.last_scan_ms = millis();
    }
    lv_label_set_text(lbl_scan_pause, g_app_context.wifi_scan.paused ? LV_SYMBOL_PLAY " RESUME" : LV_SYMBOL_PAUSE " PAUSE");
}

void cb_view_ap(lv_event_t*) { 
    g_app_context.wifi_scan.view = VIEW_AP;
    lv_obj_add_style(btn_view_ap, &style_view_active, 0);
    lv_obj_add_style(btn_view_sta, &style_view_inactive, 0);
    lv_obj_add_style(btn_view_linked, &style_view_inactive, 0);
    render_scan_list(&g_app_context);
}
void cb_view_sta(lv_event_t*) { 
    g_app_context.wifi_scan.view = VIEW_STA;
    lv_obj_add_style(btn_view_ap, &style_view_inactive, 0);
    lv_obj_add_style(btn_view_sta, &style_view_active, 0);
    lv_obj_add_style(btn_view_linked, &style_view_inactive, 0);
    render_scan_list(&g_app_context);
}
void cb_view_linked(lv_event_t*) {
    g_app_context.wifi_scan.view = VIEW_LINKED;
    lv_obj_add_style(btn_view_ap, &style_view_inactive, 0);
    lv_obj_add_style(btn_view_sta, &style_view_inactive, 0);
    lv_obj_add_style(btn_view_linked, &style_view_active, 0);
    render_scan_list(&g_app_context);
}

void cb_start_reconnect_test(lv_event_t*) {
    // Corrected access to selected_net and reconnect_sta_target
    if (g_app_context.wifi_scan.selected_net < 0 && g_app_context.wifi_scan.reconnect_sta_target < 0) return;
    if (g_app_context.audit.current_mode != AUDIT_NONE) stop_audit_action(&g_app_context);
    
    if (g_app_context.wifi_scan.selected_net >= 0) { // Corrected access
        set_promiscuous_channel(g_app_context.wifi_scan.ap_list[g_app_context.wifi_scan.selected_net].channel); // Corrected access
    }
    g_app_context.audit.current_mode = AUDIT_RECONNECT;
    g_app_context.audit.audit_timer = lv_timer_create(reconnect_tick, 50, &g_app_context);
    
    if (g_app_context.wifi_scan.reconnect_sta_target >= 0 && g_app_context.wifi_scan.reconnect_sta_target < g_app_context.wifi_scan.sta_count) { // Corrected access
        char sm[18];
        mac_str(g_app_context.wifi_scan.sta_list[g_app_context.wifi_scan.reconnect_sta_target].mac, sm);
        lv_label_set_text_fmt(lbl_audit_status, "#FF4444 RECONNECT TEST ACTIVE#\nClient: %s", sm);
    } else {
        lv_label_set_text_fmt(lbl_audit_status, "#FF4444 RECONNECT TEST ACTIVE#\nNetwork: %.16s", g_app_context.wifi_scan.ap_list[g_app_context.wifi_scan.selected_net].ssid);
    }
    lv_obj_add_flag(btn_reconnect, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_beacon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_pmkid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_stop_audit, LV_OBJ_FLAG_HIDDEN);
}

void cb_start_beacon(lv_event_t*) {
    if (g_app_context.audit.current_mode != AUDIT_NONE) stop_audit_action(&g_app_context);
    set_promiscuous_channel(1);
    g_app_context.audit.current_mode = AUDIT_BEACON;
    g_app_context.audit.beacon_idx = 0;
    g_app_context.audit.audit_timer = lv_timer_create(beacon_tick, 100, &g_app_context);
    lv_label_set_text(lbl_audit_status, "#FFAA00 BEACON LOAD ACTIVE#");
    lv_obj_add_flag(btn_reconnect, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_beacon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_pmkid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_stop_audit, LV_OBJ_FLAG_HIDDEN);
}

void cb_start_pmkid(lv_event_t*) {
    if (g_app_context.wifi_scan.selected_net < 0) return; // Corrected access
    CompanionStatus cst = {};
    const bool use_companion = companion_read_status(&cst);
    if (!use_companion && (g_app_context.capture.pcap_active || g_app_context.capture.probe_active)) {
        lv_label_set_text(lbl_audit_status, "#FF4444 Stop PCAP/Probes first#");
        return;
    }
    if (g_app_context.audit.current_mode != AUDIT_NONE) stop_audit_action(&g_app_context);

    // Local PMKID monitoring requires channel lock + callback swap; pause scan first.
    if (!use_companion) {
        g_app_context.wifi_scan.paused = true;
        esp_wifi_set_promiscuous(false);
        WiFi.scanDelete();
    }

    g_app_context.audit.pmkid_found = false;
    g_app_context.audit.pmkid_via_companion = use_companion;
    memset(g_app_context.audit.pmkid_value, 0, sizeof(g_app_context.audit.pmkid_value));
    memset(g_app_context.audit.pmkid_sta_mac, 0, sizeof(g_app_context.audit.pmkid_sta_mac));
    memset(g_app_context.audit.pmkid_target_ssid, 0, sizeof(g_app_context.audit.pmkid_target_ssid));
    memset(g_app_context.audit.pmkid_target_bssid, 0, sizeof(g_app_context.audit.pmkid_target_bssid));

    uint8_t target_bssid[6] = {0};
    uint8_t target_channel = 1;
    char target_ssid[33] = {0};

    if (g_app_context.wifi_scan.mutex && xSemaphoreTake(g_app_context.wifi_scan.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (g_app_context.wifi_scan.selected_net >= 0 && g_app_context.wifi_scan.selected_net < g_app_context.wifi_scan.ap_count) {
            APRecord& ap = g_app_context.wifi_scan.ap_list[g_app_context.wifi_scan.selected_net];
            memcpy(target_bssid, ap.bssid, 6);
            target_channel = ap.channel;
            strncpy(target_ssid, ap.ssid, 32);
            target_ssid[32] = '\0';
        }
        xSemaphoreGive(g_app_context.wifi_scan.mutex);
    }

    memcpy(g_app_context.audit.pmkid_target_bssid, target_bssid, 6);
    g_app_context.audit.pmkid_target_channel = target_channel;
    strncpy(g_app_context.audit.pmkid_target_ssid, target_ssid, sizeof(g_app_context.audit.pmkid_target_ssid) - 1);
    g_app_context.audit.pmkid_target_ssid[sizeof(g_app_context.audit.pmkid_target_ssid) - 1] = '\0';

    g_app_context.audit.current_mode = AUDIT_PMKID;
    g_app_context.audit.audit_timer = lv_timer_create(pmkid_tick, 500, &g_app_context);

    if (use_companion) {
        // Use the companion MCU for PMKID monitoring so the main radio can keep scanning.
        companion_stop_all();
        companion_clear_result();
        const bool ok = companion_set_target(target_channel, target_bssid) && companion_start_pmkid();
        if (!ok) {
            lv_label_set_text(lbl_audit_status, "#FF4444 Companion not responding#");
            stop_audit_action(&g_app_context);
            return;
        }
        lv_label_set_text(lbl_audit_status, "#00AAFF PMKID MONITORING (COMPANION)...#\nWaiting for handshake");
    } else {
        set_promiscuous_channel(target_channel);
        esp_wifi_set_promiscuous_rx_cb(pmkid_monitor_cb);
        esp_wifi_set_promiscuous(true);
        lv_label_set_text(lbl_audit_status, "#00AAFF PMKID MONITORING...#\nWaiting for handshake");
    }

    lv_obj_add_flag(btn_reconnect, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_beacon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_pmkid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_stop_audit, LV_OBJ_FLAG_HIDDEN);
}

void cb_stop_audit_action(lv_event_t*) { stop_audit_action(&g_app_context); }

void cb_toggle_ble_adv_test(lv_event_t* e) {
    if (g_app_context.ble.busy) return;
    lv_obj_t *target = lv_event_get_current_target(e);
    if (!target) return;
    lv_obj_t *lbl = lv_obj_get_child(target, 0);
    if (!lbl) return;
    if (!g_app_context.ble.adv_test_active) {
        lv_label_set_text(lbl, "STARTING...");
        req_start_ble_adv_test = true;
    } else {
        lv_label_set_text(lbl, "STOPPING...");
        req_stop_ble = true;
    }
}

void cb_toggle_ble_scan(lv_event_t* e) {
    if (g_app_context.ble.busy) return;
    lv_obj_t *target = lv_event_get_current_target(e);
    if (!target) return;
    lv_obj_t *lbl = lv_obj_get_child(target, 0);
    if (!lbl) return;
    if (!g_app_context.ble.scan_active) {
        lv_label_set_text(lbl, "STARTING...");
        req_start_ble_scan = true;
    } else {
        lv_label_set_text(lbl, "STOPPING...");
        req_stop_ble = true;
    }
}

void cb_toggle_pcap(lv_event_t* e) {
    lv_obj_t *target = lv_event_get_current_target(e);
    if (!target) return;
    lv_obj_t *lbl = lv_obj_get_child(target, 0);
    if (!lbl) return;
    if (!g_app_context.capture.pcap_active) {
        if (!sd_card_ready()) { 
            if (lbl_pcap_status) lv_label_set_text(lbl_pcap_status, "#FF4444 NO SD CARD#"); 
            return; 
        }
        lv_label_set_text(lbl, "STARTING...");
        req_start_pcap = true;
    } else {
        lv_label_set_text(lbl, "STOPPING...");
        req_stop_pcap = true;
    }
}

void cb_toggle_probes(lv_event_t* e) {
    lv_obj_t *target = lv_event_get_current_target(e);
    if (!target) return;
    lv_obj_t *lbl = lv_obj_get_child(target, 0);
    if (!lbl) return;
    if (!g_app_context.capture.probe_active) {
        lv_label_set_text(lbl, "STARTING...");
        req_start_probe = true;
    } else {
        lv_label_set_text(lbl, "STOPPING...");
        req_stop_probe = true;
    }
}

void cb_send_lora(lv_event_t* e) {
    extern lv_obj_t *ta_lora_input;
    if (ta_lora_input) {
        const char* txt = lv_textarea_get_text(ta_lora_input);
        if (txt && strlen(txt) > 0) {
            send_meshtastic_text(txt);

            time_t now_epoch_time = time(NULL);
            struct tm timeinfo;
            char time_str[10];
            localtime_r(&now_epoch_time, &timeinfo);
            strftime(time_str, sizeof(time_str), "[%H:%M]", &timeinfo);

            // Local echo to screen
            if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                char local_msg[300];
                snprintf(local_msg, sizeof(local_msg), "%s [ME]: %s\n", time_str, txt);
                if (strlen(g_app_context.lora.log_data) + strlen(local_msg) >= 2048 - 10) {
                    strcpy(g_app_context.lora.log_data, "--- Buffer Cleared ---\n");
                }
                strncat(g_app_context.lora.log_data, local_msg, 2048 - strlen(g_app_context.lora.log_data) - 1);
                g_app_context.lora.log_updated = true;
                xSemaphoreGive(g_app_context.lora.mutex);
            }

            lv_textarea_set_text(ta_lora_input, ""); // Clear after sending
        }
    }
}

void cb_send_lora_chat(lv_event_t* e) {
    extern lv_obj_t *ta_lora_chat_input;
    if (ta_lora_chat_input) {
        const char* txt = lv_textarea_get_text(ta_lora_chat_input);
        if (txt && strlen(txt) > 0) {
            send_meshtastic_text(txt);

            time_t now_epoch_time = time(NULL);
            struct tm timeinfo;
            char time_str[10];
            localtime_r(&now_epoch_time, &timeinfo);
            strftime(time_str, sizeof(time_str), "[%H:%M]", &timeinfo);

            if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                char local_msg[300];
                snprintf(local_msg, sizeof(local_msg), "%s [ME]: %s\n", time_str, txt);
                if (strlen(g_app_context.lora.chat_data) + strlen(local_msg) >= 2048 - 10) {
                    strcpy(g_app_context.lora.chat_data, "--- Buffer Cleared ---\n");
                }
                strncat(g_app_context.lora.chat_data, local_msg, 2048 - strlen(g_app_context.lora.chat_data) - 1);
                g_app_context.lora.chat_updated = true;
                xSemaphoreGive(g_app_context.lora.mutex);
            }
            lv_textarea_set_text(ta_lora_chat_input, ""); // Clear after sending
        }
    }
}

// Send a dummy config request to force the node out of ASCII mode and into Protobuf Stream API mode
void wake_meshtastic_node() {
    meshtastic_ToRadio to_radio = meshtastic_ToRadio_init_zero;
    to_radio.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    to_radio.want_config_id = 999; 

    uint8_t tx_buf[64];
    pb_ostream_t stream = pb_ostream_from_buffer(tx_buf, sizeof(tx_buf));
    if (pb_encode(&stream, meshtastic_ToRadio_fields, &to_radio)) {
        size_t len = stream.bytes_written;
        SerialLoRa.write(0x94);
        SerialLoRa.write(0xC3);
        SerialLoRa.write((len >> 8) & 0xFF);
        SerialLoRa.write(len & 0xFF);
        SerialLoRa.write(tx_buf, len);
    }
}

// ──────────────────────────────────────────────
//              Web Server Handlers
// ──────────────────────────────────────────────
void handleRoot() {
    String html = "<html><head><title>Auditor Storage</title><meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body style='font-family:sans-serif; background:#111; color:#fff;'><h1>SD Card Files</h1><ul>";
    File root = SD_MMC.open("/");
    if (!root) { server.send(500, "text/plain", "SD Card not ready"); return; }
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            html += "<li><a style='color:#00FF88;' href=\"/dl?f=" + String(file.name()) + "\">" + String(file.name()) + "</a> (" + String(file.size()) + " bytes)</li>";
        }
        file = root.openNextFile();
    }
    html += "</ul></body></html>";
    server.send(200, "text/html", html);
}

void handleDownload() {
    if (server.hasArg("f")) {
        String fileName = "/" + server.arg("f");
        File f = SD_MMC.open(fileName, FILE_READ);
        if (f) {
            server.streamFile(f, "application/octet-stream");
            f.close();
            return;
        }
    }
    server.send(404, "text/plain", "File not found");
}

void toggle_web_server() {
    g_app_context.web_server_active = !g_app_context.web_server_active;
    if (g_app_context.web_server_active) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP("Auditor_Admin", "12345678");
        server.on("/", handleRoot);
        server.on("/dl", handleDownload);
        server.begin();
        Serial.println("Web server started at 192.168.4.1");
    } else {
        server.stop();
        WiFi.softAPdisconnect(true);
        Serial.println("Web server stopped");
    }
}

// ──────────────────────────────────────────────
//             Diagnostics Helpers
// ──────────────────────────────────────────────

void reboot_cyd() {
    Serial.println("[SYSTEM] Reboot requested");
    Serial.flush();
    delay(100);
    ESP.restart();
}

void add_bc(const char* fmt, ...) {
    return; // Disabled - breadcrumbs were for debugging only
}

static inline void diag_stage(const char* name, uint32_t start_ms, uint32_t warn_ms = 25) {
    uint32_t dt = millis() - start_ms;
    if (dt >= warn_ms) {
        Serial.printf("[DIAG] slow: %s %lu ms\n", name, (unsigned long)dt);
    }
}

// ──────────────────────────────────────────────
//                  SETUP & LOOP
// ──────────────────────────────────────────────
void main_app_task(void *pvParameters);

static bool g_emergencyHeapLatch = false;

bool is_emergency_heap() {
    return g_emergencyHeapLatch;
}

void check_emergency_heap() {
    uint32_t heap = ESP.getFreeHeap();
    if (heap < 60000) {
        if (!g_emergencyHeapLatch) {
            g_emergencyHeapLatch = true;
            Serial.printf("[EMERGENCY] Heap critically low (%u) - stopping all tasks\n", heap);
            
            stop_ble(&g_app_context);
            stop_audit_action(&g_app_context);
            stop_pcap(&g_app_context);
            stop_probe_monitor(&g_app_context);
        }
    } else if (heap > 90000) {
        g_emergencyHeapLatch = false;
    }
}

void ui_queue_tick(lv_timer_t *timer) {
    uint32_t t0 = millis();
    process_ui_queue();
    perf_ui_queue.record(millis() - t0);
}

void ui_task(void *pvParameters) {
    esp_task_wdt_add(nullptr);
    g_uiTaskWdtAdded = true;

    const TickType_t period = pdMS_TO_TICKS(10);

    for (;;) {
        set_last_state_fmt("ENTER: lv_timer_handler");
        uint32_t lv_t0 = millis();
        
        lv_timer_handler();

        uint32_t ui_dt = millis() - lv_t0;
        set_last_state_fmt("EXITED: lv_timer_handler");
        
        perf_ui_timer.record(ui_dt);
        
        if (ui_dt > 25) {
            Serial.printf("[DIAG] slow: lv_timer_handler %lu ms\n", (unsigned long)ui_dt);
        }
        
        static uint32_t last_obj_print = 0;
        if (millis() - last_obj_print > 10000) {
            Serial.printf("[LVGL] Active UI Objects: %d\n", lv_obj_get_child_cnt(lv_scr_act()));
            last_obj_print = millis();
        }

        safe_wdt_reset(g_uiTaskWdtAdded);
        vTaskDelay(period);
    }
}

// Dedicated background worker for WiFi scanning, PCAP writing, and Probes.
// This completely separates the heavy lifting from the main control loop.
static inline bool time_budget_exceeded(uint32_t startMs, uint32_t budgetMs) {
    return (millis() - startMs) >= budgetMs;
}

void wifi_worker_task(void *pvParameters) {
    esp_task_wdt_add(nullptr);
    g_wifiTaskWdtAdded = true;
    
    AppContext* context = (AppContext*)pvParameters;
    const uint32_t loopBudgetMs = 20;
    
    for (;;) {
        uint32_t loopStart = millis();
        uint32_t t;
        
        t = millis();
        set_last_state_fmt("STAGE: process_pcap_queue");
        process_pcap_queue(context);
        perf_pcap.record(millis() - t);
        safe_wdt_reset(g_wifiTaskWdtAdded);
        if (time_budget_exceeded(loopStart, loopBudgetMs)) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }

        t = millis();
        set_last_state_fmt("STAGE: process_probe_queue");
        process_probe_queue(context);
        perf_probe.record(millis() - t);
        safe_wdt_reset(g_wifiTaskWdtAdded);
        if (time_budget_exceeded(loopStart, loopBudgetMs)) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }

        t = millis();
        set_last_state_fmt("STAGE: process_channel_hop");
        process_channel_hop(context);
        perf_chan_hop.record(millis() - t);
        safe_wdt_reset(g_wifiTaskWdtAdded);

        vTaskDelay(pdMS_TO_TICKS(10)); // Yield to prevent CPU starvation
    }
}

void setup() {
    Serial.begin(115200);
    SerialLoRa.setRxBufferSize(4096); // INCREASE RX BUFFER to prevent interrupt storms during NodeDB bursts
    SerialLoRa.begin(115200, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN); // RX=14, TX=21 for CYD connection

    // Let USB CDC host connect before printing crash report
    delay(2000); 
    
    g_diagMutex = xSemaphoreCreateMutex();
    g_i2cMutex = xSemaphoreCreateMutex();
    if (g_diagMutex) set_last_state_fmt("setup start");
    print_crash_recovery_banner();

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    display_init();
    sd_logger_init();

    // Bring up WiFi stack early so promiscuous operations are safe later.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous_cb);

    Serial.printf("[STARTUP] Internal RAM: Total=%u Free=%u MinFree=%u\n",
        ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
    Serial.printf("[STARTUP] PSRAM: Total=%u Free=%u MinFree=%u\n",
        ESP.getPsramSize(), ESP.getFreePsram(), ESP.getMinFreePsram());

    esp_task_wdt_config_t wdt_cfg = {.timeout_ms = 10000, .idle_core_mask = 0, .trigger_panic = true};
    esp_task_wdt_reconfigure(&wdt_cfg);

    g_app_context.status.mutex = xSemaphoreCreateMutex();
    g_app_context.lora.mutex = xSemaphoreCreateMutex();

    ui_queue.init();

    // Allocate large buffers in PSRAM
    g_app_context.lora.log_data = (char*)ps_calloc(2048, 1);
    g_app_context.lora.chat_data = (char*)ps_calloc(2048, 1);
    g_app_context.wifi_scan.ap_list = (APRecord*)ps_calloc(MAX_APS, sizeof(APRecord));
    g_app_context.wifi_scan.sta_list = (StaRecord*)ps_calloc(MAX_STAS, sizeof(StaRecord));
    if (!g_app_context.lora.log_data || !g_app_context.lora.chat_data || 
        !g_app_context.wifi_scan.ap_list || !g_app_context.wifi_scan.sta_list) {
        Serial.println("[FATAL] Could not allocate large buffers in PSRAM");
        while(1) delay(1000);
    }

    // Initialize AppContext members
    g_app_context.wifi_scan.ap_count = 0;
    g_app_context.wifi_scan.sta_count = 0;
    g_app_context.wifi_scan.view = VIEW_AP;
    g_app_context.wifi_scan.paused = true;
    g_app_context.wifi_scan.last_scan_ms = 0;
    g_app_context.wifi_scan.selected_net = -1;
    g_app_context.wifi_scan.reconnect_sta_target = -1;
    g_app_context.wifi_scan.started = false;
    g_app_context.wifi_scan.scan_timer = nullptr;
    g_app_context.audit.current_mode = AUDIT_NONE;
    g_app_context.audit.audit_timer = nullptr;
    g_app_context.audit.beacon_idx = 0;
    g_app_context.audit.pmkid_found = false;
    g_app_context.audit.pmkid_via_companion = false;
    g_app_context.audit.pmkid_target_channel = 1;
    memset(g_app_context.audit.pmkid_target_ssid, 0, sizeof(g_app_context.audit.pmkid_target_ssid));
    memset(g_app_context.audit.pmkid_value, 0, sizeof(g_app_context.audit.pmkid_value));
    memset(g_app_context.audit.pmkid_sta_mac, 0, sizeof(g_app_context.audit.pmkid_sta_mac));
    memset(g_app_context.audit.pmkid_target_bssid, 0, sizeof(g_app_context.audit.pmkid_target_bssid));
    g_app_context.capture.pcap_active = false;
    g_app_context.capture.probe_active = false;
    g_app_context.capture.pcap_queue = nullptr;
    g_app_context.capture.probe_queue = nullptr;
    g_app_context.capture.pcap_packet_count = 0;
    g_app_context.capture.last_hop_ms = 0;
    g_app_context.capture.channel = 1;
    g_app_context.capture.pcap_ch_locked = false;
    g_app_context.capture.pcap_locked_ch = 1;
    g_app_context.ble.scan_active = false;
    g_app_context.ble.adv_test_active = false;
    g_app_context.ble.busy = false;
    g_app_context.ble.nimble_ready = false;
    g_app_context.ble.scanner = nullptr;
    g_app_context.ble.packet_count = 0;
    g_app_context.ble.ring_head = 0;
    g_app_context.status.service_task = nullptr;
    
    g_app_context.lora.service_task = nullptr;
    g_app_context.lora.log_updated = false;
    g_app_context.lora.chat_updated = false;
    g_app_context.lora.nodedb_updated = false;
    g_app_context.lora.unread_chat = false;
    memset(&g_app_context.lora.stats, 0, sizeof(LoraDeviceStats));

    g_app_context.main_task_handle = nullptr;
    g_app_context.wifi_task_handle = nullptr;
    g_app_context.ui_task_handle = nullptr;

    g_app_context.web_server_active = false;
    g_app_context.device_id = WiFi.macAddress(); // Store device MAC address
    g_app_context.ui_busy = false;
    
    write_status_snapshot(sd_card_ready(), 100, false, 4200, 25); 

    // Initialize modules
    wifi_scanner_init(&g_app_context);
    audit_actions_init(&g_app_context);
    ble_tasks_init(&g_app_context);
    pcap_and_probes_init(&g_app_context);
    
    load_beacon_ssids_from_nvs(&g_app_context); // Load custom SSIDs from NVS
    if (g_app_context.audit.custom_beacon_ssids.empty()) {
        // Add some default beacon SSIDs if NVS is empty
        g_app_context.audit.custom_beacon_ssids.push_back("Free WiFi");
        g_app_context.audit.custom_beacon_ssids.push_back("Guest Network");
        g_app_context.audit.custom_beacon_ssids.push_back("FBI Surveillance Van");
    }

    xTaskCreatePinnedToCore(status_service_task, "status_task", 4096, NULL, 1, &g_app_context.status.service_task, 0);
    xTaskCreatePinnedToCore(lora_service_task, "lora_task", 16384, NULL, 1, &g_app_context.lora.service_task, 0); // Increased stack size to 16KB for protobuf

    ui_build();
    if (!tabview)    { Serial.println("FATAL: tabview NULL after ui_build");    while(1) delay(100); }
    if (!lbl_batt)   { Serial.println("FATAL: lbl_batt NULL after ui_build");   while(1) delay(100); }
    if (!lbl_sd)     { Serial.println("FATAL: lbl_sd NULL after ui_build");     while(1) delay(100); }
    if (!lbl_wifi)   { Serial.println("FATAL: lbl_wifi NULL after ui_build");   while(1) delay(100); }
    set_ui_update_context(&g_app_context);

    g_app_context.wifi_scan.scan_timer = lv_timer_create(scan_tick, 1000, &g_app_context);
    lv_timer_create(ui_update_tick, 200, &g_app_context);
    lv_timer_create(ui_queue_tick, 20, NULL); // Process UI queue safely inside LVGL context

    // Wake up the connected LoRa node
    wake_meshtastic_node();

    Serial.println("ESP32-S3 Auditor ready (Refactored).");
    
    static uint8_t* local_ui_queue_storage = (uint8_t*)ps_malloc(15 * sizeof(LocalUiEvent));
    static StaticQueue_t* local_ui_queue_buffer = (StaticQueue_t*)ps_malloc(sizeof(StaticQueue_t));
    if (local_ui_queue_storage && local_ui_queue_buffer) {
        g_local_ui_queue = xQueueCreateStatic(15, sizeof(LocalUiEvent), local_ui_queue_storage, local_ui_queue_buffer);
    } else {
        g_local_ui_queue = xQueueCreate(15, sizeof(LocalUiEvent));
    }

    // Keep Arduino loopTask on core 1; move the heavy app task to core 0.
    // Note: Do NOT esp_task_wdt_delete(NULL) here, it breaks the background Arduino core loop() WDT feeding!
    xTaskCreatePinnedToCore(main_app_task, "main_app_task", 20480, NULL, 1, &g_app_context.main_task_handle, 0); // Core 0
    xTaskCreatePinnedToCore(wifi_worker_task, "wifi_worker", 4096, &g_app_context, 1, &g_app_context.wifi_task_handle, 0); // Core 0
    xTaskCreatePinnedToCore(ui_task, "ui_task", 12288, NULL, 1, &g_app_context.ui_task_handle, 1);             // Core 1
}

void main_app_task(void *pvParameters) {
    esp_task_wdt_add(nullptr);
    g_mainTaskWdtAdded = true;

    uint32_t last_ble_ui_ms    = 0;
    uint32_t last_web_ms       = 0;
    
    const uint32_t loopBudgetMs = 20;

    while(1) {
        uint32_t t0 = millis();
        uint32_t loopStart = millis();

        // 1. Process deferred teardown and navigation safely outside of the LVGL event loop
        if (pending_stop_all) {
            g_app_context.ui_busy = true;
            
            if (pending_stop_audit) {
                stop_audit_action(&g_app_context);
            }
            if (pending_stop_ble) {
                stop_ble(&g_app_context);
            }
            if (pending_stop_pcap) {
                stop_pcap(&g_app_context);
            }
            if (pending_stop_probe) {
                stop_probe_monitor(&g_app_context);
            }

            if (pending_pause_scan && !g_app_context.wifi_scan.paused && g_app_context.wifi_scan.started) {
                g_app_context.wifi_scan.paused = true;
                // Only disable promiscuous if nothing else is using it.
                if (!g_app_context.capture.pcap_active &&
                    !g_app_context.capture.probe_active &&
                    !(g_app_context.audit.current_mode == AUDIT_PMKID && !g_app_context.audit.pmkid_via_companion)) {
                    esp_wifi_set_promiscuous(false);
                }
                queue_local_ui_text(UI_EVT_SET_SCAN_PAUSE, LV_SYMBOL_PLAY " RESUME");
            }

            pending_stop_all = false;
            pending_stop_audit = true;
            pending_pause_scan = false;
            pending_stop_ble = true;
            pending_stop_pcap = true;
            pending_stop_probe = true;

            if (pending_nav_tab >= 0) {
                LocalUiEvent ev = {};
                ev.type = UI_EVT_NAVIGATE;
                ev.tab_id = pending_nav_tab;
                if (g_local_ui_queue) xQueueSend(g_local_ui_queue, &ev, 0);
                pending_nav_tab = -1;
            }
            g_app_context.ui_busy = false;
        }

        // 2. Process deferred button toggles safely outside of the LVGL event loop
        if (req_stop_ble) {
            stop_ble(&g_app_context);
            req_stop_ble = false;
        }

        if (req_stop_pcap) {
            stop_pcap(&g_app_context);
            req_stop_pcap = false;
        }

        if (req_stop_probe) {
            stop_probe_monitor(&g_app_context);
            req_stop_probe = false;
        }

        if (req_start_ble_adv_test) {
            start_ble_adv_test(&g_app_context);
            queue_local_ui_text(UI_EVT_SET_BLE_ADV_TEST_BUTTON, "STOP BLE ADV TEST");
            queue_local_ui_text(UI_EVT_SET_BLE_STATUS, "#FF4444 BLE ADV TEST ACTIVE#\n\nEmulating Apple-like advertisements...");
            req_start_ble_adv_test = false;
        }

        if (req_start_ble_scan) {
            start_ble_scan(&g_app_context);
            queue_local_ui_text(UI_EVT_SET_BLE_SCAN_BUTTON, "STOP BLE SCAN");
            queue_local_ui_text(UI_EVT_SET_BLE_STATUS, "#00FF88 BLE SCANNING#\n\nListening...");
            req_start_ble_scan = false;
        }

        if (req_start_pcap) {
            start_pcap(&g_app_context);
            queue_local_ui_text(UI_EVT_SET_PCAP_BUTTON, "STOP PCAP");
            queue_local_ui_text(UI_EVT_SET_PCAP_STATUS, "#FFFF00 PCAP ACTIVE#\n\nCapturing...");
            req_start_pcap = false;
        }

        if (req_start_probe) {
            start_probe_monitor(&g_app_context);
            queue_local_ui_text(UI_EVT_SET_PROBE_BUTTON, "STOP PROBE MONITOR");
            req_start_probe = false;
        }

        uint32_t now = millis();
        uint32_t t;

        if (g_app_context.web_server_active && now - last_web_ms >= 10) {
            last_web_ms = now;
            t = millis();
            set_last_state_fmt("STAGE: web_server");
            server.handleClient();
            perf_web.record(millis() - t);
            safe_wdt_reset(g_mainTaskWdtAdded);
            if (time_budget_exceeded(loopStart, loopBudgetMs)) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }
        }

        if (now - last_ble_ui_ms >= 20) {
            last_ble_ui_ms = now;
            t = millis();
            set_last_state_fmt("STAGE: process_ble_scan_ui");
            process_ble_scan_ui(&g_app_context);
            safe_wdt_reset(g_mainTaskWdtAdded);
            if (time_budget_exceeded(loopStart, loopBudgetMs)) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }
        }

        check_emergency_heap();

        // Diagnostics Heartbeat
        static uint32_t last_diag_ms = 0;
        if (millis() - last_diag_ms > 10000) { // Changed to 10s intervals
            last_diag_ms = millis();

            Serial.printf(
                "[DIAG] heap=%u min=%u largest=%u psram=%u "
                "pcapQ=%u probeQ=%u ble(scan=%d advTest=%d busy=%d ready=%d)\n",
                ESP.getFreeHeap(),
                ESP.getMinFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                ESP.getFreePsram(),
                g_app_context.capture.pcap_queue ? uxQueueMessagesWaiting(g_app_context.capture.pcap_queue) : 0,
                g_app_context.capture.probe_queue ? uxQueueMessagesWaiting(g_app_context.capture.probe_queue) : 0,
                g_app_context.ble.scan_active,
                g_app_context.ble.adv_test_active,
                g_app_context.ble.busy,
                g_app_context.ble.nimble_ready
            );

            UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(NULL);
            Serial.printf("[STACK] main_app_task has %u bytes free (total 20480)\n", stack_high_water);
            
            Serial.println("\n--- [PERFORMANCE STATS (Last 10s)] ---");
            perf_ui_timer.print_and_reset("lv_timer_handler");
            perf_ui_queue.print_and_reset("process_ui_queue");
            perf_ble_ui.print_and_reset("process_ble_scan_ui");
            perf_pcap.print_and_reset("process_pcap_queue");
            perf_probe.print_and_reset("process_probe_queue");
            perf_chan_hop.print_and_reset("process_channel_hop");
            perf_lora.print_and_reset("lora_service_task");
            perf_web.print_and_reset("web_server");
            Serial.println("--------------------------------------\n");
        }

        // Always yield so lower-priority work on Core 0 gets time,
        // and prevents task watchdog timeouts.
        set_last_state_fmt("STAGE: main idle");
        safe_wdt_reset(g_mainTaskWdtAdded);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// ──────────────────────────────────────────────
//                   LOOP
// ──────────────────────────────────────────────
void loop() {
    vTaskDelay(pdMS_TO_TICKS(10));
}
