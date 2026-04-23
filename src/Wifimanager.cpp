#include "WifiManager.h"

void WifiManager::_setState(WifiState s) {
    if (_state == s) return;
    _state = s;
    if (_cb) _cb(s);
}

void WifiManager::begin(const String& ssid, const String& password) {
    _ssid     = ssid;
    _password = password;
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm); // 把发射功率降到极低
WiFi.begin(ssid.c_str(), password.c_str());
    WiFi.begin(ssid.c_str(), password.c_str());
    _connectStart = millis();
    _setState(WifiState::CONNECTING);
    Serial.printf("[WiFi] Connecting to %s\n", ssid.c_str());
}

void WifiManager::loop() {
    switch (_state) {
        case WifiState::CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[WiFi] Connected, IP: %s\n",
                              WiFi.localIP().toString().c_str());
                _setState(WifiState::CONNECTED);
            } else if (millis() - _connectStart > TIMEOUT_MS) {
                Serial.println("[WiFi] Timeout, FAILED");
                WiFi.disconnect();
                _setState(WifiState::FAILED);
            }
            break;

        case WifiState::CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WiFi] Lost connection, reconnecting...");
                WiFi.reconnect();
                _connectStart = millis();
                _setState(WifiState::CONNECTING);
            }
            break;

        default:
            break;
    }
}