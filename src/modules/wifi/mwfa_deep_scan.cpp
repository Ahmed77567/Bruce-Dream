#include "mwfa_deep_scan.h"
#include "core/display.h"
#include "core/utils.h"
#include "core/mykeyboard.h"
#include "core/wifi/wifi_common.h"
#include "core/settings.h"
#include "core/mwfa/MqttBridge.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// ─────────────────────────────────────────────────────────────────────────────
// Global State
// ─────────────────────────────────────────────────────────────────────────────
bool mwfaDeepScanActive = false;

// Timeout for TCP connect probe (milliseconds)
static const uint16_t TCP_PROBE_TIMEOUT_MS = 3000;

// Delay between consecutive probes to avoid overwhelming the ESP32
static const uint16_t INTER_PROBE_DELAY_MS = 50;

// Maximum banner bytes to read
static const size_t MAX_BANNER_LEN = 256;

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Publish TCP probe result via MQTT
// ─────────────────────────────────────────────────────────────────────────────
static void _publishTcpResult(const TcpProbeResult& result, const char* taskId) {
    if (!mwfaBridge.isConnected()) return;

    JsonDocument doc;
    doc["ip"]    = result.ip;
    doc["port"]  = result.port;
    doc["state"] = result.state;
    doc["ms"]    = result.responseMs;
    if (result.banner.length() > 0) {
        doc["banner"] = result.banner;
    }
    if (taskId && strlen(taskId) > 0) {
        doc["task_id"] = taskId;
    }

    String json;
    serializeJson(doc, json);
    mwfaBridge.publishRaw("tcp_result", json);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Publish proxy status via MQTT
// ─────────────────────────────────────────────────────────────────────────────
static void _publishProxyStatus(const char* status) {
    if (!mwfaBridge.isConnected()) return;

    JsonDocument doc;
    doc["status"]  = status;
    doc["localIp"] = WiFi.localIP().toString();
    doc["gateway"] = WiFi.gatewayIP().toString();
    doc["subnet"]  = WiFi.subnetMask().toString();
    doc["ssid"]    = WiFi.SSID();
    doc["bssid"]   = WiFi.BSSIDstr();

    String json;
    serializeJson(doc, json);
    mwfaBridge.publishRaw("proxy_status", json);
}

// ─────────────────────────────────────────────────────────────────────────────
// mwfa_deep_scan_start()
// ─────────────────────────────────────────────────────────────────────────────
void mwfa_deep_scan_start() {
    if (!wifiConnected) {
        Serial.println("[MWFA-DS] No WiFi — cannot start deep scan agent");
        displayError("No WiFi!", true);
        return;
    }

    if (!mwfaBridge.isConnected()) {
        Serial.println("[MWFA-DS] No MQTT — cannot start deep scan agent");
        displayError("No MQTT!", true);
        return;
    }

    mwfaDeepScanActive = true;

    Serial.println("[MWFA-DS] ✅ Deep Scan Agent ACTIVATED");
    Serial.printf("[MWFA-DS]    Local IP : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[MWFA-DS]    Gateway  : %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("[MWFA-DS]    Subnet   : %s\n", WiFi.subnetMask().toString().c_str());

    _publishProxyStatus("ready");

    displaySuccess("Deep Scan Agent: READY", true);
}

// ─────────────────────────────────────────────────────────────────────────────
// mwfa_deep_scan_stop()
// ─────────────────────────────────────────────────────────────────────────────
void mwfa_deep_scan_stop() {
    mwfaDeepScanActive = false;

    _publishProxyStatus("stopped");

    Serial.println("[MWFA-DS] ⛔ Deep Scan Agent STOPPED");
    displayInfo("Deep Scan Agent: Stopped", true);
}

// ─────────────────────────────────────────────────────────────────────────────
// mwfa_deep_scan_tcp_probe()
// Performs a single TCP connect probe and publishes the result via MQTT
// ─────────────────────────────────────────────────────────────────────────────
TcpProbeResult mwfa_deep_scan_tcp_probe(const char* ip, uint16_t port, const char* taskId) {
    TcpProbeResult result;
    result.ip   = ip;
    result.port = port;
    result.responseMs = 0;
    result.state = "filtered"; // default if timeout

    WiFiClient probe;
    probe.setTimeout(TCP_PROBE_TIMEOUT_MS / 1000); // setTimeout expects seconds

    unsigned long startMs = millis();
    bool connected = probe.connect(ip, port, TCP_PROBE_TIMEOUT_MS);
    unsigned long elapsed = millis() - startMs;
    result.responseMs = (uint32_t)elapsed;

    if (connected) {
        result.state = "open";

        // ── Banner Grab ──────────────────────────────────────────────────
        // Wait briefly for the service to send its banner
        unsigned long bannerStart = millis();
        while (!probe.available() && (millis() - bannerStart) < 1000) {
            delay(50);
        }

        String banner = "";
        while (probe.available() && banner.length() < MAX_BANNER_LEN) {
            char c = (char)probe.read();
            // Replace non-printable characters with dots
            if (c >= 32 && c < 127) {
                banner += c;
            } else if (c == '\r' || c == '\n') {
                banner += ' ';
            } else {
                banner += '.';
            }
        }

        if (banner.length() > 0) {
            banner.trim();
            result.banner = banner;
        }

        probe.stop();
    } else {
        // Connection refused = port is closed (RST received quickly)
        // Timeout = port is filtered
        if (elapsed < (TCP_PROBE_TIMEOUT_MS - 500)) {
            result.state = "closed";
        } else {
            result.state = "filtered";
        }
    }

    // Publish result via MQTT
    _publishTcpResult(result, taskId);

    Serial.printf("[MWFA-DS] %s:%d → %s (%lums)%s\n",
        ip, port,
        result.state.c_str(),
        result.responseMs,
        result.banner.length() > 0 ? (" [" + result.banner.substring(0, 40) + "]").c_str() : ""
    );

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// mwfa_deep_scan_port_range()
// Scans a range of ports on a single target
// ─────────────────────────────────────────────────────────────────────────────
void mwfa_deep_scan_port_range(const char* ip, uint16_t startPort, uint16_t endPort, const char* taskId) {
    if (!mwfaDeepScanActive) {
        Serial.println("[MWFA-DS] Agent not active — ignoring port_range command");
        return;
    }

    uint16_t total = endPort - startPort + 1;
    uint16_t scanned = 0;
    uint16_t openCount = 0;

    Serial.printf("[MWFA-DS] Scanning %s ports %d-%d (%d total)\n", ip, startPort, endPort, total);

    _publishProxyStatus("scanning");

    drawMainBorderWithTitle("DEEP SCAN");
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setCursor(10, BORDER_PAD_Y + FM * LH);
    tft.printf("Target: %s\n", ip);
    tft.setCursor(10, tft.getCursorY());
    tft.printf("Ports: %d-%d\n", startPort, endPort);

    for (uint16_t port = startPort; port <= endPort; port++) {
        // Allow cancellation from device
        if (check(EscPress)) {
            Serial.println("[MWFA-DS] Scan cancelled by user (Esc)");
            displayInfo("Scan cancelled", true);
            _publishProxyStatus("ready");
            return;
        }

        // Keep MQTT alive during long scans
        mwfaBridge.loop();

        TcpProbeResult result = mwfa_deep_scan_tcp_probe(ip, port, taskId);
        scanned++;

        if (result.state == "open") {
            openCount++;
            tft.setCursor(10, tft.getCursorY());
            tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
            tft.printf("  %d/tcp OPEN %s\n", port,
                result.banner.length() > 0 ? result.banner.substring(0, 20).c_str() : "");
            tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        }

        // Progress update every 10 ports
        if (scanned % 10 == 0 || port == endPort) {
            int pct = (scanned * 100) / total;
            displayRedStripe(
                String(pct) + "% | " + String(scanned) + "/" + String(total) + " | Open: " + String(openCount),
                getComplementaryColor2(bruceConfig.priColor),
                bruceConfig.priColor
            );
        }

        delay(INTER_PROBE_DELAY_MS);
    }

    // Publish scan completion
    {
        JsonDocument doc;
        doc["ip"]        = ip;
        doc["task_id"]   = taskId;
        doc["event"]     = "scan_complete";
        doc["total"]     = total;
        doc["openCount"] = openCount;
        String json;
        serializeJson(doc, json);
        mwfaBridge.publishRaw("tcp_result", json);
    }

    _publishProxyStatus("ready");

    String summary = "Done! Open: " + String(openCount) + "/" + String(total);
    Serial.printf("[MWFA-DS] %s\n", summary.c_str());
    displaySuccess(summary, true);
}

// ─────────────────────────────────────────────────────────────────────────────
// mwfa_deep_scan_port_list()
// Scans a comma-separated list of ports (e.g. "21,22,80,443,8080")
// ─────────────────────────────────────────────────────────────────────────────
void mwfa_deep_scan_port_list(const char* ip, const String& ports, const char* taskId) {
    if (!mwfaDeepScanActive) {
        Serial.println("[MWFA-DS] Agent not active — ignoring port_list command");
        return;
    }

    Serial.printf("[MWFA-DS] Scanning %s ports [%s]\n", ip, ports.c_str());

    _publishProxyStatus("scanning");

    // Parse comma-separated port list
    String remaining = ports;
    uint16_t openCount = 0;
    uint16_t totalCount = 0;

    drawMainBorderWithTitle("DEEP SCAN");
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setCursor(10, BORDER_PAD_Y + FM * LH);
    tft.printf("Target: %s\n", ip);

    while (remaining.length() > 0) {
        // Allow cancellation
        if (check(EscPress)) {
            Serial.println("[MWFA-DS] Scan cancelled by user (Esc)");
            _publishProxyStatus("ready");
            return;
        }

        mwfaBridge.loop();

        int commaIdx = remaining.indexOf(',');
        String portStr;
        if (commaIdx >= 0) {
            portStr = remaining.substring(0, commaIdx);
            remaining = remaining.substring(commaIdx + 1);
        } else {
            portStr = remaining;
            remaining = "";
        }

        portStr.trim();
        uint16_t port = (uint16_t)portStr.toInt();
        if (port == 0) continue;

        TcpProbeResult result = mwfa_deep_scan_tcp_probe(ip, port, taskId);
        totalCount++;

        if (result.state == "open") {
            openCount++;
            tft.setCursor(10, tft.getCursorY());
            tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
            tft.printf("  %d/tcp OPEN\n", port);
            tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        }

        delay(INTER_PROBE_DELAY_MS);
    }

    // Publish completion
    {
        JsonDocument doc;
        doc["ip"]        = ip;
        doc["task_id"]   = taskId;
        doc["event"]     = "scan_complete";
        doc["total"]     = totalCount;
        doc["openCount"] = openCount;
        String json;
        serializeJson(doc, json);
        mwfaBridge.publishRaw("tcp_result", json);
    }

    _publishProxyStatus("ready");

    String summary = "Done! Open: " + String(openCount) + "/" + String(totalCount);
    Serial.printf("[MWFA-DS] %s\n", summary.c_str());
    displaySuccess(summary, true);
}

// ─────────────────────────────────────────────────────────────────────────────
// mwfa_deep_scan_menu()
// Manual menu entry for Deep Scan Agent
// ─────────────────────────────────────────────────────────────────────────────
void mwfa_deep_scan_menu() {
    if (!wifiConnected) {
        if (!wifiConnectMenu()) {
            displayError("No WiFi!");
            delay(2000);
            return;
        }
    }

    if (!mwfaBridge.isConnected()) {
        displayError("Connect MQTT first!");
        delay(2000);
        return;
    }

    std::vector<Option> opts;

    String statusLabel = mwfaDeepScanActive
        ? String("● Agent: ACTIVE")
        : String("○ Agent: INACTIVE");

    opts.push_back({statusLabel, []() {}, false});

    if (!mwfaDeepScanActive) {
        opts.push_back({"Activate Agent", []() {
            mwfa_deep_scan_start();
        }});
    } else {
        opts.push_back({"Stop Agent", []() {
            mwfa_deep_scan_stop();
        }});
    }

    opts.push_back({"Manual: Scan IP", []() {
        if (!mwfaDeepScanActive) {
            displayError("Activate agent first!");
            delay(2000);
            return;
        }
        String ip = keyboard("192.168.1.1", 16, "Target IP:");
        if (ip.length() == 0 || ip == "\x1B") return;

        String portRange = keyboard("1-1000", 12, "Port range (e.g. 1-1000):");
        if (portRange.length() == 0 || portRange == "\x1B") return;

        // Parse range
        int dashIdx = portRange.indexOf('-');
        if (dashIdx > 0) {
            uint16_t startP = (uint16_t)portRange.substring(0, dashIdx).toInt();
            uint16_t endP   = (uint16_t)portRange.substring(dashIdx + 1).toInt();
            if (startP > 0 && endP > 0 && endP >= startP) {
                mwfa_deep_scan_port_range(ip.c_str(), startP, endP, "manual");
            } else {
                displayError("Invalid range");
                delay(2000);
            }
        } else {
            // Treat as comma-separated list
            mwfa_deep_scan_port_list(ip.c_str(), portRange, "manual");
        }
    }});

    addOptionToMainMenu();
    loopOptions(opts, MENU_TYPE_SUBMENU, "Deep Scan Agent");
}
