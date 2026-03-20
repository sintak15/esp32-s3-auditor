# **ESP32-S3 Auditing Workflows (LVGL High-Perf Edition)**

This guide outlines the standard operating procedures for conducting wireless security audits using your upgraded ESP32-S3 suite.

## **Workflow 1: Initial Survey (Passive)**

Map the local environment without transmitting any frames.

1. **Power On:** The device enters the **Home Hub**. Tap the **SCAN** icon.  
2. **Environment Audit:** Tap the **START** button in the footer. The WiFi icon in the status bar will turn cyan (0x00FFCC).  
3. **Signal Analysis:** Observe the scan list. Tap **APs** for access points, **STAs** for clients, or **Linked** to see which clients are talking to which routers.  
4. **Security Filtering:** Check the encryption labels (OPEN, WPA2, etc.) and RSSI bars to identify the strongest signals and networks with simpler protection settings.

## **Workflow 2: Handshake Audit (PMKID)**

Capture security data to test WPA2 PSK strength.

1. **Selection:** In the **Scan** tab, tap an SSID. It will highlight, and the device will automatically switch to the **Audit** tab.  
2. **Setup:** Verify the SSID and BSSID match your selection.  
3. **Execution:** Tap **PMKID CAPTURE**. The WiFi icon turns blue. The device will now wait for a client to associate to capture the PMKID handshake.  
4. **Completion:** Once captured, the status will show "\#00FF88 PMKID CAPTURED\!\#". The data is saved to /PMKID\_AUDIT.hc22000 and /PMKID\_AUDIT.CSV on your SD card.

## **Workflow 3: Environmental Stress Testing**

Test how local infrastructure handles frame saturation.

1. **Navigation:** Go to the **Audit** tab.  
2. **Reconnect Test:** Tap **RECONNECT TEST**. This tests how client connections recover (focused if a client was selected, otherwise broadcast).  
3. **Beacon Load Test:** Tap **BEACON LOAD**. This tests how local devices handle many simulated networks appearing simultaneously.  
4. **Monitoring:** Switch to the **PCAP** tab while an audit task is running to log the traffic for later analysis in Wireshark.

## **Workflow 4: Passive Traffic Review (Monitoring)**

Identify hidden network usage and mobile device presence.

1. **PCAP Capture:** Go to the **PCAP** tab and tap **START PCAP**. The device hops through all 13 channels, saving everything it hears to an SD card file (for example: /capture\_123456.pcap).  
2. **Probe Monitoring:** Go to the **Probes** tab and tap **START PROBE MONITOR**. This list will populate with SSIDs that nearby devices are searching for (handle responsibly). Entries are also appended to /PROBE\_MON.TXT on the SD card.  
3. **BLE Audit:** Go to the **BLE** tab. Tap **START BLE SCAN** to log every Bluetooth MAC and RSSI in the area to /BLE\_SCAN.CSV.

## **Workflow 5: Power Management**

1. **Screen timeout:** The backlight turns off after 10 minutes of inactivity to save battery.  
2. **Wake:** Tap the screen to restore brightness and continue.
