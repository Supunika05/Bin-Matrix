#pragma once // Prevent duplicate declarations
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include <WString.h> 
#include "secrets.h"
#include "AWS_connect.h"

extern const char* TOPIC_MOTOR_CONTROL;
extern const char* TOPIC_DISTANCE;
extern String rx;
extern WiFiClientSecure net;
extern MQTTClient client;

String getReadableTime();
void handleLine(const String& line);
void messageHandler(String &topic, String &payload);
void publishMessage(const char* bin_fill, int bin_perc, const char* bin_no);
void connectAWS();