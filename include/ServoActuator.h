#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>

/**
 * @brief Wrapper around an ESP32Servo instance.
 *
 * The class owns the servo, remembers its current angle and only accepts
 * the angles allowed by the original firmware (0, 45, 90, 135 and 180).
 * It does not know about MQTT or the global configuration: every parameter
 * is received through the constructor.
 */
class ServoActuator
{
public:
    /**
     * @brief Creates a servo actuator.
     *
     * @param pin          GPIO connected to the servo signal wire.
     * @param frequencyHz  PWM frequency in hertz (50 Hz for standard servos).
     * @param minPulseUs   Pulse width in microseconds for the minimum angle.
     * @param maxPulseUs   Pulse width in microseconds for the maximum angle.
     * @param initialAngle Angle in degrees applied when begin() is called.
     */
    ServoActuator(uint8_t pin,
                  uint16_t frequencyHz,
                  uint16_t minPulseUs,
                  uint16_t maxPulseUs,
                  int initialAngle);

    /**
     * @brief Attaches the servo to its pin and moves it to the initial angle.
     *
     * Must be called once, for example from setup().
     */
    void begin();

    /**
     * @brief Moves the servo to the requested angle.
     *
     * Only the angles 0, 45, 90, 135 and 180 are accepted. Any other value
     * is ignored and the servo does not move.
     *
     * @param angle Target angle in degrees.
     * @return true if the angle was accepted and applied, false otherwise.
     */
    bool setAngle(int angle);

    /**
     * @brief Returns the last angle applied to the servo.
     *
     * @return Current angle in degrees.
     */
    int getAngle() const;

private:
    /**
     * @brief Checks whether an angle is one of the allowed values.
     *
     * @param angle Angle in degrees.
     * @return true if the angle is allowed, false otherwise.
     */
    static bool isAllowedAngle(int angle);

    Servo m_servo;
    const uint8_t m_pin;
    const uint16_t m_frequencyHz;
    const uint16_t m_minPulseUs;
    const uint16_t m_maxPulseUs;
    int m_angle;
};
