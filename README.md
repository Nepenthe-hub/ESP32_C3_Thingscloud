# ESP32-C3 ThingsCloud 项目文档

**双主控协同架构 · 阶段一：配网 + MQTT 上报**  
**版本 v0.1.0 | 2025**

## 1. 项目概述
本项目基于 ESP32-C3 + STM32 双主控协同架构，ESP32-C3 作为网络与智能中枢，负责 Wi-Fi 管理、MQTT 上下行通信及后续 AI 音频处理；STM32 作为硬件执行主控，负责传感器采集、继电器/风机控制及串口屏驱动。

**阶段一目标**：打通 ESP32-C3 的配网流程与 ThingsCloud MQTT 上报链路。

## 2. 硬件环境

### 2.1 开发板
| 项目 | 参数 |
| :--- | :--- |
| 开发板型号 | ESP32-C3-DevKitM-1 |
| 框架 | Arduino (PlatformIO) |
| Flash | 4MB |
| USB 调试口 | COM4 (USB CDC) |
| 与 STM32 通信串口 | Serial1，GPIO6(RX) / GPIO7(TX) |

### 2.2 引脚说明
ESP32-C3 引脚使用注意事项：
- **GPIO18/GPIO19**：USB D-/D+，硬件占用，不可用作普通 IO
- **GPIO20/GPIO21**：与 USB 子系统绑定，不可用作 UART
- **GPIO6/GPIO7**：安全空闲引脚，用作 Serial1（与 STM32 通信）
- **GPIO4/GPIO5/GPIO8/GPIO10**：其他可用空闲引脚

### 2.3 STM32 接线
| ESP32-C3 | STM32 | 说明 |
| :--- | :--- | :--- |
| GPIO6 (RX) | TX 引脚 | 交叉连接 |
| GPIO7 (TX) | RX 引脚 | 交叉连接 |
| GND | GND | 必须共地 |

> **注意**：ESP32-C3 为 3.3V 电平。若 STM32 TX 输出为 5V，需加电平转换模块。

## 3. 软件架构

### 3.1 模块结构
| 文件 | 模块 | 职责 |
| :--- | :--- | :--- |
| `ConfigManager.h/.cpp` | 配置管理 | NVS Flash 读写 SSID/密码/房间号 |
| `WifiManager.h/.cpp` | Wi-Fi 管理 | 连接/重连状态机 |
| `MqttManager.h/.cpp` | MQTT 管理 | 连接/发布/订阅 ThingsCloud |
| `SerialProtocol.h/.cpp` | 串口协议 | 解析 cfg 消息，发 ack 回执 |
| `main.cpp` | 主程序 | 模块调度，业务逻辑编排 |

### 3.2 依赖库
| 库名 | 版本 | 用途 |
| :--- | :--- | :--- |
| knolleary/PubSubClient | ^2.8 | MQTT 客户端 |
| bblanchon/ArduinoJson | ^7.0 | JSON 解析与序列化 |

## 4. platformio.ini 配置
```ini
[env:esp32-c3-devkitm-1]
platform              = espressif32
board                 = esp32-c3-devkitm-1
framework             = arduino
monitor_speed         = 115200
monitor_filters       = send_on_enter
board_build.partitions = default.csv

lib_deps =
    knolleary/PubSubClient @ ^2.8
    bblanchon/ArduinoJson  @ ^7.0
```

## 5. MQTT 配置

| 参数 | 值 |
| :--- | :--- |
| Broker 地址 | `sh-1-mqtt.iot-api.com` |
| 端口 | 1883 |
| 上行主题 | `attributes` |
| 下行主题 | `attributes/push` |

### 5.1 ThingsCloud 属性定义
| 属性名称 | 标识符 | 数据类型 | 方向 |
| :--- | :--- | :--- | :--- |
| 电机速度 | speed | Number | 云端下发 |
| 电机方向 | dir | Number | 云端下发 |
| 当前操作员 | operator | Text | 云端下发 |
| WiFi 名称 | wifi_ssid | Text | 云端下发 |
| WiFi 密码 | wifi_pwd | Text | 云端下发 |
| 在线状态 | online | Boolean | 设备上报 |

## 6. ESP32-C3 与 STM32 串口协议

### 6.1 帧格式
采用简单定长文本帧，以 `\n` 作为帧结束符，波特率 `115200`。

**ESP32 → STM32（下行控制帧）**
```text
$CMD,<speed>,<dir>,<operator>\n
```
*示例：*
`$CMD,450,1,多巴胺\n`
`$CMD,-1,-1,\n`    （无对应字段时填 -1 或空）

**STM32 → ESP32（上行配置帧）**
```json
{"type":"cfg","seq":1,"payload":{"ssid":"WiFiName","pwd":"password","room":"301"}}\n
```

**ESP32 → STM32（ack 回执）**
```json
{"type":"ack","seq":1,"payload":{"result":"ok"}}\n
```

## 7. 启动流程

**首次上电（无配置）：**
1. 上电 → 读取 NVS → 无配置 → 等待串口下发 cfg 消息
2. 收到 cfg → 保存 SSID/密码/房间号到 NVS → 连接 Wi-Fi
3. Wi-Fi 连上 → 连接 MQTT Broker → 上报 `{"online":true}`

**断电重启（有配置）：**
1. 上电 → 读取 NVS → 有配置 → 自动连接 Wi-Fi
2. Wi-Fi 连上 → 连接 MQTT Broker → 上报在线

**云端下发 WiFi 配置：**
1. 收到 `attributes/push` 包含 `wifi_ssid` + `wifi_pwd`
2. 更新 NVS 配置 → 重新发起 Wi-Fi 连接

## 8. 调试说明

### 8.1 串口日志说明
| 日志前缀 | 含义 |
| :--- | :--- |
| `[Boot]` | 启动阶段日志 |
| `[CFG]` | 配置管理器操作 |
| `[WiFi]` | Wi-Fi 连接状态 |
| `[MQTT]` | MQTT 连接与消息 |
| `[Cfg]` | 收到串口配置消息 |
| `[CMD]` | 收到云端控制属性 |
| `[STM32] ->` | 发送给 STM32 的数据 |

### 8.2 手动测试配网
在串口调试助手（波特率 115200）发送以下 JSON（需以换行符结尾）：
```json
{"type":"cfg","seq":1,"payload":{"ssid":"WiFiName","pwd":"password","room":"301"}}
```
正常响应：
```json
{"type":"ack","seq":1,"payload":{"result":"ok"}}
```

### 8.3 常见问题
| 现象 | 原因 / 解决方法 |
| :--- | :--- |
| **boot loop 反复重启** | Serial1 使用了 GPIO20/21，改为 GPIO6/7 |
| **串口乱码** | 波特率不匹配，`monitor_speed` 设为 115200 |
| **MQTT DNS 失败** | `setServer` 使用了临时指针，改用成员变量 `_host.c_str()` |
| **NVS NOT_FOUND 报错** | 首次启动正常现象，表示 Flash 中无历史配置 |
| **MQTT rc=-2** | 网络未就绪时调用了 `begin()`，需在 WiFi 连接成功回调后再调用 |

## 9. 下一步计划
| 阶段 | 目标 | 内容 |
| :--- | :--- | :--- |
| **阶段二** | 控制闭环 | MQTT 下行 → ESP32 解析 → Serial1 发给 STM32 → ack 回执 → 上报云端 |
| **阶段三** | 断网策略 | 离线缓存操作事件，恢复连接后补报云端 |
| **阶段四** | AI/音频 | 音频采集链路完全在 ESP32-C3，STM32 只接收识别后的结构化命令 |
| **长期** | OTA 升级 | ESP32-C3 预留 HTTPS OTA 通道，支持远程固件更新 |
