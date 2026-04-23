#include "PowerManager.h"
#include <esp_sleep.h>

// ─────────────────────────────────────────────────────────────────────────────
//  begin：在 Serial.begin() 之后、其他所有初始化之前调用
//
//  ① 立即拉高 EN(GPIO5) → Q4 导通 → Q5 持续导通 → 自锁供电
//     若延后执行，用户松开按键 → Q4/Q5 截止 → 断电 → 死循环重启
//
//  ② KEY_DET(GPIO4) 配置为 INPUT_PULLUP
//
//  ③ 设置启动锁定期 2 秒：
//     上电时 GPIO4 还未初始化，光耦电路会产生电平波动
//     锁定期内忽略所有按键事件，防止误触发 SHORT_PRESS
// ─────────────────────────────────────────────────────────────────────────────
void PowerManager::begin() {
    // ① 自锁——最高优先级
    pinMode(PIN_EN, OUTPUT);
    digitalWrite(PIN_EN, HIGH);
    Serial.println("[PWR] EN(GPIO5)=HIGH  power self-latched");

    // ② 按键检测引脚
    pinMode(PIN_KEY_DET, INPUT_PULLUP);

    // ③ 启动锁定期，忽略上电瞬间的电平抖动
    _bootLockUntil = millis() + PWR_BOOT_LOCK_MS;
    Serial.printf("[PWR] KEY_DET(GPIO4) ready, boot lock %lums\n", PWR_BOOT_LOCK_MS);
}

// ─────────────────────────────────────────────────────────────────────────────
//  loop：放入 Arduino loop() 最前面
//  带防抖的边沿检测：短按事件 + 长按自动关机
// ─────────────────────────────────────────────────────────────────────────────
void PowerManager::loop() {
    bool raw = digitalRead(PIN_KEY_DET);

    // ── 启动锁定期内：同步 _lastRaw 状态，但不处理任何事件 ──────────────────
    // 同步是为了让锁定期结束后第一次读到的状态是正确基准，不会误判边沿
    if (millis() < _bootLockUntil) {
        _lastRaw = raw;
        return;
    }

    // ── 下降沿：按键按下 ──────────────────────────────────────────────────────
    if (raw == LOW && _lastRaw == HIGH) {
        uint32_t now = millis();
        if (now - _pressStart > PWR_DEBOUNCE_MS) {
            _pressStart  = now;
            _keyDown     = true;
            _longHandled = false;
            Serial.println("[PWR] Key DOWN");
        }
    }

    // ── 上升沿：按键释放 ──────────────────────────────────────────────────────
    if (raw == HIGH && _lastRaw == LOW) {
        _keyDown = false;
        uint32_t held = millis() - _pressStart;
        Serial.printf("[PWR] Key UP  held=%lums\n", held);

        // 短按：未触发长按逻辑，且持续时间超过防抖阈值
        if (!_longHandled && held >= PWR_DEBOUNCE_MS) {
            Serial.println("[PWR] -> SHORT_PRESS");
            if (_keyCb) _keyCb(KeyEvent::SHORT_PRESS);
        }
    }

    // ── 长按检测：持续按住超过阈值 → 关机 ────────────────────────────────────
    if (_keyDown && !_longHandled) {
        if (millis() - _pressStart >= PWR_LONG_PRESS_MS) {
            _longHandled = true;
            Serial.printf("[PWR] -> LONG_PRESS (%lus) → shutdown\n",
                          PWR_LONG_PRESS_MS / 1000);
            if (_keyCb) _keyCb(KeyEvent::LONG_PRESS);
            shutdown(300);
        }
    }

    _lastRaw = raw;
}

// ─────────────────────────────────────────────────────────────────────────────
//  shutdown：对外接口，供 MQTT 命令、低电量保护等调用
// ─────────────────────────────────────────────────────────────────────────────
void PowerManager::shutdown(uint32_t beforeCutMs) {
    Serial.printf("[PWR] Shutdown initiated (delay=%lums)\n", beforeCutMs);

    if (_shutdownCb) {
        Serial.println("[PWR] Running shutdown callback...");
        _shutdownCb();
    }

    if (beforeCutMs > 0) delay(beforeCutMs);

    _cutPower();
}

// ─────────────────────────────────────────────────────────────────────────────
//  _cutPower：断电序列
//
//  EN(GPIO5)=LOW → Q4 截止 → R29(10kΩ) 把 Q5 门极拉至 V-BATIN
//               → Q5(AO3401 P-MOS) 截止 → V-BATOUT 断电
//
//  调试时若外接 USB：EN=LOW 不会真正断电
//  → 调用 esp_deep_sleep_start() 兜底，功耗降至约 5µA
// ─────────────────────────────────────────────────────────────────────────────
void PowerManager::_cutPower() {
    Serial.println("[PWR] EN(GPIO5)=LOW  cutting power...");
    Serial.flush();
    delay(10);

    digitalWrite(PIN_EN, LOW);

    // 兜底：USB 调试时用深睡眠模拟断电
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_deep_sleep_start();

    while (true) { delay(1000); }  // 不可到达
}