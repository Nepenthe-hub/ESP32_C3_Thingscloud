#include "MqttManager.h"

MqttManager* MqttManager::_instance = nullptr;

void MqttManager::_staticCb(char* topic, byte* payload, unsigned int len) {
    if (!_instance) return;
    String t(topic);
    String p;
    p.reserve(len);
    for (unsigned int i = 0; i < len; i++) p += (char)payload[i];
    if (_instance->_msgCb) _instance->_msgCb(t, p);
}

void MqttManager::begin(const String& host, uint16_t port,
                        const String& deviceId, const String& roomNo,
                        const String& username, const String& password) {
    _host     = host;
    _port     = port;
    _deviceId = deviceId;
    _roomNo   = roomNo;
    _username = username;
    _password = password;
    _instance = this;

    _client.setClient(_wifi);
    _client.setServer(_host.c_str(), _port);
    _client.setCallback(_staticCb);
    _client.setKeepAlive(60);
    _client.setSocketTimeout(10);

    _connect();
}

bool MqttManager::_connect() {
    if (_host.isEmpty()) return false;

    Serial.printf("[MQTT] Connecting to %s:%d  id=%s\n",
                  _host.c_str(), _port, _deviceId.c_str());

    bool ok = _client.connect(
        _deviceId.c_str(),
        _username.c_str(),
        _password.c_str()
    );

    if (ok) {
        Serial.println("[MQTT] Connected");
        _client.subscribe(topicCmd().c_str(),     1);
        _client.subscribe(topicCommand().c_str(), 1);
        Serial.printf("[MQTT] Subscribed: %s, %s\n",
                      topicCmd().c_str(), topicCommand().c_str());
    } else {
        Serial.printf("[MQTT] Connect failed, rc=%d\n", _client.state());
    }
    return ok;
}

void MqttManager::loop() {
    if (_host.isEmpty()) return;

    if (!_client.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnect > RECONNECT_INTERVAL) {
            _lastReconnect = now;
            Serial.println("[MQTT] Reconnecting...");
            _connect();
        }
    } else {
        _client.loop();
    }
}

bool MqttManager::publish(const String& topic, const String& payload) {
    if (!_client.connected()) return false;
    bool ok = _client.publish(topic.c_str(), payload.c_str(), false);
    Serial.printf("[MQTT] -> %s : %s  [%s]\n",
                  topic.c_str(), payload.c_str(), ok ? "OK" : "FAIL");
    return ok;
}