#ifndef APP_LOGIC_H__
#define APP_LOGIC_H__

#include <rtthread.h>

/* ==================== 日志级别控制 ==================== */
#define LOG_LEVEL_NONE    0
#define LOG_LEVEL_ERROR   1
#define LOG_LEVEL_WARN    2
#define LOG_LEVEL_INFO    3
#define LOG_LEVEL_DEBUG   4

#ifndef LOG_LEVEL
#define LOG_LEVEL         LOG_LEVEL_INFO
#endif

#define LOG_COLOR_RED     "\033[31m"
#define LOG_COLOR_YELLOW  "\033[33m"
#define LOG_COLOR_GREEN   "\033[32m"
#define LOG_COLOR_BLUE    "\033[34m"
#define LOG_COLOR_RESET   "\033[0m"

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_E(tag, fmt, ...) rt_kprintf(LOG_COLOR_RED "[E/%s] " fmt LOG_COLOR_RESET "\n", tag, ##__VA_ARGS__)
#else
#define LOG_E(tag, fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_W(tag, fmt, ...) rt_kprintf(LOG_COLOR_YELLOW "[W/%s] " fmt LOG_COLOR_RESET "\n", tag, ##__VA_ARGS__)
#else
#define LOG_W(tag, fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_I(tag, fmt, ...) rt_kprintf(LOG_COLOR_GREEN "[I/%s] " fmt LOG_COLOR_RESET "\n", tag, ##__VA_ARGS__)
#else
#define LOG_I(tag, fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_D(tag, fmt, ...) rt_kprintf(LOG_COLOR_BLUE "[D/%s] " fmt LOG_COLOR_RESET "\n", tag, ##__VA_ARGS__)
#else
#define LOG_D(tag, fmt, ...)
#endif

/* ==================== 业务状态结构体 ==================== */
struct app_state {
    float temp_threshold;       /* 温度阈值 */
    float pm25_threshold;       /* PM2.5报警阈值 μg/m³ */
    uint16_t flame_threshold;   /* 火焰阈值(低于此值触发报警) */
    uint16_t mq2_threshold;     /* MQ2烟雾阈值 */
    uint8_t beep_status;        /* 蜂鸣器状态 */
    uint8_t alarm_type;         /* 告警类型: 0=无 1=温度 2=倾斜 3=振动 4=烟雾 5=火焰 6=PM2.5 */
    int alarm_state;            /* 告警状态 (0/1) */
};

/* ==================== 告警事件集 ==================== */
#define EVENT_TEMP_ALARM       (1 << 0)
#define EVENT_TILT_ALARM       (1 << 1)
#define EVENT_VIBRATION_ALARM  (1 << 2)
#define EVENT_ALARM_CLEAR      (1 << 3)
#define EVENT_SMOKE_ALARM      (1 << 4)  /* MQ2烟雾 */ 
#define EVENT_FIRE_ALARM       (1 << 5)  /* 火焰 */
#define EVENT_PM25_ALARM       (1 << 6)  /* PM2.5超标 */
#define EVENT_ALL              (EVENT_TEMP_ALARM | EVENT_TILT_ALARM | EVENT_VIBRATION_ALARM | \
                                EVENT_ALARM_CLEAR | EVENT_SMOKE_ALARM | EVENT_FIRE_ALARM | EVENT_PM25_ALARM)

/* ==================== CC2530 ZigBee 数据 ==================== */
#define CC2530_UART_DEVICE      "uart3"
#define CC2530_UART_BAUD        9600
#define CC2530_LINE_BUF_SIZE    256

/* PM2.5 传感器数据结构 (PMS7003) */
struct cc2530_pm_data {
    uint16_t pm1_0;             /* PM1.0 浓度 */
    uint16_t pm2_5;             /* PM2.5 浓度 */
    uint16_t pm10;              /* PM10 浓度 */
    uint32_t last_update_tick;  /* 最后更新时间 */
    uint8_t  valid;             /* 数据有效标志 */
};

/* 终端设备环境数据 (ADC, MQ2, Flame) */
struct cc2530_env_data {
    uint16_t adc0;              /* 终端ADC值 */
    uint16_t mq2;               /* MQ2气体传感器 */
    uint16_t flame;             /* 火焰传感器 */
    char     end_addr[16];      /* 终端地址，如 "END0x7CD0" */
    uint32_t last_update_tick;  /* 最后更新时间 */
    uint8_t  valid;             /* 数据有效标志 */
};

/* CC2530 整体数据结构 */
struct cc2530_data {
    struct cc2530_pm_data pm;       /* PM传感器数据 */
    struct cc2530_env_data env;     /* 终端环境数据 */
    uint16_t coord_adc0;            /* 协调器本地ADC值 */
    uint8_t  coord_adc_valid;       /* 协调器ADC有效标志 */
};

/* CC2530 接收状态机 */
struct cc2530_parser {
    char line_buf[CC2530_LINE_BUF_SIZE];
    uint8_t line_pos;
};

extern struct cc2530_data g_cc2530_data;

/* CC2530 模块初始化 */
void cc2530_serial_init(void);

/* 全局互斥锁 — 保护 g_app_state 的临界区 */
extern rt_mutex_t data_lock;

/* 全局状态变量 */
extern struct app_state g_app_state;

/* 告警事件集控制块 */
extern struct rt_event evt_alarm;

/* ==================== 初始化 ==================== */
void app_logic_init(void);

/* ==================== 核心处理 — 临界区保护 ==================== */
void logic_handle(float temperature);
void logic_handle_cc2530(uint16_t pm2_5, uint16_t mq2, uint16_t flame);
void logic_handle_cc2530_env(uint16_t mq2, uint16_t flame);
void logic_handle_cc2530_pm(uint16_t pm2_5);

/* ==================== BEEP 控制 ==================== */
void beep_set(uint8_t on);

/* ==================== 按键事件（仅保留UP/DOWN调整阈值）==================== */
void process_key_events(void);

/* ==================== LCD显示阈值更新 ==================== */
void update_threshold_display(void);

/* ==================== 看门狗喂狗 ==================== */
void watchdog_feed(void);

#endif
