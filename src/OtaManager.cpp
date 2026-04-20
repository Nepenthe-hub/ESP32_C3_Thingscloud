#include "OtaManager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>

void OtaManager::_setState(OtaState s, const String& info) {
    if (_stateCb) _stateCb(s, info);
}

// ─────────────────────────────────────────────
//  checkAndUpdate
// ─────────────────────────────────────────────
void OtaManager::checkAndUpdate(const String& versionUrl) {
    _setState(OtaState::CHECKING, "");
    Serial.printf("[OTA] Checking version at: %s\n", versionUrl.c_str());

    WiFiClientSecure secClient;
    secClient.setInsecure();

    HTTPClient http;
    http.begin(secClient, versionUrl);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[OTA] Version check failed, HTTP code: %d\n", code);
        http.end();
        _setState(OtaState::FAILED, "http_" + String(code));
        Serial.println("[OTA] Retrying in 10s...");
        delay(10000);
        ESP.restart();
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
//  _httpsGet：手动处理跳转，避免两次 SSL 握手同时占内存
// ─────────────────────────────────────────────
int OtaManager::_httpsGet(WiFiClientSecure& client, HTTPClient& http,
                           const String& url) {
    http.begin(client, url);
    http.setTimeout(20000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

    int code = http.GET();
    Serial.printf("[OTA] HTTP %d for %s\n", code, url.c_str());

    if (code == 301 || code == 302) {
        String location = http.getLocation();
        http.end();
        Serial.printf("[OTA] Redirect -> %s\n", location.c_str());
        http.begin(client, location);
        http.setTimeout(60000);
        code = http.GET();
        Serial.printf("[OTA] HTTP %d after redirect\n", code);
    }

    return code;
}

// ─────────────────────────────────────────────
//  _doUpdate（核心）
//  失败时自动等 10 秒重启重试，不需要手动按 RST
// ─────────────────────────────────────────────
bool OtaManager::_doUpdate(const String& binUrl) {
    _setState(OtaState::DOWNLOADING, binUrl);

    WiFiClientSecure secClient;
    secClient.setInsecure();

    HTTPClient http;
    int code = _httpsGet(secClient, http, binUrl);

    if (code != 200) {
        Serial.printf("[OTA] Download failed, HTTP code: %d\n", code);
        http.end();
        _setState(OtaState::FAILED, "http_" + String(code));
        Serial.println("[OTA] Retrying in 10s...");
        delay(10000);
        ESP.restart();
        return false;
    }

    int totalSize = http.getSize();
    Serial.printf("[OTA] Firmware size: %d bytes\n", totalSize);

    // totalSize == -1 说明服务器返回的不是固件，自动重试
    if (totalSize == -1) {
        Serial.println("[OTA] Invalid firmware size, server not ready yet.");
        http.end();
        _setState(OtaState::FAILED, "invalid_size");
        Serial.println("[OTA] Retrying in 10s...");
        delay(10000);
        ESP.restart();
        return false;
    }

    if (!Update.begin(totalSize)) {
        String errMsg = Update.errorString();
        Serial.printf("[OTA] Update.begin failed: %s\n", errMsg.c_str());
        http.end();
        _setState(OtaState::FAILED, errMsg);
        Serial.println("[OTA] Retrying in 10s...");
        delay(10000);
        ESP.restart();
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
        Serial.println("[OTA] Retrying in 10s...");
        delay(10000);
        ESP.restart();
        return false;
    }

    Serial.println("[OTA] Update successful! Rebooting in 2s...");
    _setState(OtaState::SUCCESS, "");
    delay(2000);
    ESP.restart();
    return true;
}