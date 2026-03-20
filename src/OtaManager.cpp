#include "OtaManager.h"
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>

// ─────────────────────────────────────────────
//  内部工具：改变状态并触发回调
// ─────────────────────────────────────────────
void OtaManager::_setState(OtaState s, const String& info) {
    if (_stateCb) _stateCb(s, info);
}

// ─────────────────────────────────────────────
//  checkAndUpdate
//  1. 从 versionUrl 下载 version.json
//  2. 对比版本号
//  3. 需要升级则调用 _doUpdate
//
//  version.json 格式：
//  {
//    "version": "1.0.1",
//    "url": "http://192.168.1.100:8080/firmware.bin"
//  }
// ─────────────────────────────────────────────
void OtaManager::checkAndUpdate(const String& versionUrl) {
    _setState(OtaState::CHECKING, "");
    Serial.printf("[OTA] Checking version at: %s\n", versionUrl.c_str());

    HTTPClient http;
    http.begin(versionUrl);
    http.setTimeout(8000);  // 8秒超时，防止服务器无响应时卡死
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[OTA] Version check failed, HTTP code: %d\n", code);
        http.end();
        _setState(OtaState::FAILED, "http_" + String(code));
        return;
    }

    String body = http.getString();
    http.end();

    // 解析 version.json
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[OTA] version.json parse error: %s\n", err.c_str());
        _setState(OtaState::FAILED, "json_parse_error");
        return;
    }

    String remoteVer = doc["version"] | "";
    String binUrl    = doc["url"]     | "";

    if (remoteVer.isEmpty() || binUrl.isEmpty()) {
        Serial.println("[OTA] version.json missing fields");
        _setState(OtaState::FAILED, "json_missing_fields");
        return;
    }

    Serial.printf("[OTA] Current: %s  Remote: %s\n",
                  FIRMWARE_VERSION, remoteVer.c_str());

    // 版本相同，无需升级
    if (remoteVer == String(FIRMWARE_VERSION)) {
        Serial.println("[OTA] Already up to date");
        _setState(OtaState::UP_TO_DATE, remoteVer);
        return;
    }

    // 版本不同，开始升级
    Serial.printf("[OTA] New version found: %s, starting update...\n",
                  remoteVer.c_str());
    _doUpdate(binUrl);
}

// ─────────────────────────────────────────────
//  performUpdate
//  MQTT 下发时直接提供 bin URL，跳过版本检查直接升级
// ─────────────────────────────────────────────
void OtaManager::performUpdate(const String& binUrl) {
    Serial.printf("[OTA] Force update from: %s\n", binUrl.c_str());
    _doUpdate(binUrl);
}

// ─────────────────────────────────────────────
//  _doUpdate（核心）
//  用 HTTPClient 流式下载 .bin，喂给 Update 库写 Flash
//  Update 库是 ESP32 Arduino 内置的，负责写到备用 OTA 分区
//  写完后调用 Update.end() 校验完整性，通过后重启切换分区
// ─────────────────────────────────────────────
bool OtaManager::_doUpdate(const String& binUrl) {
    _setState(OtaState::DOWNLOADING, binUrl);

    HTTPClient http;
    http.begin(binUrl);
    http.setTimeout(60000);  // 下载固件给60秒，视网速调整

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[OTA] Download failed, HTTP code: %d\n", code);
        http.end();
        _setState(OtaState::FAILED, "http_" + String(code));
        return false;
    }

    int totalSize = http.getSize();  // -1 表示 chunked，不知道总大小
    Serial.printf("[OTA] Firmware size: %d bytes\n", totalSize);

    // 告诉 Update 库准备写入，UPDATE_SIZE_UNKNOWN 兼容 chunked 传输
    if (!Update.begin(totalSize > 0 ? totalSize : UPDATE_SIZE_UNKNOWN)) {
        String errMsg = Update.errorString();
        Serial.printf("[OTA] Update.begin failed: %s\n", errMsg.c_str());
        http.end();
        _setState(OtaState::FAILED, errMsg);
        return false;
    }

    // 注册进度回调（Update 库每写一块就回调一次）
    Update.onProgress([this](size_t done, size_t total) {
        Serial.printf("[OTA] Progress: %u / %u bytes (%.1f%%)\n",
                      done, total, total > 0 ? 100.0f * done / total : 0);
        if (_progressCb) _progressCb((int)done, (int)total);
    });

    // 流式写入：从 HTTP 流中每次读一块写进 Flash，不占用大块 RAM
    _setState(OtaState::APPLYING, "");
    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();

    Serial.printf("[OTA] Written: %u bytes\n", written);

    // end() 会校验 MD5（如果服务端提供了的话），并标记新分区为启动分区
    if (!Update.end(true)) {
        String errMsg = Update.errorString();
        Serial.printf("[OTA] Update.end failed: %s\n", errMsg.c_str());
        _setState(OtaState::FAILED, errMsg);
        return false;
    }

    Serial.println("[OTA] Update successful! Rebooting in 2s...");
    _setState(OtaState::SUCCESS, "");
    delay(2000);
    ESP.restart();  // 重启后 bootloader 自动切换到新分区
    return true;    // 实际上不会执行到这里
}