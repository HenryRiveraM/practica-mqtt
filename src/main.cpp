#include <Arduino.h>
#include <cstring>
#include "config.h"
#include "UltrasonicSensor.h"
#include "ServoActuator.h"
#include "WifiManager.h"
#include "MqttManager.h"

namespace
{
// Main objects. Only this file reads Config to build them.
UltrasonicSensor sensor(Config::TRIGGER_PIN,
                        Config::ECHO_PIN,
                        Config::ECHO_TIMEOUT_US,
                        Config::SOUND_CM_PER_US,
                        Config::DISTANCE_MIN_CM,
                        Config::DISTANCE_MAX_CM);

ServoActuator servo(Config::SERVO_PIN,
                    Config::SERVO_FREQUENCY_HZ,
                    Config::SERVO_MIN_US,
                    Config::SERVO_MAX_US,
                    Config::SERVO_INITIAL_ANGLE);

WifiManager wifi(Config::WIFI_SSID,
                 Config::WIFI_PASSWORD,
                 Config::WIFI_RETRY_MS);

const MqttSettings mqttSettings = {
    Config::MQTT_HOST,
    Config::MQTT_PORT,
    Config::MQTT_USER,
    Config::MQTT_PASSWORD,
    Config::MQTT_CLIENT_PREFIX,
    Config::TOPIC_SERVO,
    Config::TOPIC_DISTANCE,
    Config::MQTT_SUBSCRIBE_QOS,
    Config::MQTT_RETAIN_DISTANCE,
    Config::MQTT_KEEPALIVE_SECONDS,
    Config::MQTT_SOCKET_TIMEOUT_SECONDS,
    Config::MQTT_RETRY_MS};

MqttManager mqtt(mqttSettings);

uint32_t lastMeasurementMs = 0;

/**
 * @brief Converts an MQTT payload into one of the five accepted angles.
 *
 * The payload is not null-terminated, so it is compared using its length.
 *
 * @param payload Raw payload bytes.
 * @param length  Number of bytes in the payload.
 * @param angle   Receives the angle when the payload is valid.
 * @return true if the payload is "0", "45", "90", "135" or "180".
 */
bool parseAngleCommand(const uint8_t* payload, unsigned int length, int& angle)
{
    static const char* const commands[] = {"0", "45", "90", "135", "180"};
    static const int angles[] = {0, 45, 90, 135, 180};

    for (size_t i = 0; i < 5; ++i)
    {
        if (length == strlen(commands[i]) && memcmp(payload, commands[i], length) == 0)
        {
            angle = angles[i];
            return true;
        }
    }
    return false;
}

/**
 * @brief Handles a command received from the broker and moves the servo.
 */
void handleCommand(const char* /*topic*/, const uint8_t* payload, unsigned int length)
{
    int angle = 0;
    if (parseAngleCommand(payload, length, angle) && servo.setAngle(angle))
    {
        Serial.printf("Servo: %d grados\n", servo.getAngle());
        return;
    }
    Serial.println("Comando ignorado: usa 0, 45, 90, 135 o 180.");
}

/**
 * @brief Measures the distance and publishes it every PUBLISH_INTERVAL_MS.
 */
void measureAndPublish()
{
    const uint32_t now = millis();
    if (now - lastMeasurementMs < Config::PUBLISH_INTERVAL_MS)
    {
        return;
    }
    lastMeasurementMs = now;

    float distance = 0.0f;
    if (!sensor.measure(distance))
    {
        Serial.println("Sin medición válida; no se publica.");
        return;
    }

    Serial.printf("Distancia: %.2f cm\n", distance);
    mqtt.publishDistance(distance);
}
}

void setup()
{
    Serial.begin(Config::SERIAL_BAUD);
    sensor.begin();
    servo.begin();
    wifi.begin();
    mqtt.begin();
    mqtt.setMessageCallback(handleCommand);
    lastMeasurementMs = millis();
}

void loop()
{
    wifi.update();
    mqtt.update(wifi.isConnected());
    measureAndPublish();
    delay(1);
}