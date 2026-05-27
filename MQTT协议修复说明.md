# MQTT协议修复说明

## 问题描述

之前错误地使用了WebSocket连接（`ws://`），但OneNET服务器只支持标准MQTT（TCP）协议。

## 修复内容

### 1. 修改连接协议

**修复前：**
```java
private static final String MQTT_BROKER_WS = "ws://mqtts.heclouds.com:8883/mqtt";
private static final String MQTT_BROKER = MQTT_BROKER_WS;  // 使用WebSocket
```

**修复后：**
```java
private static final String MQTT_BROKER_TCP = "tcp://mqtts.heclouds.com:1883";
private static final String MQTT_BROKER = MQTT_BROKER_TCP;  // 使用标准MQTT TCP
```

### 2. 修正订阅主题

**修复前：**
```java
private static final String SUBSCRIBE_TOPIC = "$sys/" + PRODUCT_ID + "/" + DEVICE_NAME + "/property/post";
private static final String REPLY_TOPIC = "$sys/" + PRODUCT_ID + "/" + DEVICE_NAME + "/property/post/reply";
```

**修复后：**
```java
// 订阅设备上报的数据（物模型属性上报）
private static final String SUBSCRIBE_TOPIC = "$sys/" + PRODUCT_ID + "/" + DEVICE_NAME + "/thing/property/post";
// 发布控制指令（物模型属性设置）
private static final String PUBLISH_TOPIC = "$sys/" + PRODUCT_ID + "/" + DEVICE_NAME + "/thing/property/set";
```

### 3. 简化订阅逻辑

**修复前：**
```java
mqttClient.subscribe(new String[]{SUBSCRIBE_TOPIC, REPLY_TOPIC}, new int[]{1, 1});
```

**修复后：**
```java
// 只订阅设备上报主题
mqttClient.subscribe(SUBSCRIBE_TOPIC, 1);
```

## OneNET MQTT协议说明

### 支持的连接方式

| 协议类型 | 地址 | 端口 | 用途 |
|---------|------|------|------|
| **标准MQTT (TCP)** | mqtts.heclouds.com | 1883 | ✅ 推荐使用 |
| SSL/TLS加密 | mqtts.heclouds.com | 8883 | 安全连接 |
| WebSocket | mqtts.heclouds.com | 8083 | Web端使用 |

### 物模型主题格式

#### 1. 设备属性上报（APP订阅）
```
$sys/{product_id}/{device_name}/thing/property/post
```
- 设备通过此主题上报属性数据
- APP订阅此主题接收数据

#### 2. 设备属性设置（APP发布）
```
$sys/{product_id}/{device_name}/thing/property/set
```
- APP通过此主题下发控制指令
- 设备订阅此主题接收指令

#### 3. 设备属性设置响应（可选）
```
$sys/{product_id}/{device_name}/thing/property/set_reply
```
- 设备回复设置结果（可选订阅）

## 数据格式

### 设备上报数据格式

**格式1：标准物模型格式**
```json
{
  "code": 0,
  "data": [
    {
      "identifier": "temperature",
      "value": "25.5"
    },
    {
      "identifier": "humidity",
      "value": "60"
    }
  ]
}
```

**格式2：参数格式**
```json
{
  "params": {
    "temperature": 25.5,
    "humidity": 60
  }
}
```

**格式3：直接属性格式**
```json
{
  "temperature": 25.5,
  "humidity": 60
}
```

### 控制指令格式

**APP下发指令：**
```json
{
  "id": "1234567890",
  "version": "1.0",
  "params": {
    "led": true
  }
}
```

## 认证方式

OneNET MQTT连接使用Token认证：

```java
connectOptions.setUserName(PRODUCT_ID);
connectOptions.setPassword(DEVICE_KEY.toCharArray());
```

**DEVICE_KEY生成规则：**
```
version=2018-10-31&res=products%2F{product_id}%2Fdevices%2F{device_name}&et={expire_time}&method=md5&sign={signature}
```

## 测试验证

### 1. 连接测试

运行APP后，查看Logcat日志：

```
D/MqttManager: ===== 初始化MQTT客户端 =====
D/MqttManager: ClientId: 67k36rzgOO.test1
D/MqttManager: Broker: tcp://mqtts.heclouds.com:1883
D/MqttManager: ProductId: 67k36rzgOO
D/MqttManager: DeviceName: test1
D/MqttManager: DeviceKey: 已配置
D/MqttManager: MQTT客户端初始化成功
D/MqttManager: ===== 正在连接MQTT服务器 =====
D/MqttManager: ===== MQTT连接成功 =====
D/MqttManager: 连接耗时: XXXms
D/MqttManager: ===== 订阅主题成功 =====
D/MqttManager: 订阅主题: $sys/67k36rzgOO/test1/thing/property/post
```

### 2. 数据接收测试

当设备上报数据时，应该看到：

```
D/MqttManager: ===== 收到MQTT消息 =====
D/MqttManager: Topic: $sys/67k36rzgOO/test1/thing/property/post
D/MqttManager: Payload: {"code":0,"data":[{"identifier":"temperature","value":"25.5"}]}
D/MainActivity: 收到MQTT消息: Topic=..., Payload=...
D/MainActivity: 解析数据 - identifier: temperature, value: 25.5
D/MainActivity: 更新温度: 25.5
```

### 3. 控制指令测试

点击控制按钮时，应该看到：

```
D/MqttManager: 发布控制指令 - Topic: $sys/67k36rzgOO/test1/thing/property/set, Payload: {"id":"...","version":"1.0","params":{"led":true}}
```

## 常见问题

### Q1: 连接失败，错误码32109

**原因：** 使用了错误的协议（WebSocket）

**解决：** 确保使用`tcp://`而不是`ws://`

### Q2: 连接失败，错误码32110

**原因：** 认证失败，用户名或密码错误

**解决：** 检查PRODUCT_ID和DEVICE_KEY配置

### Q3: 收不到数据

**原因：** 订阅的主题不正确

**解决：** 确认订阅主题为`$sys/{product_id}/{device_name}/thing/property/post`

### Q4: 主题无效错误

**原因：** 主题格式不符合OneNET规范

**解决：** 使用标准的物模型主题格式，包含`thing`路径

## 优势对比

### 标准MQTT (TCP) vs WebSocket

| 特性 | 标准MQTT (TCP) | WebSocket |
|------|---------------|-----------|
| 协议开销 | 小 | 较大 |
| 连接稳定性 | ✅ 高 | 一般 |
| 适用场景 | Android/iOS原生应用 | Web浏览器 |
| 防火墙穿透 | 需要开放1883端口 | 可使用80/443端口 |
| OneNET支持 | ✅ 完全支持 | 部分支持 |

## 总结

本次修复主要解决了以下问题：

1. ✅ 将WebSocket协议改为标准MQTT TCP协议
2. ✅ 修正订阅主题为OneNET物模型标准格式
3. ✅ 删除了不必要的REPLY_TOPIC订阅
4. ✅ 优化了日志输出，便于调试

修复后的APP应该能够正常连接OneNET MQTT服务器并接收设备上报的数据。

## 下一步

1. 重新编译并运行APP
2. 观察Logcat日志确认连接成功
3. 等待设备上报数据
4. 验证数据显示是否正常
5. 测试控制指令下发功能

如有问题，请查看Logcat中的详细日志信息。
