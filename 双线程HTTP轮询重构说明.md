# 双线程HTTP轮询重构说明

## 📋 重构概述

根据实验指导书要求，已完成Android端HTTP请求逻辑的重构，实现了双线程调度和指令优先策略。

## 🎯 核心功能

### 1. 双线程调度架构

#### Task_Control（控制任务）
- **执行频率**：每1秒执行一次
- **功能**：检查设备状态、连接状态
- **定时器**：使用独立的Timer实例
- **线程名称**：`Task_Control`

#### Task_Sensor（传感器任务）
- **执行频率**：每15秒执行一次
- **功能**：获取传感器数据（温度、湿度、光照等）
- **定时器**：使用独立的Timer实例
- **线程名称**：`Task_Sensor`
- **可暂停**：支持动态暂停和恢复

### 2. 指令优先策略

当用户点击界面开关时：
1. **立即暂停** Task_Sensor（传感器轮询）
2. **优先发送** 控制指令到服务器
3. **等待响应** 收到服务器确认后
4. **自动恢复** Task_Sensor（延迟2秒后恢复）

**实现代码**：
```java
public void sendControlCommand(String property, boolean value) {
    // 1. 暂停传感器轮询
    pauseSensorPolling();
    
    // 2. 发送控制指令
    new Thread(() -> {
        try {
            // ... HTTP POST 请求 ...
        } finally {
            // 3. 延迟恢复传感器轮询
            mainHandler.postDelayed(this::resumeSensorPolling, 2000);
        }
    }).start();
}
```

### 3. 数据占位机制

当Task_Sensor获取失败时：
- ✅ **保持旧数据**：UI界面不清空，继续显示上次成功的数据
- ⚠️ **延迟提示**：通过`onDataDelay(true)`回调通知UI
- 📊 **日志记录**：在Logcat中标记"⚠ 数据延迟"
- 🔄 **自动恢复**：下次请求成功时自动清除延迟标记

**实现代码**：
```java
// 请求失败时的处理
hasDataDelay = true;
if (callback != null) {
    mainHandler.post(() -> {
        callback.onDataDelay(true);  // 通知UI显示延迟状态
    });
}
```

## 🔧 技术实现

### 1. 数据结构

```java
// 双定时器
private Timer controlTimer;         // Task_Control定时器
private Timer sensorTimer;          // Task_Sensor定时器

// 状态标志
private boolean isRunning = false;      // 轮询器运行状态
private boolean isSensorPaused = false; // 传感器轮询暂停状态
private boolean hasDataDelay = false;   // 数据延迟标记

// 主线程Handler
private Handler mainHandler;  // 用于UI线程通信
```

### 2. 启动流程

```java
public void startPolling() {
    isRunning = true;
    isSensorPaused = false;
    hasDataDelay = false;
    
    // 启动Task_Control（1秒间隔）
    controlTimer = new Timer("Task_Control");
    controlTimer.scheduleAtFixedRate(new TimerTask() {
        @Override
        public void run() {
            fetchDeviceStatus();
        }
    }, 0, CONTROL_INTERVAL);  // 1000ms
    
    // 启动Task_Sensor（15秒间隔）
    sensorTimer = new Timer("Task_Sensor");
    sensorTimer.scheduleAtFixedRate(new TimerTask() {
        @Override
        public void run() {
            if (!isSensorPaused) {  // 检查是否被暂停
                fetchSensorData();
            }
        }
    }, 0, SENSOR_INTERVAL);  // 15000ms
}
```

### 3. 停止流程

```java
public void stopPolling() {
    isRunning = false;
    
    // 取消所有定时器
    if (controlTimer != null) {
        controlTimer.cancel();
        controlTimer = null;
    }
    if (sensorTimer != null) {
        sensorTimer.cancel();
        sensorTimer = null;
    }
}
```

### 4. 回调接口扩展

```java
public interface DataCallback {
    void onDataReceived(JSONObject data);              // 数据接收成功
    void onError(String error);                         // 错误回调
    void onControlSuccess(String property, boolean value);  // 控制成功
    void onDataDelay(boolean hasDelay);                 // 数据延迟状态
}
```

## 📊 API配置

### 查询设备属性（GET）
```
URL: https://iot-api.heclouds.com/thingmodel/query-device-property
参数: product_id=67k36rzgOO&device_name=test1
请求头: authorization: version=2018-10-31&res=products%2F67k36rzgOO%2Fdevices%2Fproject&et=1830297599&method=md5&sign=bt7UPGU6PGqQlnhj6joIzQ%3D%3D
```

### 设置设备属性（POST）
```
URL: https://iot-api.heclouds.com/thingmodel/set-device-property
请求头: 
  - Content-Type: application/json
  - authorization: {token}
请求体:
{
  "product_id": "67k36rzgOO",
  "device_name": "test1",
  "params": {
    "led": true
  }
}
```

## 🎨 UI交互优化

### 1. 控制按钮反馈
- 点击后立即更新按钮状态（本地优先）
- 发送控制指令到服务器
- 收到成功后显示Toast提示

### 2. 数据延迟提示
- 正常状态：Log显示"✓ 数据正常"
- 延迟状态：Log显示"⚠ 数据延迟"
- 可扩展：在UI上添加延迟图标指示器

### 3. 离线保护
- 设备离线时禁用控制按钮
- 按钮透明度降低至50%
- 防止无效操作

## 📝 使用示例

### MainActivity中的集成

```java
// 1. 初始化DataFetcher
dataFetcher = new DataFetcher(this);

// 2. 设置回调
dataFetcher.setDataCallback(new DataFetcher.DataCallback() {
    @Override
    public void onDataReceived(JSONObject data) {
        // 处理接收到的数据
        JSONArray parsedData = DataFetcher.parseResponse(data);
        updateUI(parsedData);
    }
    
    @Override
    public void onError(String error) {
        // 错误处理（静默，不影响UI）
        Log.w(TAG, "HTTP请求失败: " + error);
    }
    
    @Override
    public void onControlSuccess(String property, boolean value) {
        // 控制成功提示
        Toast.makeText(MainActivity.this, 
            "控制成功", Toast.LENGTH_SHORT).show();
    }
    
    @Override
    public void onDataDelay(boolean hasDelay) {
        // 数据延迟提示
        if (hasDelay) {
            Log.w(TAG, "⚠ 数据延迟");
        }
    }
});

// 3. 启动轮询
dataFetcher.startPolling();

// 4. 发送控制指令（自动触发指令优先策略）
public void controlDevice(String property, boolean value) {
    if (dataFetcher != null) {
        dataFetcher.sendControlCommand(property, value);
    }
}
```

## 🔍 调试技巧

### 1. 查看定时器状态
```java
Log.d(TAG, "Task_Control: 检查设备状态");  // 每秒出现一次
Log.d(TAG, "Task_Sensor: HTTP请求成功");   // 每15秒出现一次
```

### 2. 验证指令优先
1. 等待Task_Sensor开始执行
2. 立即点击控制按钮
3. 观察日志：
   ```
   D/DataFetcher: 传感器轮询已暂停
   D/DataFetcher: 发送控制指令: {...}
   D/DataFetcher: 控制指令发送成功
   D/DataFetcher: 传感器轮询已恢复
   ```

### 3. 测试数据延迟
1. 断开网络连接
2. 观察日志出现"⚠ 数据延迟"
3. UI保持上次数据不变
4. 恢复网络后自动恢复正常

## ⚙️ 配置参数

| 参数 | 值 | 说明 |
|------|-----|------|
| CONTROL_INTERVAL | 1000ms | Task_Control执行间隔 |
| SENSOR_INTERVAL | 15000ms | Task_Sensor执行间隔 |
| CONNECT_TIMEOUT | 3000ms | HTTP连接超时 |
| READ_TIMEOUT | 5000ms | HTTP读取超时 |
| RESUME_DELAY | 2000ms | 控制后恢复传感器轮询的延迟 |

## 🚀 性能优势

### 对比单线程轮询

| 特性 | 单线程（10秒） | 双线程（1秒+15秒） |
|------|---------------|-------------------|
| 控制响应速度 | 最多延迟10秒 | 最多延迟1秒 |
| 网络负载 | 中等 | 优化（分离高频/低频） |
| 用户体验 | 一般 | 优秀 |
| 资源消耗 | 低 | 略高但可控 |

### 优化点
1. ✅ 控制指令响应更快（1秒 vs 10秒）
2. ✅ 传感器数据不过频请求（15秒节省流量）
3. ✅ 指令优先避免冲突
4. ✅ 失败容错保证稳定性

## 📌 注意事项

### 1. Token过期
当前使用的Token有效期到2028年，如需更新：
```java
private static final String API_TOKEN = "version=2018-10-31&res=...";
```

### 2. 线程安全
- 所有UI更新必须通过`mainHandler.post()`
- HTTP请求在独立线程中执行
- TimerTask内部做好异常捕获

### 3. 内存管理
- Activity销毁时必须调用`stopPolling()`
- 避免Timer泄漏
```java
@Override
protected void onDestroy() {
    super.onDestroy();
    if (dataFetcher != null) {
        dataFetcher.stopPolling();
    }
}
```

## 🎓 实验指导书对照

### ✅ 已完成要求

1. ✅ **双线程调度**
   - Task_Control：每1秒执行，访问设备状态
   - Task_Sensor：每15秒执行，访问传感器数据

2. ✅ **指令优先策略**
   - 用户点击开关时暂停Task_Sensor
   - 优先发送控制指令
   - 收到响应后恢复轮询

3. ✅ **数据占位**
   - 请求失败时保持旧数据
   - 通过日志提示"数据延迟"
   - 不打断用户操作

## 📖 参考资料

- OneNET物模型API文档：https://open.iot.10086.cn/doc/v5/fuse/detail/919
- Android Timer文档：https://developer.android.com/reference/java/util/Timer
- 参考代码：`D:\Dsesktop\大三下\物联网系统开发\2-软件端实验资料\2-软件端实验资料\MainActivity.java`

---

**重构完成时间**：2026-05-12  
**编译状态**：✅ BUILD SUCCESSFUL  
**测试状态**：待真机测试
