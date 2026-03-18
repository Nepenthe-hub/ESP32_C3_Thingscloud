#pragma once
#include <Arduino.h>
#include <functional>

struct CfgMsg {
    String ssid;
    String pwd;
    String room;
};

class SerialProtocol {
public:
    using CfgCallback = std::function<void(const CfgMsg&)>;

    void begin(uint32_t baud = 115200);
    void loop();
    void sendAck(uint32_t seq, bool ok, const String& reason = "");
    void onCfg(CfgCallback cb) { _cfgCb = cb; }// 注册配置消息回调

private:
    String _buf;
    CfgCallback _cfgCb;
    void _dispatch(const String& line);
};