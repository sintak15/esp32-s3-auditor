#pragma once

#include <lvgl.h>
#include "constants.h"

extern lv_obj_t *main_screen, *status_bar, *tabview;
extern lv_obj_t *lbl_sd, *lbl_wifi, *lbl_gps_fix, *lbl_batt, *lbl_batt_pct;
extern lv_obj_t *tab_home, *tab_scan, *tab_pentest, *tab_gps;
extern lv_obj_t *tab_ble, *tab_pcap, *tab_probes, *tab_settings;
extern lv_obj_t *lbl_gps_info;
extern lv_obj_t *scan_list, *lbl_scan_count, *btn_scan_pause, *lbl_scan_pause;
extern lv_obj_t *btn_view_ap, *btn_view_sta, *btn_view_linked;
extern lv_obj_t *lbl_pt_target, *lbl_pt_bssid, *lbl_pt_status;
extern lv_obj_t *btn_deauth, *btn_beacon, *btn_pmkid, *btn_stop_pt;
extern lv_obj_t *lbl_ble_status;
extern lv_obj_t *lbl_pcap_status;
extern lv_obj_t *lbl_pcap_ch;
extern lv_obj_t *btn_pcap_lock;
extern lv_obj_t *low_batt_border;
extern lv_chart_series_t *ui_heap_series;
extern lv_obj_t *btn_ble_sniff;
extern lv_obj_t *temp_warning_border;
extern lv_obj_t *about_panel;
extern lv_obj_t *lbl_firmware_version;
extern lv_obj_t *lbl_build_date;
extern lv_obj_t *lbl_device_id;
extern lv_obj_t *lbl_ota_status;
extern lv_obj_t *lbl_ota_progress;
extern lv_obj_t *btn_ble_flood;
extern lv_obj_t *btn_pcap_start;
extern lv_obj_t *btn_probe_start;

extern lv_style_t style_btn_dark, style_btn_red, style_btn_orange,
                  style_btn_blue, style_view_active, style_view_inactive;

void ui_build();
void no_scroll(lv_obj_t *o);

// UI elements that are declared in ui_module.cpp but used elsewhere
extern lv_obj_t *probe_list;

extern void navigate_to(int tab);
extern void cb_nav_home(lv_event_t *e);
extern void cb_nav_scan(lv_event_t *e);
extern void cb_net_selected(lv_event_t *e);
extern void cb_pause_scan(lv_event_t *e);
extern void cb_view_ap(lv_event_t *e), cb_view_sta(lv_event_t *e), cb_view_linked(lv_event_t *e);
extern void cb_start_deauth(lv_event_t *e), cb_start_beacon(lv_event_t *e), cb_start_pmkid(lv_event_t *e), cb_stop_pentest(lv_event_t *e);
extern void cb_toggle_ble_flood(lv_event_t *e), cb_toggle_ble_sniff(lv_event_t *e), cb_toggle_pcap(lv_event_t *e), cb_toggle_probes(lv_event_t *e);
