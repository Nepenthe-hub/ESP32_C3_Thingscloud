#pragma once
#include <Arduino.h>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
//  帧格式（与 STM32 uart_esp32.h 完全一致）
//  AA BB | Type(1B) | Len(1B) | Data(N B) | Checksum(1B)
//  Checksum = XOR( Type, Len, Data[0..N-1] )
// ─────────────────────────────────────────────────────────────────────────────
#define ESP32_PKT_HEADER1   0xAAU
#define ESP32_PKT_HEADER2   0xBBU
#define ESP32_PKT_MIN_LEN   5U       // 2(hdr) + 1(type) + 1(len) + 1(cs)
#define ESP32_PKT_MAX_DATA  250U

// ── 包类型 ────────────────────────────────────────────────────────────────────
#define PKT_TYPE_SENSOR     0x01U   // 传感器数据   STM32→ESP32
#define PKT_TYPE_WIFI_CFG   0x10U   // WiFi 配置    双向
#define PKT_TYPE_ROOM_NUM   0x11U   // 房间号       双向
#define PKT_TYPE_STATUS     0x81U   // 连接状态     ESP32→STM32
#define PKT_TYPE_TIME       0x82U   // NTP 时间     ESP32→STM32
// 新增：用于回环自测
#define PKT_TYPE_ECHO_REQ   0xE0U   // 回环请求
#define PKT_TYPE_ECHO_RSP   0xE1U   // 回环应答

// ── 数据结构（#pragma pack(1) 与 STM32 侧完全一致）────────────────────────────
#pragma pack(1)

typedef struct {            // 0x01  14 字节
    uint16_t temp;
    uint16_t humi;
    uint16_t pm2_5;
    uint16_t pm10;
    uint16_t hcho;
    uint16_t co2;
    uint16_t tvoc;
} sensor_packet_t;

typedef struct {            // 0x10  96 字节
    char ssid[32];
    char pwd[64];
} wifi_cfg_packet_t;

typedef struct {            // 0x11  4 字节
    uint8_t room[4];        // ASCII，如 "B203"，无结尾 \0
} room_num_packet_t;

typedef struct {            // 0x81  2 字节
    uint8_t wifi_status;
    uint8_t mqtt_status;
} status_packet_t;

typedef struct {            // 0x82  7 字节
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  min;
    uint8_t  sec;
} time_sync_packet_t;

#pragma pack()

// ── 回调类型 ──────────────────────────────────────────────────────────────────
struct CfgMsg {
    String ssid;
    String pwd;
    String room;
};

// ─────────────────────────────────────────────────────────────────────────────
//  SerialProtocol
//
//  使用方式：
//    // ESPU1（原有，IO6/IO7）
//    SerialProtocol espu1(Serial1);
//    espu1.begin(115200, 6, 7);
//
//    // ESPU0（新增，IO19=RX / IO18=TX，接 USB 下载芯片）
//    SerialProtocol espu0(Serial0);
//    espu0.begin(115200, 19, 18);
// ─────────────────────────────────────────────────────────────────────────────
class SerialProtocol {
public:
    using CfgCallback    = std::function<void(const CfgMsg&)>;
    using SensorCallback = std::function<void(const sensor_packet_t&)>;
    using RawCallback    = std::function<void(uint8_t type,
                                              const uint8_t* data,
                                              uint8_t len)>;

    // port：传入 Serial0 / Serial1 / Serial2
    explicit SerialProtocol(HardwareSerial& port = Serial1)
        : _port(port) {}

    // ── 初始化 ────────────────────────────────────────────────────────────────
    // rx_pin / tx_pin = -1 时使用芯片默认引脚
    void begin(uint32_t baud = 115200, int rx_pin = -1, int tx_pin = -1);

    // ── 轮询（放入 Arduino loop()）────────────────────────────────────────────
    void loop();

    // ── 发送帧 ───────────────────────────────────────────────────────────────
    bool sendPacket(uint8_t type, const uint8_t* data = nullptr, uint8_t len = 0);

    // ── 常用快捷发送 ─────────────────────────────────────────────────────────
    void sendStatus(uint8_t wifi, uint8_t mqtt);
    void sendTimeSync(const time_sync_packet_t& t);

    // ── 回环自测（ESPU0 TX→RX 短接时使用）────────────────────────────────────
    // 发出一个 ECHO_REQ，在 loop() 里等待 ECHO_RSP，返回是否成功
    bool echoTest(uint32_t timeoutMs = 500);

    // ── 回调注册 ─────────────────────────────────────────────────────────────
    void onCfg(CfgCallback cb)       { _cfgCb    = cb; }
    void onSensor(SensorCallback cb) { _sensorCb = cb; }
    // 捕获所有未知包（调试用）
    void onRaw(RawCallback cb)       { _rawCb    = cb; }

    // ── 统计（用于验证帧完整性）─────────────────────────────────────────────
    uint32_t rxFrameOk()  const { return _statRxOk; }
    uint32_t rxFrameErr() const { return _statRxErr; }
    uint32_t txFrames()   const { return _statTx; }
    void     resetStats()       { _statRxOk = _statRxErr = _statTx = 0; }

private:
    HardwareSerial& _port;

    // 接收状态机
    uint8_t  _buf[ESP32_PKT_MAX_DATA + 5U];
    uint16_t _bufLen = 0;

    // 统计
    uint32_t _statRxOk  = 0;
    uint32_t _statRxErr = 0;
    uint32_t _statTx    = 0;

    // 回环测试标志
    volatile bool _echoReceived = false;

    // 回调
    CfgCallback    _cfgCb;
    SensorCallback _sensorCb;
    RawCallback    _rawCb;

    uint8_t _calcChecksum(uint8_t type, uint8_t len, const uint8_t* data) const;
    void    _dispatch(uint8_t type, uint8_t len, const uint8_t* payload);
};