<p align="center">
  <img src="https://raw.githubusercontent.com/pr3y/Bruce/main/media/pictures/bruce_hd.png" alt="Bruce Dream Logo" width="300">
</p>

<h1 align="center">Bruce Dream 🌙</h1>

<p align="center">
  <strong>The Ultimate ESP32 Multitool for Cyber Security & Network Testing</strong><br>
  <em>Customized, Optimized, and Enhanced by Ahmed77567</em>
</p>

---

## 🌟 What is Bruce Dream?
**Bruce Dream** is an advanced, heavily customized fork of the famous "Bruce" ESP32 firmware. It is an all-in-one pocket multitool designed for Red Teamers, Penetration Testers, and Hardware Hackers. This custom build specifically targets stability, efficiency, and effectiveness in WiFi auditing modules.

<p align="center">
  <img src="https://raw.githubusercontent.com/pr3y/Bruce/main/media/pictures/lilygo-t-display-s3.jpg" alt="Hardware Example" width="400">
</p>

## 🛠️ Exclusive 'Dream' Fixes & Improvements
We have heavily modified the core WiFi packet injection modules to solve widespread issues found in the original firmware.

### 1. iPhone & Modern Android Visibility Fix 📱
In the original firmware, the Beacon Spammer generated completely random MAC addresses. Modern smartphones (like iPhones and new Androids) enforce strict IEEE 802.11 standards and silently drop/ignore packets with invalid MAC headers (e.g., Multicast bits).
- **Our Fix:** We rewrote the `generateRandomWiFiMac()` function to mathematically guarantee that all fake APs have **Unicast** and **Locally Administered** MAC addresses (Setting the first byte to `0x02`). This ensures 100% visibility of your spoofed networks on all modern devices!

### 2. Single SSID Flood & Channel Lockup Fix 📡
Previously, launching the "Single SSID" attack would lock the ESP32 in a permanent `while(true)` loop on a single WiFi channel. Target devices scanning different channels would often miss the attack, rendering it ineffective.
- **Our Fix:** We replaced the infinite loop with a precise, burst-fire `for` loop (60 sequenced transmissions) that appends sequential numbers (e.g., *Network 1, Network 2*). The module now gracefully returns control, allowing the channel-hopper to rotate frequencies. The result? A massive, stable flood of spoofed networks across all channels without ever freezing the device!

### 3. UI & Size Optimization ⚡
- Removed over 50MB of unnecessary background cache and media files.
- Customized system strings, interface colors, and build environments for maximum efficiency.

---

## ⚔️ Core Features
Even with our custom fixes, **Bruce Dream** retains all the terrifying capabilities of the original firmware:
- **WiFi 802.11:** Deauth Attacks, Beacon Spamming (List, Random, Single), Evil Portal, Karma Attacks, PMKID Capture, and Handshake Sniffing.
- **Bluetooth (BLE):** Apple Device Popup Spam, Windows/Android FastPair Spam, BLE Sniffing.
- **Sub-GHz (RF):** Record, Transmit, Replay, Brute-force, and Jamming (Requires CC1101 module).
- **Infrared (IR):** Universal TV-B-Gone, Custom IR code transmitting.
- **RFID / NFC:** Reading and emulating cards (Requires PN532 or RC522).

## 🧰 Supported Hardware
- **LilyGO T-Embed CC1101** *(Primary focus of this build)*
- M5StickC / M5StickC-Plus / M5StickC-Plus2
- M5Cardputer
- ESP32-WROOM & ESP32-S3 boards

---

## 💻 Installation
1. Download the `Bruce-Dream.zip` repository.
2. Open the folder in **VS Code** with the **PlatformIO** extension installed.
3. Select your environment (e.g., `env:lilygo-t-embed-cc1101`) in the bottom bar.
4. Click the **Upload** arrow to flash it directly to your ESP32 device!

---
> **Disclaimer:** This tool is strictly for educational purposes and authorized auditing only. The developers and contributors are not responsible for any misuse or illegal activities.

*Developed with ❤️ by **Ahmed77567** based on the original Bruce project.*