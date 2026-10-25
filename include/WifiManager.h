#pragma once

#include <Arduino.h>
#include <WiFi.h>

/**
 * @brief Keeps the ESP32 connected to a Wi-Fi network.
 *
 * The class starts the connection and retries it without blocking, using
 * millis() instead of delay(). It does not know about MQTT or the global
 * configuration: every parameter is received through the constructor.
 */
class WifiManager
{
public:
    /**
     * @brief Creates a Wi-Fi manager.
     *
     * The ssid and password pointers are not copied, so the strings must
     * stay valid for the whole life of the object (for example constants).
     *
     * @param ssid     Name of the Wi-Fi network.
     * @param password Password of the Wi-Fi network.
     * @param retryMs  Minimum time in milliseconds between reconnection attempts.
     */
    WifiManager(const char* ssid, const char* password, uint32_t retryMs);

    /**
     * @brief Configures the Wi-Fi radio and starts the first connection.
     *
     * Must be called once, for example from setup().
     */
    void begin();

    /**
     * @brief Checks the connection and retries it when it is lost.
     *
     * Must be called on every iteration of loop(). It never blocks.
     */
    void update();

    /**
     * @brief Tells whether the ESP32 is connected to the network.
     *
     * @return true if connected, false otherwise.
     */
    bool isConnected() const;

private:
    const char* m_ssid;
    const char* m_password;
    const uint32_t m_retryMs;
    uint32_t m_lastAttemptMs;
    bool m_wasConnected;
};
