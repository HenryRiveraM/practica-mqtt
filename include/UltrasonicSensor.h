#pragma once

#include <Arduino.h>

/**
 * @brief Driver for an HC-SR04 ultrasonic distance sensor.
 *
 * The class only talks to the sensor pins. It does not know about MQTT,
 * the servo or the global configuration: every parameter is received
 * through the constructor.
 */
class UltrasonicSensor
{
public:
    /**
     * @brief Creates a sensor driver.
     *
     * @param triggerPin   GPIO connected to the TRIG pin of the sensor.
     * @param echoPin      GPIO connected to the ECHO pin of the sensor.
     * @param echoTimeoutUs Maximum time in microseconds to wait for the echo.
     * @param soundCmPerUs Speed of sound in centimeters per microsecond.
     * @param minDistanceCm Smallest distance considered valid, in centimeters.
     * @param maxDistanceCm Largest distance considered valid, in centimeters.
     */
    UltrasonicSensor(uint8_t triggerPin,
                     uint8_t echoPin,
                     uint32_t echoTimeoutUs,
                     float soundCmPerUs,
                     float minDistanceCm,
                     float maxDistanceCm);

    /**
     * @brief Configures the TRIG and ECHO pins.
     *
     * Must be called once, for example from setup().
     */
    void begin();

    /**
     * @brief Performs one distance measurement.
     *
     * @param[out] distanceCm Receives the distance in centimeters only when
     *                        the measurement is valid; otherwise it is left
     *                        unchanged.
     * @return true if the echo arrived in time and the distance is inside
     *         the valid range, false otherwise.
     */
    bool measure(float& distanceCm);

private:
    const uint8_t m_triggerPin;
    const uint8_t m_echoPin;
    const uint32_t m_echoTimeoutUs;
    const float m_soundCmPerUs;
    const float m_minDistanceCm;
    const float m_maxDistanceCm;
};
