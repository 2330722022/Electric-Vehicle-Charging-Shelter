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

## 项目结构详解

### 1. 主源码目录 `app/src/main/java/com/example/onenet215/`

#### 核心 Activity 类

| 文件 | 关键内容 |
|------|----------|
| **MainActivity.java** | 主 Activity，数据协调中心<br>• 管理 ViewPager2 + BottomNavigationView 页面切换<br>• `parseDeviceData()` - 解析 OneNET 设备数据<br>• `controlDevice()` - 下发控制指令<br>• `notifyAllFragments()` - 刷新所有 Fragment UI<br>• 系统通知管理（温度/烟雾/PM2.5/振动告警） |
| **HistoryActivity.java** | 历史数据看板<br>• MPAndroidChart 双线折线图（温度+烟雾）<br>• Room 数据库查询展示<br>• RecyclerView 历史记录列表 |
| **LoginActivity.java** | 用户登录页面<br>• Room 数据库用户验证<br>• SharedPreferences 登录状态持久化 |
| **RegisterActivity.java** | 用户注册页面<br>• 新用户写入 Room 数据库 |

#### Fragment 页面类

| 文件 | 关键内容 |
|------|----------|
| **FragmentSensor.java** | 传感器展示页面<br>• 温度/湿度/光照/PM2.5/烟雾/火焰数值展示<br>• 城市天气信息（Open-Meteo API）<br>• 温度趋势折线图<br>• 下拉刷新功能 |
| **FragmentStatus.java** | 状态告警页面<br>• 告警状态汇总展示（火焰/倾斜/振动/舵机）<br>• 风扇运行状态呼吸动画<br>• 设备在线状态指示 |
| **FragmentControl.java** | 设备控制页面<br>• LED/蜂鸣器/风扇开关控制<br>• 舵机角度调节（0-180° SeekBar）<br>• 温度/烟雾/PM2.5 阈值设置<br>• 自动/手动工作模式切换 |
| **FragmentAiAssistant.java** | AI 助手页面<br>• DeepSeek API 双模式对话（专业/普通）<n>• 一键传感器安全诊断<br>• 聊天历史 RecyclerView |

#### 网络通信类

| 文件 | 关键内容 |
|------|----------|
| **DataFetcher.java** | OneNET 云平台通信核心<br>• HTTP 轮询数据采集（15秒间隔）<br>• `sendControlCommand()` - 设备控制指令下发<br>• 数据缓存机制<br>• 回调接口：`OnDataReceivedListener` |
| **DeepSeekApiClient.java** | DeepSeek AI API 客户端<br>• OkHttp 异步请求<br>• 流式响应处理<br>• 传感器数据诊断提示词构建 |
| **WeatherApiClient.java** | Open-Meteo 天气 API 客户端<br>• 实时天气数据获取<br>• 城市坐标配置（35个中国城市） |

#### 数据库相关类

| 文件 | 关键内容 |
|------|----------|
| **AppDatabase.java** | Room 数据库管理器<br>• 单例模式数据库实例<br>• 包含 SensorRecord 和 User 两张表<br>• 数据库名称：`charging_station_db` |
| **SensorRecord.java** | 传感器记录实体类<br>• 字段：timestamp, temperature, humidity, mq2, pm2_5, flame, alarmType<br>• 表名：`sensor_records` |
| **SensorRecordDao.java** | 传感器数据访问接口<br>• `insert()` - 插入记录<br>• `getRecentRecords()` - 查询最近500条 |
| **User.java** | 用户实体类<br>• 字段：username, password, role<br>• 默认管理员：admin/123456 |
| **UserDao.java** | 用户数据访问接口<br>• `login()` - 登录验证<br>• `register()` - 用户注册 |

#### 工具类

| 文件 | 关键内容 |
|------|----------|
| **ToastUtils.java** | Toast 消息工具类<br>• 统一的 Toast 显示封装 |
| **SensorSnapshot.java** | 传感器数据快照类<br>• 用于 AI 诊断时的数据传递 |
| **WeatherInfo.java** | 天气信息数据类<br>• 温度、湿度、风速、天气图标等 |
| **token.java** | OneNET API Token 管理<br>• 设备密钥配置 |

---

### 2. 布局文件目录 `app/src/main/res/layout/`

| 文件 | 关键内容 |
|------|----------|
| **activity_main.xml** | 主界面布局<br>• ViewPager2 + BottomNavigationView<br>• 顶部标题栏 |
| **activity_login.xml** | 登录页面布局<br>• 用户名/密码输入框<br>• 登录/注册按钮 |
| **activity_register.xml** | 注册页面布局 |
| **activity_history.xml** | 历史数据看板布局<br>• MPAndroidChart 折线图区域<br>• RecyclerView 记录列表 |
| **fragment_sensor.xml** | 传感器页面布局<br>• 6个传感器数值卡片（温度/湿度/光照/PM2.5/烟雾/火焰）<br>• 天气信息卡片<br>• 温度趋势图 |
| **fragment_status.xml** | 状态告警页面布局<br>• 告警状态指示器<br>• 风扇状态动画区域<br>• 设备信息展示 |
| **fragment_control.xml** | 设备控制页面布局<br>• LED/蜂鸣器/风扇开关<br>• 舵机角度 SeekBar<br>• 三个阈值调节区域（温度/烟雾/PM2.5） |
| **fragment_ai_assistant.xml** | AI 助手页面布局<br>• 聊天记录 RecyclerView<br>• 输入框+发送按钮<br>• 模式切换按钮 |
| **item_history_record.xml** | 历史记录列表项布局<br>• 时间/告警/温度/湿度/烟雾/PM2.5 字段展示 |

---

### 3. 资源文件目录 `app/src/main/res/`

#### 菜单 `menu/`
| 文件 | 关键内容 |
|------|----------|
| **bottom_nav_menu.xml** | 底部导航栏菜单<br>• 传感器/状态/控制/AI 四个 Tab |

#### 图标 `drawable/`
| 文件 | 关键内容 |
|------|----------|
| **ic_nav_sensor.xml** | 传感器 Tab 图标 |
| **ic_nav_status.xml** | 状态 Tab 图标 |
| **ic_nav_control.xml** | 控制 Tab 图标 |
| **ic_nav_ai.xml** | AI Tab 图标 |
| **ic_circle_red.xml** | 红色状态指示圆点 |
| **ic_circle_green.xml** | 绿色状态指示圆点 |
| **bg_edit_text.xml** | 输入框背景样式 |
| **bg_status_badge.xml** | 状态徽章背景 |
| **bg_top_bar_gradient.xml** | 顶部栏渐变背景 |

#### 颜色/样式 `values/`
| 文件 | 关键内容 |
|------|----------|
| **colors.xml** | 应用颜色定义<br>• 主题色/传感器颜色/状态颜色 |
| **strings.xml** | 字符串资源 |
| **themes.xml** | Material3 主题配置 |
| **styles.xml** | 自定义样式 |

#### 导航栏颜色 `color/`
| 文件 | 关键内容 |
|------|----------|
| **bottom_nav_color.xml** | 底部导航栏选中/未选中颜色状态 |

---

### 4. Gradle 构建配置

| 文件 | 关键内容 |
|------|----------|
| **app/build.gradle.kts** | 模块级构建配置<br>• 依赖库：MPAndroidChart, Room, OkHttp, Material3<br>• BuildConfig 字段注入（DEEPSEEK_API_KEY）<br>• 编译 SDK 34，最小 SDK 26 |
| **build.gradle.kts** (根目录) | 项目级构建配置<br>• AGP 8.x 插件 |
| **settings.gradle.kts** | 项目设置 |
| **gradle/libs.versions.toml** | 版本目录（Version Catalog） |

---

## 数据流架构

```
OneNET 云平台
      │
      ▼
  HTTP 轮询 (15s/次) - DataFetcher.java
      │
      ▼
  parseDeviceData() → notifyAllFragments() → UI 刷新
      │
      ▼
  用户操作 → controlDevice() → HTTP POST 下发指令
      │
      ▼
  Room 数据库 (30s 定时写入) - AppDatabase.java
```

---

## 物模型标识对照表

| 功能 | 标识符 | 数据类型 | 说明 |
|------|--------|----------|------|
| 温度 | `temperature` | float | 环境温度值 |
| 湿度 | `humidity` | float | 环境湿度值 |
| 光照 | `light` | float | 光照强度 |
| PM2.5 | `pm2_5` / `pm2.5` | float | 空气质量 |
| 烟雾 | `mq2` / `smoke` | float | MQ-2 传感器值 |
| 火焰 | `flame` | float | 火焰检测 |
| 温度阈值 | `temp_threshold` | int | 温度告警阈值 |
| **烟雾阈值** | **`mq2_threshold`** | int | 烟雾告警阈值（已修正） |
| PM2.5阈值 | `pm25_threshold` | int | PM2.5告警阈值 |
| LED | `led` | bool | LED开关 |
| 蜂鸣器 | `beep` | bool | 蜂鸣器开关 |
| 风扇 | `fan_en` / `fan` | bool | 风扇开关 |
| 舵机角度 | `steeringstatus` | int | 0-180度 |
| 工作模式 | `work_mode` | bool | 自动/手动 |

---

## 开发注意事项

1. **API Key 安全**：DeepSeek API Key 通过 `local.properties` 注入，不会提交到 Git
2. **数据库版本**：当前 Room 数据库版本为 2，修改实体类需要升级版本号
3. **轮询间隔**：HTTP 轮询默认 15 秒，可在 `DataFetcher.java` 中调整
4. **数据写入间隔**：Room 数据库写入间隔 30 秒，告警变化时立即写入
5. **历史记录限制**：最多保留 500 条记录，超出后旧数据自动清理
