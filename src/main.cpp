#include <Arduino.h>
#include "ConfigManager.h"
#include "WifiManager.h"
#include "MqttManager.h"
#include "SerialProtocol.h"

ConfigManager  cfgMgr;
WifiManager    wifiMgr;
MqttManager    mqttMgr;
SerialProtocol serial;

void setup() {
    delay(1000);
    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && millis() - t < 2000) delay(10);
    Serial.println("[Boot] Starting...");

    cfgMgr.begin();
    serial.begin(115200);

    serial.onCfg([](const CfgMsg& msg) {
        Serial.printf("[Cfg] ssid=%s room=%s\n", msg.ssid.c_str(), msg.room.c_str());
        cfgMgr.save(msg.ssid, msg.pwd, msg.room);
        wifiMgr.begin(msg.ssid, msg.pwd);
    });

    wifiMgr.onStateChange([](WifiState s) {
        if (s == WifiState::CONNECTED) {
            Serial.println("[WiFi] Connected, starting MQTT...");
            DeviceCfg cfg = cfgMgr.load();
            mqttMgr.begin("sh-1-mqtt.iot-api.com", 1883,
                          cfg.deviceId, cfg.roomNo);
        }
    });

    mqttMgr.onMessage([](const String& topic, const String& payload) {
        Serial.printf("[MQTT] <- %s : %s\n", topic.c_str(), payload.c_str());
    });

    // 上电读取已保存配置，有就自动连，没有就等串口下发
    DeviceCfg saved = cfgMgr.load();
    if (saved.valid) {
        Serial.printf("[Boot] 已有配置 ssid=%s，自动连接\n", saved.ssid.c_str());
        wifiMgr.begin(saved.ssid, saved.password);
    } else {
        Serial.println("[Boot] 无配置，等待串口下发 cfg 消息");
    }
}

void loop() {
    serial.loop();
    wifiMgr.loop();
    mqttMgr.loop();
}