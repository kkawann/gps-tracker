#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "SIM800Client.h"
#include "config.h"

typedef void (*MQTTMessageCallback)(const char *topic, JsonDocument &doc);

class MQTTManager
{
private:
    SIM800Client *_client;
    PubSubClient *_mqtt;

    bool _connected;
    unsigned long _lastReconnect;
    uint8_t _retryCount;

    MQTTMessageCallback _messageCallback;

    static void mqttCallback(char *topic, byte *payload, unsigned int length);
    static MQTTManager *_instance;

public:
    MQTTManager(SIM800Client *client);
    ~MQTTManager();

    void begin();
    void loop();

    bool connect();
    bool isConnected();

    bool publish(const char *topic, const char *payload, bool retained = false);
    bool publish(const char *topic, JsonDocument &doc, bool retained = false);

    bool subscribe(const char *topic);

    void setMessageCallback(MQTTMessageCallback callback);

    void printStatus();
};

#endif