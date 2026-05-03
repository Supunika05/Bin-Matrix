#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <EEPROM.h>
#include <ESP32Servo.h>
#include <time.h>
#include "secrets.h"
#include "WiFi.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "wifi_connect.h"
#include "AWS_connect.h"
#include "Bin_name_AP.h"

// ---------- GPIO ----------
constexpr uint8_t ULTRA1_TRIG = 32;
constexpr uint8_t ULTRA1_ECHO = 33;

constexpr uint8_t ULTRA2_TRIG = 25;
constexpr uint8_t ULTRA2_ECHO = 26;

constexpr uint8_t ULTRA3_TRIG = 27;
constexpr uint8_t ULTRA3_ECHO = 14;

constexpr uint8_t LID1 = 5;
constexpr uint8_t LID2 = 18;
constexpr uint8_t LID3 = 19;

// ---------- Constants ----------
constexpr float SOUND_SPEED = 0.343f;     // mm/us
constexpr float MAX_DISTANCE_MM = 200.0f; // mm
constexpr uint32_t SENSOR_PERIOD_MS = 5000;
constexpr uint32_t POST_CLOSE_DELAY_MS = 2000; // 2 seconds wait after a lid closes
constexpr uint32_t SERVO_STEP_MS = 20;         // step interval during open/close
constexpr int SERVO_OPEN_ANGLE = 90;
constexpr int SERVO_CLOSED_ANGLE = 0;
extern String bin_name;

// ---------- Servo ----------
Servo servo1;
Servo servo2;
Servo servo3;
Servo* SERVOS[] = { &servo1, &servo2, &servo3 };

// ---------- Bin status ----------
enum BinLevel : uint8_t {
  BL_ERROR  = 0,
  BL_LOW    = 1,
  BL_MEDIUM = 2,
  BL_HIGH   = 3
};

uint8_t last_bin_status[3] = { BL_ERROR, BL_ERROR, BL_ERROR };

// <<< NEW: track last published distance percentage per bin >>>
int last_distance_perc[3] = { -1, -1, -1 };

// ---------- Timing ----------
unsigned long lastPublishTime = 0;

// ---------- Lid state machine ----------
struct LidState {
  Servo* servo;
  uint8_t state;       // 0 idle, 1 opening, 2 waiting(open), 3 closing
  uint32_t stateStart; // millis of last state/time step
  int pos;             // current angle
};

LidState lids[3];

// single active lid index (-1 if none)
int activeLid = -1;

// post-close delay tracking
bool postCloseWaiting = false;
uint32_t postCloseStart = 0;

// a small FIFO queue for pending open requests
constexpr int QUEUE_SIZE = 6;
int pendingQueue[QUEUE_SIZE];
int qHead = 0;
int qTail = 0;

bool queueEmpty() { return qHead == qTail; }
bool queueFull()  { return ((qTail + 1) % QUEUE_SIZE) == qHead; }
bool enqueueRequestInternal(int bin_no) {
  if (queueFull()) return false;
  pendingQueue[qTail] = bin_no;
  qTail = (qTail + 1) % QUEUE_SIZE;
  return true;
}
int dequeueRequestInternal() {
  if (queueEmpty()) return -1;
  int v = pendingQueue[qHead];
  qHead = (qHead + 1) % QUEUE_SIZE;
  return v;
}
bool queueContains(int bin_no) {
  for (int i = qHead; i != qTail; i = (i + 1) % QUEUE_SIZE) {
    if (pendingQueue[i] == bin_no) return true;
  }
  return false;
}

// ---------- Helpers ----------
const char* levelToString(uint8_t level) {
  switch (level) {
    case BL_LOW:    return "LOW";
    case BL_MEDIUM: return "MEDIUM";
    case BL_HIGH:   return "HIGH";
    default:        return "ERROR";
  }
}

// ---------- Non-blocking lid control ----------
void initLids() {
  lids[0] = { &servo1, 0, 0, SERVO_CLOSED_ANGLE };
  lids[1] = { &servo2, 0, 0, SERVO_CLOSED_ANGLE };
  lids[2] = { &servo3, 0, 0, SERVO_CLOSED_ANGLE };
}

// Start the lid (index 0..2) immediately (assumes no other lid active)
void startLidImmediate(int index) {
  if (index < 0 || index > 2) return;
  if (activeLid != -1) return; // safety - only start if none active

  LidState &L = lids[index];
  L.state = 1; // opening
  L.stateStart = millis();
  L.pos = SERVO_CLOSED_ANGLE;
  L.servo->write(L.pos);
  activeLid = index;

  // cancel any post-close waiting when we immediately start a lid
  postCloseWaiting = false;
  postCloseStart = 0;

  Serial.printf("Starting lid %d immediately\n", index + 1);
}

// Request to open lid: either start immediately or enqueue (non-blocking)
void requestOpenLid(int bin_no) {
  if (bin_no < 1 || bin_no > 3) {
    Serial.printf("Invalid bin number request: %d\n", bin_no);
    return;
  }
  int idx = bin_no - 1;

  // If requested lid is already active -> ignore
  if (activeLid == idx) {
    Serial.printf("Bin %d already active, ignoring request\n", bin_no);
    return;
  }

  // If it's already in queue, ignore duplicate
  if (queueContains(bin_no)) {
    Serial.printf("Bin %d already queued, ignoring duplicate\n", bin_no);
    return;
  }

  // If no lid active and not in post-close delay, start immediately
  if (activeLid == -1 && !postCloseWaiting) {
    startLidImmediate(idx);
    return;
  }

  // Otherwise enqueue
  if (!enqueueRequestInternal(bin_no)) {
    Serial.printf("Queue full, cannot queue bin %d\n", bin_no);
  } else {
    Serial.printf("Queued bin %d\n", bin_no);
  }
}

// Should be called frequently in loop()
void updateLids() {
  uint32_t now = millis();

  // Update current active lid state machine (if any)
  if (activeLid != -1) {
    LidState &L = lids[activeLid];
    if (L.state == 1) { // opening
      if (now - L.stateStart >= SERVO_STEP_MS) {
        L.stateStart = now;
        L.pos++;
        if (L.pos >= SERVO_OPEN_ANGLE) {
          L.pos = SERVO_OPEN_ANGLE;
          L.state = 2; // waiting with lid open
          L.stateStart = now;
        }
        L.servo->write(L.pos);
      }
    } else if (L.state == 2) { // waiting open for 5000 ms
      if (now - L.stateStart >= 5000UL) {
        L.state = 3; // start closing
        L.stateStart = now;
      }
    } else if (L.state == 3) { // closing
      if (now - L.stateStart >= SERVO_STEP_MS) {
        L.stateStart = now;
        L.pos--;
        if (L.pos <= SERVO_CLOSED_ANGLE) {
          L.pos = SERVO_CLOSED_ANGLE;
          // Transition to idle
          L.state = 0;
          L.stateStart = now;

          // active lid just finished closing: mark no active and start post-close wait
          Serial.printf("Lid %d closed; starting post-close delay %lums\n", activeLid + 1, (unsigned long)POST_CLOSE_DELAY_MS);
          activeLid = -1;
          postCloseWaiting = true;
          postCloseStart = now;
        }
        L.servo->write(L.pos);
      }
    }
  }

  // If post-close waiting period expired, start next queued lid (if any)
  if (postCloseWaiting && (millis() - postCloseStart >= POST_CLOSE_DELAY_MS)) {
    postCloseWaiting = false;
    int nextBin = dequeueRequestInternal();
    if (nextBin != -1) {
      // start next one
      startLidImmediate(nextBin - 1);
    } else {
      // nothing queued
      Serial.println("Post-close delay expired but queue empty");
    }
  }
}

// Return true if it's safe to measure distance for the given bin index
bool shouldMeasureBin(int idx) {
  if (idx < 0 || idx >= 3) return false;

  // Block only while lid is opening or fully open
  return !(lids[idx].state == 1 || lids[idx].state == 2);
}


// ---------- Distance and publish ----------
void distanceToGarbage(uint8_t trig, uint8_t echo, const char* bin_no, int idx) {
  if (idx < 0 || idx >= 3) return;

  if (shouldMeasureBin(idx)) { 
  
    constexpr float LOW_TH  = MAX_DISTANCE_MM * 0.5f;
    constexpr float MED_TH  = MAX_DISTANCE_MM * 0.2f;

    digitalWrite(trig, LOW);
    delayMicroseconds(2);
    digitalWrite(trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(trig, LOW);

    unsigned long timeout_us =
      (unsigned long)((MAX_DISTANCE_MM * 2.0f / SOUND_SPEED) + 1000.0f);

    unsigned long duration = pulseIn(echo, HIGH, timeout_us);

    float distance_mm = (duration > 0)
                          ? ((duration * SOUND_SPEED) / 2.0f)
                          : -1.0f;

    uint8_t level = BL_ERROR;

    int distance_perc;
    if (distance_mm > 0) {
      distance_perc = round((MAX_DISTANCE_MM - distance_mm)/MAX_DISTANCE_MM * 100);
      // clamp to 0..100
      if (distance_perc < 0) distance_perc = 0;
      if (distance_perc > 100) distance_perc = 100;
    } else {
      distance_perc = -1; // indicate invalid
    }

    if (distance_mm > 0 && distance_mm <= MED_TH)        level = BL_HIGH;
    else if (distance_mm > MED_TH && distance_mm <= LOW_TH) level = BL_MEDIUM;
    else if (distance_mm > LOW_TH && distance_mm <= MAX_DISTANCE_MM) level = BL_LOW;
    else level = BL_ERROR;

    Serial.printf("DEBUG %s: Raw duration: %lu\n", bin_no, duration);
    
    // <<< CHANGED: publish when distance_perc changes (or first time) >>>
    if (distance_perc != last_distance_perc[idx]) {
      if (client.connected()) {
        publishMessage(levelToString(level), distance_perc, bin_no);
      } else {
        Serial.println("MQTT not connected — skipping publish");
      }
      last_distance_perc[idx] = distance_perc;
      last_bin_status[idx] = level;
      Serial.printf("%s -> %s (%.1f mm) perc=%d\n", bin_no, levelToString(level), distance_mm, distance_perc);
    }
  }
  else {
    Serial.printf("Skipping %s measurement (lid active)\n", bin_no);
  }
}

// ------------------ Initialize time -------------------------

void initTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10000)) {
    Serial.println("Failed to get time");
    return;
  }

  Serial.println("Time synchronized");
}

// ---------- Serial parsing ----------
String rx;

const int getBinNo(const char* bin_type) {
  if (strcmp(bin_type, "PLASTIC") == 0) return 1;
  if (strcmp(bin_type, "PAPER") == 0) return 2;
  if (strcmp(bin_type, "ORGANIC") == 0) return 3;
  return 0;
}

void handleLine(const String& line) {
  String s = line;
  s.trim();
  s.toUpperCase();
  if (s.length() == 0) return;

  int bin_no = getBinNo(s.c_str());
  Serial.printf("Serial request to open bin %d\n", bin_no);
  requestOpenLid(bin_no);
  return;
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  pinMode(ULTRA1_TRIG, OUTPUT);
  pinMode(ULTRA1_ECHO, INPUT);
  pinMode(ULTRA2_TRIG, OUTPUT);
  pinMode(ULTRA2_ECHO, INPUT);
  pinMode(ULTRA3_TRIG, OUTPUT);
  pinMode(ULTRA3_ECHO, INPUT);

  servo1.attach(LID1);
  servo2.attach(LID2);
  servo3.attach(LID3);

  // set initial positions explicitly
  servo1.write(SERVO_CLOSED_ANGLE);
  servo2.write(SERVO_CLOSED_ANGLE);
  servo3.write(SERVO_CLOSED_ANGLE);

  initLids();

  provisioning_init();
  bin_name = provisioning_getBinName();
  Serial.printf("Active bin name: %s\n", bin_name.c_str());

  connectWiFi();
  connectAWS();
  client.subscribe(TOPIC_MOTOR_CONTROL);

  initTime();

  Serial.println("Smart Bin ready!");
}

// ---------- Loop ----------
void loop() {
    if (!isConfigActive()) {
    client.loop();
  }

  updateLids();

  if (millis() - lastPublishTime >= SENSOR_PERIOD_MS) {
    distanceToGarbage(ULTRA1_TRIG, ULTRA1_ECHO, "BIN_01", 0);
    distanceToGarbage(ULTRA2_TRIG, ULTRA2_ECHO, "BIN_02", 1);
    distanceToGarbage(ULTRA3_TRIG, ULTRA3_ECHO, "BIN_03", 2);
    lastPublishTime = millis();
  }

  provisioning_loop();
  if (isConfigActive()) {
    return;   // Stop everything else
  }

  if (!isConfigActive()) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Wi-Fi drop, reconnecting...");
      WiFi.reconnect();
      delay(200);
      return;
    }

    if (!client.connected()) {
      Serial.println("MQTT drop, reconnecting...");
      connectAWS();
      client.onMessage(messageHandler);
      client.subscribe(TOPIC_MOTOR_CONTROL);
    }
  }
  else {
    // Optional debug: show that reconnects are suppressed during config
    // (remove/comment this line if you don't want verbose logs)
    Serial.println("Provisioning active — skipping WiFi/MQTT reconnect attempts");
  }

  // Serial input handling (non-blocking)
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (rx.length()) { handleLine(rx); rx = ""; }
    } else {
      rx += c;
      if (rx.length() > 128) rx = ""; // avoid unlimited growth - drop on overflow
    }
  }
}
