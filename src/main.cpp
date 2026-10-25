#include <Arduino.h>
#include "config.h"
#include "UltrasonicSensor.h"
#include "ServoActuator.h"
#include "WifiManager.h"
#include "MqttManager.h"

namespace
{
/**
 * @brief Builds the MQTT parameters from Config.
 *
 * Only this file reads Config. Filling each field by name avoids mixing
 * the order of the values.
 */
MqttSettings makeMqttSettings()
{
    MqttSettings settings;
    settings.host = Config::MQTT_HOST;
    settings.port = Config::MQTT_PORT;
    settings.user = Config::MQTT_USER;
    settings.password = Config::MQTT_PASSWORD;
    settings.clientPrefix = Config::MQTT_CLIENT_PREFIX;
    settings.commandTopic = Config::TOPIC_SERVO_CMD;
    settings.distanceTopic = Config::TOPIC_DISTANCE;
    settings.servoStateTopic = Config::TOPIC_SERVO_STATE;
    settings.statusTopic = Config::TOPIC_STATUS;
    settings.onlinePayload = Config::STATUS_ONLINE;
    settings.offlinePayload = Config::STATUS_OFFLINE;
    settings.subscribeQos = Config::MQTT_SUBSCRIBE_QOS;
    settings.willQos = Config::MQTT_WILL_QOS;
    settings.retainDistance = Config::MQTT_RETAIN_DISTANCE;
    settings.retainServoState = Config::MQTT_RETAIN_SERVO_STATE;
    settings.retainStatus = Config::MQTT_RETAIN_STATUS;
    settings.keepAliveSeconds = Config::MQTT_KEEPALIVE_SECONDS;
    settings.socketTimeoutSeconds = Config::MQTT_SOCKET_TIMEOUT_SECONDS;
    settings.retryMs = Config::MQTT_RETRY_MS;
    return settings;
}

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

const MqttSettings mqttSettings = makeMqttSettings();

MqttManager mqtt(mqttSettings);

uint32_t lastMeasurementMs = 0;

/**
 * @brief Handles a servo command received from the broker.
 *
 * The payload is read first, because it is only valid until a new message
 * is published.
 */
void handleCommand(const char* /*topic*/, const uint8_t* payload, unsigned int length)
{
    int angle = 0;
    if (ServoActuator::parseAngle(payload, length, angle) && servo.setAngle(angle))
    {
        Serial.printf("Servo: %d grados\n", servo.getAngle());
        mqtt.publishServoState(servo.getAngle());
        return;
    }
    Serial.println("Comando ignorado: usa un entero de 0 a 180.");
}

/**
 * @brief Publishes the current servo angle after every MQTT connection.
 */
void handleConnected()
{
    mqtt.publishServoState(servo.getAngle());
}

/**
 * @brief Measures the distance and publishes it every DISTANCE_PUBLISH_INTERVAL_MS.
 */
void measureAndPublish()
{
    const uint32_t now = millis();
    if (now - lastMeasurementMs < Config::DISTANCE_PUBLISH_INTERVAL_MS)
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
    mqtt.setConnectedCallback(handleConnected);
    lastMeasurementMs = millis();
}

void loop()
{
    wifi.update();
    mqtt.update(wifi.isConnected());
    measureAndPublish();
    delay(1);
}