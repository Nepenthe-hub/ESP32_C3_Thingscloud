#include "ConfigManager.h"

static String _genDeviceId() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char buf[16];
    snprintf(buf, sizeof(buf), "ESP-%02X%02X%02X", mac[3], mac[4], mac[5]);
    return String(buf);
}

void ConfigManager::begin() {
    _prefs.begin(NS, false);
    Serial.println("[CFG] begin() called");
}

// 🔧 token 参数新增：若传空字符串则保留 NVS 中原有 token 不覆盖
void ConfigManager::save(const String& ssid, const String& pwd,
                          const String& room, const String& token) {
    _prefs.putString("ssid", ssid);
    _prefs.putString("pwd",  pwd);
    _prefs.putString("room", room);
    if (!token.isEmpty()) {
        _prefs.putString("token", token);
    }
    _prefs.putBool("valid", true);
    Serial.println("[CFG] saved");
}

DeviceCfg ConfigManager::load() {
    DeviceCfg cfg;
    cfg.ssid      = _prefs.getString("ssid",  "");
    cfg.password  = _prefs.getString("pwd",   "");
    cfg.roomNo    = _prefs.getString("room",  "");
    cfg.mqttToken = _prefs.getString("token", "");
    cfg.deviceId  = _genDeviceId();
    cfg.mqttHost  = "175.24.153.243";
    cfg.mqttPort  = 1883;
    cfg.valid     = _prefs.getBool("valid", false);
    return cfg;
}

void ConfigManager::clear() {
    _prefs.clear();
}