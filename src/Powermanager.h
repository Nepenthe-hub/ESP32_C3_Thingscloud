#pragma once
#include <Arduino.h>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
//  引脚定义（对应原理图 ESP32-C3-WROOM-02-H4）
//
//  PIN_EN      = GPIO5 (IO5)   OUTPUT
//    HIGH → Q4(SS8050) 导通 → Q5(AO3401) 门极拉低 → V-BATOUT 供电（自锁）
//    LOW  → Q4 截止 → R29(10k) 把 Q5 门极拉至 V-BATIN → Q5 截止 → 断电
//
//  PIN_KEY_DET = GPIO4 (IO4)   INPUT_PULLUP
//    LOW  → 按键按下（经 R31/U6 光耦电路拉低）
//    HIGH → 按键未按
//    原理图备注："点按开机，关机由软件查看 KEY_DET 决定"
// ─────────────────────────────────────────────────────────────────────────────
#define PIN_EN          5       // GPIO5：电源自锁控制
#define PIN_KEY_DET     4       // GPIO4：按键检测（低电平有效）

#define PWR_LONG_PRESS_MS   3000UL  // 长按多少毫秒触发关机
#define PWR_DEBOUNCE_MS     50UL    // 防抖时间
#define PWR_BOOT_LOCK_MS    2000UL  // 启动后多少毫秒内忽略按键（防误触）

enum class KeyEvent {
    SHORT_PRESS,    // 短按释放
    LONG_PRESS,     // 长按 3s → 自动触发关机
};

class PowerManager {
public:
    using ShutdownCallback = std::function<void()>;
    using KeyCallback      = std::function<void(KeyEvent)>;

    // 必须在 setup() 第一行之后立即调用，立即自锁供电
    void begin();

    // 放入 Arduino loop() 最前面
    void loop();

    // 回调注册
    void onShutdown(ShutdownCallback cb) { _shutdownCb = cb; }
    void onKey(KeyCallback cb)           { _keyCb      = cb; }

    // 主动关机（供 MQTT 命令、低电量保护等调用）
    // beforeCutMs：等待时间，让最后的 MQTT/Serial 数据发出去
    void shutdown(uint32_t beforeCutMs = 500);

    bool isKeyDown() const { return _keyDown; }

private:
    ShutdownCallback _shutdownCb;
    KeyCallback      _keyCb;

    bool     _keyDown        = false;
    bool     _lastRaw        = HIGH;
    uint32_t _pressStart     = 0;
    bool     _longHandled    = false;
    uint32_t _bootLockUntil  = 0;   // 启动锁定期截止时间

    void _cutPower();
};