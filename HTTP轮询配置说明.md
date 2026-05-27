# HTTP轮询数据获取配置说明

## 修改概述

根据参考代码和OneNET官方文档，已将HTTP数据获取方式修改为使用**物模型查询接口**，并使用正确的Token认证方式。

## 关键配置

### 1. API地址
```
https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=67k36rzgOO&device_name=test1
```

**说明：**
- 使用OneNET融合云平台的物模型查询接口
- `product_id`: 67k36rzgOO（您的产品ID）
- `device_name`: test1（您的设备名称）

### 2. Token认证
```
version=2018-10-31&res=products%2F67k36rzgOO%2Fdevices%2Fproject&et=1830297599&method=md5&sign=bt7UPGU6PGqQlnhj6joIzQ%3D%3D
```

**说明：**
- 这是产品级Token，适用于查询产品下所有设备的数据
- 通过`authorization`请求头传递
- **注意：** 不是使用`api-key`，而是使用`authorization`

### 3. 请求方式
- **方法：** GET
- **请求头：** `authorization: {token}`
- **超时设置：** 
  - 连接超时：3秒
  - 读取超时：5秒

## 数据格式

### OneNET返回格式
```json
{
  "errno": 0,
  "error": "succ",
  "data": [
    {
      "identifier": "temperature",
      "value": "25.5"
    },
    {
      "identifier": "humidity",
      "value": "60"
    },
    {
      "identifier": "light",
      "value": "1200"
    }
  ]
}
```

### 解析后的统一格式
```json
[
  {"identifier": "temperature", "value": "25.5"},
  {"identifier": "humidity", "value": "60"},
  {"identifier": "light", "value": "1200"}
]
```

## 轮询机制

### 轮询间隔
- **默认：** 10秒
- **可配置：** 在`DataFetcher.java`中修改`POLL_INTERVAL`常量

### 工作流程
1. 应用启动时立即执行一次HTTP请求
2. 之后每10秒自动执行一次请求
3. 请求成功后更新UI并缓存数据
4. 请求失败时保持上次成功的数据，不影响用户操作

### 数据缓存
- 使用`SharedPreferences`存储最后一次成功获取的数据
- 应用重启时会加载缓存数据，避免空白显示
- 缓存键名：`DataFetcherCache`

## 与参考代码的对比

### 相同点
1. ✅ 使用相同的API地址格式
2. ✅ 使用相同的Token认证方式（`authorization`请求头）
3. ✅ 使用`HttpsURLConnection`进行HTTPS请求
4. ✅ 使用Handler实现定时轮询
5. ✅ JSON解析逻辑一致（遍历data数组，提取identifier和value）

### 改进点
1. ✅ 增加了errno错误检查
2. ✅ 增加了详细的日志输出
3. ✅ 支持多种数据格式（JSONArray和JSONObject）
4. ✅ 更好的容错处理（请求失败不清空数据）
5. ✅ 数据持久化缓存

## 测试验证

### 1. 查看日志
运行APP后，在Logcat中过滤`DataFetcher`标签，应该看到：
```
D/DataFetcher: 启动数据轮询器，间隔: 10秒
D/DataFetcher: HTTP请求成功，响应数据: {...}
D/DataFetcher: 解析完成，共 X 条数据
D/DataFetcher: 数据已保存到缓存
```

### 2. 验证数据
- 温度、湿度、光照等数据应该正常显示
- 每10秒自动更新一次
- 点击"刷新数据"按钮可以手动触发更新

### 3. 错误处理
如果Token过期或配置错误，会看到：
```
E/DataFetcher: API返回错误: errno=401, error=authentication failed
W/DataFetcher: HTTP请求失败，状态码: 401
```

## 常见问题

### Q1: 显示"无数据"或"--"
**可能原因：**
1. Token已过期（et=1830297599对应的时间戳）
2. 设备没有上报数据
3. 物模型标识符不匹配

**解决方法：**
1. 检查OneNET控制台，确认设备有数据上报
2. 查看Logcat中的完整响应数据
3. 确认物模型中的属性标识符（identifier）是否正确

### Q2: HTTP 401错误
**原因：** Token认证失败

**解决方法：**
1. 检查Token是否正确
2. 确认Token未过期（et参数）
3. 重新生成Token

### Q3: 数据不更新
**可能原因：**
1. 网络连接问题
2. 设备离线
3. 轮询器未启动

**解决方法：**
1. 检查网络连接
2. 查看Logcat日志
3. 确认DataFetcher已启动（`isRunning() == true`）

## 下一步优化建议

### 1. Token动态生成
当前使用固定Token，建议改为动态生成：
```java
// 使用token.java类动态生成Token
token tk = new token();
String ok_key = tk.key(user_id, user_accesskey);
```

### 2. 添加设备控制功能
参考代码中的`HttpRequest()`方法，可以实现设备控制：
```java
POST https://iot-api.heclouds.com/thingmodel/set-device-property
Content-Type: application/json
authorization: {token}

{
  "product_id": "67k36rzgOO",
  "device_name": "test1",
  "params": {
    "led": true
  }
}
```

### 3. 增加重试机制
当请求失败时，可以增加重试次数：
```java
private static final int MAX_RETRY = 3;
```

## 参考资料

- [OneNET物模型API文档](https://open.iot.10086.cn/doc/v5/fuse/detail/919)
- 参考代码：`D:\Dsesktop\大三下\物联网系统开发\2-软件端实验资料\2-软件端实验资料\MainActivity.java`
