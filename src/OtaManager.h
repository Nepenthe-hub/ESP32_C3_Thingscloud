#pragma once
#include <Arduino.h>
#include <functional>

// 升级状态枚举，外部可以根据这个状态做UI反馈或MQTT上报
enum class OtaState {
    IDLE,         // 空闲，没有升级任务
    CHECKING,     // 正在从服务器检查版本
    DOWNLOADING,  // 正在下载固件
    APPLYING,     // 写入Flash中（通常很快，几乎感知不到）
    SUCCESS,      // 升级成功，即将重启
    FAILED,       // 升级失败
    UP_TO_DATE    // 检查完毕，已是最新版本
};

// 进度回调：当前字节数、总字节数
using OtaProgressCb = std::function<void(int current, int total)>;
// 状态变化回调
using OtaStateCb    = std::function<void(OtaState state, const String& info)>;

class OtaManager {
public:
    // 注册回调
    void onProgress(OtaProgressCb cb) { _progressCb = cb; }
    void onState(OtaStateCb cb)       { _stateCb    = cb; }

    // 主动检查版本并在需要时升级
    // versionUrl: 指向 version.json 的 HTTP 地址
    // 例如 "http://192.168.1.100:8080/version.json"
    void checkAndUpdate(const String& versionUrl);

    // 直接用已知的固件URL升级（MQTT下发时使用）
    // binUrl: 指向 firmware.bin 的 HTTP 地址
    // 例如 "http://192.168.1.100:8080/firmware.bin"
    void performUpdate(const String& binUrl);

    // 获取当前烧录的固件版本（来自编译时注入的宏）
    static const char* currentVersion() { return FIRMWARE_VERSION; }

private:
    OtaProgressCb _progressCb;
    OtaStateCb    _stateCb;

    void _setState(OtaState s, const String& info = "");
    // 实际执行下载+写Flash的函数，被上面两个public函数共同调用
    bool _doUpdate(const String& binUrl);
};