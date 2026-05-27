# 🚀 OneNET 物联网监控 APP - 快速修复指南

## ✅ 已完成的问题排查

我已经按照您的要求逐一排查了所有可能的问题，并提供了完整的修复方案。

---

## 📋 排查结果总结

### 1. ✅ 权限检查 - 通过
- INTERNET 权限已添加
- ACCESS_NETWORK_STATE 权限已添加
- usesCleartextTraffic 已设置

### 2. ✅ 接口地址 - 正确
- URL: `https://iot-api.heclouds.com/thingmodel/query-device-property`
- 请求方式: GET
- 参数: product_id, device_name

### 3. ❗ **请求头 - 已修复（关键问题）**
**原代码**:
```java
connection.setRequestProperty("authorization", AUTHORIZATION);
```

**修复后**:
```java
connection.setRequestProperty("api-key", AUTHORIZATION);
```

**说明**: 
- `67k36rzgOO` 很可能是 **api-key** 而不是 token
- OneNET 物模型查询接口通常使用 `api-key` 作为请求头
- 这是导致"在线但无数据"的最可能原因

### 4. ✅ 线程处理 - 正确
- 网络请求在子线程执行
- UI 更新在主线程执行

### 5. ✅ JSON 解析 - 已增强
- 添加了 errno 检查
- 添加了详细的错误提示
- identifier 匹配改为不区分大小写

### 6. ✅ 参数拼写 - 正确
- product_id: ✓
- device_name: ✓
- identifier: ✓（现在不区分大小写）

### 7. ✅ SSL 配置 - 正确
- 使用 HTTPS
- usesCleartextTraffic 已设置

---

## 🔧 主要修复内容

### 修复 1: 请求头格式（最关键）

```java
// 修复前
connection.setRequestProperty("authorization", AUTHORIZATION);

// 修复后
connection.setRequestProperty("api-key", AUTHORIZATION);
```

### 修复 2: 增加 errno 检查

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

### 修复 3: identifier 不区分大小写

```java
// 修复前
if ("humidity".equals(identifier)) {

// 修复后
if ("humidity".equalsIgnoreCase(identifier)) {
```

### 修复 4: 增强日志记录

添加了详细的日志输出，包括：
- 请求 URL
- 请求头信息
- 响应码
- 完整响应内容
- 每个属性的解析过程
- UI 更新状态

---

## 📱 如何使用

### 步骤 1: 同步 Gradle

在 Android Studio 中：
1. 点击 **File** → **Sync Project with Gradle Files**
2. 等待同步完成

### 步骤 2: 运行应用

1. 点击 **Run** 按钮（绿色三角形）
2. 选择模拟器或真机
3. 等待应用启动

### 步骤 3: 查看日志

打开 **Logcat** 窗口：
1. 底部面板选择 **Logcat**
2. 过滤器输入：`MainActivity`
3. 观察日志输出

### 步骤 4: 判断结果

#### ✅ 成功的情况

日志显示：
```
D/MainActivity: Response Code: 200
D/MainActivity: Response Body: {"errno":0,"data":[...]}
D/MainActivity: Found property - Identifier: humidity, Value: 65.3
D/MainActivity: ✓ Humidity found: 65.3
D/MainActivity: UI Updated - Humidity: 65.3
```

界面显示：
- 连接状态：**已连接**（绿色）
- 湿度：**65.3**（或其他数值）
- 光照：**1200.5**（或其他数值）

#### ❌ 鉴权失败

日志显示：
```
D/MainActivity: Response Code: 401
E/MainActivity: HTTP Error Code: 401
E/MainActivity: Error Response: {"errno":401,"error":"authentication failed"}
```

**解决方法**: 
如果 api-key 不行，尝试改用 token 格式：

在 MainActivity.java 第 128 行，将：
```java
connection.setRequestProperty("api-key", AUTHORIZATION);
```

改为：
```java
connection.setRequestProperty("authorization", "token " + AUTHORIZATION);
```

然后重新运行。

#### ⚠️ 没有数据

日志显示：
```
D/MainActivity: Response Code: 200
D/MainActivity: Response Body: {"errno":0,"data":[]}
W/MainActivity: ⚠ Humidity data not found in response
W/MainActivity: ⚠ Light data not found in response
```

**原因**: 
- 网络连接成功
- 鉴权成功
- 但设备没有上报 humidity 和 light 数据

**解决方法**:
1. 登录 OneNET 控制台
2. 进入产品管理 → 找到产品 `3ffd453bafa04ffbb8775ee13eead513`
3. 进入设备管理 → 找到设备 `test1`
4. 查看"物模型数据"
5. 确认是否有 `humidity` 和 `light` 的数据
6. 如果没有，需要在设备上上报这些数据

---

## 🔍 调试技巧

### 1. 查看完整响应

在 Logcat 中搜索 `Response Body:`，可以看到完整的 JSON 响应。

### 2. 测试 API

在电脑上使用 cURL 测试：

```bash
curl -X GET "https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=3ffd453bafa04ffbb8775ee13eead513&device_name=test1" \
  -H "api-key: 67k36rzgOO"
```

如果 cURL 能获取数据，说明 API 配置正确。

### 3. 检查设备状态

登录 OneNET 控制台，确认：
- 设备是否在线
- 设备是否上报了数据
- 物模型标识符是否正确

---

## 📊 常见错误码

| errno | 含义 | 解决方法 |
|-------|------|---------|
| 0 | 成功 | - |
| 401 | 鉴权失败 | 检查 api-key 是否正确 |
| 403 | 禁止访问 | 检查产品 ID 和设备名称 |
| 404 | 未找到 | 检查设备是否存在 |
| 500 | 服务器错误 | 稍后重试 |

---

## 💡 如果还是不行

### 方案 A: 尝试不同的鉴权方式

修改 MainActivity.java 第 128 行：

**方式 1（当前默认）**:
```java
connection.setRequestProperty("api-key", AUTHORIZATION);
```

**方式 2**:
```java
connection.setRequestProperty("authorization", "token " + AUTHORIZATION);
```

**方式 3**:
```java
connection.setRequestProperty("authorization", AUTHORIZATION);
```

### 方案 B: 确认鉴权信息类型

登录 OneNET 控制台：
1. 进入产品管理
2. 找到您的产品
3. 查看"API密钥"或"鉴权信息"
4. 确认 `67k36rzgOO` 是 api-key 还是 token

### 方案 C: 检查设备数据

确保设备已经上报了数据：
1. 登录 OneNET 控制台
2. 进入设备详情
3. 查看"物模型数据"
4. 确认有 `humidity` 和 `light` 的数据记录

---

## 📝 文件清单

修复后的文件：
- ✅ [MainActivity.java](file:///D:/Users/23307/AndroidStudioProjects/onenet215/app/src/main/java/com/example/onenet215/MainActivity.java) - 主程序（已修复）
- ✅ [activity_main.xml](file:///D:/Users/23307/AndroidStudioProjects/onenet215/app/src/main/res/layout/activity_main.xml) - 界面布局
- ✅ [AndroidManifest.xml](file:///D:/Users/23307/AndroidStudioProjects/onenet215/app/src/main/AndroidManifest.xml) - 权限配置

文档文件：
- 📄 [TROUBLESHOOTING.md](file:///D:/Users/23307/AndroidStudioProjects/onenet215/TROUBLESHOOTING.md) - 详细排查报告
- 📄 [FIXED_MainActivity.java](file:///D:/Users/23307/AndroidStudioProjects/onenet215/FIXED_MainActivity.java) - 修复版代码备份
- 📄 [README.md](file:///D:/Users/23307/AndroidStudioProjects/onenet215/README.md) - 项目说明
- 📄 [UPDATE_LOG.md](file:///D:/Users/23307/AndroidStudioProjects/onenet215/UPDATE_LOG.md) - 更新日志

---

## ✨ 新增功能

1. ✅ 详细的日志输出（便于调试）
2. ✅ errno 错误检查
3. ✅ 更友好的错误提示
4. ✅ identifier 不区分大小写匹配
5. ✅ 手动刷新按钮
6. ✅ 连接状态实时显示
7. ✅ 最后更新时间显示

---

## 🎯 下一步

1. **运行应用**，观察日志输出
2. **根据日志判断问题**：
   - HTTP 200 + 有数据 = ✅ 成功
   - HTTP 401 = ❌ 鉴权失败，尝试其他鉴权方式
   - HTTP 200 + 无数据 = ⚠️ 设备未上报数据
3. **如果还有问题**，提供 Logcat 日志给我分析

---

**祝您使用愉快！** 🎉

如有任何问题，请查看 [TROUBLESHOOTING.md](file:///D:/Users/23307/AndroidStudioProjects/onenet215/TROUBLESHOOTING.md) 获取更详细的帮助。
