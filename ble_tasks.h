#pragma once

#include <NimBLEDevice.h>
#include "types.h"

class BLESniffCB : public NimBLEScanCallbacks {
public:
    explicit BLESniffCB(AppContext* context);
    void onResult(const NimBLEAdvertisedDevice* dev) override;
private:
    AppContext* app_context;
};

void ble_tasks_init(AppContext* context);
void start_ble_sniff(AppContext* context);
void start_ble_flood(AppContext* context);
void stop_ble(AppContext* context);

void process_ble_sniff_ui(AppContext* context);
void process_ble_flood(AppContext* context);
