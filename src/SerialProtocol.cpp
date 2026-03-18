#include "SerialProtocol.h"
#include <ArduinoJson.h>

void SerialProtocol::begin(uint32_t baud) {
    Serial1.begin(baud, SERIAL_8N1, /*RX=*/6, /*TX=*/7);
}
void SerialProtocol::loop() {
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n') {
            _buf.trim();
            if (_buf.length() > 0) _dispatch(_buf);
            _buf = "";
        } else {
            _buf += c;
        }
    }
}

void SerialProtocol::_dispatch(const String& line) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        Serial.printf("[Serial] JSON parse error: %s\n", err.c_str());
        return;
    }
    String type = doc["type"] | "";
    uint32_t seq = doc["seq"] | 0;

    if (type == "cfg" && _cfgCb) {
        CfgMsg msg;
        msg.ssid = doc["payload"]["ssid"] | "";
        msg.pwd  = doc["payload"]["pwd"]  | "";
        msg.room = doc["payload"]["room"] | "";
        if (msg.ssid.isEmpty()) {
            sendAck(seq, false, "ssid_empty");
            return;
        }
        _cfgCb(msg);
        sendAck(seq, true, "");
    }
}

void SerialProtocol::sendAck(uint32_t seq, bool ok, const String& reason) {
    JsonDocument doc;
    doc["type"]              = "ack";
    doc["seq"]               = seq;
    doc["payload"]["result"] = ok ? "ok" : "fail";
    if (!reason.isEmpty()) doc["payload"]["reason"] = reason;
    String out;
    serializeJson(doc, out);
    Serial1.println(out);
}