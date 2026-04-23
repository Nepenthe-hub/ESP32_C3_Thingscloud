#include "ConfigManager.h"

void ConfigManager::begin() {
    _prefs.begin(NS, false);
}

void ConfigManager::save(const String& ssid, const String& pwd,
                         const String& room, const String& token) {
    _prefs.begin(NS, false);
    _prefs.putString("ssid",  ssid);
    _prefs.putString("pwd",   pwd);
    _prefs.putString("room",  room);
    _prefs.putString("token", token);
    _prefs.putBool("valid", true);
    _prefs.end();
}

DeviceCfg ConfigManager::load() {
    DeviceCfg cfg;
    _prefs.begin(NS, true);
    cfg.valid     = _prefs.getBool("valid", false);
    cfg.ssid      = _prefs.getString("ssid",  "");
    cfg.password  = _prefs.getString("pwd",   "");
    cfg.roomNo    = _prefs.getString("room",  "");
    cfg.mqttToken = _prefs.getString("token", "");
    cfg.deviceId  = "";
    cfg.mqttHost  = "";
    cfg.mqttPort  = 1883;
    _prefs.end();
    return cfg;
}

void ConfigManager::clear() {
    _prefs.begin(NS, false);
    _prefs.clear();
    _prefs.end();
}