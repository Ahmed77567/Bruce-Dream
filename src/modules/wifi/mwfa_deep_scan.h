#ifndef __MWFA_DEEP_SCAN_H__
#define __MWFA_DEEP_SCAN_H__

/**
 * MWFA — Deep Scan Agent for T-Embed
 * ─────────────────────────────────────────────────────────────────────────────
 * يعمل كـ "وكيل تنفيذ" داخل الشبكة المستهدفة:
 *   - يستقبل أوامر فحص بورتات عبر MQTT
 *   - يتصل فعلياً بالأهداف الداخلية (192.168.x.x) عبر TCP Connect
 *   - يرسل النتائج لحظة بلحظة عبر MQTT
 *
 * يُشغَّل عن بُعد من التطبيق أو الكالي عبر أمر "establish_proxy"
 * ويُوقَف عبر أمر "stop_proxy" أو زر Esc على الجهاز.
 */

#include <Arduino.h>

// ── الحالة ──────────────────────────────────────────────────────────────────
extern bool mwfaDeepScanActive;

// ── TCP Probe Result ────────────────────────────────────────────────────────
struct TcpProbeResult {
    String ip;
    uint16_t port;
    String state;      // "open" | "closed" | "filtered"
    uint32_t responseMs;
    String banner;     // أول 256 بايت من الرد (إن وُجد)
};

// ── الدوال الرئيسية ─────────────────────────────────────────────────────────

/**
 * تفعيل وضع الفحص العميق
 * - يتأكد من WiFi و MQTT
 * - يجمع معلومات الشبكة (Gateway, Subnet, Local IP)
 * - ينشر proxy_status: "ready"
 */
void mwfa_deep_scan_start();

/**
 * إيقاف وضع الفحص العميق
 * - ينشر proxy_status: "stopped"
 */
void mwfa_deep_scan_stop();

/**
 * فحص بورت واحد عبر TCP Connect
 * @param ip      عنوان الهدف (مثل "192.168.1.10")
 * @param port    رقم البورت
 * @param taskId  معرّف المهمة (لربط النتيجة بالطلب)
 * @return نتيجة الفحص
 */
TcpProbeResult mwfa_deep_scan_tcp_probe(const char* ip, uint16_t port, const char* taskId = "");

/**
 * فحص نطاق بورتات على هدف واحد
 * @param ip         عنوان الهدف
 * @param startPort  بداية النطاق
 * @param endPort    نهاية النطاق
 * @param taskId     معرّف المهمة
 */
void mwfa_deep_scan_port_range(const char* ip, uint16_t startPort, uint16_t endPort, const char* taskId = "");

/**
 * فحص قائمة بورتات محددة (مثل "21,22,80,443")
 * @param ip     عنوان الهدف
 * @param ports  سلسلة بورتات مفصولة بفاصلة
 * @param taskId معرّف المهمة
 */
void mwfa_deep_scan_port_list(const char* ip, const String& ports, const char* taskId = "");

/**
 * خيار القائمة — لتشغيل Deep Scan Agent يدوياً
 */
void mwfa_deep_scan_menu();

#endif // __MWFA_DEEP_SCAN_H__
