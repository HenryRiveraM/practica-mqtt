#include "MqttManager.h"

#include <cstring>

MqttManager::MqttManager(const MqttSettings& settings)
    : m_settings(settings),
      m_network(),
      m_client(m_network),
      m_clientId(),
      m_messageCallback(nullptr),
      m_lastAttemptMs(0),
      m_attempted(false),
      m_wifiWasConnected(false)
{
}

void MqttManager::begin()
{
    // The client id is the prefix followed by the MAC without colons.
    m_clientId = m_settings.clientPrefix;
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    m_clientId += mac;

    m_client.setServer(m_settings.host, m_settings.port);
    m_client.setCallback([this](char* topic, uint8_t* payload, unsigned int length)
    {
        handleMessage(topic, payload, length);
    });
    m_client.setKeepAlive(m_settings.keepAliveSeconds);
    m_client.setSocketTimeout(m_settings.socketTimeoutSeconds);
}

void MqttManager::setMessageCallback(MessageCallback callback)
{
    m_messageCallback = callback;
}

void MqttManager::update(bool wifiConnected)
{
    if (!wifiConnected)
    {
        // Wi-Fi was lost: close the socket and allow an immediate retry later.
        if (m_wifiWasConnected)
        {
            m_network.stop();
            m_wifiWasConnected = false;
            m_attempted = false;
        }
        return;
    }

    // Wi-Fi just came back: allow an immediate connection attempt.
    if (!m_wifiWasConnected)
    {
        m_wifiWasConnected = true;
        m_attempted = false;
    }

    if (!m_client.connected())
    {
        const uint32_t now = millis();
        if (!m_attempted || now - m_lastAttemptMs >= m_settings.retryMs)
        {
            connect();
        }
    }

    if (m_client.connected())
    {
        m_client.loop();
    }
}

bool MqttManager::isConnected()
{
    return m_client.connected();
}

bool MqttManager::publishDistance(float distanceCm)
{
    if (!m_client.connected())
    {
        Serial.println("MQTT desconectado; medición descartada.");
        return false;
    }

    char message[24];
    snprintf(message, sizeof(message), "%.2f", distanceCm);

    if (!m_client.publish(m_settings.distanceTopic, message, m_settings.retainDistance))
    {
        Serial.println("No se pudo publicar la medición.");
        return false;
    }
    return true;
}

void MqttManager::connect()
{
    m_attempted = true;
    Serial.println("Conectando a MQTT...");

    const bool hasUser = m_settings.user != nullptr && m_settings.user[0] != '\0';
    const bool connected = hasUser
        ? m_client.connect(m_clientId.c_str(), m_settings.user, m_settings.password)
        : m_client.connect(m_clientId.c_str());

    if (connected)
    {
        // Subscribe after every (re)connection, because the broker forgets it.
        if (m_client.subscribe(m_settings.commandTopic, m_settings.subscribeQos))
        {
            Serial.println("MQTT conectado y suscripción enviada.");
        }
        else
        {
            Serial.println("No se pudo enviar la suscripción; se reintentará la conexión.");
            m_client.disconnect();
        }
    }
    else
    {
        Serial.printf("Error MQTT: %d\n", m_client.state());
    }

    m_lastAttemptMs = millis();
}

void MqttManager::handleMessage(char* topic, uint8_t* payload, unsigned int length)
{
    if (!m_messageCallback)
    {
        return;
    }

    // Ignore any topic other than the command topic.
    if (strcmp(topic, m_settings.commandTopic) != 0)
    {
        return;
    }

    m_messageCallback(topic, payload, length);
}
