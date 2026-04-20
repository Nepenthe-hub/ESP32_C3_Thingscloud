#pragma once
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <functional>

using MsgCallback = std::function<void(const String&, const String&)>;
using OtaCallback = std::function<void(const String& url, const String& version)>;

class MqttManager {
public:
    // 🔧 新增 token 参数，对应 ThingsCloud 每设备独立的 Device Token
    void begin(const String& host, uint16_t port,
               const String& deviceId, const String& roomNo,
               const String& token);
    void loop();
    bool publish(const String& topic, const String& payload);

    void onMessage(MsgCallback cb) { _msgCb = cb; }
    void onOta(OtaCallback cb)     { _otaCb = cb; }

    bool isConnected()  { return _client.connected(); }
    void disconnect()   { _client.disconnect(); }

    // ─── ThingsCloud 标准主题 ───
    String topicAttrReport()  { return "attributes"; }
    String topicCmd()         { return "attributes/push"; }
    String topicEvent()       { return "events"; }
    String topicCommand()     { return "commands/send"; }
    String topicCmdReply(int id) {
        return "commands/reply/" + String(id);
    }
    // 🔧 修正：attribute -> attributes（保持与其他主题一致）
    String topicState()       { return "attributes/" + _deviceId + "/state"; }
    String topicOtaStatus()   { return "ota/" + _deviceId + "/status"; }

private:
    static MqttManager* _instance;
    static void _staticCb(char* topic, byte* payload, unsigned int len);

    bool _connect();

    WiFiClient   _wifi;
    PubSubClient _client;
    String       _host, _deviceId, _roomNo, _token;
    uint16_t     _port = 1883;
    unsigned long _lastReconnect = 0;
    static constexpr unsigned long RECONNECT_INTERVAL = 5000;

    MsgCallback _msgCb;
    OtaCallback _otaCb;
};