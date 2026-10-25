#include "ServoActuator.h"

namespace
{
// Angles accepted by the original firmware.
constexpr int ALLOWED_ANGLES[] = {0, 45, 90, 135, 180};
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
    if (!isAllowedAngle(angle))
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

bool ServoActuator::isAllowedAngle(int angle)
{
    for (const int allowed : ALLOWED_ANGLES)
    {
        if (angle == allowed)
        {
            return true;
        }
    }
    return false;
}
