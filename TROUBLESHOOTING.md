# OneNET 物联网监控 APP - 完整问题排查与修复报告

## 📋 排查顺序与结果

### ✅ 1. 权限检查

**问题**: 是否缺少 INTERNET 权限？

**检查结果**: 
- ✅ `INTERNET` 权限已添加
- ✅ `ACCESS_NETWORK_STATE` 权限已添加
- ✅ `usesCleartextTraffic="true"` 已设置

**AndroidManifest.xml 配置**:
```xml
<uses-permission android:name="android.permission.INTERNET" />
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />

<application
    ...
    android:usesCleartextTraffic="true">
```

**结论**: ✅ 权限配置正确

---

### ⚠️ 2. 接口地址检查

**问题**: 地址、参数、请求方式是否正确？

**当前配置**:
```java
API_URL = "https://iot-api.heclouds.com/thingmodel/query-device-property"
请求方式: GET
参数: product_id=3ffd453bafa04ffbb8775ee13eead513&device_name=test1
```

**OneNET 官方文档要求**:
- ✅ API 地址正确
- ✅ 请求方式正确（GET）
- ✅ 参数名称正确（product_id, device_name）

**结论**: ✅ 接口地址和参数正确

---

### ❗ 3. 请求头 authorization 检查（关键问题）

**问题**: 鉴权是否正确？

**当前代码**:
```java
connection.setRequestProperty("authorization", AUTHORIZATION);
```

**OneNET API 要求**:
根据 OneNET 官方文档，物模型查询接口的鉴权方式有以下几种：

#### 方式一：使用 api-key（推荐）
```
Header: api-key: YOUR_API_KEY
```

#### 方式二：使用 token
```
Header: authorization: token YOUR_TOKEN
```

**问题分析**:
1. 您提供的 `67k36rzgOO` 可能是 **api-key** 而不是 token
2. 如果使用 api-key，请求头应该是 `api-key` 而不是 `authorization`
3. 如果确实是 token，格式应该是 `authorization: token 67k36rzgOO`

**修复方案**:

**方案 A：如果 67k36rzgOO 是 api-key（最可能）**
```java
connection.setRequestProperty("api-key", "67k36rzgOO");
```

**方案 B：如果 67k36rzgOO 是 token**
```java
connection.setRequestProperty("authorization", "token 67k36rzgOO");
```

**建议**: 先尝试方案 A，因为大多数情况下这是 api-key

**结论**: ❗ **这是最可能导致"在线但无数据"的原因**

---

### ✅ 4. 子线程/主线程检查

**问题**: 是否在主线程请求网络导致失败？

**当前代码**:
```java
private void fetchDeviceData() {
    new Thread(new Runnable() {
        @Override
        public void run() {
            // 网络请求在子线程执行
            ...
            
            // UI 更新在主线程执行
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    updateConnectionStatus(true);
                    updateLastUpdateTime();
                }
            });
        }
    }).start();
}
```

**检查结果**:
- ✅ 网络请求在子线程（new Thread）
- ✅ UI 更新在主线程（runOnUiThread）
- ✅ 符合 Android 规范

**结论**: ✅ 线程处理正确

---

### ⚠️ 5. JSON 解析检查

**问题**: 解析逻辑是否匹配 OneNET 返回格式？

**OneNET 典型响应格式**:
```json
{
    "errno": 0,
    "error": "succ",
    "data": [
        {
            "identifier": "humidity",
            "value": "65.3",
            "update_time": "2024-01-01 12:00:00"
        },
        {
            "identifier": "light",
            "value": "1200.5",
            "update_time": "2024-01-01 12:00:00"
        }
    ]
}
```

**当前解析逻辑**:
```java
JSONObject jsonObject = new JSONObject(jsonResponse);

if (jsonObject.has("data") && jsonObject.get("data") instanceof JSONArray) {
    JSONArray dataArray = jsonObject.getJSONArray("data");
    
    for (int i = 0; i < dataArray.length(); i++) {
        JSONObject item = dataArray.getJSONObject(i);
        
        if (item.has("identifier") && item.has("value")) {
            String identifier = item.getString("identifier");
            String value = item.getString("value");
            
            if ("humidity".equals(identifier)) {
                humidity = Double.parseDouble(value);
            } else if ("light".equals(identifier)) {
                light = Double.parseDouble(value);
            }
        }
    }
}
```

**检查结果**:
- ✅ 解析逻辑基本正确
- ⚠️ 但没有检查 `errno` 字段
- ⚠️ 错误提示不够详细

**改进建议**:
1. 添加 errno 检查
2. 添加更详细的日志
3. 区分大小写比较（使用 equalsIgnoreCase）

**结论**: ⚠️ 需要增强错误处理

---

### ✅ 6. 参数拼写检查

**问题**: product_id、device_name、identifier 是否正确？

**检查结果**:
```java
PRODUCT_ID = "3ffd453bafa04ffbb8775ee13eead513"  // ✅ 正确
DEVICE_NAME = "test1"                             // ✅ 正确
identifier: "humidity"                            // ✅ 正确
identifier: "light"                               // ✅ 正确
```

**注意事项**:
- ⚠️ identifier 区分大小写，建议使用 `equalsIgnoreCase()`
- ⚠️ 确保 OneNET 平台上的物模型标识符确实是 `humidity` 和 `light`

**结论**: ✅ 参数拼写正确，但建议改为不区分大小写

---

### ✅ 7. SSL / 明文网络限制

**问题**: 是否需要处理 SSL / 明文网络限制？

**当前配置**:
```xml
android:usesCleartextTraffic="true"
```

**检查结果**:
- ✅ API 使用 HTTPS（`https://iot-api.heclouds.com`）
- ✅ 已设置 usesCleartextTraffic
- ✅ Android 9+ 默认允许 HTTPS

**结论**: ✅ SSL 配置正确

---

## 🔧 完整修复方案

### 主要修复点

#### 1. 修改请求头（最关键）

**原代码**:
```java
connection.setRequestProperty("authorization", AUTHORIZATION);
```

**修复后**:
```java
// 尝试使用 api-key（最常见）
connection.setRequestProperty("api-key", AUTHORIZATION);

// 如果不行，再尝试 token 格式
// connection.setRequestProperty("authorization", "token " + AUTHORIZATION);
```

#### 2. 增强错误处理

添加 errno 检查和详细日志：
```java
if (jsonObject.has("errno")) {
    int errno = jsonObject.getInt("errno");
    if (errno != 0) {
        String errmsg = jsonObject.optString("errmsg", "未知错误");
        Log.e(TAG, "OneNET Error: errno=" + errno + ", errmsg=" + errmsg);
        // 显示错误信息
        return;
    }
}
```

#### 3. 改进 identifier 匹配

**原代码**:
```java
if ("humidity".equals(identifier)) {
```

**修复后**:
```java
if ("humidity".equalsIgnoreCase(identifier)) {
```

#### 4. 增加详细日志

在关键位置添加日志，便于调试：
```java
Log.d(TAG, "Request URL: " + urlString);
Log.d(TAG, "Authorization Header: " + AUTHORIZATION);
Log.d(TAG, "Response Code: " + responseCode);
Log.d(TAG, "Response Body: " + jsonResponse);
Log.d(TAG, "Found property - Identifier: " + identifier + ", Value: " + value);
```

---

## 📝 使用步骤

### 步骤 1: 确认鉴权类型

登录 OneNET 控制台，确认 `67k36rzgOO` 是什么类型：

1. 进入产品管理
2. 找到您的产品
3. 查看"API密钥"或"鉴权信息"
4. 确认是 **api-key** 还是 **token**

### 步骤 2: 应用修复代码

我已经创建了修复后的文件：`FIXED_MainActivity.java`

**替换方法**:
```bash
# 备份原文件
cp app/src/main/java/com/example/onenet215/MainActivity.java \
   app/src/main/java/com/example/onenet215/MainActivity.java.bak

# 使用修复后的文件
cp FIXED_MainActivity.java \
   app/src/main/java/com/example/onenet215/MainActivity.java
```

或者在 Android Studio 中：
1. 打开 `FIXED_MainActivity.java`
2. 复制全部内容
3. 打开 `MainActivity.java`
4. 全选并粘贴
5. 保存文件

### 步骤 3: 测试不同鉴权方式

#### 测试 A：使用 api-key（推荐先试这个）

在 MainActivity.java 中找到：
```java
connection.setRequestProperty("api-key", AUTHORIZATION);
```

这已经是默认配置，直接运行即可。

#### 测试 B：如果 A 不行，改用 token

修改为：
```java
connection.setRequestProperty("authorization", "token " + AUTHORIZATION);
```

### 步骤 4: 查看日志调试

运行应用后，打开 Android Studio 的 Logcat：

1. 过滤器设置为：`MainActivity`
2. 查看关键日志：

**成功的情况**:
```
D/MainActivity: Request URL: https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=...&device_name=test1
D/MainActivity: Response Code: 200
D/MainActivity: Response Body: {"errno":0,"data":[...]}
D/MainActivity: Found property - Identifier: humidity, Value: 65.3
D/MainActivity: ✓ Humidity found: 65.3
D/MainActivity: UI Updated - Humidity: 65.3
```

**鉴权失败的情况**:
```
D/MainActivity: Response Code: 401
E/MainActivity: HTTP Error Code: 401
E/MainActivity: Error Response: {"errno":401,"error":"authentication failed"}
```

**没有数据的情况**:
```
D/MainActivity: Response Code: 200
D/MainActivity: Response Body: {"errno":0,"data":[]}
W/MainActivity: ⚠ Humidity data not found in response
W/MainActivity: ⚠ Light data not found in response
```

---

## 🎯 常见问题诊断

### 问题 1: 显示"已连接"但数据是"无数据"

**可能原因**:
1. ✅ 网络连接成功
2. ❌ 设备没有上报 humidity/light 数据
3. ❌ 物模型标识符不匹配

**解决方法**:
1. 登录 OneNET 控制台
2. 进入设备管理 → test1
3. 查看"物模型数据"
4. 确认是否有 humidity 和 light 的数据
5. 如果没有，需要在设备上上报这些数据

### 问题 2: 显示"连接失败"或 HTTP 401

**可能原因**:
1. ❌ 鉴权信息错误
2. ❌ 请求头格式不正确

**解决方法**:
1. 确认使用的是 api-key 还是 token
2. 尝试不同的请求头格式：
   - `api-key: 67k36rzgOO`
   - `authorization: token 67k36rzgOO`
   - `authorization: 67k36rzgOO`

### 问题 3: 显示"JSON 解析错误"

**可能原因**:
1. ❌ 响应格式不符合预期
2. ❌ 返回了 HTML 错误页面

**解决方法**:
1. 查看 Logcat 中的完整响应内容
2. 确认返回的是 JSON 格式
3. 检查 URL 和参数是否正确

---

## 📊 完整的请求示例

### 正确的 cURL 测试命令

在电脑上测试 API 是否正常：

```bash
# 使用 api-key
curl -X GET "https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=3ffd453bafa04ffbb8775ee13eead513&device_name=test1" \
  -H "api-key: 67k36rzgOO"

# 或使用 token
curl -X GET "https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=3ffd453bafa04ffbb8775ee13eead513&device_name=test1" \
  -H "authorization: token 67k36rzgOO"
```

如果 cURL 能获取到数据，说明 API 配置正确，问题在 Android 代码。

---

## ✅ 修复清单

- [x] 1. 权限配置正确
- [x] 2. 接口地址正确
- [ ] 3. **请求头格式需要确认（最关键）**
- [x] 4. 线程处理正确
- [x] 5. JSON 解析已增强
- [x] 6. 参数拼写正确
- [x] 7. SSL 配置正确
- [x] 8. 添加了详细日志
- [x] 9. 添加了 errno 检查
- [x] 10. identifier 匹配改为不区分大小写

---

## 🚀 快速修复步骤总结

1. **使用我提供的 FIXED_MainActivity.java 替换原文件**
2. **确认鉴权类型**（api-key 还是 token）
3. **运行应用并查看 Logcat 日志**
4. **根据日志输出判断问题所在**
5. **如果还是不行，在 OneNET 控制台确认设备是否上报了数据**

---

## 📞 技术支持

如果按照以上步骤仍然无法解决，请提供以下信息：

1. Logcat 完整日志（从启动到第一次刷新）
2. OneNET 控制台中设备的物模型数据截图
3. 使用 cURL 测试 API 的结果
4. 确认 67k36rzgOO 是 api-key 还是 token

---

**最后更新**: 2026-05-11  
**版本**: v2.0 - 完整修复版
