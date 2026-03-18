#include "ConfigManager.h"


// 生成设备 ID，格式为 "ESP-XXXXXX"，其中 XXXXXX 是 MAC 地址的后三个字节的十六进制表示
static String _genDeviceId() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char buf[16];
    snprintf(buf, sizeof(buf), "ESP-%02X%02X%02X", mac[3], mac[4], mac[5]);
    return String(buf);
}
// 目前的实现只是框架，实际的存储逻辑还未完成
void ConfigManager::begin() {
    // 暂时注释掉 Preferences，先验证其他逻辑
    // _prefs.begin(NS, false);
    Serial.println("[CFG] begin() called");
}
// 保存配置，目前只是打印日志，实际的存储逻辑还未完成
void ConfigManager::save(const String& ssid, const String& pwd, const String& room) {
    Serial.println("[CFG] save() called");
}
// 加载配置，目前返回一个默认的 DeviceCfg 对象，实际的加载逻辑还未完成
DeviceCfg ConfigManager::load() {
    DeviceCfg cfg;
    cfg.ssid     = "";
    cfg.password = "";
    cfg.roomNo   = "";
    cfg.deviceId = _genDeviceId();
    cfg.mqttHost = "broker.emqx.io";
    cfg.mqttPort = 1883;
    cfg.valid    = false;
    return cfg;
}
// 清除配置，目前只是一个空函数，实际的清除逻辑还未完成
void ConfigManager::clear() {}