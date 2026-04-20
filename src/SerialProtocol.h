#pragma once
#include <Arduino.h>
#include <functional>

// ─────────────────────────────────────────────
//  帧格式（与 STM32 uart_esp32.h 完全一致）
//  AA BB | Type(1B) | Len(1B) | Data(N B) | Checksum(1B)
//  Checksum = XOR( Type, Len, Data[0..N-1] )
// ─────────────────────────────────────────────
#define ESP32_PKT_HEADER1   0xAAU
#define ESP32_PKT_HEADER2   0xBBU
#define ESP32_PKT_MIN_LEN   5U
#define ESP32_PKT_MAX_DATA  250U

// 包类型定义（与 STM32 保持一致）
#define PKT_TYPE_SENSOR     0x01U   // 传感器数据 STM32->ESP32
#define PKT_TYPE_WIFI_CFG   0x10U   // WiFi配置   双向
#define PKT_TYPE_ROOM_NUM   0x11U   // 房间号     双向
#define PKT_TYPE_STATUS     0x81U   // 连接状态   ESP32->STM32
#define PKT_TYPE_TIME       0x82U   // NTP时间    ESP32->STM32

// ─────────────────────────────────────────────
//  数据结构（与 STM32 uart_esp32.h #pragma pack(1) 完全一致）
// ─────────────────────────────────────────────
#pragma pack(1)

// 0x01 传感器数据
typedef struct {
    uint16_t temp;
    uint16_t humi;
    uint16_t pm2_5;
    uint16_t pm10;
    uint16_t hcho;
    uint16_t co2;
    uint16_t tvoc;
} sensor_packet_t;  // 14字节

// 0x10 WiFi配置
typedef struct {
    char ssid[32];
    char pwd[64];
} wifi_cfg_packet_t;  // 96字节

// 0x11 房间号
typedef struct {
    uint8_t room[4];  // ASCII，如"B203"，无结尾\0
} room_num_packet_t;  // 4字节

// 0x81 连接状态
typedef struct {
    uint8_t wifi_status;
    uint8_t mqtt_status;
} status_packet_t;  // 2字节

// 0x82 NTP时间
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  min;
    uint8_t  sec;
} time_sync_packet_t;  // 7字节

#pragma pack()

// ─────────────────────────────────────────────
//  回调类型定义
// ─────────────────────────────────────────────
struct CfgMsg {
    String ssid;
    String pwd;
    String room;
};

class SerialProtocol {
public:
    using CfgCallback    = std::function<void(const CfgMsg&)>;
    using SensorCallback = std::function<void(const sensor_packet_t&)>;

    void begin(uint32_t baud = 115200);
    void loop();

    // 注册回调
    void onCfg(CfgCallback cb)       { _cfgCb    = cb; }
    void onSensor(SensorCallback cb) { _sensorCb = cb; }

    // 发送帧给 STM32
    void sendPacket(uint8_t type, const uint8_t *data, uint8_t len);
    void sendStatus(uint8_t wifi, uint8_t mqtt);

private:
    // 接收缓冲区，最大一帧
    uint8_t  _buf[ESP32_PKT_MAX_DATA + 5U];
    uint16_t _bufLen = 0;

    CfgCallback    _cfgCb;
    SensorCallback _sensorCb;

    uint8_t  _calcChecksum(uint8_t type, uint8_t len, const uint8_t *data);
    void     _dispatch(uint8_t type, uint8_t len, const uint8_t *payload);
};