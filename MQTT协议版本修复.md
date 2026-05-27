# MQTT协议版本修复 - 3.1.1

## 问题现象

```
报错：无效协议版本 (1)
```

## 核心原因

**OneNET新版融合云只支持 MQTT 3.1.1**

而Eclipse Paho客户端默认使用的是 **MQTT 3.0**（协议版本号为1），导致协议版本不匹配。

## 解决方案

### 只需添加一行代码！

在 `MqttManager.java` 的 `initMqtt()` 方法中，设置MQTT协议版本：

```java
// 设置MQTT协议版本为3.1.1（OneNET融合云要求）
connectOptions.setMqttVersion(MqttConnectOptions.MQTT_VERSION_3_1_1);
```

### 完整配置示例

```java
connectOptions = new MqttConnectOptions();
connectOptions.setUserName(PRODUCT_ID);
connectOptions.setPassword(DEVICE_KEY.toCharArray());
connectOptions.setConnectionTimeout(30);
connectOptions.setKeepAliveInterval(60);
connectOptions.setAutomaticReconnect(true);
connectOptions.setCleanSession(true);
connectOptions.setMaxReconnectDelay(10000);

// ⭐ 关键：设置MQTT协议版本为3.1.1
connectOptions.setMqttVersion(MqttConnectOptions.MQTT_VERSION_3_1_1);
Log.d(TAG, "已设置MQTT协议版本: 3.1.1");
```

## MQTT协议版本对照

| 协议版本 | 版本号常量 | 协议号 | OneNET支持 |
|---------|-----------|--------|-----------|
| MQTT 3.1 | `MQTT_VERSION_3_1` | 3 | ❌ 不支持 |
| **MQTT 3.1.1** | **`MQTT_VERSION_3_1_1`** | **4** | **✅ 支持** |
| MQTT 5.0 | `MQTT_VERSION_5_0` | 5 | ❌ 不支持 |

## 验证方法

### 1. 查看日志

运行APP后，在Logcat中应该看到：

```
D/MqttManager: 已设置MQTT协议版本: 3.1.1
D/MqttManager: ===== 正在连接MQTT服务器 =====
D/MqttManager: ===== MQTT连接成功 =====
```

### 2. 连接成功标志

- ✅ 不再报"无效协议版本"错误
- ✅ 显示"MQTT连接成功"
- ✅ 能够订阅主题
- ✅ 能够接收设备上报数据

## 为什么需要MQTT 3.1.1？

### MQTT 3.1 vs 3.1.1 区别

| 特性 | MQTT 3.1 | MQTT 3.1.1 |
|------|----------|------------|
| 发布年份 | 2010 | 2014 |
| OASIS标准 | ❌ | ✅ |
| ClientId限制 | 最多23字符 | 无限制 |
| Will消息 | 基础支持 | 增强支持 |
| 兼容性 | 旧版 | 广泛兼容 |

### OneNET的选择

OneNET新版融合云选择MQTT 3.1.1的原因：
1. **标准化**：3.1.1是OASIS正式标准
2. **兼容性**：更好的跨平台兼容
3. **稳定性**：更成熟的协议实现
4. **功能**：支持更多高级特性

## 常见错误

### 错误1：不设置协议版本

```java
// ❌ 错误：使用默认版本（3.0）
connectOptions = new MqttConnectOptions();
// 没有设置setMqttVersion
```

**结果**：报"无效协议版本 (1)"错误

### 错误2：设置错误的版本

```java
// ❌ 错误：设置为3.1
connectOptions.setMqttVersion(MqttConnectOptions.MQTT_VERSION_3_1);
```

**结果**：同样报协议版本错误

### 正确做法

```java
// ✅ 正确：设置为3.1.1
connectOptions.setMqttVersion(MqttConnectOptions.MQTT_VERSION_3_1_1);
```

## 其他云平台要求

| 云平台 | 要求的MQTT版本 |
|--------|---------------|
| **OneNET融合云** | **3.1.1** |
| 阿里云IoT | 3.1.1 |
| 腾讯云IoT | 3.1.1 |
| AWS IoT | 3.1.1 |
| Azure IoT | 3.1.1 |

**结论**：目前主流物联网云平台都要求MQTT 3.1.1

## 测试步骤

1. **重新编译项目**
   ```
   Build → Make Project
   ```

2. **运行APP**

3. **观察Logcat**
   - 过滤标签：`MqttManager`
   - 查找："已设置MQTT协议版本: 3.1.1"
   - 查找："MQTT连接成功"

4. **验证功能**
   - 等待设备上报数据
   - 检查UI是否更新
   - 测试控制指令下发

## 故障排查

### 如果仍然连接失败

1. **检查网络**
   ```bash
   ping mqtts.heclouds.com
   ```

2. **检查配置**
   - PRODUCT_ID是否正确
   - DEVICE_NAME是否正确
   - DEVICE_KEY是否有效

3. **查看详细日志**
   ```
   Logcat过滤器：MqttManager
   查看完整的错误信息
   ```

4. **尝试SSL连接**
   ```java
   private static final String MQTT_BROKER = "ssl://mqtts.heclouds.com:8883";
   ```

## 总结

### 修复前
- ❌ 使用MQTT 3.0（默认）
- ❌ 报"无效协议版本 (1)"
- ❌ 无法连接OneNET

### 修复后
- ✅ 使用MQTT 3.1.1
- ✅ 协议版本匹配
- ✅ 成功连接OneNET
- ✅ 正常收发数据

### 关键代码
```java
connectOptions.setMqttVersion(MqttConnectOptions.MQTT_VERSION_3_1_1);
```

**只需这一行，问题解决！** 🎉

## 参考资料

- [MQTT 3.1.1规范](http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html)
- [OneNET MQTT文档](https://open.iot.10086.cn/doc/mqtt/)
- [Eclipse Paho文档](https://www.eclipse.org/paho/)
