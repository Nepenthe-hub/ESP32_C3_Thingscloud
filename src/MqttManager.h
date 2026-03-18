#pragma once
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <functional>
#include <Arduino.h>

class MqttManager {
public:
    using MsgCallback = std::function<void(const String& topic, const String& payload)>;

    void begin(const String& host, uint16_t port,
               const String& deviceId, const String& roomNo);
    void loop();
    bool publish(const String& topic, const String& payload);
    void onMessage(MsgCallback cb) { _msgCb = cb; }
    bool connected() { return _client.connected(); }

    String topicEvent() const { return "attributes"; }
    String topicState() const { return "attributes"; }
    String topicCmd()   const { return "attributes/push"; }

private:
    WiFiClient   _wifi;
    PubSubClient _client;
    String _deviceId, _roomNo, _host;
    uint16_t _port;
    MsgCallback _msgCb;
    uint32_t _lastReconnect = 0;
    static constexpr uint32_t RECONNECT_INTERVAL = 5000;

    bool _connect();
    static void _staticCb(char* topic, byte* payload, unsigned int len);
    static MqttManager* _instance;
};