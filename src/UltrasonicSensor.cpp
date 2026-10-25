#include "UltrasonicSensor.h"

UltrasonicSensor::UltrasonicSensor(uint8_t triggerPin,
                                   uint8_t echoPin,
                                   uint32_t echoTimeoutUs,
                                   float soundCmPerUs,
                                   float minDistanceCm,
                                   float maxDistanceCm)
    : m_triggerPin(triggerPin),
      m_echoPin(echoPin),
      m_echoTimeoutUs(echoTimeoutUs),
      m_soundCmPerUs(soundCmPerUs),
      m_minDistanceCm(minDistanceCm),
      m_maxDistanceCm(maxDistanceCm)
{
}

void UltrasonicSensor::begin()
{
    pinMode(m_triggerPin, OUTPUT);
    digitalWrite(m_triggerPin, LOW);
    pinMode(m_echoPin, INPUT);
}

bool UltrasonicSensor::measure(float& distanceCm)
{
    // Send a 10 us pulse on TRIG to start a measurement.
    digitalWrite(m_triggerPin, LOW);
    delayMicroseconds(2);
    digitalWrite(m_triggerPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(m_triggerPin, LOW);

    // Measure how long ECHO stays HIGH. A result of 0 means timeout.
    const unsigned long duration = pulseIn(m_echoPin, HIGH, m_echoTimeoutUs);

    // The sound travels to the object and back, so divide by two.
    const float distance = duration * m_soundCmPerUs / 2.0f;

    if (duration == 0 || distance < m_minDistanceCm || distance > m_maxDistanceCm)
    {
        return false;
    }

    distanceCm = distance;
    return true;
}
