#include "ServoActuator.h"

namespace
{
// Valid angle range, in degrees.
constexpr int MIN_ANGLE = 0;
constexpr int MAX_ANGLE = 180;

// "180" is the longest valid text.
constexpr unsigned int MAX_DIGITS = 3;
}

ServoActuator::ServoActuator(uint8_t pin,
                             uint16_t frequencyHz,
                             uint16_t minPulseUs,
                             uint16_t maxPulseUs,
                             int initialAngle)
    : m_pin(pin),
      m_frequencyHz(frequencyHz),
      m_minPulseUs(minPulseUs),
      m_maxPulseUs(maxPulseUs),
      m_angle(initialAngle)
{
}

void ServoActuator::begin()
{
    m_servo.setPeriodHertz(m_frequencyHz);
    m_servo.attach(m_pin, m_minPulseUs, m_maxPulseUs);
    m_servo.write(m_angle);
}

bool ServoActuator::setAngle(int angle)
{
    if (angle < MIN_ANGLE || angle > MAX_ANGLE)
    {
        return false;
    }

    m_servo.write(angle);
    m_angle = angle;
    return true;
}

int ServoActuator::getAngle() const
{
    return m_angle;
}

bool ServoActuator::parseAngle(const uint8_t* text, unsigned int length, int& angle)
{
    // Reject empty or too long text before reading any byte.
    if (text == nullptr || length == 0 || length > MAX_DIGITS)
    {
        return false;
    }

    int value = 0;
    for (unsigned int i = 0; i < length; ++i)
    {
        // Only digits are accepted, so signs and letters are rejected here.
        if (text[i] < '0' || text[i] > '9')
        {
            return false;
        }
        value = value * 10 + (text[i] - '0');
    }

    if (value < MIN_ANGLE || value > MAX_ANGLE)
    {
        return false;
    }

    angle = value;
    return true;
}