#ifndef __MWFA_SCANNER_H__
#define __MWFA_SCANNER_H__

#include <Arduino.h>

// ── الوضع الحالي للـ Relay ──────────────────────────────────────────────────
extern bool mwfaRelayModeActive;

// ── الإعدادات (تُحفظ/تُقرأ من bruceConfig) ──────────────────────────────────
extern char mwfaBrokerHost[64];
extern int  mwfaBrokerPort;
extern char mwfaDeviceId[32];
extern char mwfaBrokerUser[64];
extern char mwfaBrokerPass[64];

// ── دوال القائمة ─────────────────────────────────────────────────────────────
void mwfa_scanner_menu();     // ARP Scan + إرسال النتائج
void mwfa_relay_menu();       // إدارة وضع الـ Relay Mode (connect/disconnect)
void mwfa_relay_config();     // إعدادات الـ Broker

#endif // __MWFA_SCANNER_H__
