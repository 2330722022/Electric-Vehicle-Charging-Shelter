# PMS7003空气质量传感器协议实现

## 协议概述

PMS7003是一款数字式空气质量传感器，使用串口通信协议传输PM1.0、PM2.5、PM10等颗粒物浓度数据。

## 协议帧结构

```
+------+------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+
| 0x42 | 0x4D | LEN_L  | LEN_H  | PM1.0L | PM1.0H | PM2.5L | PM2.5H | PM10L  | PM10H  |  ...   | CHKL   | CHKH   |
+------+------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+--------+
```

- **帧头**：固定为0x42 0x4D
- **长度**：LEN_H LEN_L，总数据长度（不包括帧头和长度字段）
- **数据**：包括PM1.0、PM2.5、PM10等浓度值
- **校验和**：CHKH CHKL，从帧头开始的所有字节的累加和

## 实现细节

### 1. 数据接收和解析

```c
static void SerialApp_ProcessPMS7003Data(uint8 *data, uint16 len)
{
  uint16 i;
  
  for (i = 0; i < len; i++)
  {
    if (pms7003State == 0)  // 等待帧头
    {
      if (data[i] == PMS7003_HEADER)  // 0x42
      {
        pms7003State = 1;
        pms7003Buffer[0] = data[i];
        pms7003Index = 1;
      }
    }
    else if (pms7003State == 1)  // 等待第二个帧头字节
    {
      if (data[i] == PMS7003_HEADER_SECOND)  // 0x4D
      {
        pms7003State = 2;
        pms7003Buffer[pms7003Index++] = data[i];
      }
      else
      {
        pms7003State = 0;  // 重置状态
        pms7003Index = 0;
      }
    }
    else if (pms7003State == 2)  // 接收数据
    {
      pms7003Buffer[pms7003Index++] = data[i];
      
      if (pms7003Index >= 4)  // 已接收到长度字段
      {
        uint16 frameLen = BUILD_UINT16(pms7003Buffer[3], pms7003Buffer[2]);
        
        if (pms7003Index >= (frameLen + 4))  // 完整数据包
        {
          // 计算校验和
          uint16 calcChecksum = 0;
          uint16 recvChecksum = BUILD_UINT16(pms7003Buffer[frameLen+3], pms7003Buffer[frameLen+2]);
          
          for (i = 0; i < (frameLen + 2); i++)
          {
            calcChecksum += pms7003Buffer[i];
          }
          
          if (calcChecksum == recvChecksum)  // 校验通过
          {
            // 提取PM数据
            uint16 pm1_0 = BUILD_UINT16(pms7003Buffer[5], pms7003Buffer[4]);
            uint16 pm2_5 = BUILD_UINT16(pms7003Buffer[7], pms7003Buffer[6]);
            uint16 pm10_0 = BUILD_UINT16(pms7003Buffer[9], pms7003Buffer[8]);
            
            // 处理数据...
          }
          
          // 重置状态
          pms7003State = 0;
          pms7003Index = 0;
        }
      }
    }
  }
}
```

### 2. 数据格式

**PM数据格式**：
- PM1.0: 2字节，小端格式
- PM2.5: 2字节，小端格式  
- PM10: 2字节，小端格式

**Zigbee传输格式**：
```
[0x01][PM1.0_L][PM1.0_H][PM2.5_L][PM2.5_H][PM10_L][PM10_H]
```

### 3. 调试输出

- **串口调试**：通过UART0输出详细数据
- **LCD显示**：在LCD屏幕上显示PM2.5和PM10数值

## 硬件连接

```
PMS7003    CC2530
VCC    ->  5V
GND    ->  GND
TX     ->  P0.2 (UART0_RX)
RX     ->  P0.3 (UART0_TX)
SET    ->  GND (主动模式)
```

## 通信参数

- **波特率**：9600 bps
- **数据位**：8位
- **停止位**：1位
- **校验位**：无
- **流控制**：无

## 数据处理流程

1. **初始化**：配置UART0为9600波特率
2. **数据接收**：通过UART中断接收PMS7003数据
3. **协议解析**：识别帧头、解析数据、校验数据
4. **数据处理**：提取PM1.0、PM2.5、PM10浓度值
5. **调试输出**：通过串口和LCD显示数据
6. **Zigbee传输**：将数据通过Zigbee网络发送

## 错误处理

- **帧头错误**：重置状态机
- **校验和错误**：丢弃数据包
- **超时处理**：数据不完整时重置状态

## 扩展功能

- **数据过滤**：可添加移动平均滤波
- **阈值报警**：设置PM2.5阈值触发报警
- **数据记录**：存储历史数据
- **多传感器支持**：可同时连接多个PMS7003

## 测试建议

1. **硬件测试**：确认传感器连接正确，SET引脚接地
2. **通信测试**：检查UART通信是否正常
3. **协议测试**：验证数据解析是否正确
4. **网络测试**：确认Zigbee数据传输正常
5. **集成测试**：测试完整的数据采集和传输流程

## 注意事项

- PMS7003需要5V供电
- 传感器需要预热2-3分钟才能稳定
- 避免在高湿度环境使用
- 定期校准传感器以保证精度