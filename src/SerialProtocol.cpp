#include "SerialProtocol.h"

void SerialProtocol::begin(uint32_t baud) {
    // Serial1 接外设模块（对应原理图 ESPU1，IO6=RX IO7=TX）
    // Serial（USB CDC）用于 PC 调试，不在此处初始化，由 main.cpp setup() 负责
    Serial1.begin(baud, SERIAL_8N1, 6, 7);
}

// ─────────────────────────────────────────────
//  loop：从 Serial1 逐字节读入，拼帧后解析
//  状态机逻辑：
//  等待 AA -> 等待 BB -> 读 Type/Len -> 读 Data -> 校验 -> dispatch
// ─────────────────────────────────────────────
void SerialProtocol::loop() {
    while (Serial1.available()) {
        uint8_t c = Serial1.read();

        // 缓冲区溢出保护：丢掉当前帧，重新开始
        if (_bufLen >= sizeof(_buf)) {
            _bufLen = 0;
        }

        _buf[_bufLen++] = c;

        // 至少需要 2 字节才能判断帧头
        if (_bufLen < 2) continue;

        // 检查帧头 AA BB
        if (_buf[0] != ESP32_PKT_HEADER1 || _buf[1] != ESP32_PKT_HEADER2) {
            // 帧头不对，丢弃第一个字节，尝试重新同步
            memmove(_buf, _buf + 1, _bufLen - 1);
            _bufLen--;
            continue;
        }

        // 帧头对了但还没收到 Type 和 Len
        if (_bufLen < 4) continue;

        uint8_t type = _buf[2];
        uint8_t len  = _buf[3];

        // 检查 len 合法性
        if (len > ESP32_PKT_MAX_DATA) {
            // 非法长度，丢弃帧头，重新同步
            memmove(_buf, _buf + 1, _bufLen - 1);
            _bufLen--;
            continue;
        }

        // 完整帧长 = 2(header) + 1(type) + 1(len) + len(data) + 1(checksum)
        uint16_t fullLen = (uint16_t)(5U + len);

        // 数据还没收完，继续等
        if (_bufLen < fullLen) continue;

        // 收到完整帧，校验 checksum
        uint8_t cs = _calcChecksum(type, len, &_buf[4]);
        if (cs != _buf[4 + len]) {
            Serial.printf("[Serial] checksum error: calc=0x%02X recv=0x%02X\n",
                          cs, _buf[4 + len]);
            // 校验失败，丢弃帧头，尝试重新同步
            memmove(_buf, _buf + 1, _bufLen - 1);
            _bufLen--;
            continue;
        }

        // 校验通过，分发处理
        _dispatch(type, len, &_buf[4]);

        // 消费掉这一帧，保留后续多余字节
        uint16_t remaining = _bufLen - fullLen;
        if (remaining > 0) {
            memmove(_buf, _buf + fullLen, remaining);
        }
        _bufLen = remaining;
    }
}

// ─────────────────────────────────────────────
//  _calcChecksum：XOR(type, len, data[0..n-1])
// ─────────────────────────────────────────────
uint8_t SerialProtocol::_calcChecksum(uint8_t type, uint8_t len, const uint8_t *data) {
    uint8_t cs = type ^ len;
    for (uint8_t i = 0; i < len; i++) {
        cs ^= data[i];
    }
    return cs;
}

// ─────────────────────────────────────────────
//  _dispatch：根据包类型调用对应回调
// ─────────────────────────────────────────────
void SerialProtocol::_dispatch(uint8_t type, uint8_t len, const uint8_t *payload) {
    switch (type) {

        // 0x10 WiFi配置（STM32 -> ESP32）
        case PKT_TYPE_WIFI_CFG: {
            if (len < sizeof(wifi_cfg_packet_t)) {
                Serial.printf("[Serial] PKT_TYPE_WIFI_CFG len too short: %d\n", len);
                break;
            }
            const wifi_cfg_packet_t *wf = (const wifi_cfg_packet_t *)payload;
            if (_cfgCb) {
                CfgMsg msg;
                msg.ssid = String(wf->ssid);
                msg.pwd  = String(wf->pwd);
                msg.room = "";  // WiFi包里没有房间号，由 0x11 单独发
                Serial.printf("[Serial] WiFi cfg: ssid=%s\n", wf->ssid);
                _cfgCb(msg);
            }
            break;
        }

        // 0x11 房间号（STM32 -> ESP32）
        case PKT_TYPE_ROOM_NUM: {
            if (len < sizeof(room_num_packet_t)) {
                Serial.printf("[Serial] PKT_TYPE_ROOM_NUM len too short: %d\n", len);
                break;
            }
            const room_num_packet_t *rn = (const room_num_packet_t *)payload;
            // 房间号是 4 字节 ASCII，无 \0，手动构造 String
            char roomStr[5] = {0};
            memcpy(roomStr, rn->room, 4);
            Serial.printf("[Serial] Room num: %s\n", roomStr);
            // 房间号单独到来时，只更新 room，ssid/pwd 留空
            // main.cpp 的 onCfg 回调需要处理 ssid 为空的情况（只更新房间号）
            if (_cfgCb) {
                CfgMsg msg;
                msg.ssid = "";
                msg.pwd  = "";
                msg.room = String(roomStr);
                _cfgCb(msg);
            }
            break;
        }

        // 0x01 传感器数据（STM32 -> ESP32，可选处理）
        case PKT_TYPE_SENSOR: {
            if (len < sizeof(sensor_packet_t)) {
                Serial.printf("[Serial] PKT_TYPE_SENSOR len too short: %d\n", len);
                break;
            }
            const sensor_packet_t *sp = (const sensor_packet_t *)payload;
            Serial.printf("[Serial] Sensor: T=%d H=%d PM2.5=%d CO2=%d TVOC=%d\n",
                          sp->temp, sp->humi, sp->pm2_5, sp->co2, sp->tvoc);
            if (_sensorCb) {
                _sensorCb(*sp);
            }
            break;
        }

        default:
            Serial.printf("[Serial] unknown type=0x%02X len=%d\n", type, len);
            break;
    }
}

// ─────────────────────────────────────────────
//  sendPacket：组帧发送给 STM32
// ─────────────────────────────────────────────
void SerialProtocol::sendPacket(uint8_t type, const uint8_t *data, uint8_t len) {
    if (len > ESP32_PKT_MAX_DATA) return;

    uint8_t pkt[ESP32_PKT_MAX_DATA + 5U];
    pkt[0] = ESP32_PKT_HEADER1;
    pkt[1] = ESP32_PKT_HEADER2;
    pkt[2] = type;
    pkt[3] = len;
    if (len > 0 && data != nullptr) {
        memcpy(&pkt[4], data, len);
    }
    pkt[4 + len] = _calcChecksum(type, len, (len > 0 && data != nullptr) ? data : (const uint8_t *)"");

    Serial1.write(pkt, len + 5U);
}

// ─────────────────────────────────────────────
//  sendStatus：主动告知 STM32 当前 WiFi/MQTT 状态
//  在 WiFi 和 MQTT 状态变化时调用
// ─────────────────────────────────────────────
void SerialProtocol::sendStatus(uint8_t wifi, uint8_t mqtt) {
    status_packet_t pkt;
    pkt.wifi_status = wifi;
    pkt.mqtt_status = mqtt;
    sendPacket(PKT_TYPE_STATUS, (const uint8_t *)&pkt, sizeof(pkt));
    Serial.printf("[Serial] status sent: WiFi=%d MQTT=%d\n", wifi, mqtt);
}