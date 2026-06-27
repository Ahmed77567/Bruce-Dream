#include "mwfa_scanner.h"
#include "core/display.h"
#include "core/utils.h"
#include "core/mykeyboard.h"
#include "core/wifi/wifi_common.h"
#include "core/settings.h"
#include "core/mwfa/MqttBridge.h"
#include <WiFi.h>
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "lwip/etharp.h"

// ─────────────────────────────────────────────────────────────────────────────
// Global state
// ─────────────────────────────────────────────────────────────────────────────
bool mwfaRelayModeActive = false;
char mwfaBrokerHost[64]  = MWFA_DEFAULT_BROKER;
int  mwfaBrokerPort      = MWFA_DEFAULT_PORT;
char mwfaDeviceId[32]    = MWFA_DEFAULT_DEVICE;
char mwfaBrokerUser[64]  = "";
char mwfaBrokerPass[64]  = "";

// ─────────────────────────────────────────────────────────────────────────────
// mwfa_scanner_menu()
// يفحص الشبكة عبر ARP ثم يرسل النتائج عبر MQTT (أو HTTP قديم كـ fallback)
// ─────────────────────────────────────────────────────────────────────────────
void mwfa_scanner_menu() {
    if (!wifiConnected) {
        if (!wifiConnectMenu()) {
            displayError("Failed to connect!");
            delay(2000);
            return;
        }
    }

    displayInfo("Scanning Network (ARP)...", false);

    // Get netif handle
    esp_netif_t* esp_netinterface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (esp_netinterface == nullptr) {
        displayError("Failed to get netif handle");
        delay(2000);
        return;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(esp_netinterface, &ip_info) != ESP_OK) {
        displayError("Can't get IP info");
        delay(2000);
        return;
    }

    if (ip_info.ip.addr == 0 || ip_info.netmask.addr == 0) {
        displayError("WiFi has no IP/netmask");
        delay(2000);
        return;
    }

    uint32_t ip_le      = ntohl(ip_info.ip.addr);
    uint32_t netmask_le = ntohl(ip_info.netmask.addr);
    uint32_t gateway_le = ntohl(ip_info.gw.addr);

    const uint32_t networkAddress = gateway_le & netmask_le;
    const uint32_t broadcast      = networkAddress | ~netmask_le;

    struct netif* net_iface = (struct netif*)esp_netif_get_netif_impl(esp_netinterface);
    if (net_iface == nullptr) {
        displayError("Netif impl not ready");
        delay(2000);
        return;
    }

    etharp_cleanup_netif(net_iface);

    // ── ARP Probe ─────────────────────────────────────────────────────────
    uint32_t hostsScanned = 0;
    const uint32_t totalHosts = broadcast - networkAddress - 1;
    static uint32_t lastUpdate = 0;

    displayInfo("Sending ARP Probes...", false);

    for (uint32_t ip = networkAddress + 1; ip < broadcast; ip++) {
        if (ip == ip_le || ip == gateway_le) continue;
        ip4_addr_t ip_be{htonl(ip)};
        hostsScanned++;

        if (millis() - lastUpdate > 500) {
            displayRedStripe(
                "Probing " + String(hostsScanned) + "/" + String(totalHosts),
                getComplementaryColor2(bruceConfig.priColor),
                bruceConfig.priColor
            );
            lastUpdate = millis();
        }
        etharp_request(net_iface, &ip_be);
        vTaskDelay(20u / portTICK_PERIOD_MS);
    }

    // ── تجميع النتائج وإرسالها ────────────────────────────────────────────
    displayInfo("Sending to MWFA...", false);

    String routerSsid  = WiFi.SSID();
    String routerBssid = WiFi.BSSIDstr();
    String routerIp    = IPAddress(ip_info.gw.addr).toString();

    int successCount = 0;
    int failCount    = 0;
    int hostsFound   = 0;

    // ── دالة مساعدة للإرسال عبر MQTT أو HTTP ─────────────────────────────
    auto sendDevice = [&](const String& mac, const String& devIp,
                          const String& ssid, const String& bssid,
                          bool isGateway) -> bool {
        hostsFound++;

        if (mwfaRelayModeActive && mwfaBridge.isConnected()) {
            // ✅ إرسال عبر MQTT
            mwfaBridge.publishArp(mac, devIp, ssid, bssid, isGateway);
            return true;
        } else {
            // ⚠️ Fallback: HTTP مباشر (للاختبار على الشبكة المحلية)
            // (يمكن إزالته لاحقاً)
            Serial.printf("[MWFA] No MQTT — skipping %s\n", mac.c_str());
            return false;
        }
    };

    // إرسال معلومات الـ Router
    if (sendDevice(routerBssid, routerIp, routerSsid, routerBssid, true))
        successCount++;
    else
        failCount++;

    // إرسال الأجهزة من جدول ARP
    for (uint32_t i = 0; i < ARP_TABLE_SIZE; ++i) {
        ip4_addr_t* ip_ret;
        eth_addr*   eth_ret;
        struct netif* iface_ptr = net_iface;

        if (etharp_get_entry(i, &ip_ret, &iface_ptr, &eth_ret)) {
            String devIp = IPAddress(ip_ret->addr).toString();

            char macBuf[18];
            snprintf(macBuf, sizeof(macBuf),
                "%02X:%02X:%02X:%02X:%02X:%02X",
                eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
            String devMac(macBuf);

            if (sendDevice(devMac, devIp, routerSsid, routerBssid, false))
                successCount++;
            else
                failCount++;

            delay(30);
        }
    }

    etharp_cleanup_netif(net_iface);

    String resultMsg = "Found: " + String(hostsFound) + " Sent: " + String(successCount);
    if (successCount > 0) displaySuccess(resultMsg);
    else                  displayError(resultMsg);

    delay(3000);
}

// ─────────────────────────────────────────────────────────────────────────────
// mwfa_relay_config()
// إدخال إعدادات الـ Broker (IP + Port + Device ID)
// ─────────────────────────────────────────────────────────────────────────────
void mwfa_relay_config() {
    // إدخال IP الـ Broker
    String hostStr = keyboard(String(mwfaBrokerHost), 64, "Broker IP/Host:");
    if (hostStr.length() > 0 && hostStr.length() < 64) {
        hostStr.toCharArray(mwfaBrokerHost, sizeof(mwfaBrokerHost));
    }

    // إدخال Port
    String portStr = keyboard(String(mwfaBrokerPort), 6, "Broker Port:");
    if (portStr.length() > 0) {
        mwfaBrokerPort = portStr.toInt();
    }

    // إدخال Device ID
    String idStr = keyboard(String(mwfaDeviceId), 32, "Device ID:");
    if (idStr.length() > 0 && idStr.length() < 32) {
        idStr.toCharArray(mwfaDeviceId, sizeof(mwfaDeviceId));
    }

    // إدخال Username
    String userStr = keyboard(String(mwfaBrokerUser), 64, "MQTT User:");
    if (userStr.length() > 0 && userStr.length() < 64) {
        userStr.toCharArray(mwfaBrokerUser, sizeof(mwfaBrokerUser));
    }

    // إدخال Password
    String passStr = keyboard(String(mwfaBrokerPass), 64, "MQTT Pass:");
    if (passStr.length() > 0 && passStr.length() < 64) {
        passStr.toCharArray(mwfaBrokerPass, sizeof(mwfaBrokerPass));
    }

    displaySuccess("Config saved!", true);
}

// ─────────────────────────────────────────────────────────────────────────────
// mwfa_relay_menu()
// قائمة إدارة وضع الـ Relay Mode
// ─────────────────────────────────────────────────────────────────────────────
void mwfa_relay_menu() {
    options_t opts;

    // حالة الاتصال الحالية
    String statusLabel = mwfaBridge.isConnected()
        ? String("● MQTT: ") + mwfaBridge.getBrokerHost()
        : String("○ MQTT: Disconnected");

    opts.push_back({statusLabel, []() {}, false}); // عنصر للعرض فقط

    if (!mwfaBridge.isConnected()) {
        opts.push_back({"Connect to Broker", []() {
            if (!wifiConnected) {
                if (!wifiConnectMenu()) {
                    displayError("No WiFi!");
                    delay(2000);
                    return;
                }
            }
            bool ok = mwfaBridge.connect(mwfaBrokerHost, mwfaBrokerPort, mwfaDeviceId, mwfaBrokerUser, mwfaBrokerPass);
    if (ok) {
                mwfaRelayModeActive = true;
                displaySuccess("MWFA Relay Active!", true);
            }
        }});
    } else {
        opts.push_back({"Disconnect", []() {
            mwfaBridge.disconnect();
            mwfaRelayModeActive = false;
            displayInfo("MWFA Relay Stopped", true);
        }});
    }

    opts.push_back({"Configure Broker", []() { mwfa_relay_config(); }});
    opts.push_back({"ARP Scan & Send",  []() { mwfa_scanner_menu(); }});

    addOptionToMainMenu();
    loopOptions(opts, MENU_TYPE_SUBMENU, "MWFA Relay Mode");
}
