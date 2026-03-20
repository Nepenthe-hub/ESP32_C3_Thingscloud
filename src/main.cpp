#include <Arduino.h>
#include <ArduinoJson.h>
#include "ConfigManager.h"
#include "WifiManager.h"
#include "MqttManager.h"
#include "SerialProtocol.h"
#include "OtaManager.h"  // 新增

ConfigManager  cfgMgr;
WifiManager    wifiMgr;
MqttManager    mqttMgr;
SerialProtocol stmSerial;
OtaManager     otaMgr;   // 新增

// ── OTA 服务器地址，改成你电脑的局域网 IP ──────────────────────
// 运行 python -m http.server 8080 的那台电脑的 IP
// Windows: ipconfig  /  Mac/Linux: ifconfig
static const char* OTA_VERSION_URL = "http://192.168.111.123:8080/version.json";
void sendToSTM32(int speed, int dir, const String& op) {
    char buf[128];
    snprintf(buf, sizeof(buf), "$CMD,%d,%d,%s\n", speed, dir, op.c_str());
    Serial1.print(buf);
    Serial.printf("[STM32] -> %s", buf);
}

void setup() {
    delay(1000);
    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && millis() - t < 2000) delay(10);
    Serial.printf("[Boot] Starting... Firmware v%s  ← 这是新版本!\n", OtaManager::currentVersion());

    Serial1.begin(115200, SERIAL_8N1, 6, 7);
    Serial.println("[Boot] Serial1 (STM32) ready");

    cfgMgr.begin();
    stmSerial.begin(115200);

    // ── OTA 回调注册 ────────────────────────────────────────────
    // 状态变化时：通过 MQTT 上报给云端，方便远程监控
    otaMgr.onState([](OtaState state, const String& info) {
        String stateStr;
        switch (state) {
            case OtaState::CHECKING:    stateStr = "checking";    break;
            case OtaState::DOWNLOADING: stateStr = "downloading"; break;
            case OtaState::APPLYING:    stateStr = "applying";    break;
            case OtaState::SUCCESS:     stateStr = "success";     break;
            case OtaState::FAILED:      stateStr = "failed";      break;
            case OtaState::UP_TO_DATE:  stateStr = "up_to_date";  break;
            default:                    stateStr = "idle";         break;
        }
        Serial.printf("[OTA] State: %s  info: %s\n",
                      stateStr.c_str(), info.c_str());

        // 上报到 MQTT（连接时才发，OtaState::APPLYING 之后 MQTT 可能来不及发）
        JsonDocument doc;
        doc["state"]   = stateStr;
        doc["version"] = OtaManager::currentVersion();
        if (!info.isEmpty()) doc["info"] = info;
        String payload;
        serializeJson(doc, payload);
        mqttMgr.publish(mqttMgr.topicOtaStatus(), payload);
    });

    // 下载进度：每次更新打印到串口（不频繁发 MQTT，避免阻塞）
    otaMgr.onProgress([](int current, int total) {
        // Update 库内部已经有打印，这里留空或做自己的进度条
        (void)current; (void)total;
    });
    // ────────────────────────────────────────────────────────────

    stmSerial.onCfg([](const CfgMsg& msg) {
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

            // ── 新增：WiFi 连上后自动检查一次版本 ──────────────
            // 放在这里是因为 OTA 必须在 WiFi 就绪后才能发起 HTTP 请求
            Serial.println("[OTA] Auto-checking version on boot...");
            otaMgr.checkAndUpdate(OTA_VERSION_URL);
            // ────────────────────────────────────────────────────
        }
    });

    mqttMgr.onMessage([](const String& topic, const String& payload) {
        Serial.printf("[MQTT] <- %s : %s\n", topic.c_str(), payload.c_str());

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Serial.printf("[MQTT] JSON parse error: %s\n", err.c_str());
            return;
        }

        if (doc.containsKey("wifi_ssid") && doc.containsKey("wifi_pwd")) {
            String ssid = doc["wifi_ssid"].as<String>();
            String pwd  = doc["wifi_pwd"].as<String>();
            Serial.printf("[CFG] 收到新WiFi配置 ssid=%s\n", ssid.c_str());
            cfgMgr.save(ssid, pwd, cfgMgr.load().roomNo);
            wifiMgr.begin(ssid, pwd);
            return;
        }

        int speed = doc.containsKey("speed")    ? (int)doc["speed"]            : -1;
        int dir   = doc.containsKey("dir")      ? (int)doc["dir"]              : -1;
        String op = doc.containsKey("operator") ? doc["operator"].as<String>() : "";
        if (speed != -1 || dir != -1 || op.length() > 0) {
            sendToSTM32(speed, dir, op);
        }
    });

    // ── 新增：MQTT OTA 触发回调 ──────────────────────────────────
    mqttMgr.onOta([](const String& url, const String& version) {
        Serial.printf("[OTA] MQTT trigger: url=%s ver=%s\n",
                      url.c_str(), version.c_str());
        otaMgr.performUpdate(url);
    });
    // ────────────────────────────────────────────────────────────

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