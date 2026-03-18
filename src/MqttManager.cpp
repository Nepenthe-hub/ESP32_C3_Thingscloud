#include "MqttManager.h"

MqttManager* MqttManager::_instance = nullptr;

void MqttManager::_staticCb(char* topic, byte* payload, unsigned int len) {
    if (!_instance || !_instance->_msgCb) return;
    String t(topic);
    String p((char*)payload, len);
    _instance->_msgCb(t, p);
}

void MqttManager::begin(const String& host, uint16_t port,
                         const String& deviceId, const String& roomNo) {
    _host     = host;
    _port     = port;
    _deviceId = deviceId;
    _roomNo   = roomNo;
    _instance = this;

    _client.setClient(_wifi);
    _client.setServer(_host.c_str(), _port);
    _client.setCallback(_staticCb);
    _client.setKeepAlive(60);
    _client.setBufferSize(512);
}

bool MqttManager::_connect() {
    Serial.printf("[MQTT] Connecting to host=%s port=%d\n", _host.c_str(), _port);
    String clientId = "esp32-" + _deviceId;
    String lwtPayload = "{\"online\":false}";

    bool ok = _client.connect(
        clientId.c_str(),
        "9a7lf0qlp3p9odgq",
        "rg8EHtRQ3R",
        topicState().c_str(), 0, true,
        lwtPayload.c_str()
    );

    if (ok) {
        Serial.println("[MQTT] Connected");
        _client.subscribe(topicCmd().c_str());

        // ThingsCloud 格式上报
        String payload = "{\"online\":true,\"room\":\"" + _roomNo + "\"}";
        publish(topicEvent(), payload);
        Serial.printf("[MQTT] Published: %s\n", payload.c_str());
    } else {
        Serial.printf("[MQTT] Connect failed, rc=%d\n", _client.state());
    }
    return ok;
}

void MqttManager::loop() {
    if (_host.isEmpty()) return;
    if (_client.connected()) {
        _client.loop();
        return;
    }
    if (millis() - _lastReconnect > RECONNECT_INTERVAL) {
        _lastReconnect = millis();
        Serial.println("[MQTT] Reconnecting...");
        _connect();
    }
}

bool MqttManager::publish(const String& topic, const String& payload) {
    if (!_client.connected()) return false;
    return _client.publish(topic.c_str(), payload.c_str(), false);
}