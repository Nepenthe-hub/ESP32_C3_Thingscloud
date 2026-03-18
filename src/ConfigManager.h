#pragma once
#include <Preferences.h>
#include <Arduino.h>

struct DeviceCfg {
    String ssid;
    String password;
    String roomNo;
    String deviceId;
    String mqttHost;
    uint16_t mqttPort;
    bool valid;
};

class ConfigManager {
public:
    void begin();
    void save(const String& ssid, const String& pwd, const String& room);
    DeviceCfg load();
    void clear();

private:
    Preferences _prefs;
    static constexpr const char* NS = "devcfg";
};