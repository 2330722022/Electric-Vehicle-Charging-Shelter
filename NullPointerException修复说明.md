# NullPointerException 修复说明

## 🔴 问题描述

应用启动后立即崩溃，错误信息：
```
java.lang.NullPointerException: Attempt to invoke virtual method 
'int org.json.JSONArray.length()' on a null object reference
at com.example.onenet215.MainActivity.DealJsonData(MainActivity.java:139)
```

---

## 🔍 问题分析

### 根本原因

`jsonObject.optJSONArray("data")` 返回了 `null`，但代码直接调用了 `data.length()`，导致空指针异常。

### 为什么 data 是 null？

可能的原因：
1. **API 返回了错误信息**（errno != 0），没有 data 字段
2. **Token 鉴权失败**，返回的是错误响应而不是数据
3. **JSON 结构不符合预期**，可能返回的是其他格式

---

## ✅ 修复方案

### 修复 1: 添加 null 检查

**修复前**（会崩溃）：
```java
void DealJsonData(String JSON) throws JSONException {
    JSONObject jsonObject = new JSONObject(JSON);
    JSONArray data = jsonObject.optJSONArray("data");
    for (int i = 0; i < data.length(); i++) {  // ❌ data 可能是 null
        ...
    }
}
```

**修复后**（安全）：
```java
void DealJsonData(String JSON) throws JSONException {
    JSONObject jsonObject = new JSONObject(JSON);
    
    // 检查是否有错误码
    if (jsonObject.has("errno")) {
        int errno = jsonObject.getInt("errno");
        if (errno != 0) {
            String errmsg = jsonObject.optString("errmsg", "未知错误");
            android.util.Log.e(TAG, "OneNET Error: errno=" + errno + ", errmsg=" + errmsg);
            android.util.Log.e(TAG, "Full response: " + JSON);
            return;  // 提前返回，不继续解析
        }
    }
    
    JSONArray data = jsonObject.optJSONArray("data");
    
    // 检查 data 是否为 null
    if (data == null) {
        android.util.Log.e(TAG, "data is null, response: " + JSON);
        return;  // 提前返回，避免空指针异常
    }
    
    // 现在可以安全地遍历 data
    for (int i = 0; i < data.length(); i++) {
        ...
    }
}
```

### 修复 2: 添加详细日志

在 `GetOnenetData()` 方法中添加日志，方便调试：

```java
// 记录请求信息
android.util.Log.d(TAG, "Request URL: " + path);
android.util.Log.d(TAG, "Authorization: " + ok_key);

// 记录响应码
int responseCode = connection.getResponseCode();
android.util.Log.d(TAG, "Response Code: " + responseCode);

// 记录响应内容
String jsonResponse = response.toString();
android.util.Log.d(TAG, "Response Body: " + jsonResponse);

// 记录错误信息
if (responseCode != 200) {
    android.util.Log.e(TAG, "HTTP Error: " + responseCode);
    android.util.Log.e(TAG, "Error Response: " + errorResponse);
}
```

### 修复 3: 增加超时时间

**修复前**：
```java
connection.setConnectTimeout(1500);
connection.setReadTimeout(1500);
```

**修复后**：
```java
connection.setConnectTimeout(5000);  // 5秒
connection.setReadTimeout(5000);     // 5秒
```

---

## 📊 修改的文件

### MainActivity.java

**主要修改**：
1. ✅ 添加了 `TAG` 常量用于日志
2. ✅ 在 `DealJsonData()` 中添加了 errno 检查
3. ✅ 在 `DealJsonData()` 中添加了 data null 检查
4. ✅ 在 `GetOnenetData()` 中添加了详细日志
5. ✅ 增加了超时时间（1500ms → 5000ms）
6. ✅ 改进了错误处理和提示

---

## 🔧 如何调试

### 步骤 1: 运行应用

重新编译并运行应用。

### 步骤 2: 查看 Logcat

打开 Android Studio 的 Logcat 窗口，过滤器设置为 `MainActivity`。

### 步骤 3: 分析日志

#### 情况 A: 正常情况

应该看到类似这样的日志：
```
D/MainActivity: Request URL: https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=...&device_name=test1
D/MainActivity: Authorization: version=2020-05-29&res=...
D/MainActivity: Response Code: 200
D/MainActivity: Response Body: {"errno":0,"data":[{"identifier":"humidity","value":"65.3"},...]}
D/MainActivity: Updated humidity: 65.3
D/MainActivity: Updated light: 1200.5
```

#### 情况 B: Token 鉴权失败

如果看到：
```
D/MainActivity: Response Code: 401
E/MainActivity: HTTP Error: 401
E/MainActivity: Error Response: {"errno":401,"error":"authentication failed"}
```

**解决方法**：
- 检查 Token 生成是否正确
- 查看 System.out 日志中的 Authorization 值
- 确认 user_id 和 user_accesskey 是否正确

#### 情况 C: 没有 data 字段

如果看到：
```
D/MainActivity: Response Code: 200
D/MainActivity: Response Body: {"errno":0,"data":[]}
E/MainActivity: data is null, response: {...}
```

**原因**：设备没有上报数据

**解决方法**：
1. 登录 OneNET 控制台
2. 进入设备管理 → test1
3. 查看"物模型数据"
4. 确认设备是否上报了 humidity 和 light 数据

#### 情况 D: OneNET 返回错误

如果看到：
```
D/MainActivity: Response Code: 200
D/MainActivity: Response Body: {"errno":4,"error":"invalid parameter"}
E/MainActivity: OneNET Error: errno=4, errmsg=invalid parameter
```

**原因**：参数错误（如 product_id 或 device_name 不正确）

**解决方法**：
- 检查 product_id 是否正确
- 检查 device_name 是否正确
- 确认设备是否存在

---

## 🎯 预期结果

修复后，应用应该：
1. ✅ 不再崩溃
2. ✅ 在 Logcat 中显示详细的请求和响应信息
3. ✅ 如果 API 返回错误，显示友好的错误提示
4. ✅ 如果 data 为 null，记录日志但不崩溃
5. ✅ 如果成功获取数据，正常显示湿度和光照

---

## 📝 关键改进点

| 项目 | 修复前 | 修复后 |
|------|--------|--------|
| 空指针检查 | ❌ 没有 | ✅ 有 |
| errno 检查 | ❌ 没有 | ✅ 有 |
| 日志输出 | ⚠️ 很少 | ✅ 详细 |
| 错误提示 | ⚠️ 简单 | ✅ 详细 |
| 超时时间 | 1.5秒 | 5秒 |
| 崩溃风险 | ❌ 高 | ✅ 低 |

---

## 🚀 下一步

1. **重新编译运行应用**
2. **查看 Logcat 日志**，找到以下关键信息：
   - Response Code
   - Response Body
   - 是否有错误信息
3. **根据日志判断问题**：
   - 如果是 401：Token 鉴权失败
   - 如果是 200 但 data 为空：设备没有数据
   - 如果有 errno：查看错误信息
4. **提供日志给我**，如果还有问题

---

## 💡 常见问题

### Q1: 为什么之前会崩溃？

A: 因为 `optJSONArray("data")` 返回 null 时，代码直接调用 `data.length()`，导致空指针异常。

### Q2: 修复后还会崩溃吗？

A: 不会。现在添加了 null 检查，即使 data 是 null 也不会崩溃，只会记录日志并返回。

### Q3: 如果还是看不到数据怎么办？

A: 查看 Logcat 日志：
- 如果 Response Code 是 200，但 data 为空 → 设备没有上报数据
- 如果 Response Code 是 401 → Token 鉴权失败
- 如果有 errno → 查看错误信息

### Q4: 如何确认 Token 是否正确？

A: 在 Logcat 中搜索 `Authorization:`，应该看到完整的 Token 字符串。然后使用 cURL 测试：
```bash
curl -X GET "https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=3ffd453bafa04ffbb8775ee13eead513&device_name=test1" \
  -H "authorization: YOUR_TOKEN_HERE"
```

---

**现在请重新运行应用，并查看 Logcat 日志！** 🚀
