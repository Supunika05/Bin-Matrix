#ifndef BIN_NAME_AP_H
#define BIN_NAME_AP_H

#include <Arduino.h>

constexpr int CONFIG_BTN_PIN = 4;               // change if your button uses another pin
constexpr unsigned long BTN_DEBOUNCE_MS = 50;
constexpr unsigned long AP_AUTO_CLOSE_MS = 5 * 60 * 1000UL; // 5 minutes
constexpr int EEPROM_SIZE = 512;
constexpr int BIN_NAME_ADDR = 0;
constexpr int BIN_NAME_MAX = 64;

// Note: AP_SSID and AP_PASS are defined in the .cpp (do NOT initialize them here)
extern const char* AP_SSID;
extern const char* AP_PASS;

// Provisioning API
void provisioning_init();
void provisioning_loop();
void provisioning_startNow();
String provisioning_getBinName();
bool isConfigActive();

#endif // BIN_NAME_AP_H
