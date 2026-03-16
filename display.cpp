#include "display.h"
#include "constants.h"
#include <TFT_eSPI.h>
#include <Wire.h>

// Forward declare from ui_module
extern lv_obj_t *lbl_batt, *lbl_batt_pct, *lbl_sd, *lbl_gps_fix, *lbl_wifi;
extern lv_obj_t *lbl_gps_info;
extern lv_obj_t* tabview;

// Global objects for this module
TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_color_t *lvgl_buf1 = nullptr;
static lv_color_t *lvgl_buf2 = nullptr;

static AppContext* ui_context = nullptr;

void set_ui_update_context(AppContext* context) {
    ui_context = context;
}

TouchPoint get_touch() {
    TouchPoint pt = {0, 0, false};
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission() != 0) return pt;
    Wire.requestFrom(TOUCH_ADDR, (uint8_t)5);
    if (Wire.available() == 5) {
        uint8_t t = Wire.read(), xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
        if (t > 0 && t < 3) {
            pt.pressed = true;
            pt.x = constrain(((uint16_t)(xh & 0x0F) << 8) | xl, 0, SCREEN_W);
            pt.y = constrain(((uint16_t)(yh & 0x0F) << 8) | yl, 0, SCREEN_H);
        }
    }
    return pt;
}

void lvgl_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *cm) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)cm, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

void lvgl_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static int16_t lx = 0, ly = 0;
    TouchPoint pt = get_touch();
    if (pt.pressed) {
        lx = pt.x;
        ly = pt.y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = lx;
    data->point.y = ly;
}

void display_init() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    pinMode(TP_RST, OUTPUT);
    digitalWrite(TP_RST, LOW);
    delay(100);
    digitalWrite(TP_RST, HIGH);
    delay(50);

    Wire.begin(I2C_SDA, I2C_SCL, 100000);

    tft.begin();
    tft.setRotation(0);
    tft.invertDisplay(true);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lvgl_buf1 = (lv_color_t *)ps_malloc(SCREEN_W * SCREEN_H * sizeof(lv_color_t));
    lvgl_buf2 = (lv_color_t *)ps_malloc(SCREEN_W * SCREEN_H * sizeof(lv_color_t));
    if (!lvgl_buf1 || !lvgl_buf2) {
        Serial.println("FATAL: PSRAM alloc failed");
        while (1) delay(1000);
    }

    lv_disp_draw_buf_init(&draw_buf, lvgl_buf1, lvgl_buf2, SCREEN_W * SCREEN_H);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.flush_cb = lvgl_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read;
    lv_indev_drv_register(&indev_drv);
}

static GpsSnapshot read_gps_snapshot() {
  GpsSnapshot s = {};
  if (ui_context && ui_context->gps.mutex && xSemaphoreTake(ui_context->gps.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    s = ui_context->gps.snap;
    xSemaphoreGive(ui_context->gps.mutex);
  }
  return s;
}

static StatusSnapshot read_status_snapshot() {
  StatusSnapshot s = {};
  if (ui_context && ui_context->status.mutex && xSemaphoreTake(ui_context->status.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    s = ui_context->status.snap;
    xSemaphoreGive(ui_context->status.mutex);
  }
  return s;
}


void ui_update_tick(lv_timer_t *timer) {
    if (!ui_context) return;
    // Guard against timer firing before ui_build() has finished assigning all objects
    if (!lbl_batt || !lbl_batt_pct || !lbl_sd || !lbl_gps_fix || !lbl_wifi || !tabview) return;

    GpsSnapshot gs = read_gps_snapshot();
    StatusSnapshot ss = read_status_snapshot();

    // Battery
    uint32_t batt_col = ss.batteryPct < 20 ? 0xFF4444 : ss.batteryPct < 50 ? 0xFFFF00 : 0xFFFFFF;
    lv_obj_set_style_text_color(lbl_batt, lv_color_hex(batt_col), 0);
    char bpbuf[8];
    snprintf(bpbuf, sizeof(bpbuf), "%d%%", ss.batteryPct);
    lv_label_set_text(lbl_batt_pct, bpbuf);
    lv_obj_set_style_text_color(lbl_batt_pct, lv_color_hex(batt_col), 0);

    // SD icon
    lv_obj_set_style_text_color(lbl_sd, lv_color_hex(ss.sdMounted ? 0x00FF88 : 0xFF4444), 0);

    // GPS icon
    if (gs.locValid)
        lv_obj_set_style_text_color(lbl_gps_fix, lv_color_hex(0x00FF88), 0);
    else if (gs.charsProcessed > 10) {
        static bool gb = false;
        gb = !gb;
        lv_obj_set_style_text_color(lbl_gps_fix, lv_color_hex(gb ? 0xFFFF00 : 0x444444), 0);
    } else {
        lv_obj_set_style_text_color(lbl_gps_fix, lv_color_hex(0x444444), 0);
    }

    // WiFi icon colour
    uint32_t wc = 0xFFFFFF;
    if (ui_context->pentest.current_mode == PT_DEAUTH) wc = 0xFF4444;
    else if (ui_context->pentest.current_mode == PT_BEACON) wc = 0xFFFF00;
    else if (ui_context->pentest.current_mode == PT_PMKID) wc = 0x4488FF;
    else if (ui_context->sniffer.pcap_active || ui_context->sniffer.probe_active) wc = 0xFF8800;
    else if (!ui_context->wifi_scan.paused) wc = 0x00FFFF;
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(wc), 0);

    // GPS tab info (only update if the tab is active)
    if (lv_tabview_get_tab_act(tabview) == 3) {
        if (!gs.locValid) {
            if (gs.charsProcessed > 10) {
                static uint32_t lc = 0;
                uint32_t rate = gs.charsProcessed - lc;
                lc = gs.charsProcessed;
                uint32_t secs = millis() / 1000;
                char utc[24] = "??:??:??", dat[16] = "??/??/????";
                if (gs.timeValid) snprintf(utc, sizeof(utc), "%02u:%02u:%02u UTC", gs.hour, gs.minute, gs.second);
                if (gs.dateValid) snprintf(dat, sizeof(dat), "%02u/%02u/%04u", gs.day, gs.month, gs.year);
                lv_label_set_text_fmt(lbl_gps_info,
                                      "#FFAA00 SEARCHING...#\n\n"
                                      "#AAAAAA TIME:#   %s\n#AAAAAA DATE:#   %s\n\n"
                                      "#AAAAAA LAT:#    %.6f\n#AAAAAA LON:#    %.6f\n"
                                      "#AAAAAA ALT:#    %.1fm\n#AAAAAA SPD:#    %.1f km/h\n"
                                      "#AAAAAA HDG:#    %.1f deg\n\n"
                                      "#AAAAAA SATS:#   %lu\n#AAAAAA HDOP:#   %.1f\n"
                                      "#AAAAAA CHARS:#  %lu (%lu/2s)\n#AAAAAA UPTIME:# %lus",
                                      utc, dat,
                                      gs.locValid ? gs.lat : 0.0, gs.locValid ? gs.lon : 0.0,
                                      gs.altValid ? gs.altMeters : 0.0, gs.speedValid ? gs.speedKmph : 0.0,
                                      gs.courseValid ? gs.courseDeg : 0.0, (unsigned long)gs.sats,
                                      gs.hdopValid ? gs.hdop : 0.0,
                                      (unsigned long)gs.charsProcessed, (unsigned long)rate, (unsigned long)secs);
            } else {
                lv_label_set_text(lbl_gps_info,
                                  "#888888 No signal yet...#\n\n#555555 chars rx: 0#\n\n"
                                  "Check wiring:\nGPS TX -> IO43\nGPS RX -> IO44");
            }
        } else {
            char utc[24] = "??:??:??", dat[16] = "??/??/????";
            if (gs.timeValid) snprintf(utc, sizeof(utc), "%02u:%02u:%02u UTC", gs.hour, gs.minute, gs.second);
            if (gs.dateValid) snprintf(dat, sizeof(dat), "%02u/%02u/%04u", gs.day, gs.month, gs.year);
            lv_label_set_text_fmt(lbl_gps_info,
                                  "#00FF88 FIX OK#\n\n"
                                  "#AAAAAA TIME:#   %s\n#AAAAAA DATE:#   %s\n\n"
                                  "#AAAAAA LAT:#    %.6f\n#AAAAAA LON:#    %.6f\n"
                                  "#AAAAAA ALT:#    %.1fm\n#AAAAAA SPD:#    %.1f km/h\n"
                                  "#AAAAAA HDG:#    %.1f deg\n\n"
                                  "#AAAAAA SATS:#   %lu\n#AAAAAA HDOP:#   %.1f",
                                  utc, dat,
                                  gs.lat, gs.lon,
                                  gs.altMeters, gs.speedKmph,
                                  gs.courseValid ? gs.courseDeg : 0.0,
                                  (unsigned long)gs.sats,
                                  gs.hdopValid ? gs.hdop : 99.9);
        }
    }
}
