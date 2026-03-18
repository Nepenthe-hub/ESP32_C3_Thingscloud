#pragma once
#include <WiFi.h>
#include <functional>

enum class WifiState { IDLE, CONNECTING, CONNECTED, FAILED };

class WifiManager {
public:
    using StateCallback = std::function<void(WifiState)>;

    void begin(const String& ssid, const String& password);
    void loop();                     // 放进 Arduino loop()
    WifiState state() const { return _state; }
    void onStateChange(StateCallback cb) { _cb = cb; }

private:
    String _ssid, _password;
    WifiState _state = WifiState::IDLE;
    StateCallback _cb;
    uint32_t _connectStart = 0;
    static constexpr uint32_t TIMEOUT_MS = 15000;

    void _setState(WifiState s);
};