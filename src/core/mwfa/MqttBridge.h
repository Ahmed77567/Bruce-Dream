#pragma once
#ifndef __MWFA_MQTT_BRIDGE_H__
#define __MWFA_MQTT_BRIDGE_H__

/**
 * MWFA — MQTT Bridge for ESP32 (T-Embed CC1101)
 * ─────────────────────────────────────────────────────────────────────────────
 * يتصل بـ Mosquitto Broker على الـ VPS وينشر البيانات كـ JSON.
 *
 * الاستخدام:
 *   MqttBridge& mqtt = MqttBridge::getInstance();
 *   mqtt.connect("192.168.1.1", 1883, "device01");
 *   mqtt.publishArp(mac, ip, ssid, bssid);
 *   mqtt.loop();  // استدعِها في loop() أو task
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// ─────────────────────────────────────────────────────────────────────────────
// إعدادات MWFA الافتراضية (تُعدَّل من الـ UI أو config)
// ─────────────────────────────────────────────────────────────────────────────
#define MWFA_DEFAULT_BROKER   ""              // يُدخله المستخدم من قائمة MWFA Relay Mode
#define MWFA_DEFAULT_PORT     1883
#define MWFA_DEFAULT_DEVICE   "device01"
#define MWFA_KEEPALIVE_MS     30000UL   // heartbeat كل 30 ثانية
#define MWFA_RECONNECT_MS     5000UL    // محاولة إعادة الاتصال كل 5 ثوانٍ
#define MWFA_QUEUE_SIZE       20        // حد الرسائل المُؤجَّلة في الذاكرة

// ─────────────────────────────────────────────────────────────────────────────
class MqttBridge {
public:
    // Singleton
    static MqttBridge& getInstance();

    // منع النسخ
    MqttBridge(const MqttBridge&)            = delete;
    MqttBridge& operator=(const MqttBridge&) = delete;

    // ── Life Cycle ────────────────────────────────────────────────────────
    /**
     * الاتصال بالـ Broker
     * @param host     عنوان IP أو domain للـ VPS
     * @param port     المنفذ (افتراضي 1883)
     * @param deviceId معرّف هذا الجهاز (مثل "device01")
     * @param username كلمة المرور (اختياري)
     * @param password كلمة المرور (اختياري)
     */
    bool connect(const char* host,
                 uint16_t    port     = MWFA_DEFAULT_PORT,
                 const char* deviceId = MWFA_DEFAULT_DEVICE,
                 const char* username = nullptr,
                 const char* password = nullptr);

    /**
     * قطع الاتصال وتعطيل الـ Relay Mode
     */
    void disconnect();

    /**
     * يجب استدعاؤها بشكل دوري (في loop أو FreeRTOS task)
     * تتولى:
     *   - إعادة الاتصال عند الانقطاع
     *   - إرسال الـ heartbeat
     *   - معالجة رسائل الاشتراك
     */
    void loop();

    // ── State ─────────────────────────────────────────────────────────────
    bool isConnected();
    bool isRelayActive()  const;
    void setRelayActive(bool active);
    String getDeviceId()  const { return _deviceId; }
    String getBrokerHost() const { return _host; }

    // ── Publish Helpers ───────────────────────────────────────────────────

    /**
     * نشر نتيجة ARP Scan
     * مسار: mwfa/<deviceId>/arp
     */
    void publishArp(const String& macAddress,
                    const String& ipAddress,
                    const String& ssid,
                    const String& bssid,
                    bool          isGateway = false);

    /**
     * نشر شبكة WiFi مُكتشَفة
     * مسار: mwfa/<deviceId>/wifi
     */
    void publishWifi(const String& ssid,
                     const String& bssid,
                     int           channel,
                     int           rssi,
                     const String& encryption = "");

    /**
     * نشر إشارة راديو CC1101
     * مسار: mwfa/<deviceId>/rf
     */
    void publishRf(float          frequency,
                   const String&  protocol,
                   uint32_t       value,
                   int            bits,
                   const String&  rawData = "",
                   int            rssi    = 0,
                   unsigned long  duration = 0);

    /**
     * نشر حالة الجهاز
     * مسار: mwfa/<deviceId>/status
     */
    void publishStatus(const String& status = "online");

    /**
     * نشر حالة وكيل الفحص العميق
     * مسار: mwfa/<deviceId>/proxy_status
     */
    void publishProxyStatus(const String& status,
                            const String& localIp   = "",
                            const String& gateway   = "",
                            const String& subnet    = "");

    /**
     * نشر رسالة JSON خام (للاستخدام المتقدم)
     * @param subTopic المسار الفرعي (مثل "arp", "wifi", "rf")
     * @param json     النص JSON
     */
    bool publishRaw(const char* subTopic, const String& json);

private:
    MqttBridge();
    ~MqttBridge();

    // ── Internal ──────────────────────────────────────────────────────────
    bool     _tryReconnect();
    String   _buildTopic(const char* subTopic) const;
    void     _sendHeartbeat();
    static void _onMessage(char* topic, byte* payload, unsigned int length);

    // ── Members ───────────────────────────────────────────────────────────
    WiFiClient       _wifiClient;
    WiFiClientSecure _wifiClientSecure;
    PubSubClient     _mqttClient;

    String   _host;
    uint16_t _port        = MWFA_DEFAULT_PORT;
    String   _deviceId    = MWFA_DEFAULT_DEVICE;
    String   _username;
    String   _password;

    bool     _relayActive = false;
    unsigned long _lastReconnectAttempt = 0;
    unsigned long _lastHeartbeat        = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global accessor (مثل millis, Serial)
// ─────────────────────────────────────────────────────────────────────────────
extern MqttBridge& mwfaBridge;

#endif // __MWFA_MQTT_BRIDGE_H__
