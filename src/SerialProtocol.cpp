#include "SerialProtocol.h"

// ─────────────────────────────────────────────────────────────────────────────
//  begin
// ─────────────────────────────────────────────────────────────────────────────
void SerialProtocol::begin(uint32_t baud, int rx_pin, int tx_pin) {
    if (rx_pin >= 0 && tx_pin >= 0) {
        _port.begin(baud, SERIAL_8N1, rx_pin, tx_pin);
    } else {
        _port.begin(baud, SERIAL_8N1);
    }
    _bufLen = 0;
    resetStats();
    Serial.printf("[ESPU] begin baud=%u rx=%d tx=%d\n", baud, rx_pin, tx_pin);
}

// ─────────────────────────────────────────────────────────────────────────────
//  loop：逐字节状态机，帧头同步 → 长度验证 → 校验 → dispatch
// ─────────────────────────────────────────────────────────────────────────────
void SerialProtocol::loop() {
    while (_port.available()) {
        uint8_t c = _port.read();

        // 缓冲区溢出保护
        if (_bufLen >= (uint16_t)sizeof(_buf)) {
            _bufLen = 0;
        }
        _buf[_bufLen++] = c;

        // 至少需要 2 字节才能判断帧头
        if (_bufLen < 2) continue;

        // 帧头 AA BB 校验，不对则滑窗丢弃
        if (_buf[0] != ESP32_PKT_HEADER1 || _buf[1] != ESP32_PKT_HEADER2) {
            memmove(_buf, _buf + 1, _bufLen - 1);
            _bufLen--;
            continue;
        }

        // 等待 Type + Len 字节
        if (_bufLen < 4) continue;

        uint8_t  type    = _buf[2];
        uint8_t  dataLen = _buf[3];

        // 非法负载长度
        if (dataLen > ESP32_PKT_MAX_DATA) {
            
            memmove(_buf, _buf + 1, _bufLen - 1);
            _bufLen--;
            _statRxErr++;
            continue;
        }

        // 完整帧 = 2+1+1+dataLen+1
        uint16_t fullLen = (uint16_t)(5U + dataLen);
        if (_bufLen < fullLen) continue;

        // 校验
        uint8_t cs = _calcChecksum(type, dataLen, &_buf[4]);
        if (cs != _buf[4 + dataLen]) {
            Serial.printf("[ESPU] Checksum mismatch type=0x%02X calc=0x%02X got=0x%02X\n",
                          type, cs, _buf[4 + dataLen]);
            memmove(_buf, _buf + 1, _bufLen - 1);
            _bufLen--;
            _statRxErr++;
            continue;
        }

        // 校验通过
        _statRxOk++;
        _dispatch(type, dataLen, &_buf[4]);

        // 消费掉这帧，保留后续字节
        uint16_t remaining = _bufLen - fullLen;
        if (remaining > 0) memmove(_buf, _buf + fullLen, remaining);
        _bufLen = remaining;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  _calcChecksum：XOR(type, len, data[0..n-1])
// ─────────────────────────────────────────────────────────────────────────────
uint8_t SerialProtocol::_calcChecksum(uint8_t type, uint8_t len,
                                       const uint8_t* data) const {
    uint8_t cs = type ^ len;
    for (uint8_t i = 0; i < len; i++) cs ^= data[i];
    return cs;
}

// ─────────────────────────────────────────────────────────────────────────────
//  _dispatch
// ─────────────────────────────────────────────────────────────────────────────
void SerialProtocol::_dispatch(uint8_t type, uint8_t len,
                                const uint8_t* payload) {
    switch (type) {

        // ── 0x01 传感器数据 ────────────────────────────────────────────────────
        case PKT_TYPE_SENSOR: {
            if (len < sizeof(sensor_packet_t)) {
                Serial.printf("[ESPU] PKT_SENSOR too short: %u\n", len);
                break;
            }
            const sensor_packet_t* sp = (const sensor_packet_t*)payload;
            Serial.printf("[ESPU] Sensor T=%u H=%u PM2.5=%u CO2=%u TVOC=%u\n",
                          sp->temp, sp->humi, sp->pm2_5, sp->co2, sp->tvoc);
            if (_sensorCb) _sensorCb(*sp);
            break;
        }

        // ── 0x10 WiFi 配置 ────────────────────────────────────────────────────
        case PKT_TYPE_WIFI_CFG: {
            if (len < sizeof(wifi_cfg_packet_t)) {
                Serial.printf("[ESPU] PKT_WIFI_CFG too short: %u\n", len);
                break;
            }
            const wifi_cfg_packet_t* wf = (const wifi_cfg_packet_t*)payload;
            Serial.printf("[ESPU] WiFi cfg ssid=%s\n", wf->ssid);
            if (_cfgCb) {
                CfgMsg msg;
                msg.ssid = String(wf->ssid);
                msg.pwd  = String(wf->pwd);
                msg.room = "";
                _cfgCb(msg);
            }
            break;
        }

        // ── 0x11 房间号 ───────────────────────────────────────────────────────
        case PKT_TYPE_ROOM_NUM: {
            if (len < sizeof(room_num_packet_t)) {
                Serial.printf("[ESPU] PKT_ROOM_NUM too short: %u\n", len);
                break;
            }
            const room_num_packet_t* rn = (const room_num_packet_t*)payload;
            char roomStr[5] = {0};
            memcpy(roomStr, rn->room, 4);
            Serial.printf("[ESPU] Room num: %s\n", roomStr);
            if (_cfgCb) {
                CfgMsg msg;
                msg.ssid = "";
                msg.pwd  = "";
                msg.room = String(roomStr);
                _cfgCb(msg);
            }
            break;
        }

        // ── 0xE0 回环请求 → 立即回应 0xE1 ────────────────────────────────────
        case PKT_TYPE_ECHO_REQ: {
            Serial.println("[ESPU] Echo REQ -> sending RSP");
            sendPacket(PKT_TYPE_ECHO_RSP, payload, len);
            break;
        }

        // ── 0xE1 回环应答（echoTest() 等候这个包）────────────────────────────
        case PKT_TYPE_ECHO_RSP: {
            Serial.println("[ESPU] Echo RSP received");
            _echoReceived = true;
            break;
        }

        default:
            Serial.printf("[ESPU] Unknown type=0x%02X len=%u\n", type, len);
            if (_rawCb) _rawCb(type, payload, len);
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendPacket：组帧发送
//  返回 true = 写入串口成功（不代表对方收到）
// ─────────────────────────────────────────────────────────────────────────────
bool SerialProtocol::sendPacket(uint8_t type,
                                 const uint8_t* data, uint8_t len) {
    if (len > ESP32_PKT_MAX_DATA) return false;

    uint8_t pkt[ESP32_PKT_MAX_DATA + 5U];
    pkt[0] = ESP32_PKT_HEADER1;
    pkt[1] = ESP32_PKT_HEADER2;
    pkt[2] = type;
    pkt[3] = len;
    if (len > 0 && data != nullptr) memcpy(&pkt[4], data, len);
    pkt[4 + len] = _calcChecksum(type, len,
                                  (len > 0 && data) ? data
                                                    : (const uint8_t*)"");
    size_t written = _port.write(pkt, (size_t)(len + 5U));
    _statTx++;
    return (written == (size_t)(len + 5U));
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendStatus
// ─────────────────────────────────────────────────────────────────────────────
void SerialProtocol::sendStatus(uint8_t wifi, uint8_t mqtt) {
    status_packet_t pkt{ wifi, mqtt };
    sendPacket(PKT_TYPE_STATUS, (const uint8_t*)&pkt, sizeof(pkt));
    
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendTimeSync
// ─────────────────────────────────────────────────────────────────────────────
void SerialProtocol::sendTimeSync(const time_sync_packet_t& t) {
    sendPacket(PKT_TYPE_TIME, (const uint8_t*)&t, sizeof(t));
    Serial.printf("[ESPU] TimeSync %u-%02u-%02u %02u:%02u:%02u\n",
                  t.year, t.month, t.day, t.hour, t.min, t.sec);
}

// ─────────────────────────────────────────────────────────────────────────────
//  echoTest
//  需要把 ESPU0 的 TX(IO18) 短接到 RX(IO19)，或者由对端自动回应
//
//  流程：
//    1. 发送 ECHO_REQ 包（负载 = 4 字节魔术数）
//    2. 在 timeoutMs 内循环调用 loop() 等待 ECHO_RSP
//    3. 返回是否在超时前收到正确应答
// ─────────────────────────────────────────────────────────────────────────────
bool SerialProtocol::echoTest(uint32_t timeoutMs) {
    const uint8_t magic[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

    _echoReceived = false;
    bool sent = sendPacket(PKT_TYPE_ECHO_REQ, magic, sizeof(magic));
    if (!sent) {
        Serial.println("[ESPU] echoTest: sendPacket failed");
        return false;
    }
    Serial.printf("[ESPU] echoTest: waiting %ums for RSP\n", timeoutMs);

    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        loop();
        if (_echoReceived) {
            
            return true;
        }
        delay(1);
    }
    
    return false;
}