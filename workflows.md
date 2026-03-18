# **ESP32-S3 Auditing Workflows (LVGL High-Perf Edition)**

This guide outlines the standard operating procedures for conducting wireless security audits using your upgraded ESP32-S3 suite.

## **Workflow 1: Initial Reconnaissance (Passive)**

Map the local environment without transmitting any frames.

1. **Power On:** The device enters the **Home Hub**. Tap the **SCAN** icon.  
2. **Environment Audit:** Tap the **START** button in the footer. The WiFi icon in the status bar will turn cyan (0x00FFCC).  
3. **Signal Analysis:** Observe the scan list. Tap **APs** for access points, **STAs** for clients, or **Linked** to see which clients are talking to which routers.  
4. **Security Filtering:** Check the encryption labels (OPEN, WPA2, etc.) and RSSI bars to identify the strongest, most vulnerable targets.

## **Workflow 2: Targeted Handshake Audit (PMKID)**

Capture security data to test WPA2 PSK strength.

1. **Selection:** In the **Scan** tab, tap a target SSID. It will highlight, and the device will automatically switch to the **Audit** tab.  
2. **Setup:** Verify the SSID and BSSID match your target.  
3. **Execution:** Tap **PMKID CAPTURE**. The WiFi icon turns blue. The device will now wait for a client to associate to capture the PMKID handshake.  
4. **Completion:** Once captured, the status will show "\#00FF88 PMKID CAPTURED\!\#". The data is saved to /PMKID.hc22000 on your SD card.

## **Workflow 3: Environmental Stress Testing**

Test how local infrastructure handles frame saturation.

1. **Navigation:** Go to the **Audit** tab.  
2. **Deauth Test:** Tap **DEAUTH TEST**. This tests the resilience of client connections (Targeted if a client was picked, otherwise Broadcast).  
3. **Beacon Flood:** Tap **BEACON FLOOD**. This tests how local devices handle hundreds of "ghost" networks appearing simultaneously.  
4. **Monitoring:** Switch to the **PCAP** tab while an audit task is running to log the traffic for later analysis in Wireshark.

## **Workflow 4: Passive Traffic Analysis (Sniffing)**

Identify hidden network usage and mobile device presence.

1. **PCAP Capture:** Go to the **PCAP** tab and tap **START PCAP**. The device hops through all 13 channels, saving everything it hears to an SD card file (/cap\_timestamp.pcap).  
2. **Probe Sniffing:** Go to the **Probes** tab and tap **START PROBE SNIFF**. This list will populate with SSIDs that nearby phones are searching for (revealing where people have been).  
3. **BLE Audit:** Go to the **BLE** tab. Tap **START BLE SNIFF** to log every Bluetooth MAC and RSSI in the area to /BLE\_LOG.CSV.

## **Workflow 5: Power Management**

1. **Sleep:** The device enters deep sleep after 5 minutes of inactivity to save battery.  
2. **Wake:** Simply tap the screen to trigger the **TP\_INT** pin and resume your session instantly.