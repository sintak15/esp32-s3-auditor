#ifndef WIFI_ATTACKS_H
#define WIFI_ATTACKS_H

#include <Arduino.h>
#include <vector>
#include <WString.h>

void startDeauthFlood(const uint8_t* bssid, const uint8_t* client_mac);
void startBeaconFlood(uint16_t count, const std::vector<String>& ssids);
void stopFloods();

#endif // WIFI_ATTACKS_H
