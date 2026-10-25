#pragma once
#include <Arduino.h>

namespace Config
{
// Wi-Fi credentials
constexpr char WIFI_SSID[] = "YOUR_WIFI";
constexpr char WIFI_PASSWORD[] = "YOUR_PASSWORD";

// MQTT broker
constexpr char MQTT_HOST[] = "broker.hivemq.com";
constexpr uint16_t MQTT_PORT = 1883;  // Plain TCP, no TLS.
constexpr char MQTT_USER[] = "";      // Empty: anonymous connection.
constexpr char MQTT_PASSWORD[] = "";
// The MAC address is appended to the prefix to identify each ESP32.
constexpr char MQTT_CLIENT_PREFIX[] = "practica-esp32-";

// Topics 
constexpr char TOPIC_DISTANCE[] = "practica/mqtt/iot/distancia";
constexpr char TOPIC_SERVO[] = "practica/mqtt/iot/servo";
constexpr bool MQTT_RETAIN_DISTANCE = false;
constexpr uint8_t MQTT_SUBSCRIBE_QOS = 1;
constexpr uint16_t MQTT_KEEPALIVE_SECONDS = 15;
constexpr uint16_t MQTT_SOCKET_TIMEOUT_SECONDS = 2;

// Timing
constexpr uint32_t WIFI_RETRY_MS = 10000;
constexpr uint32_t MQTT_RETRY_MS = 5000;
constexpr uint32_t PUBLISH_INTERVAL_MS = 5000;
constexpr uint32_t SERIAL_BAUD = 115200;

// Ultrasonic sensor (HC-SR04)
constexpr uint8_t TRIGGER_PIN = 5;
constexpr uint8_t ECHO_PIN = 18;
constexpr uint32_t ECHO_TIMEOUT_US = 30000;
constexpr float SOUND_CM_PER_US = 0.0343f;
constexpr float DISTANCE_MIN_CM = 2.0f;
constexpr float DISTANCE_MAX_CM = 400.0f;

// Servo
constexpr uint8_t SERVO_PIN = 19;
constexpr uint16_t SERVO_FREQUENCY_HZ = 50;
constexpr uint16_t SERVO_MIN_US = 500;
constexpr uint16_t SERVO_MAX_US = 2400;
constexpr uint8_t SERVO_INITIAL_ANGLE = 90;
}