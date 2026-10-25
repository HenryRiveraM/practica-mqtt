#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <functional>

/**
 * @brief Parameters needed by MqttManager.
 *
 * The text pointers are not copied, so the strings must stay valid for the
 * whole life of the MqttManager object (for example constants).
 */
struct MqttSettings
{
    const char* host;               ///< Broker host name.
    uint16_t port;                  ///< Broker TCP port.
    const char* user;               ///< User name; empty for anonymous access.
    const char* password;           ///< Password; ignored when user is empty.
    const char* clientPrefix;       ///< Prefix of the client id; the MAC is appended.
    const char* commandTopic;       ///< Topic the device subscribes to for commands.
    const char* distanceTopic;      ///< Topic where distances are published.
    const char* servoStateTopic;    ///< Topic where the servo angle is published.
    const char* statusTopic;        ///< Topic for the online/offline status.
    const char* onlinePayload;      ///< Message published after connecting.
    const char* offlinePayload;     ///< Last Will message published by the broker.
    uint8_t subscribeQos;           ///< QoS used to subscribe (0 or 1).
    uint8_t willQos;                ///< QoS of the Last Will (0 or 1).
    bool retainDistance;            ///< Retain flag for distance messages.
    bool retainServoState;          ///< Retain flag for servo state messages.
    bool retainStatus;              ///< Retain flag for the status and the Last Will.
    uint16_t keepAliveSeconds;      ///< MQTT keep-alive interval in seconds.
    uint16_t socketTimeoutSeconds;  ///< Socket timeout in seconds.
    uint32_t retryMs;               ///< Minimum time between connection attempts.
};

/**
 * @brief Manages the MQTT connection, subscription and publications.
 *
 * The class keeps the broker connection alive without blocking, registers a
 * Last Will, publishes the online status, subscribes again after every
 * reconnection and hands incoming commands to a callback. It does not know
 * about the servo, the sensor or the global configuration.
 *
 * Note: PubSubClient 2.8 publishes messages with QoS 0 only. The QoS values
 * in MqttSettings apply to the subscription and to the Last Will.
 */
class MqttManager
{
public:
    /**
     * @brief Signature of the function called when a command arrives.
     *
     * The payload is not null-terminated, so the length must be used. The
     * payload is only valid during the call: use it before publishing.
     */
    using MessageCallback = std::function<void(const char* topic,
                                               const uint8_t* payload,
                                               unsigned int length)>;

    /**
     * @brief Signature of the function called after each successful connection.
     */
    using ConnectedCallback = std::function<void()>;

    /**
     * @brief Creates an MQTT manager.
     *
     * @param settings Connection, topic and timing parameters.
     */
    explicit MqttManager(const MqttSettings& settings);

    /** The object holds a network client, so it cannot be copied. */
    MqttManager(const MqttManager&) = delete;
    MqttManager& operator=(const MqttManager&) = delete;

    /**
     * @brief Configures the MQTT client.
     *
     * Builds the client id from the prefix and the MAC address, so it must
     * be called after the Wi-Fi mode has been set (after WifiManager::begin()).
     */
    void begin();

    /**
     * @brief Sets the function that receives the commands.
     *
     * Only messages from the command topic are delivered.
     *
     * @param callback Function called for every command received.
     */
    void setMessageCallback(MessageCallback callback);

    /**
     * @brief Sets the function called after connecting, subscribing and
     *        publishing the online status.
     *
     * Useful to publish values that must be sent again after every
     * reconnection.
     *
     * @param callback Function called after each successful connection.
     */
    void setConnectedCallback(ConnectedCallback callback);

    /**
     * @brief Maintains the connection and processes incoming messages.
     *
     * Must be called on every iteration of loop(). It never blocks for long.
     *
     * @param wifiConnected Current Wi-Fi state, usually WifiManager::isConnected().
     */
    void update(bool wifiConnected);

    /**
     * @brief Tells whether the client is connected to the broker.
     *
     * @return true if connected, false otherwise.
     */
    bool isConnected();

    /**
     * @brief Publishes a distance with two decimals on the distance topic.
     *
     * @param distanceCm Distance in centimeters.
     * @return true if the message was sent, false if the client is
     *         disconnected or the publication failed.
     */
    bool publishDistance(float distanceCm);

    /**
     * @brief Publishes the servo angle as text on the servo state topic.
     *
     * @param angle Current servo angle in degrees.
     * @return true if the message was sent, false if the client is
     *         disconnected or the publication failed.
     */
    bool publishServoState(int angle);

private:
    /** Tries to connect, subscribe and publish the online status. */
    void connect();

    /** Receives every message from PubSubClient and filters by topic. */
    void handleMessage(char* topic, uint8_t* payload, unsigned int length);

    const MqttSettings m_settings;
    WiFiClient m_network;
    PubSubClient m_client;
    String m_clientId;
    MessageCallback m_messageCallback;
    ConnectedCallback m_connectedCallback;
    uint32_t m_lastAttemptMs;
    bool m_attempted;
    bool m_wifiWasConnected;
};