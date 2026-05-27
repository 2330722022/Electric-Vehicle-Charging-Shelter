# OneNET MQTT认证失败修复指南

## 错误现象

```
E/MqttManager: ===== MQTT连接失败 =====
错误的用户名或密码 (4)
错误码: 4
错误原因: 认证失败
完整错误信息: 错误的用户名或密码
```

## 核心原因

**使用了产品级Token进行MQTT连接，但OneNET要求使用设备级Token。**

### 当前配置（❌ 错误）

```java
private static final String DEVICE_KEY = "version=2018-10-31&res=products%2F67k36rzgOO%2Fdevices%2Fproject&et=1830297599&method=md5&sign=bt7UPGU6PGqQlnhj6joIzQ%3D%3D";
```

**问题分析：**
- `res=products/67k36rzgOO/devices/project` ← 这是**产品级**资源路径
- 适用于HTTP API调用
- ❌ **不适用于MQTT连接认证**

### 正确配置（✅ 需要）

```java
private static final String DEVICE_KEY = "version=2018-10-31&res=products%2F67k36rzgOO%2Fdevices%2Ftest1&et=xxx&method=md5&sign=xxx";
```

**关键区别：**
- `res=products/67k36rzgOO/devices/test1` ← 这是**设备级**资源路径
- ✅ **适用于MQTT连接认证**

## OneNET认证方式对比

### 1. 产品级Token

| 项目 | 值 |
|------|-----|
| **资源路径** | `products/{product_id}/devices/project` |
| **适用范围** | 产品下所有设备 |
| **适用场景** | HTTP API调用 |
| **MQTT支持** | ❌ 不支持 |
| **示例** | `res=products/67k36rzgOO/devices/project` |

**用途：**
- 查询产品下所有设备
- 批量操作设备
- HTTP RESTful API调用

### 2. 设备级Token

| 项目 | 值 |
|------|-----|
| **资源路径** | `products/{product_id}/devices/{device_name}` |
| **适用范围** | 单个指定设备 |
| **适用场景** | MQTT连接、设备级API |
| **MQTT支持** | ✅ 支持 |
| **示例** | `res=products/67k36rzgOO/devices/test1` |

**用途：**
- MQTT连接认证
- 单个设备数据查询
- 设备控制指令下发

### 3. 设备密钥（Device Key）

部分OneNET产品形态提供简单的设备密钥：

| 项目 | 值 |
|------|-----|
| **格式** | 简单字符串 |
| **长度** | 通常32位 |
| **适用场景** | MQTT连接 |
| **MQTT支持** | ✅ 支持 |
| **示例** | `abc123def456ghi789jkl012mno345pq` |

## 解决方案

### 方案1：在OneNET平台获取设备密钥（最简单）

#### 步骤：

1. **登录OneNET平台**
   - 访问：https://open.iot.10086.cn/

2. **进入产品管理**
   - 找到产品ID：`67k36rzgOO`
   - 点击进入产品详情

3. **找到设备**
   - 在设备列表中找到设备：`test1`
   - 点击设备名称进入详情

4. **查看设备密钥**
   - 查找"设备密钥"、"鉴权信息"或"认证信息"
   - 复制设备密钥（通常是32位字符串）

5. **修改代码**
   ```java
   private static final String DEVICE_KEY = "你的设备密钥";
   ```

### 方案2：生成设备级Token

如果平台只提供Token生成功能，需要生成设备级Token。

#### Token格式

```
version=2018-10-31&res=products%2F{product_id}%2Fdevices%2F{device_name}&et={expire_time}&method=md5&sign={signature}
```

#### 参数说明

| 参数 | 说明 | 示例值 |
|------|------|--------|
| version | 版本固定值 | `2018-10-31` |
| res | 资源路径（URL编码） | `products%2F67k36rzgOO%2Fdevices%2Ftest1` |
| et | 过期时间戳（Unix时间） | `1830297599` |
| method | 签名方法 | `md5` |
| sign | MD5签名 | 计算得出 |

#### 签名计算方法

```
sign = MD5(version={version}&res={res}&et={et}&method={method}&key={secret_key})
```

其中：
- `{secret_key}` 是产品的秘钥（在产品设置中查看）
- 需要对整个字符串进行MD5加密
- 结果需要进行Base64编码和URL编码

#### 在线工具生成

可以使用OneNET官方提供的Token生成工具：
- 访问OneNET文档中心的"鉴权说明"
- 使用在线Token生成器
- 输入产品信息和设备名称
- 生成设备级Token

### 方案3：使用代码动态生成（高级）

如果需要动态生成Token，可以添加Token计算工具类：

```java
public class TokenGenerator {
    public static String generateDeviceToken(String productId, String deviceName, 
                                            String secretKey, long expireTime) {
        try {
            String version = "2018-10-31";
            String res = "products/" + productId + "/devices/" + deviceName;
            String encodedRes = URLEncoder.encode(res, "UTF-8");
            String method = "md5";
            
            // 构建签名字符串
            String signStr = version + "&res=" + encodedRes + "&et=" + expireTime + 
                           "&method=" + method + "&key=" + secretKey;
            
            // 计算MD5
            MessageDigest md = MessageDigest.getInstance("MD5");
            byte[] digest = md.digest(signStr.getBytes("UTF-8"));
            
            // Base64编码
            String sign = Base64.encodeToString(digest, Base64.NO_WRAP);
            
            // URL编码
            sign = URLEncoder.encode(sign, "UTF-8");
            
            // 组装Token
            return "version=" + version + "&res=" + encodedRes + 
                   "&et=" + expireTime + "&method=" + method + "&sign=" + sign;
                   
        } catch (Exception e) {
            Log.e("TokenGenerator", "生成Token失败", e);
            return null;
        }
    }
}
```

## 验证步骤

### 1. 确认设备存在

在OneNET平台确认：
- ✅ 产品ID：`67k36rzgOO` 存在
- ✅ 设备名称：`test1` 已创建
- ✅ 设备状态：正常

### 2. 获取正确的认证信息

从OneNET平台获取以下任一：
- 设备密钥（推荐，最简单）
- 设备级Token

### 3. 修改代码

在 `MqttManager.java` 第42行替换：

```java
// 如果使用设备密钥
private static final String DEVICE_KEY = "你的32位设备密钥";

// 或者使用设备级Token
private static final String DEVICE_KEY = "version=2018-10-31&res=products%2F67k36rzgOO%2Fdevices%2Ftest1&et=1830297599&method=md5&sign=xxx";
```

### 4. 重新编译运行

```
Build → Make Project
Run → Run 'app'
```

### 5. 查看日志

成功的日志应该显示：

```
D/MqttManager: ===== 初始化MQTT客户端 =====
D/MqttManager: ClientId: 67k36rzgOO.test1
D/MqttManager: Broker: tcp://mqtts.heclouds.com:1883
D/MqttManager: ProductId: 67k36rzgOO
D/MqttManager: DeviceName: test1
D/MqttManager: DeviceKey: 已配置 (XX位)
D/MqttManager: 已设置MQTT协议版本: 3.1.1
D/MqttManager: MQTT客户端初始化成功
D/MqttManager: ===== 正在连接MQTT服务器 =====
D/MqttManager: ===== MQTT连接成功 =====
D/MqttManager: 连接耗时: XXXms
D/MqttManager: ===== 订阅主题成功 =====
```

## 常见错误对照

### 错误1：使用产品级Token

**错误信息：**
```
错误的用户名或密码 (4)
```

**原因：**
```java
res=products/67k36rzgOO/devices/project  // ❌ 产品级
```

**解决：**
```java
res=products/67k36rzgOO/devices/test1    // ✅ 设备级
```

### 错误2：设备名称不匹配

**错误信息：**
```
错误的用户名或密码 (4)
```

**原因：**
- 代码中：`DEVICE_NAME = "test1"`
- 平台上：设备名可能是 `"device1"` 或其他

**解决：**
确保代码中的设备名称与OneNET平台完全一致（区分大小写）

### 错误3：Token过期

**错误信息：**
```
错误的用户名或密码 (4)
```

**原因：**
Token的`et`参数（过期时间）已过去

**解决：**
生成新的Token，设置更长的有效期

### 错误4：签名错误

**错误信息：**
```
错误的用户名或密码 (4)
```

**原因：**
MD5签名计算错误

**解决：**
- 检查签名算法是否正确
- 确认使用的secret_key正确
- 使用在线工具验证

### 错误5：Product ID错误

**错误信息：**
```
错误的用户名或密码 (4)
或
无效的产品ID
```

**原因：**
PRODUCT_ID填写错误

**解决：**
确认Product ID为 `67k36rzgOO`（区分大小写）

## 快速检查清单

在运行APP前，确认以下各项：

- [ ] OneNET平台已创建产品 `67k36rzgOO`
- [ ] 产品中已创建设备 `test1`
- [ ] 已从平台获取设备密钥或设备级Token
- [ ] 代码中 `PRODUCT_ID = "67k36rzgOO"`
- [ ] 代码中 `DEVICE_NAME = "test1"`
- [ ] 代码中 `DEVICE_KEY` 已替换为正确的值
- [ ] 如果是Token，`res` 中包含 `devices/test1` 而不是 `devices/project`
- [ ] MQTT协议版本设置为 `3.1.1`
- [ ] Broker地址为 `tcp://mqtts.heclouds.com:1883`

## 获取帮助

### OneNET官方资源

1. **开发文档**
   - https://open.iot.10086.cn/doc/mqtt/
   - 查看"设备接入" → "MQTT协议" → "鉴权说明"

2. **控制台**
   - https://open.iot.10086.cn/console/
   - 查看产品和设备信息

3. **技术支持**
   - OneNET论坛
   - 官方QQ群
   - 工单系统

### 调试技巧

1. **使用MQTT客户端测试**
   - 下载MQTT.fx或MQTT Explorer
   - 使用相同的配置测试连接
   - 排除代码问题

2. **查看详细日志**
   ```
   Logcat过滤器：MqttManager
   级别：Debug
   ```

3. **网络抓包**
   - 使用Wireshark抓包
   - 分析MQTT CONNECT报文
   - 查看认证字段

## 总结

### 问题根源
使用了产品级Token（`devices/project`）进行MQTT连接，但OneNET要求使用设备级Token（`devices/test1`）。

### 解决步骤
1. 在OneNET平台获取设备`test1`的密钥或Token
2. 替换代码中的`DEVICE_KEY`
3. 确保`res`参数包含正确的设备名称
4. 重新编译运行

### 关键点
- ✅ MQTT连接必须使用**设备级**认证
- ✅ HTTP API可以使用**产品级**认证
- ✅ 设备名称必须与平台完全一致
- ✅ Token不能过期

### 下一步
获取正确的设备认证信息后，替换代码即可成功连接！🚀
