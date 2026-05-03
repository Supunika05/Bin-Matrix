#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <Arduino.h>

// CONFIG
constexpr int CONFIG_BTN_PIN = 0;               // boot pin
constexpr unsigned long BTN_DEBOUNCE_MS = 50;
constexpr unsigned long AP_AUTO_CLOSE_MS = 5 * 60 * 1000UL; // 5 minutes
DNSServer dnsServer;
constexpr byte DNS_PORT = 53;
constexpr int EEPROM_SIZE = 512;
constexpr int BIN_NAME_ADDR = 0;
constexpr int BIN_NAME_MAX = 64;
const char* AP_SSID = "SmartBin_Setup";
const char* AP_PASS = "BinMatrix123";
String bin_name = "UNASSIGNED";

// Globals
WebServer server(80);
bool configActive = false;
unsigned long configStartMillis = 0;

// button debouncing
unsigned long lastBtnChange = 0;
bool lastBtnState = HIGH; // using INPUT_PULLUP -> not pressed = HIGH

// Utility: device UID
String getDeviceUID() {
  uint64_t chipid = ESP.getEfuseMac();
  char uid[17];
  snprintf(uid, sizeof(uid), "%04X%08X",
           (uint16_t)(chipid >> 32),
           (uint32_t)chipid);
  return String(uid);
}

// EEPROM helpers for bin name
void saveBinName(const String &name) {
  EEPROM.begin(EEPROM_SIZE);
  // make sure to not overflow
  int len = min((int)name.length(), BIN_NAME_MAX - 1);
  for (int i = 0; i < BIN_NAME_MAX; ++i) {
    if (i < len) EEPROM.write(BIN_NAME_ADDR + i, name[i]);
    else EEPROM.write(BIN_NAME_ADDR + i, 0);
  }
  EEPROM.commit();
  EEPROM.end();
}

String loadBinName() {
  EEPROM.begin(EEPROM_SIZE);
  char buf[BIN_NAME_MAX + 1];
  for (int i = 0; i < BIN_NAME_MAX; ++i) {
    buf[i] = (char)EEPROM.read(BIN_NAME_ADDR + i);
    if (buf[i] == 0) break;
  }
  buf[BIN_NAME_MAX] = '\0';
  EEPROM.end();

  if (strlen(buf) == 0) return String("UNNAMED_BIN");
  return String(buf);
}

// Web handlers
void handleRoot() {
  String current = loadBinName();
  String uid = getDeviceUID();

  String page =
      "<!doctype html>"
      "<html lang='en'>"
      "<head>"
      "<meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<title>Bin Matrix Setup</title>"

      "<style>"
      "body{"
      "  margin:0;"
      "  font-family:Arial,Helvetica,sans-serif;"
      "  background:#ffffff;"
      "  display:flex;"
      "  justify-content:center;"
      "  align-items:center;"
      "  height:100vh;"
      "}"

      ".container{"
      "  width:90%;"
      "  max-width:400px;"
      "  text-align:center;"
      "}"

      "h1{"
      "  color:#2f6b4f;"
      "  margin-bottom:10px;"
      "}"

      "h2{"
      "  font-weight:normal;"
      "  margin:10px 0 25px;"
      "}"

      "p{"
      "  font-size:14px;"
      "  color:#555;"
      "}"

      "input{"
      "  width:100%;"
      "  padding:12px;"
      "  margin:10px 0;"
      "  border-radius:25px;"
      "  border:1.5px solid #2f6b4f;"
      "  font-size:14px;"
      "  outline:none;"
      "}"

      "button{"
      "  width:100%;"
      "  padding:12px;"
      "  margin-top:20px;"
      "  background:#2f6b4f;"
      "  color:white;"
      "  border:none;"
      "  border-radius:25px;"
      "  font-size:16px;"
      "  cursor:pointer;"
      "}"

      "button:hover{"
      "  background:#25543f;"
      "}"

      "</style>"
      "</head>"

      "<body>"
      "<div class='container'>"

      "<h1>BIN MATRIX</h1>"
      "<h2>Create bin profile</h2>"

      "<p>Device UID:<br><b>" + uid + "</b></p>"

      "<form method='POST' action='/save'>"
      "<input name='bin' maxlength='" + String(BIN_NAME_MAX - 1) + "' "
      "value='" + current + "' placeholder='Bin Name' required>"
      "<button type='submit'>Save</button>"
      "</form>"

      "<p style='margin-top:20px;'>After saving, the device will reboot.</p>"

      "</div>"
      "</body>"
      "</html>";

  server.send(200, "text/html", page);
}

void handleSave() {
  String bin = server.arg("bin");
  bin.trim();
  if (bin.length() == 0) {
    server.send(400, "text/plain", "Bin name empty");
    return;
  }

  saveBinName(bin);
  String page = "<!doctype html><html><head><meta charset='utf-8'><title>Saved</title></head><body>"
                "<h3>Saved bin name: " + bin + "</h3>"
                "<p>Rebooting device...</p>"
                "</body></html>";
  server.send(200, "text/html", page);

  // small delay to let client receive page, then restart
  delay(1000);
  ESP.restart();
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// Start/stop AP and server
void startConfigAP() {
  if (configActive) return;

  Serial.println("Starting Config AP...");
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS, 1, false, 4); // channel 1, hidden=false, maxconn=4
  if (!ok) {
    Serial.println("softAP failed");
    return;
  }

  IPAddress ip = WiFi.softAPIP();
  Serial.printf("AP IP: %s\n", ip.toString().c_str());

  dnsServer.start(DNS_PORT, "*", ip);

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();

  configActive = true;
  configStartMillis = millis();
}

void stopConfigAP() {
  if (!configActive) return;

  Serial.println("Stopping Config AP...");
  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  // Switch back to station mode; do not auto connect here (your existing WiFi logic should run)
  WiFi.mode(WIFI_STA);
  configActive = false;
}

bool isConfigActive() {
  return configActive;
}

// Call from setup()

void provisioning_init() {
  pinMode(CONFIG_BTN_PIN, INPUT_PULLUP); // button active LOW
  lastBtnState = digitalRead(CONFIG_BTN_PIN);
  // ensure EEPROM exists
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.end();
  Serial.printf("Provisioning initialized. Current bin name: %s\n", loadBinName().c_str());
}

// Call from loop()

void provisioning_loop() {
  // handle AP server if active
  if (configActive) {
    dnsServer.processNextRequest(); // Captive portal
    server.handleClient();
    // auto-close after timeout
    if (millis() - configStartMillis >= AP_AUTO_CLOSE_MS) {
      Serial.println("Config AP timeout reached, closing");
      stopConfigAP();
    }
  }

  // poll config button (non-blocking)
  bool cur = digitalRead(CONFIG_BTN_PIN);
  if (cur != lastBtnState) {
    unsigned long now = millis();
    if (now - lastBtnChange > BTN_DEBOUNCE_MS) {
      lastBtnChange = now;
      lastBtnState = cur;
      // falling edge => button pressed (active LOW)
      if (cur == LOW) {
        Serial.println("Config button pressed -> starting provisioning AP");
        startConfigAP();
      }
    }
  }
}

// Optional helper: call this anywhere to programmatically open provisioning AP
void provisioning_startNow() {
  startConfigAP();
}

// Optional helper: get saved bin name
String provisioning_getBinName() {
  return loadBinName();
}
