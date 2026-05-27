# OneNET 物联网监控 APP

## 项目说明

这是一个完整的 Android 物联网监控应用，用于从中国移动 OneNET 平台获取并显示设备数据。

## 功能特性

✅ 开机自动获取设备数据  
✅ 实时显示湿度和光照数据  
✅ 每2秒自动刷新数据  
✅ 显示连接状态  
✅ 简洁美观的 UI 界面  

## 技术实现

- **开发语言**: Java
- **最低兼容**: Android 8.0 (API 26)
- **通信方式**: HTTP API
- **后台服务**: 中国移动 OneNET 物联网平台

## OneNET 配置信息

- **产品 ID**: 3ffd453bafa04ffbb8775ee13eead513
- **设备名称**: test1
- **鉴权信息**: 67k36rzgOO
- **物模型标识符**: humidity（湿度）、light（光照）

## 项目结构

```
app/src/main/
├── java/com/example/onenet215/
│   └── MainActivity.java          # 主活动，包含所有业务逻辑
├── res/layout/
│   └── activity_main.xml          # 主界面布局
└── AndroidManifest.xml            # 应用清单文件（包含网络权限）
```

## 编译和运行

### 前置要求

1. 安装 Android Studio
2. 配置 Android SDK（最低 API 26）

### 运行步骤

1. 在 Android Studio 中打开项目文件夹
2. 等待 Gradle 同步完成（首次打开可能需要下载依赖）
3. 点击 "Run" 按钮或按 Shift+F10 运行应用
4. 选择模拟器或真机进行调试

### 注意事项

- 确保设备已连接网络
- 首次运行需要授予网络权限
- 如果数据显示 "--"，请检查 OneNET 设备是否在线并有数据上报

## 核心功能说明

### 1. 网络请求
- 使用 HttpURLConnection 进行 HTTP GET 请求
- 请求 URL: `https://iot-api.heclouds.com/thingmodel/query-device-property`
- 请求头: `authorization: 67k36rzgOO`
- 参数: `product_id` 和 `device_name`

### 2. 数据处理
- 子线程执行网络请求（避免阻塞主线程）
- 解析 JSON 响应，提取 humidity 和 light 值
- 主线程更新 UI（符合 Android 规范）

### 3. 自动刷新
- 使用 Handler 实现定时任务
- 每 2 秒自动刷新一次数据
- 在 onDestroy 中停止刷新，避免内存泄漏

### 4. UI 设计
- 使用 CardView 卡片式布局
- 显示连接状态（绿色=已连接，红色=连接失败）
- 显示最后更新时间
- 响应式设计，适配不同屏幕

## 常见问题

### Q: 编译时出现 "Unresolved reference 'cardview'" 错误
A: 这是 Gradle 同步问题，请执行以下操作：
   1. 点击 Android Studio 菜单栏的 "File" → "Sync Project with Gradle Files"
   2. 或者点击工具栏中的大象图标（Sync Project）
   3. 等待同步完成后重新编译

### Q: 应用运行时显示 "连接失败"
A: 请检查：
   1. 设备是否已连接网络
   2. OneNET 产品 ID 和设备名称是否正确
   3. 鉴权信息是否正确
   4. OneNET 设备是否在线

### Q: 数据显示 "--"
A: 请检查：
   1. OneNET 设备上是否有 humidity 和 light 数据上报
   2. 物模型标识符是否正确
   3. 查看 Logcat 日志，搜索 "MainActivity" 标签查看详细错误信息

## 日志调试

在 Android Studio 的 Logcat 中可以查看以下日志：
- 标签: `MainActivity`
- 关键日志:
  - `Response Code`: HTTP 响应码
  - `Response`: 完整的 JSON 响应
  - `Error`: 错误信息

## 许可证

本项目仅供学习和参考使用。

## 联系方式

如有问题，请查看 OneNET 官方文档：https://open.iot.10086.cn/doc/
