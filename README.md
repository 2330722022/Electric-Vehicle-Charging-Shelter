# 电动车充电棚智能监测系统

基于 RT-Thread 和 STM32F407 的智能充电棚环境监测与安全管理系统

## 项目简介

本项目实现了一个基于嵌入式系统的电动车充电棚智能监测解决方案，集成了温湿度监测、光照检测、烟雾火焰检测、一氧化碳检测、倾斜监测、舵机控制、LCD 显示和云端上传等功能。使用 RT-Thread 操作系统管理多任务调度，保障充电棚安全运行。

## 仓库分支说明

| 分支 | 说明 |
|------|------|
| `zigbee代码` | Zigbee 无线通信模块代码 |
| `rt-thread代码` | RT-Thread 主控端代码（本分支）— STM32F407 环境监测与控制系统 |

## 硬件平台

- **主控芯片**: STM32F407ZGT6
- **无线通信**: RW007 (SPI WiFi 模块)
- **屏幕**: ST7789 240x240 LCD
- **传感器**:
  - AHT10 - 温湿度传感器 (I2C3)
  - AP3216C - 光照 + 接近传感器 (I2C2)
  - ICM20608 - 六轴加速度计+陀螺仪 (I2C2)
  - MQ-2 / 火焰传感器 - 通过 CC2530 ZigBee 无线采集
  - PMS7003 - PM2.5/PM10 颗粒物传感器 (通过 CC2530 ZigBee)
- **执行器**:
  - 舵机  - 货架角度控制 (PWM2 CH4)
  - 蜂鸣器 - 声音报警
  - 风扇  - 通风散热 (PE2 + YS-1 继电器模块)
  - LED   - 灯光控制

## 功能特性

### OS多线程调度 (RT-Thread)
| 优先级 | 线程 | 功能 |
|--------|------|------|
| 8 | logic | 逻辑处理 — 告警事件→蜂鸣器+舵机+风扇关停 |
| 10 | mqtt0 | PAHO MQTT 内部线程 |
| 12 | cc2530_rx | CC2530 ZigBee 串口接收 |
| 15 | onenet | OneNET 数据上传 + 下行命令消费 |
| 18 | web | HTTP Web Server |
| 20 | LVGL | LCD 显示渲染 |
| 22 | sensor | 传感器采集 (5秒周期) |
| 23 | http* | HTTP 异步处理 (按需创建) |

### OS同步原语使用
- **互斥量 (Mutex)**: `data_lock` 保护全局状态 `g_app_state`，防止多线程数据竞争
- **信号量 (Semaphore)**: `net_ready_sem` 同步WiFi就绪状态，HTTP/OneNET线程阻塞等待
- **事件集 (Event)**: `evt_alarm` 实现生产者-消费者模式（传感器→逻辑线程）

### 环境监测
- 实时监测充电棚温度、湿度、光照数据 (AHT10 + AP3216C)
- 烟雾浓度检测与火焰检测，预警火灾风险 (CC2530 ZigBee 无线采集)
- PM2.5/PM1.0/PM10 颗粒物浓度监测 (PMS7003, 通过 CC2530 ZigBee)
- 振动检测 (ICM20608 陀螺仪)
- 倾斜检测已禁用（充电棚为固定安装，无倾倒场景）

### 舵机控制
- 基于 PWM 信号控制舵机角度（0°-180°），告警时自动旋转至90°位置

### 显示界面
- LVGL 图形界面，BSP单"LVGL"线程处理所有UI操作，线程安全
- 中文字库支持，传感器数据图标化展示

### 云端通信 (OneNET)
- MQTT 协议上传环境数据 (30秒周期) + 告警数据 (状态变化触发)
- 物模型属性：temperature, humidity, light, pm2_5, mq2, flame, alarm_state, alarm_type, beep, temp_threshold, vibration, fan_en, fan_status, relay
- 下行命令控制：LED、BEEP、风扇开关(fan_en)、温度阈值设置

### 本地HTTP控制 (端口80)
- `GET /get_status` — 获取当前状态JSON（含风扇状态）
- `GET /api/data` — 全量传感器数据（温度/湿度/光照/MQ2/火焰/PM2.5）
- `GET /api/set?type=threshold&val=40.0` — 设置温度阈值 (范围 20.0~80.0°C)
- `GET /api/set?type=beep&val=1` — 控制蜂鸣器
- `GET /api/set?type=fan&val=1` — 控制风扇开关 (1=开启, 0=关闭)
- `GET /api/set?type=smoke&val=2000` — 设置烟雾阈值
- `GET /api/set?type=flame&val=500` — 设置火焰阈值
- `GET /api/set?type=pm25&val=150` — 设置 PM2.5 阈值

### 风扇通风控制策略
- **手动模式**: 云端下发 `fan_en=true` 或 HTTP `type=fan&val=1`，无条件开启风扇
- **自动通风**: 当温度超过「阈值 - 5°C」且未触发告警时，自动开启风扇通风散热
  - 默认阈值 80°C → 超过 75°C 时自动通风
  - 演示时将阈值调低至 30°C，室温即可触发自动通风
- **消防保护**: 检测到烟雾或火焰告警时，**强制关闭风扇**（防止助燃，逻辑线程立即关停）
- 风扇状态 `fan_status` 实时上报到 OneNET 云端供 APP 显示

### 报警功能
- 温度过高报警 (阈值可通过 APP / 按键 / 云端远程设置，默认 80°C)
- 烟雾/火焰火警报警（通过 CC2530 ZigBee 无线采集）
- PM2.5 超标报警
- 振动检测报警（ICM20608 陀螺仪）已禁用（充电棚固定安装场景）
- 蜂鸣器声音提示 + 舵机动作 + LCD 视觉报警 + 风扇自动关停

## 软件架构

详细的架构分析请查看 [ARCHITECTURE_ANALYSIS.md](ARCHITECTURE_ANALYSIS.md)

```
applications/
├── main.c              # 主程序入口 — OS对象创建 + 线程启动
├── app_logic.c/h       # 业务逻辑中心 — 互斥量临界区保护
├── app_sensor.c/h      # 传感器采集模块 — 低优先级线程 + 事件集告警
├── app_net.c/h         # 网络通信模块 — 信号量同步 + ISR安全命令处理
├── CO_specific.c       # 一氧化碳传感器检测
├── smoke_fire.c        # 烟雾火焰传感器检测
├── smart_ev_log.c      # 智能充电棚日志记录
├── lv_port_disp.c      # LVGL 显示驱动适配
├── my_font_cn_16.c     # 中文字库
├── Alarm.c             # 报警图标
├── Environmental.c     # 温度图标
├── Megaphone.c         # 蜂鸣器图标
├── connected.c         # WiFi 连接图标
├── light.c             # 光照图标
├── onenet_upload.c     # 上传图标
├── tilt.c              # 倾斜图标
└── waterprof.c         # 湿度图标
```

## 报警阈值
- 温度报警: 默认 80°C (HTTP 本地可调 20~80°C，OneNET 云端可调 20~50°C)
- 烟雾浓度报警: 超过预设阈值触发 (默认 MQ2 > 1500)
- 火焰报警: 低于预设阈值触发 (默认 Flame < 800)
- PM2.5 报警: 超过预设阈值 (默认 200μg/m³)
- 倾斜检测: 已禁用（充电棚固定安装场景）

## 编译与烧录

使用 RT-Thread Studio IDE 开发：

1. 用 RT-Thread Studio 打开本项目
2. 项目右键 → Incremental Build 编译
3. 下载到开发板

## 配套Android APP

路径: [apk/onenet2.1.5-app-debug.apk](apk/onenet2.1.5-app-debug.apk)

功能:
- 实时查看温度、湿度、光照、PM2.5、烟雾、火焰数据
- 远程设置温度阈值
- 远程控制蜂鸣器、LED、风扇
- 接收火警/告警推送
- OneNET 物模型属性查看

## 目录结构

```
onenet/
├── applications/          # 应用代码
├── apk/                  # 配套Android APP
├── packages/             # RT-Thread 软件包
│   ├── aht10-latest/     # AHT10 驱动
│   ├── ap3216c-latest/   # AP3216C 驱动
│   ├── icm20608-latest/  # ICM20608 驱动
│   ├── onenet-latest/    # OneNET MQTT 接入
│   └── pahomqtt-latest/  # PAHO MQTT客户端
├── rtconfig.h            # RT-Thread 配置
└── README.md             # 项目说明文档

## GPIO 引脚分配

| 功能 | 引脚 | 模式 | 说明 |
|------|------|------|------|
| 蜂鸣器 (BEEP) | PB0 | 推挽输出 | 高电平响 |
| 风扇 (FAN) | **PE2** | 推挽输出 | 高电平开，经 YS-1 继电器驱动 |
| 舵机 (SERVO) | PWM2 CH4 | AF_PP | 20ms 周期 PWM |
| WK_UP 按键 | PC5 | 输入上拉 | 增加温度阈值 |
| DOWN 按键 | PC4 | 输入上拉 | 减少温度阈值 |
| LED | PC0~PC3 | 推挽输出 | 状态指示灯 |
| WiFi (RW007) | SPI2 | AF_PP | WiFi 通信 |
| LCD (ST7789) | SPI1 | AF_PP | 240x240 显示 |
| ZigBee (CC2530) | USART2 | AF_PP | 串口接收传感器数据 |

## 风扇硬件接线 (PE2 + YS-1 继电器)

```
            4脚控制端侧                   3口输出端侧
   ┌─────────────────────┐     ┌─────────────────────┐
   │  VCC  →  开发板 5V   │     │  NC   →  不接        │
   │  GND  →  开发板 GND  │     │  COM  →  开发板 5V   │
   │  IN   →  PE2        │     │  NO   →  风扇红线(+) │
   │  (第4脚) → 不接      │     └─────────────────────┘
   └─────────────────────┘

   风扇黑线(-) → 开发板 GND（直接接，不经继电器）
```

> **注意**: YS-1 继电器模块不同批次丝印排列可能不同。若风扇不转，在继电器吸合状态下用风扇红线依次碰触 3 个输出端子，能转的那个就是真正的 NO 端。

## 截图预览

LCD 显示界面包含以下区域：
- **顶部**: WiFi状态图标、OneNET上传图标、中文标题"充电棚环境监测"
- **中部**: 温度、湿度、光照、PM2.5 数据展示
- **底部**: 阈值显示、报警状态、蜂鸣器图标

## 作者

电动车充电棚智能监测系统开发

## 致谢

- RT-Thread 操作系统 & 社区
- OneNET 物联网平台
- LVGL 图形库
- PAHO MQTT C/C++ client library
