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
MqttBridge::MqttBridge() {
    _mqttClient.setBufferSize(1024);  // رسائل JSON حتى 1KB
    _mqttClient.setKeepAlive(15);
    _mqttClient.setCallback(MqttBridge::_onMessage);
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

    if (port == 8883) {
        _wifiClientSecure.setInsecure(); // قبول شهادة HiveMQ
        _mqttClient.setClient(_wifiClientSecure);
    } else {
        _mqttClient.setClient(_wifiClient);
    }

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
bool MqttBridge::isConnected()   { return _mqttClient.connected(); }
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
    bool retain = (String(subTopic) == "proxy_status" || String(subTopic) == "status");
    bool ok = _mqttClient.publish(topic.c_str(), json.c_str(), retain);

    if (ok) {
        Serial.printf("[MWFA] → %s\n", topic.c_str());
    } else {
        Serial.printf("[MWFA] ✗ Failed to publish to %s\n", topic.c_str());
    }
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// publishProxyStatus() — mwfa/<deviceId>/proxy_status
// ─────────────────────────────────────────────────────────────────────────────
void MqttBridge::publishProxyStatus(const String& status,
                                     const String& localIp,
                                     const String& gateway,
                                     const String& subnet)
{
    String json = "{";
    json += "\"status\":\""  + status + "\"";
    if (localIp.length() > 0) json += ",\"localIp\":\"" + localIp + "\"";
    if (gateway.length() > 0) json += ",\"gateway\":\"" + gateway + "\"";
    if (subnet.length() > 0)  json += ",\"subnet\":\""  + subnet  + "\"";
    json += "}";

    publishRaw("proxy_status", json);
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
bool MqttBridge::_tryReconnect() {
    String clientId = String("bruce-") + _deviceId + "-" + String(millis());

    String willTopic = _buildTopic("proxy_status");
    String willMsg = "{\"status\":\"offline\"}";

    bool ok;
    if (_username.length() > 0) {
        ok = _mqttClient.connect(clientId.c_str(),
                                  _username.c_str(),
                                  _password.c_str(),
                                  willTopic.c_str(),
                                  1, // QoS
                                  true, // Retain
                                  willMsg.c_str());
    } else {
        ok = _mqttClient.connect(clientId.c_str(),
                                  nullptr,
                                  nullptr,
                                  willTopic.c_str(),
                                  1,
                                  true,
                                  willMsg.c_str());
    }

    if (ok) {
        _relayActive  = true;
        _lastHeartbeat = millis();
        Serial.println("[MWFA] ✅ MQTT Connected!");
        displayInfo("MWFA: Connected!", true);
        publishStatus("online");
        
        // الاشتراك بقناة الأوامر الخاصة بهذا الجهاز
        String commandTopic = String("mwfa/commands/") + _deviceId;
        _mqttClient.subscribe(commandTopic.c_str());
        Serial.printf("[MWFA] Subscribed to %s\n", commandTopic.c_str());
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

#include <ArduinoJson.h>
#include "modules/wifi/mwfa_scanner.h"
#include "modules/wifi/mwfa_deep_scan.h"

struct ProbeArgs {
    String ip;
    uint16_t port;
    String taskId;
};

void task_tcp_probe(void *pvParameters) {
    ProbeArgs *args = (ProbeArgs*)pvParameters;
    mwfa_deep_scan_tcp_probe(args->ip.c_str(), args->port, args->taskId.c_str());
    delete args;
    vTaskDelete(NULL);
}

struct RangeArgs {
    String ip;
    uint16_t startPort;
    uint16_t endPort;
    String taskId;
};

void task_port_range(void *pvParameters) {
    RangeArgs *args = (RangeArgs*)pvParameters;
    mwfa_deep_scan_port_range(args->ip.c_str(), args->startPort, args->endPort, args->taskId.c_str());
    delete args;
    vTaskDelete(NULL);
}

struct ListArgs {
    String ip;
    String ports;
    String taskId;
};

void task_port_list(void *pvParameters) {
    ListArgs *args = (ListArgs*)pvParameters;
    mwfa_deep_scan_port_list(args->ip.c_str(), args->ports, args->taskId.c_str());
    delete args;
    vTaskDelete(NULL);
}

void MqttBridge::_onMessage(char* topic, byte* payload, unsigned int length) {
    Serial.printf("[MWFA] Command received on topic: %s\n", topic);
    
    // تحويل الـ payload إلى نص
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';
    Serial.printf("[MWFA] Payload: %s\n", message);

    // تحليل الـ JSON القادم
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
        Serial.print(F("[MWFA] deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    String command = doc["command"] | "";
    
    if (command == "scan") {
        Serial.println("[MWFA] Executing Remote Command: scan");
        displayInfo("Remote Scan Cmd!", false);
        mwfa_scanner_menu();
    } else if (command == "establish_proxy") {
        Serial.println("[MWFA] Executing Remote Command: establish_proxy");
        displayInfo("Deep Scan Agent Activating...", false);
        mwfa_deep_scan_start();
    } else if (command == "tcp_probe") {
        String ip = doc["ip"] | "";
        int port  = doc["port"] | 0;
        String taskId = doc["task_id"] | "";
        if (ip.length() > 0 && port > 0) {
            Serial.printf("[MWFA] TCP Probe: %s:%d\n", ip.c_str(), port);
            ProbeArgs *args = new ProbeArgs{ip, (uint16_t)port, taskId};
            xTaskCreate(task_tcp_probe, "tcp_probe", 4096, args, 1, NULL);
        } else {
            Serial.println("[MWFA] tcp_probe: missing ip or port");
        }
    } else if (command == "port_scan") {
        String ip    = doc["ip"] | "";
        String ports = doc["ports"] | "";
        String taskId = doc["task_id"] | "";
        if (ip.length() > 0 && ports.length() > 0) {
            Serial.printf("[MWFA] Port Scan: %s [%s]\n", ip.c_str(), ports.c_str());
            int dashIdx = ports.indexOf('-');
            if (dashIdx > 0) {
                uint16_t startP = (uint16_t)ports.substring(0, dashIdx).toInt();
                uint16_t endP   = (uint16_t)ports.substring(dashIdx + 1).toInt();
                if (startP > 0 && endP > 0 && endP >= startP) {
                    RangeArgs *args = new RangeArgs{ip, startP, endP, taskId};
                    xTaskCreate(task_port_range, "port_range", 4096, args, 1, NULL);
                }
            } else {
                ListArgs *args = new ListArgs{ip, ports, taskId};
                xTaskCreate(task_port_list, "port_list", 4096, args, 1, NULL);
            }
        } else {
            Serial.println("[MWFA] port_scan: missing ip or ports");
        }
    } else if (command == "stop_proxy") {
        Serial.println("[MWFA] Executing Remote Command: stop_proxy");
        mwfa_deep_scan_stop();
    } else if (command == "rf_attack") {
        Serial.println("[MWFA] Executing Remote Command: rf_attack");
        displayWarning("RF Attack Cmd!", false);
    } else {
        Serial.printf("[MWFA] Unknown command: %s\n", command.c_str());
    }
}
