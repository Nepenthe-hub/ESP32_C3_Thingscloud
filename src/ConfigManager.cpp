#include "ConfigManager.h"

// 静态函数，用于生成全局唯一设备ID (相当于 STM32 的 UID/ChipID)
static String _genDeviceId() {
    uint8_t mac[6];
    // ESP32内部出厂烧录好了MAC地址，用来当作唯一的序列号
    esp_efuse_mac_get_default(mac);
    char buf[16];
    snprintf(buf, sizeof(buf), "ESP-%02X%02X%02X", mac[3], mac[4], mac[5]);
    return String(buf);
}

void ConfigManager::begin() {
    // 类似于挂载文件系统或者初始化STM32内部Flash读写接口 (Preferences是对NVS - Non Volatile Storage的封装)
    _prefs.begin(NS, false);
    Serial.println("[CFG] begin() called");
}

void ConfigManager::save(const String& ssid, const String& pwd, const String& room) {
    // 按键值对方式存储到内部Flash防掉电，有点类似STM32的Flash EEPROM emulation功能
    _prefs.putString("ssid", ssid);
    _prefs.putString("pwd",  pwd);
    _prefs.putString("room", room);
    _prefs.putBool("valid",  true);
    Serial.println("[CFG] saved");
}

DeviceCfg ConfigManager::load() {
    DeviceCfg cfg;
    // 从Flash中根据键名读取对应值，第二个参数是没找到时的默认值
    cfg.ssid     = _prefs.getString("ssid", "");
    cfg.password = _prefs.getString("pwd",  "");
    cfg.roomNo   = _prefs.getString("room", "");
    cfg.deviceId = _genDeviceId();
    cfg.mqttHost = "sh-1-mqtt.iot-api.com";
    cfg.mqttPort = 1883;
    cfg.valid    = _prefs.getBool("valid", false);
    return cfg;
}

void ConfigManager::clear() {
    // 擦除这一区域的数据
    _prefs.clear();
}