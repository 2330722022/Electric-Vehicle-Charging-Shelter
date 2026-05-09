// SerialApp传感器数据采集和Zigbee通信使用示例
// 基于TI Z-Stack框架实现混合传感器数据透传

#include "SerialApp.h"
#include "SensorConfig.h"
#include "SensorDataHandler.h"

// 硬件连接说明：
// 1. PMS7003空气质量传感器：
//    - VCC: 5V
//    - GND: GND
//    - TX: P0.2 (UART0_RX)
//    - RX: P0.3 (UART0_TX)
//    - SET: GND (主动模式)
//
// 2. ADC模拟传感器：
//    - 传感器输出: P0.0 (ADC0)
//    - 传感器VCC: 3.3V
//    - 传感器GND: GND
//
// 3. LED指示：
//    - LED1: 网络状态
//    - LED2: 连接状态
//    - LED3: 数据传输状态

// 功能特性：
// 1. 自动PMS7003数据采集和解析
// 2. 定时ADC传感器数据采集
// 3. 自动Zigbee网络数据传输
// 4. 数据格式化和校验
// 5. 串口调试输出

// 数据格式说明：
// PMS7003数据包格式：
// [0x01][PM1.0_L][PM1.0_H][PM2.5_L][PM2.5_H][PM10_L][PM10_H]
//
// ADC数据包格式：
// [0x02][ADC_VALUE_L][ADC_VALUE_H]

// 使用方法：
// 1. 编译并烧录到CC2530设备
// 2. 设备会自动加入Zigbee网络
// 3. 连接PMS7003传感器到UART0
// 4. 连接模拟传感器到ADC0通道
// 5. 数据会自动采集并通过Zigbee发送

// 配置参数修改：
// 在SensorConfig.h中可以修改：
// - ADC采集间隔
// - ADC通道选择
// - ADC分辨率
// - PMS7003波特率
// - 数据传输重试次数

// 调试输出：
// 通过串口可以看到：
// - 网络连接信息
// - 传感器数据
// - 传输状态
// - 错误信息

// 扩展支持：
// 可以添加更多传感器类型：
// 1. 温湿度传感器 (DHT11/DHT22)
// 2. 光照传感器 (BH1750)
// 3. 气体传感器 (MQ系列)
// 4. 压力传感器 (BMP180)