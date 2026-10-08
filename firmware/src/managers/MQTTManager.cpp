#include "MQTTManager.h"

MQTTManager *MQTTManager::_instance = nullptr;

MQTTManager::MQTTManager(SIM800Client *client)
{
    _client = client;
    _mqtt = new PubSubClient(*_client);
    _connected = false;
    _lastReconnect = 0;
    _retryCount = 0;
    _messageCallback = nullptr;
    _instance = this;
}

MQTTManager::~MQTTManager()
{
    if (_mqtt)
        delete _mqtt;
}

void MQTTManager::begin()
{
    _mqtt->setServer(MQTT_SERVER, MQTT_PORT);
    _mqtt->setCallback(mqttCallback);
    // ⚠️ از 1024 به 1536 - پیام‌های location/backlog (تا 10 نقطه در هر batch)
    // می‌تونن به راحتی از 1024 بایت رد بشن
    _mqtt->setBufferSize(1536);
    _mqtt->setKeepAlive(60);

    MQTT_LOGLN(F("[MQTT] Configured"));
}

void MQTTManager::mqttCallback(char *topic, byte *payload, unsigned int length)
{
    if (!_instance || !_instance->_messageCallback)
        return;

    MQTT_LOG(F("[MQTT] ← "));
    MQTT_LOG(topic);
    MQTT_LOG(F(" ("));
    MQTT_LOG(length);
    MQTT_LOGLN(F(" bytes)"));

    // ⚠️ هم‌راستا با setBufferSize بالا
    StaticJsonDocument<1536> doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error)
    {
        MQTT_LOG(F("[MQTT] JSON parse error: "));
        MQTT_LOGLN(error.c_str());
        return;
    }

    _instance->_messageCallback(topic, doc);
}

void MQTTManager::loop()
{
    _client->loop();

    if (_mqtt->connected())
    {
        _mqtt->loop();
        _retryCount = 0;
        _connected = true;
    }
    else
    {
        if (_connected)
        {
            _connected = false;
            MQTT_LOGLN(F("[MQTT] Connection lost"));
        }

        unsigned long now = millis();

        unsigned long backoff = MQTT_RECONNECT_DELAY;
        for (uint8_t i = 0; i < _retryCount && i < 5; i++)
        {
            backoff *= 2;
        }
        if (backoff > 120000UL)
        {
            backoff = 120000UL;
        }

        if (now - _lastReconnect > backoff)
        {
            _lastReconnect = now;
            connect();
        }
    }
}

bool MQTTManager::connect()
{
    MQTT_LOG(F("[MQTT] Connecting... "));

    bool ok;
    if (strlen(MQTT_USER) > 0)
    {
        ok = _mqtt->connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
    }
    else
    {
        ok = _mqtt->connect(MQTT_CLIENT_ID);
    }

    if (ok)
    {
        MQTT_LOGLN(F("OK"));
        _connected = true;
        _retryCount = 0;

        subscribe(TOPIC_RELAY_CMD);
        subscribe(TOPIC_AUTH_REQ);
        subscribe(TOPIC_AUTH_VERIFY);
        subscribe(TOPIC_GEOFENCE_ALERT);
        subscribe(TOPIC_GEOFENCE_CONFIG);
        subscribe(TOPIC_SYNC_ACK); // ⚠️ جدید - جواب سرور به sync/hello
        subscribe(TOPIC_OTA_CMD);  // ⚠️ OTA update commands

        MQTT_LOGLN(F("[MQTT] ✓ Subscribed"));

        return true;
    }

    MQTT_LOG(F("FAILED, rc="));
    MQTT_LOGLN(_mqtt->state());

    _retryCount++;
    return false;
}

bool MQTTManager::isConnected()
{
    return _mqtt->connected();
}

bool MQTTManager::publish(const char *topic, const char *payload, bool retained)
{
    if (!_mqtt->connected())
    {
        MQTT_LOGLN(F("[MQTT] Not connected"));
        return false;
    }

    bool ok = _mqtt->publish(topic, payload, retained);

    if (ok)
    {
        MQTT_LOG(F("[MQTT] → "));
        MQTT_LOG(topic);
        MQTT_LOG(F(": "));
        MQTT_LOGLN(payload);
    }
    else
    {
        MQTT_LOGLN(F("[MQTT] Publish failed"));
    }

    return ok;
}

bool MQTTManager::publish(const char *topic, JsonDocument &doc, bool retained)
{
    // ⚠️ از 512 به 1536 - قبلاً پیام‌های بزرگ (مثل location/backlog) خاموش truncate می‌شدن
    char buffer[1536];
    serializeJson(doc, buffer);
    return publish(topic, buffer, retained);
}

bool MQTTManager::subscribe(const char *topic)
{
    bool ok = _mqtt->subscribe(topic);

    if (ok)
    {
        MQTT_LOG(F("[MQTT] ✓ Subscribed to: "));
        MQTT_LOGLN(topic);
    }

    return ok;
}

void MQTTManager::setMessageCallback(MQTTMessageCallback callback)
{
    _messageCallback = callback;
}

void MQTTManager::printStatus()
{
    Serial.println(F("\n╔═══════════════════════════════════╗"));
    Serial.println(F("║        MQTT STATUS                ║"));
    Serial.println(F("╠═══════════════════════════════════╣"));

    Serial.print(F("║ Server:     "));
    Serial.print(MQTT_SERVER);
    Serial.print(F(":"));
    Serial.println(MQTT_PORT);

    Serial.print(F("║ Connected:  "));
    Serial.println(isConnected() ? "✓ YES     " : "✗ NO      ");

    Serial.print(F("║ Client ID:  "));
    Serial.println(MQTT_CLIENT_ID);

    Serial.println(F("╚═══════════════════════════════════╝\n"));
}