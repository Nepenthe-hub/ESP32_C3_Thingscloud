// #include "soc/soc.h"
// #include "soc/rtc_cntl_reg.h"
// #include <Arduino.h>
// #include <ArduinoJson.h>
// #include <esp_now.h>
// #include <WiFi.h>
// #include <WiFiUdp.h>
// #include <nvs_flash.h>
// #include "ConfigManager.h"
// #include "WifiManager.h"
// #include "MqttManager.h"
// #include "PowerManager.h"
// #include "SerialProtocol.h"

// // ─────────────────────────────────────────────────────────────────────────────
// //  配置常量
// // ─────────────────────────────────────────────────────────────────────────────
// static const char* WIFI_SSID     = "GZJG";
// static const char* WIFI_PWD      = "88888888";
// static const char* DEFAULT_ROOM  = "Room_1";
// static const char* MQTT_HOST     = "sh-1-mqtt.iot-api.com";
// static const char* MQTT_USERNAME = "9a7lf0qlp3p9odgq";
// static const char* MQTT_PASSWORD = "rg8EHtRQ3R";

// // ─────────────────────────────────────────────────────────────────────────────
// //  引脚
// // ─────────────────────────────────────────────────────────────────────────────
// #define PIN_POWEROK  10
// #define PIN_POWER     3

// // ─────────────────────────────────────────────────────────────────────────────
// //  全局对象
// // ─────────────────────────────────────────────────────────────────────────────
// static WiFiUDP  _udp;
// static bool     _udpReady = false;
// static uint16_t UDP_PORT  = 12345;

// ConfigManager    cfgMgr;
// WifiManager      wifiMgr;
// MqttManager      mqttMgr;
// PowerManager     pwrMgr;

// SerialProtocol   espu0(Serial0);
// SerialProtocol   espu1(Serial1);

// uint8_t           broadcastAddress[] = {0xDC, 0xDA, 0x0C, 0xD4, 0x93, 0x30};
// typedef struct    { int id; float temp; float hum; } struct_message;
// struct_message    myData;
// esp_now_peer_info_t peerInfo;

// // ─────────────────────────────────────────────────────────────────────────────
// //  UDP 日志
// // ─────────────────────────────────────────────────────────────────────────────
// void ulog(const char* fmt, ...) {
//     if (!_udpReady) return;
//     char buf[256];
//     va_list args;
//     va_start(args, fmt);
//     vsnprintf(buf, sizeof(buf), fmt, args);
//     va_end(args);
//     _udp.beginPacket("255.255.255.255", UDP_PORT);
//     _udp.write((const uint8_t*)buf, strlen(buf));
//     _udp.endPacket();
// }

// static String safeDeviceId() {
//     String id = "C3_" + WiFi.macAddress();
//     id.replace(":", "");
//     return id;
// }

// // ─────────────────────────────────────────────────────────────────────────────
// //  电池检测
// // ─────────────────────────────────────────────────────────────────────────────
// float readBatteryVoltage() {
//     pinMode(PIN_POWEROK, OUTPUT);
//     digitalWrite(PIN_POWEROK, LOW);
//     delay(300);
//     int sum = 0;
//     for (int i = 0; i < 8; i++) { sum += analogRead(PIN_POWER); delay(10); }
//     float adcV = (sum / 8) * 3.3f / 4095.0f;
//     float batV = adcV * 2.0f;
//     digitalWrite(PIN_POWEROK, HIGH);
//     pinMode(PIN_POWEROK, INPUT);
//     if (batV < 2.5f || batV > 4.5f) {
//         ulog("[BAT] Abnormal %.2fV, ignored\n", batV);
//         return 0.0f;
//     }
//     return batV;
// }

// int batteryPercent(float v) {
//     if (v >= 4.2f) return 100;
//     if (v <= 3.0f) return 0;
//     return (int)((v - 3.0f) / 1.2f * 100.0f);
// }

// void reportBattery() {
//     float v = readBatteryVoltage();
//     if (v == 0.0f) return;
//     int p = batteryPercent(v);
//     ulog("[BAT] %.2fV  %d%%\n", v, p);
//     if (v < 3.1f) {
//         ulog("[BAT] Low battery! Shutting down.\n");
//         if (mqttMgr.isConnected()) {
//             JsonDocument doc;
//             doc["event"] = "low_battery_shutdown";
//             doc["bat_v"] = v; doc["bat_percent"] = p;
//             String pl; serializeJson(doc, pl);
//             mqttMgr.publish("events", pl);
//         }
//         pwrMgr.shutdown(800);
//         return;
//     }
//     if (mqttMgr.isConnected()) {
//         JsonDocument doc;
//         doc["bat_v"] = v; doc["bat_percent"] = p;
//         String pl; serializeJson(doc, pl);
//         mqttMgr.publish("attributes", pl);
//     }
// }

// // ─────────────────────────────────────────────────────────────────────────────
// //  关机回调
// // ─────────────────────────────────────────────────────────────────────────────
// void onSystemShutdown() {
//     ulog("[SYS] Shutdown: sending offline event...\n");
//     if (mqttMgr.isConnected()) {
//         JsonDocument doc;
//         doc["event"] = "device_offline"; doc["reason"] = "power_off";
//         String pl; serializeJson(doc, pl);
//         mqttMgr.publish("events", pl);
//         delay(100);
//         mqttMgr.disconnect();
//     }
// }

// // ─────────────────────────────────────────────────────────────────────────────
// //  串口接收处理
// // ─────────────────────────────────────────────────────────────────────────────
// static void onSerialSensor(const sensor_packet_t& sp) {
//     ulog("[ESPU1] T=%u H=%u PM2.5=%u CO2=%u TVOC=%u\n",
//          sp.temp, sp.humi, sp.pm2_5, sp.co2, sp.tvoc);
//     if (!mqttMgr.isConnected()) return;
//     JsonDocument doc;
//     doc["temp"]  = sp.temp  / 10.0f;
//     doc["humi"]  = sp.humi  / 10.0f;
//     doc["pm2_5"] = sp.pm2_5;
//     doc["co2"]   = sp.co2;
//     doc["tvoc"]  = sp.tvoc;
//     String pl; serializeJson(doc, pl);
//     mqttMgr.publish("attributes", pl);
// }

// static void onSerialCfg(const CfgMsg& msg) {
//     if (!msg.ssid.isEmpty()) {
//         ulog("[ESPU1] WiFi cfg ssid=%s\n", msg.ssid.c_str());
//         DeviceCfg cur = cfgMgr.load();
//         cfgMgr.save(msg.ssid, msg.pwd,
//                     msg.room.isEmpty() ? cur.roomNo : msg.room,
//                     cur.mqttToken);
//         ESP.restart();
//     } else if (!msg.room.isEmpty()) {
//         ulog("[ESPU1] Room update: %s\n", msg.room.c_str());
//         DeviceCfg cur = cfgMgr.load();
//         cfgMgr.save(cur.ssid, cur.password, msg.room, cur.mqttToken);
//     }
// }

// // ─────────────────────────────────────────────────────────────────────────────
// //  setup
// // ─────────────────────────────────────────────────────────────────────────────
// void setup() {
//     // ══ ① 禁用欠压复位，防止 WiFi 启动瞬间电流冲击触发 BROWNOUT ═══════════
//     WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

//     // ══ ② PowerManager 自锁 ════════════════════════════════════════════════
//     pwrMgr.begin();
//     pwrMgr.onShutdown(onSystemShutdown);
//     pwrMgr.onKey([](KeyEvent ev) {
//         if (ev == KeyEvent::SHORT_PRESS) ulog("[PWR] Short press\n");
//     });

//     // ══ ③ NVS ═══════════════════════════════════════════════════════════════
//     esp_err_t err = nvs_flash_init();
//     if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//         nvs_flash_erase();
//         nvs_flash_init();
//     }
//     cfgMgr.begin();

//     // ══ ④ 串口协议初始化 ════════════════════════════════════════════════════
//     espu0.begin(115200, /*rx*/20, /*tx*/21);
//     ulog("\n[SYS] Booting...\n");

//     espu1.begin(115200, /*rx*/6, /*tx*/7);
//     espu1.onSensor(onSerialSensor);
//     espu1.onCfg(onSerialCfg);

//     // ══ ⑤ ESP-NOW ═══════════════════════════════════════════════════════════
//     WiFi.mode(WIFI_STA);
//     if (esp_now_init() == ESP_OK) {
//         esp_now_register_send_cb([](const uint8_t*, esp_now_send_status_t s) {
//             ulog("[ESP-NOW] Send %s\n", s == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
//         });
//         esp_now_register_recv_cb([](const uint8_t*, const uint8_t* data, int len) {
//             if (len == (int)sizeof(myData)) {
//                 memcpy(&myData, data, sizeof(myData));
//                 ulog("[ESP-NOW] ID=%d T=%.2f H=%.2f\n",
//                      myData.id, myData.temp, myData.hum);
//                 if (mqttMgr.isConnected()) {
//                     JsonDocument doc;
//                     doc["temp"] = myData.temp;
//                     doc["hum"]  = myData.hum;
//                     String pl; serializeJson(doc, pl);
//                     mqttMgr.publish("attributes", pl);
//                 }
//             }
//         });
//         memcpy(peerInfo.peer_addr, broadcastAddress, 6);
//         peerInfo.channel = 0; peerInfo.encrypt = false;
//         esp_now_add_peer(&peerInfo);
//     }

//     // ══ ⑥ WiFi ══════════════════════════════════════════════════════════════
//     wifiMgr.onStateChange([](WifiState s) {
//         if (s == WifiState::CONNECTED) {
//             _udp.begin(UDP_PORT);
//             _udpReady = true;
//             ulog("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
//             espu1.sendStatus(1, 0);
//             DeviceCfg cfg = cfgMgr.load();
//             String room = cfg.roomNo.isEmpty() ? DEFAULT_ROOM : cfg.roomNo;
//             mqttMgr.begin(MQTT_HOST, 1883,
//                           safeDeviceId(), room,
//                           MQTT_USERNAME, MQTT_PASSWORD);
//         } else if (s == WifiState::FAILED) {
//             ulog("[WiFi] FAILED\n");
//             espu1.sendStatus(0, 0);
//         }
//     });

//     // ══ ⑦ MQTT ══════════════════════════════════════════════════════════════
//     mqttMgr.onMessage([](const String& topic, const String& payload) {
//         ulog("[MQTT] <- %s : %s\n", topic.c_str(), payload.c_str());
//         JsonDocument doc;
//         if (deserializeJson(doc, payload) == DeserializationError::Ok) {
//             String cmd = doc["cmd"] | "";
//             if (cmd == "power_off") {
//                 ulog("[PWR] Remote power_off!\n");
//                 pwrMgr.shutdown(600);
//             }
//         }
//     });

//     wifiMgr.begin(WIFI_SSID, WIFI_PWD);
//     ulog("[SYS] Setup done\n");
// }

// // ─────────────────────────────────────────────────────────────────────────────
// //  loop
// // ─────────────────────────────────────────────────────────────────────────────
// void loop() {
//     pwrMgr.loop();
//     wifiMgr.loop();
//     mqttMgr.loop();
//     espu0.loop();
//     espu1.loop();

//     static unsigned long lastLoop = 0;
//     if (millis() - lastLoop > 5000) {
//         lastLoop = millis();
//         myData.id = 1; myData.temp = 25.5; myData.hum = 60.0;
//         esp_now_send(broadcastAddress, (uint8_t*)&myData, sizeof(myData));

//         bool mqttOk = mqttMgr.isConnected();
//         espu1.sendStatus(1, mqttOk ? 1 : 0);
//         ulog("[LOOP] MQTT=%s  ESPU1 rxOk=%u rxErr=%u\n",
//              mqttOk ? "OK" : "FAIL",
//              espu1.rxFrameOk(), espu1.rxFrameErr());
//     }

//     static unsigned long lastBat = 0;
//     if (_udpReady && millis() - lastBat > 30000) {
//         lastBat = millis();
//         reportBattery();
//     }
// }
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <nvs_flash.h>
#include "ConfigManager.h"
#include "WifiManager.h"
#include "MqttManager.h"
#include "PowerManager.h"
#include "SerialProtocol.h"

// ─────────────────────────────────────────────────────────────────────────────
//  配置常量
// ─────────────────────────────────────────────────────────────────────────────
static const char* WIFI_SSID     = "GZJG";
static const char* WIFI_PWD      = "88888888";
static const char* DEFAULT_ROOM  = "Room_1";
static const char* MQTT_HOST     = "sh-1-mqtt.iot-api.com";
static const char* MQTT_USERNAME = "9a7lf0qlp3p9odgq";
static const char* MQTT_PASSWORD = "rg8EHtRQ3R";

// ─────────────────────────────────────────────────────────────────────────────
//  引脚
// ─────────────────────────────────────────────────────────────────────────────
#define PIN_POWEROK  10
#define PIN_POWER     3

// ─────────────────────────────────────────────────────────────────────────────
//  全局对象
// ─────────────────────────────────────────────────────────────────────────────
static WiFiUDP  _udp;
static bool     _udpReady = false;
static uint16_t UDP_PORT  = 12345;

ConfigManager    cfgMgr;
WifiManager      wifiMgr;
MqttManager      mqttMgr;
PowerManager     pwrMgr;

// 【关键修改 1】：espu0 绑定到真正的硬件串口 Serial0，不要去抢 USB 的 Serial
SerialProtocol   espu0(Serial0);
// espu1 依然绑定到硬件串口 Serial1，负责和 STM32 通信
SerialProtocol   espu1(Serial1);

uint8_t           broadcastAddress[] = {0xDC, 0xDA, 0x0C, 0xD4, 0x93, 0x30};
typedef struct    { int id; float temp; float hum; } struct_message;
struct_message    myData;
esp_now_peer_info_t peerInfo;

// ─────────────────────────────────────────────────────────────────────────────
//  UDP 日志
// ─────────────────────────────────────────────────────────────────────────────
void ulog(const char* fmt, ...) {
    if (!_udpReady) return;
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    _udp.beginPacket("255.255.255.255", UDP_PORT);
    _udp.write((const uint8_t*)buf, strlen(buf));
    _udp.endPacket();
}

static String safeDeviceId() {
    String id = "C3_" + WiFi.macAddress();
    id.replace(":", "");
    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
//  电池检测 (省略内部逻辑，保持不变)
// ─────────────────────────────────────────────────────────────────────────────
float readBatteryVoltage() { /* ... 保持原样 ... */ return 4.0f; }
int batteryPercent(float v) { /* ... 保持原样 ... */ return 100; }
void reportBattery() { /* ... 保持原样 ... */ }
void onSystemShutdown() { /* ... 保持原样 ... */ }

// ─────────────────────────────────────────────────────────────────────────────
//  串口接收处理
// ─────────────────────────────────────────────────────────────────────────────
static void onSerialSensor(const sensor_packet_t& sp) {
    // 这里的日志会从 USB 串口打印到电脑屏幕上
    Serial.printf("[ESPU1] T=%u H=%u PM2.5=%u CO2=%u TVOC=%u\n",
         sp.temp, sp.humi, sp.pm2_5, sp.co2, sp.tvoc);
    ulog("[ESPU1] T=%u H=%u\n", sp.temp, sp.humi);
    
    if (!mqttMgr.isConnected()) return;
    JsonDocument doc;
    doc["temp"]  = sp.temp  / 10.0f;
    doc["humi"]  = sp.humi  / 10.0f;
    doc["pm2_5"] = sp.pm2_5;
    doc["co2"]   = sp.co2;
    doc["tvoc"]  = sp.tvoc;
    String pl; serializeJson(doc, pl);
    mqttMgr.publish("attributes", pl);
}

static void onSerialCfg(const CfgMsg& msg) {
    if (!msg.ssid.isEmpty()) {
        Serial.printf("[ESPU1] WiFi cfg ssid=%s\n", msg.ssid.c_str());
        DeviceCfg cur = cfgMgr.load();
        cfgMgr.save(msg.ssid, msg.pwd,
                    msg.room.isEmpty() ? cur.roomNo : msg.room,
                    cur.mqttToken);
        ESP.restart();
    } else if (!msg.room.isEmpty()) {
        Serial.printf("[ESPU1] Room update: %s\n", msg.room.c_str());
        DeviceCfg cur = cfgMgr.load();
        cfgMgr.save(cur.ssid, cur.password, msg.room, cur.mqttToken);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  setup
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    // 开启 USB 调试串口打印 (波特率随便设，USB CDC不依赖物理波特率)
    Serial.begin(115200);
    delay(1000); // 稍微等一下 USB 枚举

    // 禁用欠压复位 (虽然禁用了，但没大电容硬件依然会宕机)
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // PowerManager 自锁
    pwrMgr.begin();
    pwrMgr.onShutdown(onSystemShutdown);
    pwrMgr.onKey([](KeyEvent ev) {
        if (ev == KeyEvent::SHORT_PRESS) Serial.println("[PWR] Short press");
    });

    // NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    cfgMgr.begin();

    Serial.println("\n[SYS] Booting...");

    // 【关键修改 2】：espu0 (外设0) 绑定到 C3 的默认 UART0 引脚：RX=20(引脚11), TX=21(引脚12)
    espu0.begin(115200, /*rx*/ 20, /*tx*/ 21);

    // espu1 (STM32外设) 绑定到 UART1 引脚：RX=6, TX=7
    espu1.begin(115200, /*rx*/ 6, /*tx*/ 7);
    espu1.onSensor(onSerialSensor);
    espu1.onCfg(onSerialCfg);

    // ESP-NOW 初始化
    // WiFi.mode(WIFI_STA);
    // if (esp_now_init() == ESP_OK) {
    //     esp_now_register_send_cb([](const uint8_t*, esp_now_send_status_t s) {
    //         // Serial.printf("[ESP-NOW] Send %s\n", s == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
    //     });
    //     memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    //     peerInfo.channel = 0; peerInfo.encrypt = false;
    //     esp_now_add_peer(&peerInfo);
    // }

    // WiFi 回调
    wifiMgr.onStateChange([](WifiState s) {
        if (s == WifiState::CONNECTED) {
            _udp.begin(UDP_PORT);
            _udpReady = true;
            Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
            espu1.sendStatus(1, 0);
            DeviceCfg cfg = cfgMgr.load();
            String room = cfg.roomNo.isEmpty() ? DEFAULT_ROOM : cfg.roomNo;
            mqttMgr.begin(MQTT_HOST, 1883, safeDeviceId(), room, MQTT_USERNAME, MQTT_PASSWORD);
        } else if (s == WifiState::FAILED) {
            Serial.println("[WiFi] FAILED");
            espu1.sendStatus(0, 0);
        }
    });

    // // 开始连接 WiFi
    // wifiMgr.begin(WIFI_SSID, WIFI_PWD);
    // Serial.println("[SYS] Setup done");
}

// ─────────────────────────────────────────────────────────────────────────────
//  loop
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    pwrMgr.loop();
    wifiMgr.loop();
    mqttMgr.loop();
    
    // 轮询两个硬件外设串口
    espu0.loop();
    espu1.loop();

    static unsigned long lastLoop = 0;
    if (millis() - lastLoop > 5000) {
        lastLoop = millis();
        myData.id = 1; myData.temp = 25.5; myData.hum = 60.0;
        esp_now_send(broadcastAddress, (uint8_t*)&myData, sizeof(myData));

        bool mqttOk = mqttMgr.isConnected();
        espu1.sendStatus(1, mqttOk ? 1 : 0);
    }
}