#pragma once
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <functional>

using MsgCallback = std::function<void(const String&, const String&)>;
// OTA 专用回调：url（固件地址）、version（目标版本，可为空）
using OtaCallback = std::function<void(const String& url, const String& version)>;

class MqttManager {
public:
    void begin(const String& host, uint16_t port,
               const String& deviceId, const String& roomNo);
    void loop();
    bool publish(const String& topic, const String& payload);

    void onMessage(MsgCallback cb) { _msgCb = cb; }
    void onOta(OtaCallback cb)     { _otaCb = cb; }  // 新增：注册OTA触发回调

    // 主题生成
    String topicCmd()   { return "attribute/" + _deviceId + "/set"; }
    String topicEvent() { return "attribute/" + _deviceId + "/event"; }
    String topicState() { return "attribute/" + _deviceId + "/state"; }
    // 新增：OTA 相关主题
    // 接收升级指令：ota/{deviceId}/update
    String topicOtaUpdate() { return "ota/" + _deviceId + "/update"; }
    // 上报升级状态：ota/{deviceId}/status
    String topicOtaStatus() { return "ota/" + _deviceId + "/status"; }

private:
    static MqttManager* _instance;
    static void _staticCb(char* topic, byte* payload, unsigned int len);

    bool _connect();

    WiFiClient   _wifi;
    PubSubClient _client;
    String       _host, _deviceId, _roomNo;
    uint16_t     _port = 1883;
    unsigned long _lastReconnect = 0;
    static constexpr unsigned long RECONNECT_INTERVAL = 5000;

    MsgCallback _msgCb;
    OtaCallback _otaCb;  // 新增
};