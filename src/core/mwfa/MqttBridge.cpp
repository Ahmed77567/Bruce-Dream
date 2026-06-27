#include "MqttBridge.h"
#include "core/display.h"   // displayInfo, displayError
#include "core/settings.h"  // bruceConfig

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────
MqttBridge& MqttBridge::getInstance() {
    static MqttBridge instance;
    return instance;
}

// Global reference for convenience
MqttBridge& mwfaBridge = MqttBridge::getInstance();

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────
MqttBridge::MqttBridge() : _mqttClient(_wifiClient) {
    _mqttClient.setBufferSize(1024);  // رسائل JSON حتى 1KB
    _mqttClient.setKeepAlive(60);
}

MqttBridge::~MqttBridge() {
    if (_mqttClient.connected()) {
        _mqttClient.disconnect();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// connect()
// ─────────────────────────────────────────────────────────────────────────────
bool MqttBridge::connect(const char* host,
                          uint16_t    port,
                          const char* deviceId,
                          const char* username,
                          const char* password)
{
    _host     = host;
    _port     = port;
    _deviceId = deviceId;
    _username = username ? username : "";
    _password = password ? password : "";

    _mqttClient.setServer(host, port);

    Serial.printf("[MWFA] Connecting to MQTT %s:%d as %s\n", host, port, deviceId);
    displayInfo(String("MQTT → ") + host, false);

    return _tryReconnect();
}

// ─────────────────────────────────────────────────────────────────────────────
// disconnect()
// ─────────────────────────────────────────────────────────────────────────────
void MqttBridge::disconnect() {
    _relayActive = false;
    if (_mqttClient.connected()) {
        // رسالة وداع
        publishStatus("offline");
        _mqttClient.disconnect();
    }
    Serial.println("[MWFA] Disconnected from MQTT broker");
}

// ─────────────────────────────────────────────────────────────────────────────
// loop() — يُستدعى في الـ loop الرئيسي أو task
// ─────────────────────────────────────────────────────────────────────────────
void MqttBridge::loop() {
    if (!_relayActive) return;

    if (!_mqttClient.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > MWFA_RECONNECT_MS) {
            _lastReconnectAttempt = now;
            _tryReconnect();
        }
        return;
    }

    _mqttClient.loop();

    // Heartbeat دوري
    unsigned long now = millis();
    if (now - _lastHeartbeat > MWFA_KEEPALIVE_MS) {
        _lastHeartbeat = now;
        _sendHeartbeat();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// isConnected() / isRelayActive()
// ─────────────────────────────────────────────────────────────────────────────
bool MqttBridge::isConnected()   const { return _mqttClient.connected(); }
bool MqttBridge::isRelayActive() const { return _relayActive; }
void MqttBridge::setRelayActive(bool active) { _relayActive = active; }

// ─────────────────────────────────────────────────────────────────────────────
// publishArp() — mwfa/<deviceId>/arp
// ─────────────────────────────────────────────────────────────────────────────
void MqttBridge::publishArp(const String& macAddress,
                             const String& ipAddress,
                             const String& ssid,
                             const String& bssid,
                             bool          isGateway)
{
    String json = "{";
    json += "\"macAddress\":\"" + macAddress + "\",";
    json += "\"ipAddress\":\""  + ipAddress  + "\",";
    json += "\"ssid\":\""       + ssid       + "\",";
    json += "\"bssid\":\""      + bssid      + "\",";
    json += "\"isGateway\":"    + String(isGateway ? "true" : "false");
    json += "}";

    publishRaw("arp", json);
}

// ─────────────────────────────────────────────────────────────────────────────
// publishWifi() — mwfa/<deviceId>/wifi
// ─────────────────────────────────────────────────────────────────────────────
void MqttBridge::publishWifi(const String& ssid,
                              const String& bssid,
                              int           channel,
                              int           rssi,
                              const String& encryption)
{
    String json = "{";
    json += "\"ssid\":\""       + ssid       + "\",";
    json += "\"bssid\":\""      + bssid      + "\",";
    json += "\"channel\":"      + String(channel)  + ",";
    json += "\"rssi\":"         + String(rssi)     + ",";
    json += "\"encryption\":\"" + encryption + "\"";
    json += "}";

    publishRaw("wifi", json);
}

// ─────────────────────────────────────────────────────────────────────────────
// publishRf() — mwfa/<deviceId>/rf
// ─────────────────────────────────────────────────────────────────────────────
void MqttBridge::publishRf(float          frequency,
                            const String&  protocol,
                            uint32_t       value,
                            int            bits,
                            const String&  rawData,
                            int            rssi,
                            unsigned long  duration)
{
    String json = "{";
    json += "\"frequency\":"  + String(frequency, 2) + ",";
    json += "\"protocol\":\"" + protocol + "\",";
    json += "\"value\":"      + String(value)  + ",";
    json += "\"bits\":"       + String(bits)   + ",";
    json += "\"rssi\":"       + String(rssi)   + ",";
    json += "\"duration\":"   + String((unsigned long)duration);
    if (rawData.length() > 0) {
        json += ",\"rawData\":\"" + rawData + "\"";
    }
    json += "}";

    publishRaw("rf", json);
}

// ─────────────────────────────────────────────────────────────────────────────
// publishStatus() — mwfa/<deviceId>/status
// ─────────────────────────────────────────────────────────────────────────────
void MqttBridge::publishStatus(const String& status) {
    String json = "{";
    json += "\"status\":\""   + status + "\",";
    json += "\"firmware\":\"" + String(BRUCE_VERSION) + "\",";
    json += "\"ip\":\""       + WiFi.localIP().toString() + "\"";
    json += "}";

    publishRaw("status", json);
}

// ─────────────────────────────────────────────────────────────────────────────
// publishRaw() — الأساس لكل publish
// ─────────────────────────────────────────────────────────────────────────────
bool MqttBridge::publishRaw(const char* subTopic, const String& json) {
    if (!_mqttClient.connected()) {
        Serial.printf("[MWFA] Cannot publish '%s' — not connected\n", subTopic);
        return false;
    }

    String topic = _buildTopic(subTopic);
    bool ok = _mqttClient.publish(topic.c_str(), json.c_str(), false);

    if (ok) {
        Serial.printf("[MWFA] → %s\n", topic.c_str());
    } else {
        Serial.printf("[MWFA] ✗ Failed to publish to %s\n", topic.c_str());
    }
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
bool MqttBridge::_tryReconnect() {
    String clientId = String("bruce-") + _deviceId + "-" + String(millis());

    bool ok;
    if (_username.length() > 0) {
        ok = _mqttClient.connect(clientId.c_str(),
                                  _username.c_str(),
                                  _password.c_str());
    } else {
        ok = _mqttClient.connect(clientId.c_str());
    }

    if (ok) {
        _relayActive  = true;
        _lastHeartbeat = millis();
        Serial.println("[MWFA] ✅ MQTT Connected!");
        displayInfo("MWFA: Connected!", true);
        publishStatus("online");
    } else {
        int state = _mqttClient.state();
        Serial.printf("[MWFA] ✗ MQTT connect failed, state=%d\n", state);
        displayError(String("MWFA: MQTT err ") + String(state), false);
    }

    return ok;
}

String MqttBridge::_buildTopic(const char* subTopic) const {
    return String("mwfa/") + _deviceId + "/" + subTopic;
}

void MqttBridge::_sendHeartbeat() {
    publishStatus("online");
    Serial.println("[MWFA] ♥ Heartbeat sent");
}
