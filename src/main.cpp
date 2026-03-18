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
        DeviceCfg cfg = cfgMgr.load();
        wifiMgr.begin(cfg.ssid, cfg.password);
    });

    wifiMgr.onStateChange([](WifiState s) {
        if (s == WifiState::CONNECTED) {
            DeviceCfg cfg = cfgMgr.load();
            mqttMgr.begin(cfg.mqttHost, cfg.mqttPort, cfg.deviceId, cfg.roomNo);
        }
    });

    mqttMgr.onMessage([](const String& topic, const String& payload) {
        Serial.printf("[MQTT] <- %s : %s\n", topic.c_str(), payload.c_str());
    });

    // 直接填真实 WiFi 先测通网络
    wifiMgr.begin("GZJG", "88888888");
    DeviceCfg cfg = cfgMgr.load();
    // 把第43行改成这样
mqttMgr.begin("broker.emqx.io", 1883, cfg.deviceId, cfg.roomNo);
}

void loop() {
    serial.loop();
    wifiMgr.loop();
    mqttMgr.loop();
}
