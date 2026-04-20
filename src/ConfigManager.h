#pragma once
#include <Preferences.h>
#include <Arduino.h>

struct DeviceCfg {
    String ssid;
    String password;
    String roomNo;
    String deviceId;
    String mqttHost;
    String mqttToken;   // 🔧 新增：ThingsCloud Device Token，每块板子独立
    uint16_t mqttPort;
    bool valid;
};

class ConfigManager {
public:
    void begin();
    void save(const String& ssid, const String& pwd,
              const String& room, const String& token = "");
    DeviceCfg load();
    void clear();

private:
    Preferences _prefs;
    static constexpr const char* NS = "devcfg";
};