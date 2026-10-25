#include "WifiManager.h"

WifiManager::WifiManager(const char* ssid, const char* password, uint32_t retryMs)
    : m_ssid(ssid),
      m_password(password),
      m_retryMs(retryMs),
      m_lastAttemptMs(0),
      m_wasConnected(false)
{
}

void WifiManager::begin()
{
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(m_ssid, m_password);
    m_lastAttemptMs = millis();
}

void WifiManager::update()
{
    const uint32_t now = millis();

    if (WiFi.status() != WL_CONNECTED)
    {
        // Report the loss only once, when the state changes.
        if (m_wasConnected)
        {
            Serial.println("Wi-Fi desconectado.");
            m_wasConnected = false;
        }

        // Retry only after the waiting time has passed.
        if (now - m_lastAttemptMs >= m_retryMs)
        {
            m_lastAttemptMs = now;
            Serial.println("Reintentando Wi-Fi...");
            WiFi.disconnect();
            WiFi.begin(m_ssid, m_password);
        }
        return;
    }

    // Report the connection only once, when the state changes.
    if (!m_wasConnected)
    {
        m_wasConnected = true;
        Serial.print("Wi-Fi conectado. IP: ");
        Serial.println(WiFi.localIP());
    }
}

bool WifiManager::isConnected() const
{
    return WiFi.status() == WL_CONNECTED;
}
