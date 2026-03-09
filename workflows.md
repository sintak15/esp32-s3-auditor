# **ESP32-S3 Pentesting Workflows**

This guide outlines the standard operating procedures for conducting wireless security audits using your 2.8" ESP32-S3 device.

## **Workflow 1: Initial Reconnaissance (Passive)**

The goal is to map the local 2.4GHz environment without transmitting any packets.

1. **Power On:** The device automatically enters the **PENTEST SCAN** tab.  
2. **Environmental Audit:** Observe the "PENTESTING AIRWAVES" status. The device is currently performing a passive scan.  
3. **Signal Analysis:** Look for networks with high RSSI values (closest to 0dBm). These are your primary targets due to signal proximity.  
4. **Security Filtering:** Identify "OPEN" vs "SECURE" networks. Focus on networks using older protocols or weak configurations.

## **Workflow 2: Targeted Deauthentication Audit**

Used to test a client's ability to reconnect or to capture handshakes on an external machine.

1. **Selection:** In the **PENTEST SCAN** tab, tap the specific SSID you wish to audit. The entry will highlight in cyan.  
2. **Module Switch:** Tap the **ATK** button in the header.  
3. **Engagement:** Verify the "TARGET ACQUIRED" information matches your intended BSSID.  
4. **Execution:** Tap **DEAUTH TARGET**. The device begins sending deauthentication frames to the target AP, effectively testing the connection's resilience.

## **Workflow 3: Beacon Frame Saturation (Stress Test)**

Used to test how local devices handle high-density SSID environments (SSID flooding).

1. **Navigation:** Ensure you are in the **PENTEST ATTACK** tab.  
2. **Selection:** A target does not necessarily need to be selected for global spam, but selecting a high-traffic channel is recommended.  
3. **Execution:** Tap **BEACON SPAM**. The device will begin broadcasting thousands of fake SSID frames, appearing as a multitude of "ghost" networks to nearby devices.

## **Workflow 4: Manual Re-Scanning**

Used when moving locations to refresh the target list immediately.

1. **Scan Tab:** Navigate to **PENTEST SCAN**.  
2. **Refresh:** Tap the **\[REFRESH\]** button (now located in the bottom right).  
3. **Verification:** Confirm the list updates with new SSIDs and updated RSSI values.