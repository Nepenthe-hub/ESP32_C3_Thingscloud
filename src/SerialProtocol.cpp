#include "SerialProtocol.h"
#include <ArduinoJson.h>

void SerialProtocol::begin(uint32_t baud) {
    Serial1.begin(baud, SERIAL_8N1, /*RX=*/6, /*TX=*/7);
}
// 解析串口输入的 JSON 消息，支持多行输入，遇到换行符时触发一次解析
void SerialProtocol::loop() {
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n') {
            _buf.trim();// 去掉首尾空白字符，避免解析错误
            if (_buf.length() > 0) _dispatch(_buf);
            _buf = "";// 处理完一行后清空缓冲区，准备接收下一行
        } else {
            _buf += c;
        }
    }
}
//dispatch:分发调度
/*
    这个函数的作用是根据输入的JSON字符串解析出消息类型和内容，并调用相应的回调函数来处理这些消息。
    它首先尝试将输入字符串解析成一个JSON对象，如果解析失败就打印错误信息并返回。
    然后它从JSON对象中读取"type"字段来判断消息类型，目前只处理"type"为"cfg"的配置消息。
    如果是配置消息，就从"payload"字段中读取WiFi SSID、密码和房间号等信息，构造一个CfgMsg结构体，并调用之前注册的配置回调函数来处理这个配置。
    最后根据处理结果发送一个ACK响应给发送方，告知操作是否成功以及失败原因（如果有的话）。

*/
void SerialProtocol::_dispatch(const String& line) {
    JsonDocument doc;// ArduinoJson库的核心数据结构，用来存储解析后的JSON对象
    DeserializationError err = deserializeJson(doc, line);// 解析输入的JSON字符串，如果格式错误会返回错误信息
    if (err) {
        Serial.printf("[Serial] JSON parse error: %s\n", err.c_str());
        return;
    }
    String type = doc["type"] | "";// 从JSON对象中读取"type"字段，默认值为空字符串，如果没有这个字段或者不是字符串类型的话，就会得到默认值
    uint32_t seq = doc["seq"] | 0;// 从JSON对象中读取"seq"字段，默认值为0，类似上面type的处理方式
    // 根据"type"字段的值来决定如何处理这条消息，目前只处理"type"为"cfg"的配置消息
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
/*
    sendAck:发送应答消息
    seq:消息序列号，用于关联请求和响应
    ok:表示操作是否成功
    reason:如果操作失败，可以提供一个失败原因的字符串
这个函数会构造一个JSON对象，包含"type"为"ack"，"seq"为传入的序列号，以及一个"payload"对象，其中包含"result"字段表示成功或失败，如果有失败原因还会包含"reason"字段。最后将这个JSON对象序列化成字符串并通过Serial1发送出去。
*/
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