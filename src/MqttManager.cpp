#include "MqttManager.h"
#include <ArduinoJson.h>

MqttManager* MqttManager::_instance = nullptr;

void MqttManager::_staticCb(char* topic, byte* payload, unsigned int len) {
    if (!_instance) return;
    String t(topic);
    String p((char*)payload, len);

    if (t == _instance->topicCommand()) {
        JsonDocument doc;
        if (deserializeJson(doc, p) != DeserializationError::Ok) return;

        String method = doc["method"] | "";

        if (method == "otaUpgrade") {
            bool   upgrade = doc["params"]["upgrade"] | false;
            String version = doc["params"]["version"] | "";
            String url     = doc["params"]["url"]     | "";

            if (!upgrade) {
                Serial.println("[OTA] Already up to date (ThingsCloud)");
                return;
            }
            Serial.printf("[OTA] ThingsCloud push: ver=%s url=%s\n",
                          version.c_str(), url.c_str());
            if (_instance->_otaCb && !url.isEmpty()) {
                _instance->_otaCb(url, version);
            }
            return;
        }

        if (_instance->_msgCb) _instance->_msgCb(t, p);
        return;
    }

    if (_instance->_msgCb) _instance->_msgCb(t, p);
}

void MqttManager::begin(const String& host, uint16_t port,
                         const String& deviceId, const String& roomNo,
                         const String& token) {
    _host     = host;
    _port     = port;
    _deviceId = deviceId;
    _roomNo   = roomNo;
    _token    = token;   // 🔧 保存 Token
    _instance = this;

    _client.setClient(_wifi);
    _client.setServer(_host.c_str(), _port);
    _client.setCallback(_staticCb);
    _client.setKeepAlive(60);
    _client.setBufferSize(512);
}

bool MqttManager::_connect() {
    if (_token.isEmpty()) {
        // 🔧 Token 为空时拒绝连接，防止被服务器以 rc=5 踢掉污染重连计时
        Serial.println("[MQTT] Token is empty, skipping connect. Please configure token.");
        return false;
    }

    Serial.printf("[MQTT] Connecting to host=%s port=%d id=%s\n",
                  _host.c_str(), _port, _deviceId.c_str());

    String clientId   = "esp32-" + _deviceId;
    String lwtPayload = "{\"online\":false}";

    // 🔧 用户名固定为项目 Access Key，密码使用每设备独立的 Token
    bool ok = _client.connect(
        clientId.c_str(),
        "9a7lf0qlp3p9odgq",   // ThingsCloud 项目 Access Key（所有设备共用）
        _token.c_str(),         // 🔧 每设备独立的 Device Token
        topicState().c_str(), 0, true,
        lwtPayload.c_str()
    );

    if (ok) {
        Serial.println("[MQTT] Connected");
        _client.subscribe(topicCmd().c_str());
        Serial.printf("[MQTT] Subscribed: %s\n", topicCmd().c_str());
        _client.subscribe(topicCommand().c_str());
        Serial.printf("[MQTT] Subscribed: %s\n", topicCommand().c_str());
        publish(topicState(), "{\"online\":true}");
    } else {
        int rc = _client.state();
        Serial.printf("[MQTT] Connect failed, rc=%d", rc);
        // 打印常见错误原因，方便现场排查
        switch (rc) {
            case -4: Serial.println(" (TIMEOUT)");           break;
            case -3: Serial.println(" (LOST)");              break;
            case -2: Serial.println(" (FAILED)");            break;
            case  1: Serial.println(" (BAD_PROTOCOL)");      break;
            case  2: Serial.println(" (BAD_CLIENT_ID)");     break;
            case  3: Serial.println(" (UNAVAILABLE)");       break;
            case  4: Serial.println(" (BAD_CREDENTIALS) <- Token 错误或未在 ThingsCloud 注册此设备"); break;
            case  5: Serial.println(" (UNAUTHORIZED)");      break;
            default: Serial.println("");                      break;
        }
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