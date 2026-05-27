# 更新日志 - 添加手动刷新按钮

## 更新日期
2026-05-11

## 更新内容

### 1. 新增功能
✅ **添加手动刷新按钮**
- 在界面底部添加了"手动刷新数据"按钮
- 点击按钮可立即获取最新数据
- 按钮采用蓝色主题，与整体 UI 风格一致

### 2. 优化数据显示逻辑
✅ **改进数据缺失处理**
- 当 API 返回成功但没有 humidity 或 light 数据时，显示"无数据"而不是"--"
- 当完全找不到数据时，弹出 Toast 提示"未找到湿度和光照数据"
- 当响应中没有 data 数组时，显示"无数据"并提示"响应中无数据"
- 当 JSON 解析失败时，显示"错误"并显示详细错误信息

### 3. 增强日志记录
✅ **添加详细日志**
- 记录每个找到的属性及其值：`Found property: xxx = xxx`
- 记录湿度更新：`Updated humidity: xxx`
- 记录光照更新：`Updated light: xxx`
- 记录未找到数据的警告：`No humidity data found` / `No light data found`
- 记录响应中无数据的错误：`No data array in response`

## 文件修改清单

### 1. activity_main.xml
**位置**: `app/src/main/res/layout/activity_main.xml`

**修改内容**:
```xml
<Button
    android:id="@+id/btnRefresh"
    android:layout_width="match_parent"
    android:layout_height="wrap_content"
    android:text="手动刷新数据"
    android:textSize="16sp"
    android:layout_marginTop="16dp"
    android:backgroundTint="#2196F3" />
```

### 2. MainActivity.java
**位置**: `app/src/main/java/com/example/onenet215/MainActivity.java`

**修改内容**:

#### (1) 添加 Button 导入和成员变量
```java
import android.widget.Button;

private Button btnRefresh;
```

#### (2) 初始化按钮并设置点击事件
```java
btnRefresh = findViewById(R.id.btnRefresh);

// 设置按钮点击事件
btnRefresh.setOnClickListener(v -> {
    Toast.makeText(MainActivity.this, "正在刷新数据...", Toast.LENGTH_SHORT).show();
    fetchDeviceData();
});
```

#### (3) 优化 parseAndShowData 方法
- 移除 `if (foundHumidity || foundLight)` 条件判断
- 即使没有找到数据，也会更新 UI 显示"无数据"
- 添加详细的日志记录
- 添加更友好的错误提示

## 使用说明

### 如何使用手动刷新按钮

1. **自动刷新**: 应用启动后会自动每 2 秒刷新一次数据
2. **手动刷新**: 点击底部的"手动刷新数据"按钮可立即获取最新数据
3. **查看状态**: 
   - 连接状态显示为绿色"已连接"表示网络请求成功
   - 连接状态显示为红色"连接失败"表示网络请求失败

### 数据显示说明

| 显示内容 | 含义 |
|---------|------|
| 数字（如 65.3） | 成功获取到数据 |
| "无数据" | API 返回成功，但没有该属性的数据 |
| "错误" | JSON 解析失败或其他错误 |
| "--" | 初始状态，尚未获取数据 |

### 调试方法

如果数据显示"无数据"，请按以下步骤排查：

1. **查看 Logcat 日志**
   - 过滤器设置为 `MainActivity`
   - 查看是否有 `Found property: xxx = xxx` 日志
   - 查看是否有 `No humidity data found` 或 `No light data found` 警告

2. **检查 OneNET 设备**
   - 确认设备是否在线
   - 确认设备是否上报了 humidity 和 light 数据
   - 确认物模型标识符是否正确（必须是 `humidity` 和 `light`）

3. **检查网络连接**
   - 确认设备已连接互联网
   - 确认可以访问 `https://iot-api.heclouds.com`

4. **查看完整响应**
   - 在 Logcat 中搜索 `Response:` 查看完整的 JSON 响应
   - 确认响应中包含 `data` 数组
   - 确认 `data` 数组中包含 `identifier` 为 `humidity` 和 `light` 的对象

## 示例日志输出

### 正常情况
```
D/MainActivity: Response Code: 200
D/MainActivity: Response: {"errno":0,"data":[{"identifier":"humidity","value":"65.3"},{"identifier":"light","value":"1200.5"}]}
D/MainActivity: Found property: humidity = 65.3
D/MainActivity: Found property: light = 1200.5
D/MainActivity: Updated humidity: 65.3
D/MainActivity: Updated light: 1200.5
```

### 没有数据的情况
```
D/MainActivity: Response Code: 200
D/MainActivity: Response: {"errno":0,"data":[]}
W/MainActivity: No humidity data found
W/MainActivity: No light data found
```

### 只有部分数据的情况
```
D/MainActivity: Response Code: 200
D/MainActivity: Response: {"errno":0,"data":[{"identifier":"humidity","value":"65.3"}]}
D/MainActivity: Found property: humidity = 65.3
D/MainActivity: Updated humidity: 65.3
W/MainActivity: No light data found
```

## 注意事项

1. **自动刷新仍然有效**: 添加手动刷新按钮后，自动刷新功能（每 2 秒）仍然正常工作
2. **避免频繁点击**: 虽然可以手动刷新，但建议不要过于频繁点击，以免给服务器造成压力
3. **数据延迟**: OneNET 平台的数据可能有几秒的延迟，这是正常现象
4. **权限要求**: 确保应用已获得网络权限（已在 AndroidManifest.xml 中声明）

## 下一步优化建议

1. **添加下拉刷新**: 可以使用 SwipeRefreshLayout 实现下拉刷新
2. **添加刷新指示器**: 在刷新时显示进度条或加载动画
3. **缓存数据**: 在网络断开时显示最后一次成功获取的数据
4. **错误重试**: 添加自动重试机制，在网络失败时自动重试
5. **数据图表**: 添加历史数据图表，显示数据变化趋势
