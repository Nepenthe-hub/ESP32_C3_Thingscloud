#include "MqttManager.h"

MqttManager* MqttManager::_instance = nullptr;
/*
    静态回调函数，用于处理接收到的MQTT消息
*/
void MqttManager::_staticCb(char* topic, byte* payload, unsigned int len) {
    if (!_instance || !_instance->_msgCb) return;
    String t(topic);
    String p((char*)payload, len);
    _instance->_msgCb(t, p);
}
/*
    begin: 初始化MQTT客户端，设置服务器地址、端口、设备ID、房间号等信息，并注册回调函数
*/
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
/*
    _connect: 尝试连接MQTT服务器，连接成功后订阅命令主题，并发布在线状态消息
*/
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
        String payload = "{\"online\":true}";
        publish(topicEvent(), payload);
        Serial.printf("[MQTT] Published: %s\n", payload.c_str());
    } else {
        Serial.printf("[MQTT] Connect failed, rc=%d\n", _client.state());
    }
    return ok;
}
/*
    loop: MQTT客户端的主循环函数，负责保持连接和处理消息
    如果当前没有连接，就会定期尝试重新连接
*/
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
/*
    publish: 发布MQTT消息到指定主题
    如果当前没有连接，就返回false
*/
bool MqttManager::publish(const String& topic, const String& payload) {
    if (!_client.connected()) return false;
    return _client.publish(topic.c_str(), payload.c_str(), false);
}