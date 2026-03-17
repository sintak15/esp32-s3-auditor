# **ESP32-S3 Pentest Suite 🛡️**

A high-performance wireless auditing and penetration testing suite built for the ESP32-S3. This tool utilizes LVGL for a smooth, touch-enabled UI and features dual-core FreeRTOS processing for high-speed packet sniffing, BLE spoofing, and wardriving.

## **⚠️ Disclaimer**

**This tool is strictly for educational purposes and authorized auditing of networks you own or have explicit permission to test.** Do not use this device to interfere with public, private, or enterprise networks without authorization.

## **✨ Features**

* **Dual-Core Architecture:** UI runs cleanly on Core 1 while intensive packet sniffing/hopping runs on Core 0\.  
* **WiFi Reconnaissance:** \* AP and Station (Client) scanning.  
  * Target mapping and AP-Client association tracking.  
* **Active WiFi Auditing:**  
  * Deauthentication testing (Targeted or Broadcast).  
  * Beacon Flooding (SSID spoofing).  
  * PMKID Capture (Saved directly to SD card for Hashcat).  
* **Passive Sniffing (High Speed):**  
  * **PCAP Capture:** Captures raw 802.11 frames to .pcap on the SD card using a high-speed PSRAM ring buffer.  
  * **Probe Request Sniffing:** Monitors nearby devices searching for hidden/known networks.  
* **Bluetooth (BLE) Module:**  
  * BLE Device Sniffing (Logs unique MACs and RSSI).  
  * Apple BLE Spam/Spoofing (Pop-up flooding).  

## **🛠️ Hardware Requirements**

* **Microcontroller:** ESP32-S3 (with PSRAM enabled, OPI recommended)  
* **Display:** 2.8" IPS TFT LCD (ST7789/ILI9341) with Capacitive/I2C Touch (CST8xx)  
* **Storage:** SD/MMC Card Module (Required for PCAP and PMKID saving)  

## **⚙️ Dependencies**

Install the following libraries in the Arduino IDE:

* TFT\_eSPI (Configure User\_Setup.h for your specific display pins)  
* lvgl (v8.3.x recommended)  
* NimBLE-Arduino  

## **🚀 Installation & Build**

1. Create a folder named esp32-s3-pentester and place esp32-s3-pentester.ino inside it.  
2. Open the project in the Arduino IDE.  
3. Select your ESP32-S3 board. Ensure **PSRAM is enabled** in the Tools menu (e.g., "OPI PSRAM").  
4. Set the partition scheme to allow enough space (e.g., 16MB (3MB APP, 9MB FATFS) or similar Large APP setting).  
5. Compile and upload.