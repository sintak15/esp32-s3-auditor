#pragma once

#include <NimBLEDevice.h>
#include "types.h"

class BLEScanCB : public NimBLEScanCallbacks {
public:
    explicit BLEScanCB(AppContext* context);
    void onResult(const NimBLEAdvertisedDevice* dev) override;
private:
    AppContext* app_context;
};

void ble_tasks_init(AppContext* context);
void start_ble_scan(AppContext* context);
void start_ble_adv_test(AppContext* context);
void stop_ble(AppContext* context);

void process_ble_scan_ui(AppContext* context);
