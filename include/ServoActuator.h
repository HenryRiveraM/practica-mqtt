#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>

/**
 * @brief Wrapper around an ESP32Servo instance.
 *
 * The class owns the servo, remembers its current angle and accepts any
 * integer angle from 0 to 180 degrees. It does not know about MQTT or the
 * global configuration: every parameter is received through the constructor.
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
     * Any integer from 0 to 180 is accepted. Other values are ignored and
     * the servo does not move.
     *
     * @param angle Target angle in degrees.
     * @return true if the angle was valid and applied, false otherwise.
     */
    bool setAngle(int angle);

    /**
     * @brief Returns the last angle applied to the servo.
     *
     * @return Current angle in degrees.
     */
    int getAngle() const;

    /**
     * @brief Converts text bytes into a valid servo angle.
     *
     * The text must contain only one to three digits (no sign, spaces or
     * other characters) and represent a value from 0 to 180. The bytes do
     * not need to be null-terminated. This function does not move the servo.
     *
     * @param text   Bytes of the text to convert.
     * @param length Number of bytes in the text.
     * @param[out] angle Receives the angle only when the text is valid.
     * @return true if the text is a valid angle, false otherwise.
     */
    static bool parseAngle(const uint8_t* text, unsigned int length, int& angle);

private:
    Servo m_servo;
    const uint8_t m_pin;
    const uint16_t m_frequencyHz;
    const uint16_t m_minPulseUs;
    const uint16_t m_maxPulseUs;
    int m_angle;
};