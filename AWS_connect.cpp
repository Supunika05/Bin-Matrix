#include <sys/_intsup.h>
#include "WString.h"
#include "HardwareSerial.h"
#include "esp32-hal-gpio.h"
#include "secrets.h"
#include "esp_camera.h"
#include "AWS_connect.h"
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>

const char* TOPIC_MOTOR_CONTROL = "web/servo/control";
const char* TOPIC_DISTANCE = "esp32/ultrasonic/distance";
extern String rx;
extern String bin_name;

WiFiClientSecure net;
MQTTClient client;

// ------------- Create readable timestamp ------------------
String getReadableTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "TIME_NOT_SET";
  }

  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void messageHandler(String &topic, String &payload) {
  // Parse JSON payload for a "command" field and call handleLine() with it
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  if (doc["command"]) {
    String msg = doc["command"].as<String>();
    msg.trim();
    Serial.print("Command (from MQTT): ");
    Serial.println(msg);

    // call the command parser directly with the payload command
    handleLine(msg);
  }
}

const char* getBinType(const char* bin_no) {
  if (strcmp(bin_no, "BIN_01") == 0) return "plastic";
  if (strcmp(bin_no, "BIN_02") == 0) return "paper";
  if (strcmp(bin_no, "BIN_03") == 0) return "organic";
  return "unknown";
}

void publishMessage(const char* bin_fill, int bin_perc, const char* bin_no) {
  JsonDocument doc2;
  
  const char* bin_type = getBinType(bin_no);

  doc2["timestamp"] = getReadableTime(); // Local uptime
  doc2["bin_name"] = bin_name;
  doc2["bin_type"] = bin_type;
  doc2["fill_level"] = bin_fill;
  doc2["fill_percentage"] = bin_perc;
  doc2["status"] = "active";

  // Serialize JSON to a buffer
  char jsonBuffer[512];
  serializeJson(doc2, jsonBuffer);

  // Publish to AWS IoT Core
  if (client.publish(TOPIC_DISTANCE, jsonBuffer)) {
    Serial.println("AWS Publish: Success");
  } else {
    Serial.println("AWS Publish: Failed!");
  }
}


void connectAWS() {
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  client.begin(AWS_IOT_ENDPOINT, 8883, net);
  client.setCleanSession(true);
  Serial.print("Connecting to AWS IoT");
  while (!client.connect(THINGNAME)) { Serial.print("."); delay(200); }
  Serial.println("\nAWS IoT OK");

  client.onMessage(messageHandler);
  client.subscribe(TOPIC_MOTOR_CONTROL);
}