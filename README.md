# 充电棚智能消防监控系统

基于 OneNET 云平台的充电棚消防安全 Android 监控 App，支持 **HTTP 轮询数据采集**、**传感器状态可视化**、**设备远程控制**、**AI 智能诊断**及**系统告警通知**。

---

## 功能概览

| 模块 | 说明 |
|------|------|
| **传感器页面** | 温度/湿度/光照/PM2.5/烟雾/火焰实时展示 + 下拉刷新 + 温度趋势图（MPAndroidChart） |
| **状态告警页面** | 火焰/倾斜/振动/舵机角度告警汇总，风扇运行呼吸动画 |
| **设备控制页面** | 风扇/继电器开关、舵机角度 0-180° 调节、温度/烟雾/PM2.5 阈值设置 |
| **AI 助手页面** | 专业/普通双模式对话 + 一键传感器安全诊断（DeepSeek API） |
| **历史数据看板** | Room 数据库本地持久化，温度 & 烟雾双线趋势图 + 数据日志流水 |
| **系统通知** | 火焰/温度/PM2.5/振动告警自动推送通知栏，点击跳转 App |

---

## 技术栈

| 类别 | 技术 |
|------|------|
| **语言** | Java 11 |
| **UI 框架** | Material3 + ViewPager2 + CardView + SwipeRefreshLayout + NestedScrollView |
| **网络通信** | HttpURLConnection（OneNET HTTP 轮询 + 控制指令），OkHttp（AI API） |
| **数据可视化** | MPAndroidChart（温度/烟雾趋势折线图） |
| **数据库** | Room（SQLite）传感器记录本地持久化 |
| **AI 集成** | DeepSeek API（deepseek-chat）一键诊断 + 双模式对话 |
| **天气服务** | Open-Meteo 免费天气 API |
| **构建工具** | Gradle Kotlin DSL（AGP 8.x） |

---

## 构建说明

### 环境要求
- Android Studio 2024+
- JDK 11+
- Android SDK 26+

### API Key 配置
项目使用 DeepSeek API 提供 AI 对话功能，API Key 通过 `local.properties` 配置（**已 Git 忽略，不会提交到仓库**）：

1. 在项目根目录创建/编辑 `local.properties`：
```properties
sdk.dir=你的Android SDK路径
DEEPSEEK_API_KEY=你的DeepSeek_API_Key
```

2. 编译运行即可，Gradle 会自动将 Key 注入 `BuildConfig.DEEPSEEK_API_KEY`

### 编译
```bash
./gradlew assembleDebug
```
输出 APK：`app/build/outputs/apk/debug/充电棚消防网关_v1.0_debug.apk`

---

## 项目结构

```
app/src/main/java/com/example/onenet215/
├── MainActivity.java          # 主 Activity，数据协调 & Fragment 管理
├── HistoryActivity.java       # 历史数据看板
├── LoginActivity.java         # 登录页面
├── FragmentSensor.java        # 传感器展示 Fragment
├── FragmentStatus.java        # 状态告警 Fragment
├── FragmentControl.java       # 设备控制 Fragment
├── FragmentAiAssistant.java   # AI 助手 Fragment
├── DataFetcher.java           # OneNET HTTP 轮询 & 控制指令
├── DeepSeekApiClient.java     # DeepSeek AI API 客户端
├── WeatherApiClient.java      # Open-Meteo 天气 API 客户端
├── AppDatabase.java           # Room 数据库
├── SensorRecord.java          # 传感器记录实体
└── SensorRecordDao.java       # 数据库 DAO
```

---

## 数据流架构

```
OneNET 云平台
      │
      ▼
  HTTP 轮询 (15s/次)
      │
      ▼
  parseDeviceData() → notifyAllFragments() → UI 刷新
      │
      ▼
  用户操作 → controlDevice() → HTTP POST 下发指令
      │
      ▼
  Room 数据库 (15s 定时写入)
```
