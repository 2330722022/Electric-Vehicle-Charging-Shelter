# SerialApp传感器数据采集和Zigbee通信系统

## 系统概述

基于TI Z-Stack框架的SerialApp增强版本，支持混合传感器数据采集和Zigbee网络通信。

## 支持的传感器

### 1. PMS7003空气质量传感器
- PM1.0、PM2.5、PM10浓度检测
- UART通信接口
- 自动数据解析和校验

### 2. ADC模拟传感器
- 12位分辨率ADC采集
- 支持多通道配置
- 自动电压转换
- 定时采集（默认5秒）

## 硬件连接

### PMS7003连接
```
PMS7003    CC2530
VCC    ->  5V
GND    ->  GND
TX     ->  P0.2 (UART0_RX)
RX     ->  P0.3 (UART0_TX)
SET    ->  GND (主动模式)
```

### ADC传感器连接
```
传感器    CC2530
VCC    ->  3.3V
GND    ->  GND
OUT    ->  P0.0 (ADC0)
```

## 数据格式

### PMS7003数据包
```
[0x01][PM1.0_L][PM1.0_H][PM2.5_L][PM2.5_H][PM10_L][PM10_H]
```
- 第1字节：传感器类型 (0x01 = PMS7003)
- 第2-3字节：PM1.0数值 (小端格式)
- 第4-5字节：PM2.5数值 (小端格式)
- 第6-7字节：PM10数值 (小端格式)

### ADC数据包
```
[0x02][ADC_VALUE_L][ADC_VALUE_H]
```
- 第1字节：传感器类型 (0x02 = ADC)
- 第2-3字节：ADC原始值 (小端格式)

## 配置参数

在 `SensorConfig.h` 中可以修改以下参数：

### ADC配置
```c
#define SENSOR_ADC_CHANNEL           HAL_ADC_CHANNEL_0    // ADC通道
#define SENSOR_ADC_RESOLUTION        HAL_ADC_RESOLUTION_12 // 分辨率
#define SENSOR_ADC_INTERVAL_MS       5000                // 采集间隔(ms)
```

### PMS7003配置
```c
#define PMS7003_UART_BAUDRATE        HAL_UART_BR_9600    // 波特率
```

## 编译和烧录

1. 使用IAR Embedded Workbench for 8051
2. 打开项目文件：`SerialApp.eww`
3. 选择目标配置：RouterEB-Pro
4. 编译项目
5. 使用CC Debugger烧录到CC2530

## 使用方法

### 1. 设备初始化
- 设备启动后自动初始化ADC和UART
- 自动加入Zigbee网络
- LED1亮起表示网络连接成功

### 2. 传感器数据采集
- PMS7003数据通过UART实时采集
- ADC数据按配置间隔定时采集
- 数据自动格式化和校验

### 3. Zigbee数据传输
- 数据自动发送到协调器
- 支持自动重传机制
- LED3闪烁表示数据传输

### 4. 串口调试
- 通过串口可以看到调试信息
- 波特率：9600
- 数据格式：8N1

## 扩展开发

### 添加新传感器类型

1. 在 `SensorConfig.h` 中定义新的传感器类型：
```c
#define SENSOR_TYPE_NEW_SENSOR      0x05
```

2. 在 `SerialApp.c` 中添加数据处理函数：
```c
static void SerialApp_ProcessNewSensorData(uint8 *data, uint16 len)
{
  // 实现传感器数据解析
}
```

3. 在数据发送函数中添加支持：
```c
case SENSOR_TYPE_NEW_SENSOR:
  SerialApp_ProcessNewSensorData(data, len);
  break;
```

### 修改ADC通道

在 `SensorConfig.h` 中修改：
```c
#define SENSOR_ADC_CHANNEL           HAL_ADC_CHANNEL_1  // 改为通道1
```

支持的通道：
- HAL_ADC_CHANNEL_0 到 HAL_ADC_CHANNEL_7
- HAL_ADC_CHANNEL_TEMP (温度传感器)
- HAL_ADC_CHANNEL_VDD (电压检测)

## 故障排除

### 1. PMS7003无数据
- 检查串口连接
- 确认SET引脚接地
- 检查波特率设置

### 2. ADC数据异常
- 检查传感器连接
- 确认电压范围（0-3.3V）
- 检查ADC通道配置

### 3. Zigbee网络连接失败
- 检查协调器是否工作
- 确认设备类型配置
- 检查RF频率设置

## 技术支持

- 基于TI Z-Stack 2.5.1a
- CC2530芯片
- Zigbee Home Automation Profile

## 版本历史

- v1.0: 初始版本，支持PMS7003和ADC传感器
- 基于SerialApp增强开发