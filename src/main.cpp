#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <nvs_flash.h>
#include "ConfigManager.h"
#include "WifiManager.h"
#include "MqttManager.h"
#include "SerialProtocol.h"
#include "OtaManager.h"

// =========================================================
// ⚠️ 新板子首次烧录必填
// =========================================================
static const char* DEFAULT_WIFI_SSID  = "YOUR_WIFI_NAME";
static const char* DEFAULT_WIFI_PWD   = "YOUR_WIFI_PASSWORD";
static const char* DEFAULT_ROOM       = "Room_1";
static const char* DEFAULT_MQTT_TOKEN = "YOUR_DEVICE_TOKEN";
// =========================================================

// ─── UDP 日志 ────────────────────────────────────────────
static WiFiUDP  _udp;
static bool     _udpReady    = false;
static uint16_t UDP_LOG_PORT = 12345;

void ulog(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.print(buf);
    if (_udpReady) {
        _udp.beginPacket("255.255.255.255", UDP_LOG_PORT);
        _udp.write((const uint8_t*)buf, strlen(buf));
        _udp.endPacket();
    }
}
// ─────────────────────────────────────────────────────────

uint8_t broadcastAddress[] = {0xDC, 0xDA, 0x0C, 0xD4, 0x93, 0x30};

typedef struct struct_message {
    int   id;
    float temp;
    float hum;
} struct_message;

struct_message      myData;
esp_now_peer_info_t peerInfo;

ConfigManager  cfgMgr;
WifiManager    wifiMgr;
MqttManager    mqttMgr;
SerialProtocol stmSerial;
OtaManager     otaMgr;

bool          _otaPendingCheck = false;
unsigned long _lastOtaCheck    = 0;
unsigned long _wifiConnectedAt = 0;
static constexpr unsigned long OTA_FIRST_DELAY_MS = 30000UL;

static String safeDeviceId(const DeviceCfg& cfg) {
    String id = cfg.deviceId;
    if (id.isEmpty() || id.length() < 2) {
        id = "C3_" + WiFi.macAddress();
        id.replace(":", "");
    }
    return id;
}

static String safeRoomNo(const DeviceCfg& cfg) {
    return cfg.roomNo.isEmpty() ? "Room_Default" : cfg.roomNo;
}

void sendToSTM32(int speed, int dir, const String& op) {
    char buf[128];
    snprintf(buf, sizeof(buf), "$CMD,%d,%d,%s\n", speed, dir, op.c_str());
    Serial1.print(buf);
    ulog("[Module] -> %s", buf);
}

void setup() {
    delay(3000);
    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && millis() - t < 2000) delay(10);
    ulog("[Boot] Starting... Firmware v%s\n", OtaManager::currentVersion());

    // ─── NVS 初始化 ───
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // ─── ESP-NOW ───
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        ulog("[ESP-NOW] Error initializing\n");
    } else {
        ulog("[ESP-NOW] Initialized\n");
        esp_now_register_send_cb([](const uint8_t *mac_addr, esp_now_send_status_t status) {
            ulog("[ESP-NOW] Send %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
        });
        esp_now_register_recv_cb([](const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
            if (len == sizeof(myData)) {
                memcpy(&myData, incomingData, sizeof(myData));
                ulog("[ESP-NOW] Recv ID=%d Temp=%.2f Hum=%.2f\n",
                    myData.id, myData.temp, myData.hum);
            }
        });
        memcpy(peerInfo.peer_addr, broadcastAddress, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }

    cfgMgr.begin();
    stmSerial.begin(115200);

    // ─── OTA 回调 ───
    otaMgr.onState([](OtaState state, const String& info) {
        String stateStr;
        switch (state) {
            case OtaState::CHECKING:    stateStr = "checking";    break;
            case OtaState::DOWNLOADING:
                stateStr = "downloading";
                mqttMgr.disconnect();
                break;
            case OtaState::APPLYING:    stateStr = "applying";    break;
            case OtaState::SUCCESS:     stateStr = "success";     break;
            case OtaState::FAILED:      stateStr = "failed";      break;
            case OtaState::UP_TO_DATE:  stateStr = "up_to_date";  break;
            default:                    stateStr = "idle";         break;
        }
        ulog("[OTA] State: %s  info: %s\n", stateStr.c_str(), info.c_str());
        if (mqttMgr.isConnected()) {
            JsonDocument doc;
            doc["state"]   = stateStr;
            doc["version"] = OtaManager::currentVersion();
            if (!info.isEmpty()) doc["info"] = info;
            String payload;
            serializeJson(doc, payload);
            mqttMgr.publish(mqttMgr.topicOtaStatus(), payload);
        }
    });

    otaMgr.onProgress([](int current, int total) { (void)current; (void)total; });

    // ─── 串口配置回调 ───
    stmSerial.onCfg([](const CfgMsg& msg) {
        DeviceCfg cur = cfgMgr.load();
        if (!msg.ssid.isEmpty()) {
            if (msg.ssid == cur.ssid && msg.pwd == cur.password) {
                ulog("[Cfg] WiFi unchanged, skip\n");
                return;
            }
            ulog("[Cfg] New WiFi: ssid=%s\n", msg.ssid.c_str());
            String room = msg.room.isEmpty() ? cur.roomNo : msg.room;
            cfgMgr.save(msg.ssid, msg.pwd, room, cur.mqttToken);
            wifiMgr.begin(msg.ssid, msg.pwd);
        } else if (!msg.room.isEmpty()) {
            if (msg.room == cur.roomNo) {
                ulog("[Cfg] Room unchanged, skip\n");
                return;
            }
            ulog("[Cfg] New Room: %s\n", msg.room.c_str());
            cfgMgr.save(cur.ssid, cur.password, msg.room, cur.mqttToken);
            if (mqttMgr.isConnected()) mqttMgr.disconnect();
            DeviceCfg updated = cfgMgr.load();
            mqttMgr.begin("175.24.153.243", 1883,
                          safeDeviceId(updated), safeRoomNo(updated),
                          updated.mqttToken);
        }
    });

    // ─── WiFi 状态回调 ───
    wifiMgr.onStateChange([](WifiState s) {
        if (s == WifiState::CONNECTED) {
            ulog("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

            // ✅ WiFi 连上后启动 UDP 日志广播
            _udp.begin(UDP_LOG_PORT);
            _udpReady = true;
            ulog("[UDP] Logger ready on port %d\n", UDP_LOG_PORT);

            DeviceCfg cfg = cfgMgr.load();
            ulog("[MQTT] deviceId = %s\n", safeDeviceId(cfg).c_str());
            ulog("[MQTT] roomNo   = %s\n", safeRoomNo(cfg).c_str());
            ulog("[MQTT] token    = %s\n",
                 cfg.mqttToken.isEmpty() ? "<<EMPTY! 请填 Token>>" : cfg.mqttToken.c_str());

            mqttMgr.begin("175.24.153.243", 1883,
                          safeDeviceId(cfg), safeRoomNo(cfg),
                          cfg.mqttToken);

            _wifiConnectedAt = millis();
            _otaPendingCheck = true;
            stmSerial.sendStatus(1, 0);

        } else if (s == WifiState::FAILED) {
            ulog("[WiFi] FAILED\n");
            stmSerial.sendStatus(0, 0);
        } else if (s == WifiState::CONNECTING) {
            ulog("[WiFi] Connecting...\n");
        }
    });

    // ─── MQTT 消息回调 ───
    mqttMgr.onMessage([](const String& topic, const String& payload) {
        ulog("[MQTT] <- %s : %s\n", topic.c_str(), payload.c_str());
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            ulog("[MQTT] JSON parse error: %s\n", err.c_str());
            return;
        }
        if (doc["wifi_ssid"].is<String>() && doc["wifi_pwd"].is<String>()) {
            String ssid  = doc["wifi_ssid"].as<String>();
            String pwd   = doc["wifi_pwd"].as<String>();
            String token = doc["mqtt_token"] | "";
            DeviceCfg cur = cfgMgr.load();
            cfgMgr.save(ssid, pwd, cur.roomNo, token.isEmpty() ? cur.mqttToken : token);
            wifiMgr.begin(ssid, pwd);
            return;
        }
        if (doc["mqtt_token"].is<String>()) {
            String token = doc["mqtt_token"].as<String>();
            DeviceCfg cur = cfgMgr.load();
            cfgMgr.save(cur.ssid, cur.password, cur.roomNo, token);
            mqttMgr.disconnect();
            DeviceCfg updated = cfgMgr.load();
            mqttMgr.begin("175.24.153.243", 1883,
                          safeDeviceId(updated), safeRoomNo(updated),
                          updated.mqttToken);
            return;
        }
        int speed = doc["speed"]    | -1;
        int dir   = doc["dir"]      | -1;
        String op = doc["operator"] | "";
        if (speed != -1 || dir != -1 || op.length() > 0) {
            sendToSTM32(speed, dir, op);
        }
    });

    // ─── 首次启动 ───
    DeviceCfg saved = cfgMgr.load();
    if (saved.valid && saved.ssid.length() > 0) {
        ulog("[Boot] 已有配置 ssid=%s\n", saved.ssid.c_str());
        wifiMgr.begin(saved.ssid, saved.password);
    } else {
        ulog("[Boot] 新板子，写入默认配置\n");
        cfgMgr.save(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PWD,
                    DEFAULT_ROOM, DEFAULT_MQTT_TOKEN);
        wifiMgr.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PWD);
    }
}

void loop() {
    stmSerial.loop();
    wifiMgr.loop();
    mqttMgr.loop();

    static unsigned long lastEspNow = 0;
    if (millis() - lastEspNow > 5000) {
        lastEspNow = millis();
        myData.id   = 1;
        myData.temp = 25.5;
        myData.hum  = 60.0;
        esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
    }

    bool firstCheckReady = _otaPendingCheck
                           && _wifiConnectedAt > 0
                           && (millis() - _wifiConnectedAt > OTA_FIRST_DELAY_MS);
    bool periodicCheck   = (_lastOtaCheck != 0)
                           && (millis() - _lastOtaCheck > OTA_CHECK_INTERVAL_MS);

    if (wifiMgr.state() == WifiState::CONNECTED && (firstCheckReady || periodicCheck)) {
        _otaPendingCheck = false;
        _lastOtaCheck    = millis();
        ulog("[OTA] Checking for new firmware...\n");
        otaMgr.checkAndUpdate(GITHUB_VERSION_URL);
    }
}