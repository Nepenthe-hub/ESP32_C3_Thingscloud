#include <Arduino.h>
#include <ArduinoJson.h>
#include "ConfigManager.h"
#include "WifiManager.h"
#include "MqttManager.h"
#include "SerialProtocol.h"

ConfigManager  cfgMgr;
WifiManager    wifiMgr;
MqttManager    mqttMgr;
SerialProtocol stmSerial;

// 通过 GPIO6/GPIO7 发送命令给 STM32
void sendToSTM32(int speed, int dir, const String& op) {
    char buf[128];
    snprintf(buf, sizeof(buf), "$CMD,%d,%d,%s\n", speed, dir, op.c_str());
    Serial1.print(buf);
    Serial.printf("[STM32] -> %s", buf);  // 调试打印
}

void setup() {
    delay(1000);
    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && millis() - t < 2000) delay(10);
    Serial.println("[Boot] Starting...");

    // GPIO6/GPIO7 串口，用于和 STM32 通信
    Serial1.begin(115200, SERIAL_8N1, 6, 7);
    Serial.println("[Boot] Serial1 (STM32) ready");

    cfgMgr.begin();
    stmSerial.begin(115200);

    // 串口下发配置回调
    stmSerial.onCfg([](const CfgMsg& msg) {
        Serial.printf("[Cfg] ssid=%s room=%s\n", msg.ssid.c_str(), msg.room.c_str());
        cfgMgr.save(msg.ssid, msg.pwd, msg.room);
        wifiMgr.begin(msg.ssid, msg.pwd);
    });

    // WiFi 连上后启动 MQTT
    wifiMgr.onStateChange([](WifiState s) {
        if (s == WifiState::CONNECTED) {
            Serial.println("[WiFi] Connected, starting MQTT...");
            DeviceCfg cfg = cfgMgr.load();
            mqttMgr.begin("sh-1-mqtt.iot-api.com", 1883,
                          cfg.deviceId, cfg.roomNo);
        }
    });

    // 云端下行消息解析
    mqttMgr.onMessage([](const String& topic, const String& payload) {
        Serial.printf("[MQTT] <- %s : %s\n", topic.c_str(), payload.c_str());

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Serial.printf("[MQTT] JSON parse error: %s\n", err.c_str());
            return;
        }

        // WiFi 配置下发
        if (doc.containsKey("wifi_ssid") && doc.containsKey("wifi_pwd")) {
            String ssid = doc["wifi_ssid"].as<String>();
            String pwd  = doc["wifi_pwd"].as<String>();
            Serial.printf("[CFG] 收到新WiFi配置 ssid=%s\n", ssid.c_str());
            cfgMgr.save(ssid, pwd, cfgMgr.load().roomNo);
            wifiMgr.begin(ssid, pwd);
            return;  // WiFi配置和控制命令互斥，直接返回
        }

        // 收集控制属性，有任意一个就发给 STM32
        int speed = doc.containsKey("speed")    ? (int)doc["speed"]              : -1;
        int dir   = doc.containsKey("dir")      ? (int)doc["dir"]                : -1;
        String op = doc.containsKey("operator") ? doc["operator"].as<String>()   : "";

        if (speed != -1 || dir != -1 || op.length() > 0) {
            sendToSTM32(speed, dir, op);
        }
    });

    // 上电读取已保存配置
    DeviceCfg saved = cfgMgr.load();
    if (saved.valid) {
        Serial.printf("[Boot] 已有配置 ssid=%s，自动连接\n", saved.ssid.c_str());
        wifiMgr.begin(saved.ssid, saved.password);
    } else {
        Serial.println("[Boot] 无配置，等待串口下发 cfg 消息");
    }
}

void loop() {
    stmSerial.loop();
    wifiMgr.loop();
    mqttMgr.loop();
}
