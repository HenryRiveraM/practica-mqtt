#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <cstring>
#include "config.h"

WiFiClient network;
PubSubClient mqtt(network);
Servo servo;
String clientId;
uint32_t lastWifiAttempt = 0;
uint32_t lastMqttAttempt = 0;
uint32_t lastMeasurement = 0;
bool wifiWasConnected = false;
bool mqttAttempted = false;

void receiveMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, Config::TOPIC_SERVO) != 0) return;
  // El payload MQTT no termina necesariamente en un carácter nulo.
  const char* commands[] = {"0", "45", "90", "135", "180"};
  const uint8_t angles[] = {0, 45, 90, 135, 180};
  for (size_t i = 0; i < 5; ++i) {
    if (length == strlen(commands[i]) &&
        memcmp(payload, commands[i], length) == 0) {
      servo.write(angles[i]);
      Serial.printf("Servo: %u grados\n", angles[i]);
      return;
    }
  }
  Serial.println("Comando ignorado: usa 0, 45, 90, 135 o 180.");
}

void maintainConnections() {
  const uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected) {
      Serial.println("Wi-Fi desconectado.");
      network.stop();
      wifiWasConnected = false;
      mqttAttempted = false;
    }
    if (now - lastWifiAttempt >= Config::WIFI_RETRY_MS) {
      lastWifiAttempt = now;
      Serial.println("Reintentando Wi-Fi...");
      WiFi.disconnect();
      WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);
    }
    return;
  }
  if (!wifiWasConnected) {
    wifiWasConnected = true;
    mqttAttempted = false;
    Serial.print("Wi-Fi conectado. IP: ");
    Serial.println(WiFi.localIP());
  }
  if (mqtt.connected()) return;
  if (mqttAttempted && now - lastMqttAttempt < Config::MQTT_RETRY_MS) return;
  mqttAttempted = true;
  Serial.println("Conectando a MQTT...");
  const bool connected = Config::MQTT_USER[0] != '\0'
      ? mqtt.connect(clientId.c_str(), Config::MQTT_USER, Config::MQTT_PASSWORD)
      : mqtt.connect(clientId.c_str());
  if (connected) {
    if (mqtt.subscribe(Config::TOPIC_SERVO, Config::MQTT_SUBSCRIBE_QOS)) {
      Serial.println("MQTT conectado y suscripción enviada.");
    } else {
      Serial.println("No se pudo enviar la suscripción; se reintentará la conexión.");
      mqtt.disconnect();
    }
  } else {
    Serial.printf("Error MQTT: %d\n", mqtt.state());
  }
  lastMqttAttempt = millis();
}

void measureAndPublish() {
  const uint32_t now = millis();
  if (now - lastMeasurement < Config::PUBLISH_INTERVAL_MS) return;
  lastMeasurement = now;
  digitalWrite(Config::TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(Config::TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(Config::TRIGGER_PIN, LOW);
  const unsigned long duration = pulseIn(Config::ECHO_PIN, HIGH, Config::ECHO_TIMEOUT_US);
  const float distance = duration * Config::SOUND_CM_PER_US / 2.0f;
  if (duration == 0 || distance < Config::DISTANCE_MIN_CM ||
      distance > Config::DISTANCE_MAX_CM) {
    Serial.println("Sin medición válida; no se publica.");
    return;
  }
  char message[24];
  snprintf(message, sizeof(message), "%.2f", distance);
  Serial.printf("Distancia: %s cm\n", message);
  if (mqtt.connected()) {
    if (!mqtt.publish(Config::TOPIC_DISTANCE, message, Config::MQTT_RETAIN_DISTANCE)) {
      Serial.println("No se pudo publicar la medición.");
    }
  } else {
    Serial.println("MQTT desconectado; medición descartada.");
  }
}

void setup() {
  Serial.begin(Config::SERIAL_BAUD);
  pinMode(Config::TRIGGER_PIN, OUTPUT);
  digitalWrite(Config::TRIGGER_PIN, LOW);
  pinMode(Config::ECHO_PIN, INPUT);
  servo.setPeriodHertz(Config::SERVO_FREQUENCY_HZ);
  servo.attach(Config::SERVO_PIN, Config::SERVO_MIN_US, Config::SERVO_MAX_US);
  servo.write(Config::SERVO_INITIAL_ANGLE);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  clientId = Config::MQTT_CLIENT_PREFIX;
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  clientId += mac;
  mqtt.setServer(Config::MQTT_HOST, Config::MQTT_PORT);
  mqtt.setCallback(receiveMessage);
  mqtt.setKeepAlive(Config::MQTT_KEEPALIVE_SECONDS);
  mqtt.setSocketTimeout(Config::MQTT_SOCKET_TIMEOUT_SECONDS);
  WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);
  lastWifiAttempt = millis();
  lastMeasurement = millis();
}

void loop() {
  maintainConnections();
  if (mqtt.connected()) mqtt.loop();
  measureAndPublish();
  delay(1);
}
