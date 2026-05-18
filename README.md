# 电动车充电棚环境监测系统 - ZigBee 部分

基于 TI Z-Stack 协议栈和 CC2530 芯片，实现充电棚环境传感器数据采集与 ZigBee 无线传输。

---

## 1. 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                        协调器 (Coordinator)                  │
│  CC2530 + 串口输出                                          │
│  • 建立 ZigBee 网络                                          │
│  • 汇聚两个终端数据                                          │
│  • CSV 格式输出到星火一号开发板                               │
│  • CSV格式: PM1.0,PM2.5,PM10,MQ2,Flame\r\n                  │
└─────────┬───────────────────────────────────┬───────────────┘
          │           ZigBee 2.4GHz            │
          │                                   │
┌─────────▼──────────┐              ┌─────────▼──────────┐
│    终端1 (EndDevice) │              │    终端2 (EndDevice) │
│  CC2530 + PMS7003   │              │  CC2530 + MQ2 + 火焰│
│  • PM1.0/PM2.5/PM10 │              │  • 可燃气体浓度      │
│  • UART接PMS7003    │              │  • 火焰强度检测      │
│  • LCD显示PM数据     │              │  • LCD显示传感器数据  │
└─────────────────────┘              └─────────────────────┘
```

---

## 2. 文件结构

```
Z-Stack透传/
├── README.md                     ← 本说明文件
├── Projects/zstack/
│   ├── Utilities/SerialApp/
│   │   ├── Source/
│   │   │   ├── SerialApp.c       ← 核心应用代码
│   │   │   ├── SerialApp.h       ← 传感器类型、引脚、事件定义
│   │   │   ├── SensorConfig.h    ← 传感器配置宏
│   │   │   ├── DebugUtils.h      ← 调试工具宏
│   │   │   └── SensorDataHandler.c ← 传感器数据处理
│   │   └── CC2530DB/
│   │       ├── SerialApp.ewp     ← IAR 工程文件
│   │       ├── SerialApp.ewd     ← IAR 调试配置
│   │       ├── CoordinatorEB-Pro/ ← 协调器编译输出
│   │       └── EndDeviceEB-Pro/   ← 终端编译输出
│   └── ZMain/TI2530DB/
│       └── ZMain.c               ← 系统入口
└── Components/
    ├── hal/                      ← 硬件抽象层 (ADC, UART, LCD, LED)
    ├── osal/                     ← 操作系统抽象层
    └── stack/                    ← ZigBee 协议栈
```

---

## 3. 支持的传感器

### 3.1 PMS7003 空气质量传感器（终端1）

| 参数 | 值 |
|------|-----|
| 协议 | UART 9600bps 8N1 无流控 |
| 帧格式 | 32字节二进制帧（帧头 `0x42 0x4D`） |
| 校验方式 | 前30字节累加和校验 |
| 数据字段 | PM1.0(字节4-5)、PM2.5(字节6-7)、PM10(字节8-9) |
| 连接引脚 | TX→P0.2 (UART0 RX), RX→P0.3 (UART0 TX) |
| 工作模式 | 主动模式（SET→GND） |
| 代码位置 | [SerialApp_ProcessPMS7003Data()](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.c#L845) |

### 3.2 MQ-2 可燃气体传感器（终端2）

| 参数 | 值 |
|------|-----|
| 类型 | 模拟量输出 |
| ADC通道 | P0.5 (ADC5 / HAL_ADC_CHANNEL_5) |
| 分辨率 | 12位 (0~4095) |
| 电压范围 | 0~3.3V |
| 采样间隔 | 5000ms |
| 定义位置 | [SerialApp.h#L98](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.h#L98) |

### 3.3 火焰传感器（终端2）

| 参数 | 值 |
|------|-----|
| 类型 | 模拟量输出 |
| ADC通道 | P0.6 (ADC6 / HAL_ADC_CHANNEL_6) |
| 分辨率 | 12位 (0~4095) |
| 电压范围 | 0~3.3V |
| 采样间隔 | 5000ms |
| 定义位置 | [SerialApp.h#L102](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.h#L102) |

---

## 4. 引脚分配总表

```
          ┌──────────────────────────────────┐
          │          CC2530 引脚              │
          ├──────┬───────────────────────────┤
          │ P0.0 │ ADC0 (通用模拟传感器)      │
          │ P0.1 │ 按键 KEY1                  │
          │ P0.2 │ UART0 RX (PMS7003 TX)     │
          │ P0.3 │ UART0 TX (PMS7003 RX)     │
          │ P0.4 │ DEBUG (JTAG TMS)          │
          │ P0.5 │ ADC5 (MQ-2 传感器)         │
          │ P0.6 │ ADC6 (火焰传感器)          │
          │ P1.0 │ LED1                      │
          │ P1.1 │ LED2                      │
          │ P1.2 │ LCD SCL                   │
          │ P1.3 │ LCD SDA                   │
          └──────┴───────────────────────────┘
```

---

## 5. IAR 编译与烧录

### 5.1 工程配置

IAR 工程文件位于 `Projects\zstack\Utilities\SerialApp\CC2530DB\SerialApp.ewp`，包含三个编译配置：

| 配置名称 | 用途 | 预处理器宏 |
|----------|------|-----------|
| **CoordinatorEB-Pro** | 协调器 | `ZIGBEEPRO`, `HAL_UART=TRUE`, `SERIAL_APP_PORT=0`, `LCD_SUPPORTED` |
| **EndDeviceEB-Pro** | 终端(共用) | `ZIGBEEPRO`, `NWK_AUTO_POLL`, `HAL_UART=TRUE`, `SERIAL_APP_PORT=0`, `LCD_SUPPORTED`, `xPOWER_SAVING` |
| RouterEB-Pro | 路由器(备用) | 暂未使用 |

### 5.2 编译步骤

```
1. 打开 IAR EW8051
2. 菜单: File → Open → Workspace
   选择: Projects\zstack\Utilities\SerialApp\CC2530DB\SerialApp.eww

3. 选择配置:
   下拉框选择 CoordinatorEB-Pro (协调器)
   或 EndDeviceEB-Pro (终端)

4. 菜单: Project → Rebuild All (或按 F7)

5. 编译产物:
   CC2530DB\CoordinatorEB-Pro\Exe\SerialApp.hex
   CC2530DB\EndDeviceEB-Pro\Exe\SerialApp.hex
```

### 5.3 ⚠️ 烧录注意事项

> **重要：两个终端烧录相同的 EndDeviceEB-Pro 固件！**

| 设备 | 烧录配置 | 固件文件 |
|------|----------|----------|
| 协调器 | CoordinatorEB-Pro | `CoordinatorEB-Pro\Exe\SerialApp.hex` |
| 终端1 (带PMS7003) | EndDeviceEB-Pro | `EndDeviceEB-Pro\Exe\SerialApp.hex` |
| 终端2 (带MQ2/火焰) | EndDeviceEB-Pro | `EndDeviceEB-Pro\Exe\SerialApp.hex` |

**为什么两个终端可以烧相同固件？**

代码设计为自适应模式：
- PMS7003 解析器仅在检测到有效帧头 (`0x42 0x4D`) 时才工作
- 终端1：PMS7003 正常输出 PM 数据，LCD 显示 PM2.5/PM10
- 终端2：无 PMS7003 连接，解析器自动跳过，LCD 显示 MQ2/Flame 值
- 两个终端都会读取各自连接的 ADC 通道（悬空通道读数无意义，不影响系统）

### 5.4 烧录工具

使用 **TI SmartRF Flash Programmer**：
1. 连接 CC Debugger 到 CC2530 调试口
2. 选择对应 hex 文件
3. 点击 "Perform actions"

---

## 6. ZigBee 数据传输协议

### 6.1 PMS7003 数据帧（二进制传输）

| 字节 | 内容 |
|------|------|
| 0 | 传感器类型 `0x01` (SENSOR_TYPE_PMS7003) |
| 1~32 | PMS7003 原始 32 字节帧 |

- 总长度：**33 字节**（固定长度，非字符串！）
- 传输方式：`AF_DataRequest` 二进制模式（不使用 strlen）
- 代码位置：[SerialApp_SendSensorData()](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.c#L755)

### 6.2 ADC/MQ2/Flame 数据帧

| 字节 | 内容 |
|------|------|
| 0 | 递增序号 |
| 1 | 传感器类型 (`0x02`=ADC, `0x03`=MQ2, `0x04`=Flame) |
| 2 | 数据低字节 (小端序) |
| 3 | 数据高字节 (小端序) |

- 总长度：**4 字节**

### 6.3 协调器 CSV 输出格式（给星火一号）

```
PM1.0,PM2.5,PM10,MQ2,Flame\r\n
例: 17,24,27,435,2033
```

- 单位：PM 值为 µg/m³，MQ2/Flame 为 ADC 原始值 (0~4095)
- 串口设置：9600bps, 8N1, 无流控

---

## 7. 调试与串口输出

### 7.1 协调器串口输出示例

```
[COORD] Network connected
[COORD] ADC: ADC0=143(0.12V)
17,24,27,435,2033    ← CSV 格式数据
```

### 7.2 终端串口输出示例

```
[END] connected
[END] ADC: ADC0=144(0.12V) MQ2=435(0.35V) Flame=2033(1.64V)
```

### 7.3 串口设置

| 参数 | 值 |
|------|-----|
| 波特率 | **9600** |
| 数据位 | 8 |
| 停止位 | 1 |
| 校验位 | None |
| 流控 | 无 |

---

## 8. 关键代码位置

| 功能 | 文件 | 行号 |
|------|------|------|
| 传感器类型定义 | [SerialApp.h](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.h) | L88-L103 |
| PMS7003 帧解析 | [SerialApp.c](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.c) | L845-L960 |
| ADC 传感器读取 | [SerialApp.c](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.c) | L700-L752 |
| 传感器数据发送 | [SerialApp.c](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.c) | L755-L800 |
| 协调器 PMS7003 接收 | [SerialApp.c](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.c) | L358-L430 |
| UART 环形缓冲区 | [SerialApp.c](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.c) | L161-L175 |
| UART 回调 (存入缓冲区) | [SerialApp.c](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.c) | L582-L605 |

---

## 9. 常见问题

### Q1: 协调器和终端无法连接？

1. 确保协调器**先上电**，等 LED 稳定后再给终端上电
2. 确认协调器烧录的是 `CoordinatorEB-Pro`，终端是 `EndDeviceEB-Pro`
3. 检查 PAN ID 是否一致（f8wConfig.cfg 中 `-DZDAPP_CONFIG_PAN_ID=0xFFFF` 表示自动）
4. 两台设备距离不要太远（室内建议 5m 内）

### Q2: PMS7003 一直显示 Checksum error？

1. 检查 PMS7003 是否已上电（需要5V供电）
2. 确认 SET 引脚已接 GND（主动模式）
3. 检查 TX/RX 接线：PMS7003 TX → CC2530 P0.2
4. 用 USB 转串口模块直接读 PMS7003，确认传感器本身输出正常
5. 终端串口输出中查找 `PMS7003 Raw:` 行，确认 32 字节帧完整

### Q3: 终端 LCD 不显示？

1. LCD 使用 I2C 接口（P1.2 SCL, P1.3 SDA）
2. 检查预处理器宏是否包含 `LCD_SUPPORTED`
3. 两个终端 LCD 显示内容不同：
   - 终端1（PMS7003）：显示 PM2.5/PM10 值
   - 终端2（MQ2/Flame）：显示 MQ2/Flame 值

### Q4: 串口助手卡死或乱码？

1. 确认波特率设置为 **9600**
2. 确认勾选了 "HEX 显示" 或 "文本模式"
3. 如果仍有乱码，检查是否有其他程序占用同一串口
4. 终端仅输出 `[END]` 开头的 ASCII 文本行，不含二进制数据

### Q5: 如何修改采样间隔？

在 [SerialApp.h](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.h) 中修改：
```c
#define SENSOR_ADC_INTERVAL  5000   // 单位: 毫秒，默认5秒
```

### Q6: 如何修改 PMS7003 帧校验逻辑？

PMS7003 32 字节帧结构：
- 字节 0~1: 帧头 (0x42 0x4D)
- 字节 2~29: 数据字段
- 字节 30~31: 前 30 字节累加和校验

修改校验逻辑在 [SerialApp_ProcessPMS7003Data()](file:///Projects/zstack/Utilities/SerialApp/Source/SerialApp.c#L920-L940)。

---

## 10. 后续扩展建议

1. **添加温湿度传感器**（如 DHT22）：使用 GPIO 或额外 ADC 通道
2. **低功耗优化**：终端已启用 `xPOWER_SAVING`，可进一步优化唤醒周期
3. **OTA 升级**：Z-Stack 支持 OTA 固件升级，可加入 OTA 功能
4. **更多终端**：ZigBee 网络支持最多 65535 个节点
5. **云端对接**：通过星火一号 WiFi 模块上传数据到云平台

---

## 11. 开发环境

| 工具 | 版本 |
|------|------|
| IDE | IAR Embedded Workbench for 8051 8.10+ |
| 协议栈 | TI Z-Stack-CC2530-2.5.1a |
| 芯片 | CC2530F256 |
| 烧录器 | CC Debugger |
| 调试串口 | USB 转 TTL (CP2102/CH340) |
