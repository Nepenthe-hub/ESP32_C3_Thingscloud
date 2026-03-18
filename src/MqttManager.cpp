#include "MqttManager.h"
#include <ArduinoJson.h>

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
    _client.setServer(host.c_str(), port);
    _client.setCallback(_staticCb);
    _client.setKeepAlive(60);
    _client.setBufferSize(512);
}

bool MqttManager::_connect() {
    String clientId = "esp32-" + _deviceId;
    // LWT：遗嘱消息，断线时自动发到 state 主题
    String lwtPayload = "{\"online\":false,\"device_id\":\"" + _deviceId + "\"}";

    bool ok = _client.connect(
        clientId.c_str(),
        nullptr, nullptr,        // 若 Broker 需要认证，在这里填 user/pwd
        topicState().c_str(), 0, true,
        lwtPayload.c_str()
    );
    if (ok) {
        Serial.println("[MQTT] Connected");
        _client.subscribe(topicCmd().c_str());

        // 上线后立刻发一条 event 上报设备信息
        JsonDocument doc;
        doc["type"]      = "event";
        doc["device_id"] = _deviceId;
        doc["room_no"]   = _roomNo;
        doc["event"]     = "device_online";
        doc["fw_ver"]    = "0.1.0";
        String out;
        serializeJson(doc, out);
        publish(topicEvent(), out);
    } else {
        Serial.printf("[MQTT] Connect failed, rc=%d\n", _client.state());
    }
    return ok;
}

void MqttManager::loop() {
    if (_client.connected()) {
        _client.loop();
        return;
    }
    // 断线重连，指数退避简化版（固定 5s 间隔）
    if (millis() - _lastReconnect > RECONNECT_INTERVAL) {
        _lastReconnect = millis();
        Serial.println("[MQTT] Reconnecting...");
        _connect();
    }
}

bool MqttManager::publish(const String& topic, const String& payload) {
    if (!_client.connected()) return false;
    return _client.publish(topic.c_str(), payload.c_str(), /*retain=*/false);
}