#include "MqttManager.h"
#include <ArduinoJson.h>

MqttManager* MqttManager::_instance = nullptr;

void MqttManager::_staticCb(char* topic, byte* payload, unsigned int len) {
    if (!_instance) return;
    String t(topic);
    String p((char*)payload, len);

    // ── 新增：OTA 指令主题单独处理 ──────────────────────────────
    // 期望收到的 payload 格式：
    // { "url": "http://192.168.1.100:8080/firmware.bin", "version": "1.0.1" }
    // version 字段可选，仅用于日志打印
    if (_instance->_otaCb && t == _instance->topicOtaUpdate()) {
        JsonDocument doc;
        if (deserializeJson(doc, p) == DeserializationError::Ok) {
            String url     = doc["url"]     | "";
            String version = doc["version"] | "";
            if (!url.isEmpty()) {
                Serial.printf("[MQTT] OTA trigger received, url=%s ver=%s\n",
                              url.c_str(), version.c_str());
                _instance->_otaCb(url, version);
                return;  // OTA 和普通消息互斥，直接返回
            }
        }
        Serial.println("[MQTT] OTA payload invalid, ignored");
        return;
    }
    // ────────────────────────────────────────────────────────────

    // 普通业务消息走原来的回调
    if (_instance->_msgCb) _instance->_msgCb(t, p);
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
    String clientId  = "esp32-" + _deviceId;
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
        // ── 新增：同时订阅 OTA 指令主题 ──
        _client.subscribe(topicOtaUpdate().c_str());
        Serial.printf("[MQTT] Subscribed: %s\n", topicOtaUpdate().c_str());
        // ─────────────────────────────────
        String payload = "{\"online\":true}";
        publish(topicEvent(), payload);
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