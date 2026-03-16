#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

void wifi_scanner_init(AppContext *ctx);
void wifi_scanner_task(void *param);
const char* enc_str(uint8_t enc);   // <-- added declaration

#ifdef __cplusplus
}
#endif