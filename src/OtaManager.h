#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <functional>

// ── GitHub OTA 配置 ──────────────────────────────────────────
// 把下面的地址换成你自己的仓库，version.json 放在 main 分支根目录
#define GITHUB_VERSION_URL "https://gitee.com/vigorous-wu/ESP32_C3_Thingscloud/raw/main/firmware.bin"

// 每隔多久检查一次新版本（6小时）
#define OTA_CHECK_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL)

enum class OtaState {
    IDLE,
    CHECKING,
    DOWNLOADING,
    APPLYING,
    SUCCESS,
    FAILED,
    UP_TO_DATE
};

using OtaProgressCb = std::function<void(int current, int total)>;
using OtaStateCb    = std::function<void(OtaState state, const String& info)>;

class OtaManager {
public:
    void onProgress(OtaProgressCb cb) { _progressCb = cb; }
    void onState(OtaStateCb cb)       { _stateCb    = cb; }

    void checkAndUpdate(const String& versionUrl);
    void performUpdate(const String& binUrl);

    static const char* currentVersion() { return FIRMWARE_VERSION; }

private:
    OtaProgressCb _progressCb;
    OtaStateCb    _stateCb;

    void _setState(OtaState s, const String& info = "");
    bool _doUpdate(const String& binUrl);
    int  _httpsGet(WiFiClientSecure& client, HTTPClient& http, const String& url);
};