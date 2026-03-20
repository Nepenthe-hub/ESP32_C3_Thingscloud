#include "OtaManager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>  // 改用 HTTPS 客户端
#include <Update.h>
#include <ArduinoJson.h>

void OtaManager::_setState(OtaState s, const String& info) {
    if (_stateCb) _stateCb(s, info);
}

// ─────────────────────────────────────────────
//  checkAndUpdate
//  从 versionUrl 下载 version.json，对比版本号，需要则升级
//
//  version.json 格式：
//  {
//    "version": "1.0.2",
//    "url": "https://github.com/.../firmware.bin"
//  }
// ─────────────────────────────────────────────
void OtaManager::checkAndUpdate(const String& versionUrl) {
    _setState(OtaState::CHECKING, "");
    Serial.printf("[OTA] Checking version at: %s\n", versionUrl.c_str());

    WiFiClientSecure secClient;
    secClient.setInsecure();  // 跳过证书验证，省去维护根证书的麻烦

    HTTPClient http;
    http.begin(secClient, versionUrl);
    http.setTimeout(8000);
    // GitHub 会 302 跳转到 CDN，必须允许跟随跳转
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[OTA] Version check failed, HTTP code: %d\n", code);
        http.end();
        _setState(OtaState::FAILED, "http_" + String(code));
        return;
    }

    String body = http.getString();
    http.end();

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

    if (remoteVer == String(FIRMWARE_VERSION)) {
        Serial.println("[OTA] Already up to date");
        _setState(OtaState::UP_TO_DATE, remoteVer);
        return;
    }

    Serial.printf("[OTA] New version found: %s, starting update...\n",
                  remoteVer.c_str());
    _doUpdate(binUrl);
}

// ─────────────────────────────────────────────
//  performUpdate：MQTT 下发时直接用 URL 升级
// ─────────────────────────────────────────────
void OtaManager::performUpdate(const String& binUrl) {
    Serial.printf("[OTA] Force update from: %s\n", binUrl.c_str());
    _doUpdate(binUrl);
}

// ─────────────────────────────────────────────
//  _doUpdate（核心）
//  HTTPS 流式下载 .bin，写入备用 OTA 分区，校验后重启
// ─────────────────────────────────────────────
bool OtaManager::_doUpdate(const String& binUrl) {
    _setState(OtaState::DOWNLOADING, binUrl);

    WiFiClientSecure secClient;
    secClient.setInsecure();  // 跳过证书验证

    HTTPClient http;
    http.begin(secClient, binUrl);
    http.setTimeout(60000);
    // GitHub releases 会跳转到 objects.githubusercontent.com，必须跟随
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[OTA] Download failed, HTTP code: %d\n", code);
        http.end();
        _setState(OtaState::FAILED, "http_" + String(code));
        return false;
    }

    int totalSize = http.getSize();
    Serial.printf("[OTA] Firmware size: %d bytes\n", totalSize);

    if (!Update.begin(totalSize > 0 ? totalSize : UPDATE_SIZE_UNKNOWN)) {
        String errMsg = Update.errorString();
        Serial.printf("[OTA] Update.begin failed: %s\n", errMsg.c_str());
        http.end();
        _setState(OtaState::FAILED, errMsg);
        return false;
    }

    Update.onProgress([this](size_t done, size_t total) {
        Serial.printf("[OTA] Progress: %u / %u bytes (%.1f%%)\n",
                      done, total, total > 0 ? 100.0f * done / total : 0);
        if (_progressCb) _progressCb((int)done, (int)total);
    });

    _setState(OtaState::APPLYING, "");
    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();

    Serial.printf("[OTA] Written: %u bytes\n", written);

    if (!Update.end(true)) {
        String errMsg = Update.errorString();
        Serial.printf("[OTA] Update.end failed: %s\n", errMsg.c_str());
        _setState(OtaState::FAILED, errMsg);
        return false;
    }

    Serial.println("[OTA] Update successful! Rebooting in 2s...");
    _setState(OtaState::SUCCESS, "");
    delay(2000);
    ESP.restart();
    return true;
}