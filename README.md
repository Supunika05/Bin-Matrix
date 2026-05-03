# Bin Matrix – Smart IoT Waste Management System

Bin Matrix is an **ESP32-based smart waste management system** that automates waste segregation, monitors bin fill levels in real time, and sends data to the cloud for remote monitoring.

## Features

* **Three-bin waste segregation system**

  * Plastic bin
  * Paper bin
  * Organic bin

* **Servo-controlled lids**

  * Opens the correct bin lid based on received waste type command
  * Handles multiple requests using a queue system
  * Prevents lid collisions with non-blocking state control

* **Ultrasonic fill-level monitoring**

  * Measures waste level inside each bin
  * Calculates fill percentage
  * Detects changes in bin capacity usage

* **AWS IoT integration**

  * Publishes real-time bin data to AWS IoT Core using MQTT
  * Sends:

    * Bin name
    * Bin type
    * Fill level
    * Fill percentage
    * Timestamp
    * Active status

* **Dynamic MQTT topics**

  * Each bin uses its own topic based on its unique bin name
  * Supports deployment of multiple bins in different locations

* **Wi-Fi provisioning system**

  * ESP32 creates an access point when the boot button is pressed
  * Users can connect to the device and assign/change a unique bin name through a web page

* **EEPROM storage**

  * Stores bin name permanently
  * Retains configuration after restart

* **Time synchronization**

  * Uses NTP to generate readable timestamps for cloud records

## How it works

1. User powers on the ESP32 smart bin
2. Device connects to Wi-Fi and AWS IoT Core
3. Waste classification system sends waste type command
4. Correct lid opens automatically
5. Ultrasonic sensors monitor bin fill levels
6. Changes in fill percentage trigger MQTT updates
7. Data is stored and monitored remotely through AWS services

## Technologies Used

* ESP32
* Arduino C++
* AWS IoT Core
* MQTT
* EEPROM
* Ultrasonic Sensors
* Servo Motors
* NTP Time Sync

## Use Case

Designed for smart cities, public waste bins, campuses, offices, and automated recycling systems to improve waste collection efficiency and encourage proper waste segregation.
