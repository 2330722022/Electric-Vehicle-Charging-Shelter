#include "app_logic.h"
#include "app_net.h"
#include <board.h>
#include <rtdevice.h>
#include <stdio.h>
#include <string.h>

/*
 * 模块：业务逻辑中心 (app_logic.c) — 电动车充电棚环境监测
 * 版本：v2.0 充电棚专版
 * 日期：2026-05-19
 * 功能：统一管理共享资源（阈值、告警状态、蜂鸣器、舵机），通过互斥量实现临界区保护。
 *       CC2530 ZigBee 网关数据接收：PM2.5/PM1.0/PM10 颗粒物 + MQ2 烟雾 + 火焰传感器。
 *       EV充电棚专属判定：火焰报警 > 烟雾报警 > PM2.5 超标。
 * OS 概念体现：
 *   1. 互斥量 (Mutex)：data_lock 保护 g_app_state，防止多线程数据竞争。
 *      例如 Android HTTP 修改阈值和硬件按键修改阈值可能同时发生，必须互斥。
 *   2. 事件集 (Event)：逻辑线程阻塞等待传感器线程发送的告警事件，
 *      实现线程间异步通知，避免轮询浪费 CPU。
 *   3. 共享资源封装：将分散的全局变量封装到 struct app_state 中统一管理。
 */

#define TAG "logic"

/* ==================== 引脚定义 ==================== */
#define BEEP_PIN        GET_PIN(B, 0)       // 蜂鸣器 (高电平响)
#define KEY_WK_UP       GET_PIN(C, 5)       // WK_UP按键 - 增加阈值
#define KEY_DOWN        GET_PIN(C, 4)       // DOWN按键 - 减少阈值

/* ==================== 舵机驱动 ==================== */
#define SERVO_PWM_DEV      "pwm2"
#define SERVO_PWM_CHANNEL  4
#define SERVO_PERIOD_NS    20000000
#define SERVO_MIN_PULSE_NS 500000
#define SERVO_MAX_PULSE_NS 2500000
#define SERVO_ANGLE_MIN    0
#define SERVO_ANGLE_MAX    180

static struct rt_device_pwm *servo_pwm_dev = RT_NULL;
static int servo_last_angle = -1;

/* ==================== 全局 RT-Thread 对象 ==================== */
/*
 * data_lock:
 *   互斥量 — 保护 g_app_state（阈值、beep、alarm_state）的临界区。
 *   所有读/写 g_app_state 的线程（逻辑线程、按键处理、HTTP 服务、OneNET 下行）
 *   必须先持有此锁，防止数据竞争（Data Race）。
 */
rt_mutex_t data_lock = RT_NULL;

/*
 * g_app_state:
 *   全局业务状态结构体 — 将分散的全局变量（temp_threshold, beep_status, alarm_state）
 *   封装为单一结构体，便于互斥量统一保护。
 */
struct app_state g_app_state = {
    .temp_threshold  = 80.0f,
    .pm25_threshold  = 200.0f,
    .flame_threshold = 800,
    .mq2_threshold   = 1500,
    .beep_status     = 0,
    .alarm_type      = 0,
    .alarm_state     = 0,
};

/*
 * evt_alarm:
 *   事件集 — 传感器线程检测到异常时发送事件，逻辑线程阻塞等待。
 *   实现生产者-消费者模式，避免轮询。
 */
struct rt_event evt_alarm;

/* 按键事件标志位（ISR 快速置位，线程轮询处理）*/
static volatile uint8_t key_event_flags = 0;
#define KEY_EVENT_WKUP_SHORT    (1 << 0)
#define KEY_EVENT_DOWN          (1 << 2)

/* ==================== CC2530 ZigBee 数据 ==================== */
/*
 * g_cc2530_data:
 *   存储从 CC2530 串口接收到的传感器数据（PMS7003 颗粒物浓度）。
 *   由 cc2530_recv_thread 写入，其他线程（逻辑、HTTP、OneNET）只读。
 *
 *   数据流：CC2530(9600bps) -> USART3(PB10/PB11) -> cc2530_recv_thread
 *         -> 行解析 -> g_cc2530_data
 */
struct cc2530_data g_cc2530_data = {0};

/* CC2530 接收行缓冲（线程局部）*/
static struct cc2530_parser g_cc2530_parser = {0};

/* ==================== 舵机操作 ==================== */
static int servo_init(void)
{
    servo_pwm_dev = (struct rt_device_pwm *)rt_device_find(SERVO_PWM_DEV);
    if (servo_pwm_dev == RT_NULL)
    {
        LOG_E(TAG, "Servo: find %s failed!", SERVO_PWM_DEV);
        return -1;
    }

    rt_pwm_set(servo_pwm_dev, SERVO_PWM_CHANNEL, SERVO_PERIOD_NS, SERVO_MIN_PULSE_NS);
    rt_pwm_enable(servo_pwm_dev, SERVO_PWM_CHANNEL);

    servo_last_angle = SERVO_ANGLE_MIN;
    LOG_I(TAG, "Servo: initialized on %s channel %d", SERVO_PWM_DEV, SERVO_PWM_CHANNEL);
    return 0;
}

static void servo_set_angle(int angle)
{
    if (servo_pwm_dev == RT_NULL) return;
    if (angle < SERVO_ANGLE_MIN || angle > SERVO_ANGLE_MAX) return;
    if (angle == servo_last_angle) return;

    uint32_t pulse_ns = SERVO_MIN_PULSE_NS + (uint32_t)((uint32_t)angle * (SERVO_MAX_PULSE_NS - SERVO_MIN_PULSE_NS) / SERVO_ANGLE_MAX);
    rt_pwm_set(servo_pwm_dev, SERVO_PWM_CHANNEL, SERVO_PERIOD_NS, pulse_ns);
    servo_last_angle = angle;
    LOG_I(TAG, "Servo: set angle %d", angle);
}

/* ==================== BEEP 控制 ==================== */
/*
 * beep_set:
 *   互斥量保护的 BEEP 控制函数。
 *   临界区：写入 g_app_state.beep_status 和操作硬件引脚必须原子化，
 *   防止 Android 设置 BEEP 和按键设置 BEEP 同时发生时产生竞态。
 */
void beep_set(uint8_t on)
{
    rt_mutex_take(data_lock, RT_WAITING_FOREVER);   /* 进入临界区 */
    g_app_state.beep_status = on;
    rt_pin_write(BEEP_PIN, on ? PIN_HIGH : PIN_LOW);
    rt_mutex_release(data_lock);                     /* 退出临界区 */
}

/* ==================== 核心逻辑处理 ==================== */
/*
 * logic_handle:
 *   被传感器线程调用的入口函数。负责根据温度值判定是否需要告警，
 *   并通过事件集通知逻辑线程执行实际操作。
 *
 *   临界区保护：
 *     - 读取 g_app_state.temp_threshold 前必须持有 data_lock，
 *       防止在读取过程中被 Android/按键修改阈值。
 *     - 写入 g_app_state.alarm_state 同理。
 */
void logic_handle(float temperature)
{
    int should_alarm = 0;
    static int was_temp_alarm = 0;

    while (rt_mutex_take(data_lock, RT_WAITING_NO) != RT_EOK)
        rt_thread_mdelay(1);

    /* 临界区：原子读取阈值 */
    float threshold = g_app_state.temp_threshold;

    if (temperature > threshold)
    {
        should_alarm = 1;
        g_app_state.alarm_state = 1;
        LOG_W(TAG, "Temp %.1f > threshold %.1f, ALARM", temperature, threshold);
    }
    else
    {
        g_app_state.alarm_state = 0;
    }
    rt_mutex_release(data_lock);                     /* 退出临界区 */

    /* 发送事件通知逻辑线程 */
    if (should_alarm)
    {
        rt_event_send(&evt_alarm, EVENT_TEMP_ALARM);
        was_temp_alarm = 1;
    }
    else if (was_temp_alarm)
    {
        rt_event_send(&evt_alarm, EVENT_ALARM_CLEAR);
        was_temp_alarm = 0;
    }
}

/* ==================== CC2530环境数据判定 ==================== */
/*
 * logic_handle_cc2530_env:
 *   MQ2烟雾 / 火焰传感器告警判定。仅当 CC2530 环境数据有效时调用。
 *   优先级: 火焰 > 烟雾
 */
void logic_handle_cc2530_env(uint16_t mq2, uint16_t flame)
{
    int should_alarm = 0;
    uint8_t alarm_type = 0;

    while (rt_mutex_take(data_lock, RT_WAITING_NO) != RT_EOK)
        rt_thread_mdelay(1);

    uint16_t flame_th = g_app_state.flame_threshold;
    uint16_t mq2_th = g_app_state.mq2_threshold;
    rt_mutex_release(data_lock);

    if (flame < flame_th)
    {
        should_alarm = 1;
        alarm_type = 5;
        LOG_W(TAG, "FIRE ALARM! flame=%u < %u", flame, flame_th);
    }
    else if (mq2 > mq2_th)
    {
        should_alarm = 1;
        alarm_type = 4;
        LOG_W(TAG, "SMOKE ALARM! mq2=%u > %u", mq2, mq2_th);
    }

    if (should_alarm)
    {
        rt_mutex_take(data_lock, RT_WAITING_FOREVER);
        g_app_state.alarm_state = 1;
        g_app_state.alarm_type = alarm_type;
        rt_mutex_release(data_lock);

        if (alarm_type == 5)
            rt_event_send(&evt_alarm, EVENT_FIRE_ALARM);
        else
            rt_event_send(&evt_alarm, EVENT_SMOKE_ALARM);
    }
}

/*
 * logic_handle_cc2530_pm:
 *   PM2.5/PM10 颗粒物告警判定。仅当 PM 数据有效时调用。
 */
void logic_handle_cc2530_pm(uint16_t pm2_5)
{
    int should_alarm = 0;

    while (rt_mutex_take(data_lock, RT_WAITING_NO) != RT_EOK)
        rt_thread_mdelay(1);

    float pm25_th = g_app_state.pm25_threshold;
    rt_mutex_release(data_lock);

    if (pm2_5 > (uint16_t)pm25_th)
    {
        should_alarm = 1;
        LOG_W(TAG, "PM2.5 ALARM! pm2_5=%u > %.0f", pm2_5, pm25_th);
    }

    if (should_alarm)
    {
        rt_mutex_take(data_lock, RT_WAITING_FOREVER);
        g_app_state.alarm_state = 1;
        g_app_state.alarm_type = 6;
        rt_mutex_release(data_lock);

        rt_event_send(&evt_alarm, EVENT_PM25_ALARM);
    }
}

/* 保留原函数签名以兼容, 内部拆分调用 */
void logic_handle_cc2530(uint16_t pm2_5, uint16_t mq2, uint16_t flame)
{
    logic_handle_cc2530_pm(pm2_5);
    logic_handle_cc2530_env(mq2, flame);
}

/* ==================== 逻辑处理线程 ==================== */
/*
 * 逻辑线程：
 *   阻塞等待传感器线程发送的告警事件，收到事件后执行舵机/BEEP操作。
 *   这种事件驱动模型体现了 RT-Thread 线程间异步通信的能力。
 */
static void logic_thread_entry(void *parameter)
{
    rt_uint32_t events;

    LOG_I(TAG, "Logic thread started, waiting for events...");

    while (1)
    {
        rt_err_t evt_ret = rt_event_recv(&evt_alarm,
                          EVENT_ALL,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          1000, &events);

        watchdog_feed();

        if (evt_ret == RT_EOK)
        {
            rt_mutex_take(data_lock, RT_WAITING_FOREVER);

            if (events & EVENT_FIRE_ALARM)
            {
                g_app_state.beep_status = 1;
                g_app_state.alarm_state = 1;
                g_app_state.alarm_type = 5;
                rt_pin_write(BEEP_PIN, PIN_HIGH);
                servo_set_angle(90);
                LOG_W(TAG, "EVENT: FIRE_ALARM -> beep ON, servo 90");
            }
            else if (events & EVENT_SMOKE_ALARM)
            {
                g_app_state.beep_status = 1;
                g_app_state.alarm_state = 1;
                g_app_state.alarm_type = 4;
                rt_pin_write(BEEP_PIN, PIN_HIGH);
                servo_set_angle(90);
                LOG_W(TAG, "EVENT: SMOKE_ALARM -> beep ON, servo 90");
            }
            else if (events & EVENT_PM25_ALARM)
            {
                g_app_state.alarm_state = 1;
                g_app_state.alarm_type = 6;
                LOG_I(TAG, "EVENT: PM25_ALARM -> alert only");
            }
            else if (events & EVENT_TEMP_ALARM)
            {
                g_app_state.beep_status = 1;
                g_app_state.alarm_state = 1;
                g_app_state.alarm_type = 1;
                rt_pin_write(BEEP_PIN, PIN_HIGH);
                servo_set_angle(90);
                LOG_W(TAG, "EVENT: TEMP_ALARM -> beep ON, servo 90");
            }
            else if (events & EVENT_TILT_ALARM)
            {
                g_app_state.alarm_state = 1;
                g_app_state.alarm_type = 2;
                rt_pin_write(BEEP_PIN, PIN_HIGH);
                LOG_W(TAG, "EVENT: TILT_ALARM -> beep ON");
            }
            else if (events & EVENT_VIBRATION_ALARM)
            {
                g_app_state.alarm_state = 1;
                g_app_state.alarm_type = 3;
                LOG_I(TAG, "EVENT: VIBRATION_ALARM");
            }
            else if (events & EVENT_ALARM_CLEAR)
            {
                g_app_state.beep_status = 0;
                g_app_state.alarm_state = 0;
                g_app_state.alarm_type = 0;
                rt_pin_write(BEEP_PIN, PIN_LOW);
                servo_set_angle(0);
            }

            rt_mutex_release(data_lock);
        }
    }
}

/* ==================== 按键回调 ==================== */
/*
 * 按键 ISR 回调 — 仅快速置位标志位，实际处理由传感器线程轮询完成。
 * 这种"ISR 生产 + 线程消费"模式是 RTOS 中断处理的经典范式。
 */
static void key_wkup_callback(void *args)
{
    key_event_flags |= KEY_EVENT_WKUP_SHORT;
}

static void key_down_callback(void *args)
{
    key_event_flags |= KEY_EVENT_DOWN;
}

/* ==================== 按键事件处理 ==================== */
/*
 * process_key_events:
 *   由传感器线程周期性调用，消费按键事件。
 *
 *   临界区保护：
 *     - 修改 g_app_state.temp_threshold 时持有 data_lock，
 *       确保与 Android HTTP/OneNET 下行互斥。
 */
void process_key_events(void)
{
    uint8_t flags = key_event_flags;
    if (flags == 0) return;
    key_event_flags = 0;

    if (flags & (KEY_EVENT_WKUP_SHORT | KEY_EVENT_DOWN))
    {
        rt_mutex_take(data_lock, RT_WAITING_FOREVER);        /* 进入临界区 */

        if (flags & KEY_EVENT_WKUP_SHORT)
        {
            g_app_state.temp_threshold += 0.5f;
            if (g_app_state.temp_threshold > 80.0f)
                g_app_state.temp_threshold = 80.0f;
            LOG_I(TAG, "WK_UP: threshold +0.5 -> %.1f", g_app_state.temp_threshold);
        }
        if (flags & KEY_EVENT_DOWN)
        {
            g_app_state.temp_threshold -= 0.5f;
            if (g_app_state.temp_threshold < 20.0f)
                g_app_state.temp_threshold = 20.0f;
            LOG_I(TAG, "DOWN: threshold -0.5 -> %.1f", g_app_state.temp_threshold);
        }

        rt_mutex_release(data_lock);                          /* 退出临界区 */

        update_threshold_display();
        publish_temp_threshold();                             /* 同步到 OneNET */
    }
}

/* ==================== 阈值LCD显示更新（弱符号，由main.c重写）==================== */
RT_WEAK void update_threshold_display(void)
{
}

/* ==================== 按键初始化 ==================== */
static void app_key_init(void)
{
    rt_pin_mode(KEY_WK_UP, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(KEY_WK_UP, PIN_IRQ_MODE_FALLING, key_wkup_callback, RT_NULL);
    rt_pin_irq_enable(KEY_WK_UP, PIN_IRQ_ENABLE);

    rt_pin_mode(KEY_DOWN, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(KEY_DOWN, PIN_IRQ_MODE_FALLING, key_down_callback, RT_NULL);
    rt_pin_irq_enable(KEY_DOWN, PIN_IRQ_ENABLE);

    LOG_I(TAG, "Key init done (WK_UP=PC5, DOWN=PC4)");
}

/* ==================== CC2530 数据解析 ==================== */
/*
 * cc2530_parse_line:
 *   解析 CC2530 串口发来的一行文本数据。
 *   支持5种格式：
 *     1. [COORD] ADC: ADC0=143(0.12V)               — 协调器本地ADC
 *     2. [COORD] RX [END0x38A1] PMS7003: PM1.0=21... — 终端PM数据
 *     3. [COORD] RX [END0x7CD0] ADC0=145 MQ2=281...  — 终端合并传感数据
 *     4. [END0x7CD0] 21,32,36,281,2038               — CSV简略格式(推荐)
 *     5. 35,48,52                                     — 纯CSV(旧格式兼容)
 */
static void cc2530_parse_line(const char *line)
{
    uint16_t pm1_0 = 0, pm2_5 = 0, pm10 = 0;
    uint16_t adc0 = 0, mq2 = 0, flame = 0;
    char addr[16] = {0};
    static rt_tick_t last_pm_log_tick;
    rt_tick_t now = rt_tick_get();
    int line_len;

    if (line == RT_NULL) return;

    line_len = (int)strlen(line);
    if (line_len < 8)
    {
        LOG_W(TAG, "RX too short (%dB), dropped", line_len);
        return;
    }

    /* 格式1: [COORD] ADC: ADC0=143(0.12V) */
    if (strstr(line, "[COORD] ADC:") == line)
    {
        int n = sscanf(line, "[COORD] ADC: ADC0=%hu", &adc0);
        if (n != 1)
        {
            LOG_W(TAG, "Parse COORD_ADC failed (sscanf ret=%d)", n);
            return;
        }
        g_cc2530_data.coord_adc0 = adc0;
        g_cc2530_data.coord_adc_valid = 1;
        if (now - last_pm_log_tick > 3000) {
            LOG_D(TAG, "Coord ADC: %hu", adc0);
            last_pm_log_tick = now;
        }
        return;
    }

    /* 格式2: [COORD] RX [END0x38A1] PMS7003: PM1.0=21 PM2.5=32 PM10=37 */
    if (strstr(line, "[COORD] RX [") == line && strstr(line, "PMS7003:"))
    {
        int n = sscanf(line, "[COORD] RX [%15[^]]] PMS7003: PM1.0=%hu PM2.5=%hu PM10=%hu",
                       addr, &pm1_0, &pm2_5, &pm10);
        if (n != 4)
        {
            LOG_W(TAG, "Parse PMS7003 failed (sscanf ret=%d, need 4)", n);
            return;
        }

        rt_mutex_take(data_lock, RT_WAITING_FOREVER);
        g_cc2530_data.pm.pm1_0 = pm1_0;
        g_cc2530_data.pm.pm2_5 = pm2_5;
        g_cc2530_data.pm.pm10 = pm10;
        g_cc2530_data.pm.last_update_tick = now;
        g_cc2530_data.pm.valid = 1;
        rt_mutex_release(data_lock);

        if (now - last_pm_log_tick > 3000) {
            LOG_I(TAG, "PM from %s: %hu,%hu,%hu", addr, pm1_0, pm2_5, pm10);
            last_pm_log_tick = now;
        }
        return;
    }

    /* 格式3: [COORD] RX [END0x7CD0] ADC0=145 MQ2=281 Flame=2038 */
    if (strstr(line, "[COORD] RX [") == line && strstr(line, "ADC0="))
    {
        int n = sscanf(line, "[COORD] RX [%15[^]]] ADC0=%hu MQ2=%hu Flame=%hu",
                       addr, &adc0, &mq2, &flame);
        if (n != 4)
        {
            LOG_W(TAG, "Parse ENV failed (sscanf ret=%d, need 4)", n);
            return;
        }

        rt_mutex_take(data_lock, RT_WAITING_FOREVER);
        strncpy(g_cc2530_data.env.end_addr, addr, sizeof(g_cc2530_data.env.end_addr) - 1);
        g_cc2530_data.env.end_addr[sizeof(g_cc2530_data.env.end_addr) - 1] = '\0';
        g_cc2530_data.env.adc0 = adc0;
        g_cc2530_data.env.mq2 = mq2;
        g_cc2530_data.env.flame = flame;
        g_cc2530_data.env.last_update_tick = now;
        g_cc2530_data.env.valid = 1;
        rt_mutex_release(data_lock);

        LOG_I(TAG, "Env from %s: ADC=%hu MQ2=%hu Flame=%hu", addr, adc0, mq2, flame);
        return;
    }

    /* 格式4: [END0x7CD0] 21,32,36,281,2038 (推荐 — 5字段CSV) */
    if (line[0] == '[' && strstr(line, "END") == line + 1)
    {
        /* 尝试5字段: PM1.0,PM2.5,PM10,MQ2,Flame */
        int n = sscanf(line, "[%15[^]]] %hu,%hu,%hu,%hu,%hu",
                       addr, &pm1_0, &pm2_5, &pm10, &mq2, &flame);
        if (n == 6)
        {
            rt_mutex_take(data_lock, RT_WAITING_FOREVER);
            g_cc2530_data.pm.pm1_0 = pm1_0;
            g_cc2530_data.pm.pm2_5 = pm2_5;
            g_cc2530_data.pm.pm10 = pm10;
            g_cc2530_data.pm.last_update_tick = now;
            g_cc2530_data.pm.valid = 1;
            strncpy(g_cc2530_data.env.end_addr, addr, sizeof(g_cc2530_data.env.end_addr) - 1);
            g_cc2530_data.env.end_addr[sizeof(g_cc2530_data.env.end_addr) - 1] = '\0';
            g_cc2530_data.env.mq2 = mq2;
            g_cc2530_data.env.flame = flame;
            g_cc2530_data.env.last_update_tick = now;
            g_cc2530_data.env.valid = 1;
            rt_mutex_release(data_lock);

            LOG_I(TAG, "CSV from %s: PM=%hu,%hu,%hu MQ2=%hu Flame=%hu",
                  addr, pm1_0, pm2_5, pm10, mq2, flame);
            return;
        }

        /* 兼容3字段: [END0xXXXX] PM1.0,PM2.5,PM10 */
        n = sscanf(line, "[%15[^]]] %hu,%hu,%hu", addr, &pm1_0, &pm2_5, &pm10);
        if (n == 4)
        {
            rt_mutex_take(data_lock, RT_WAITING_FOREVER);
            g_cc2530_data.pm.pm1_0 = pm1_0;
            g_cc2530_data.pm.pm2_5 = pm2_5;
            g_cc2530_data.pm.pm10 = pm10;
            g_cc2530_data.pm.last_update_tick = now;
            g_cc2530_data.pm.valid = 1;
            rt_mutex_release(data_lock);

            if (now - last_pm_log_tick > 3000) {
                LOG_I(TAG, "PM from %s: %hu,%hu,%hu", addr, pm1_0, pm2_5, pm10);
                last_pm_log_tick = now;
            }
            return;
        }

        LOG_W(TAG, "Parse CSV failed (sscanf ret=%d), raw: %.40s", n, line);
        return;
    }

    /* 格式5(旧): 纯CSV — "PM1.0,PM2.5,PM10" 向后兼容 */
    {
        int n = sscanf(line, "%hu,%hu,%hu", &pm1_0, &pm2_5, &pm10);
        if (n == 3)
        {
            rt_mutex_take(data_lock, RT_WAITING_FOREVER);
            g_cc2530_data.pm.pm1_0 = pm1_0;
            g_cc2530_data.pm.pm2_5 = pm2_5;
            g_cc2530_data.pm.pm10 = pm10;
            g_cc2530_data.pm.last_update_tick = now;
            g_cc2530_data.pm.valid = 1;
            rt_mutex_release(data_lock);

            if (now - last_pm_log_tick > 3000) {
                LOG_I(TAG, "PM(raw): %hu,%hu,%hu", pm1_0, pm2_5, pm10);
                last_pm_log_tick = now;
            }
            return;
        }
    }

    LOG_D(TAG, "Unrecognized frame: %.48s", line);
}

/* ==================== CC2530 串口接收线程（静态分配） ==================== */
static rt_uint8_t cc2530_rx_stack[2304];
static struct rt_thread cc2530_rx_thread;
static rt_sem_t cc2530_rx_sem = RT_NULL;

/*
 * cc2530_rx_indicate:
 *   串口中断回调 — 由 RT-Thread 串口驱动在 ISR 上下文调用。
 *   仅释放信号量唤醒接收线程，不执行任何阻塞操作。
 *   体现 ISR → 线程的异步通知模式。
 */
static rt_err_t cc2530_rx_indicate(rt_device_t dev, rt_size_t size)
{
    if (cc2530_rx_sem != RT_NULL)
        rt_sem_release(cc2530_rx_sem);
    return RT_EOK;
}

/*
 * cc2530_recv_thread:
 *   基于信号量触发的高效串口接收线程。
 *
 *   改进前（轮询模式）：rt_device_read(-1) 阻塞等待，栈帧始终活跃。
 *   改进后（事件驱动）：线程阻塞在 rt_sem_take()，栈休眠；有数据时 ISR
 *   释放信号量唤醒线程，批量读取所有可用字节。
 *
 *   数据流：
 *     CC2530 --UART(9600bps)--> USART3(PB10/PB11)
 *     --> 硬件 RXNE 中断 → 驱动缓冲区 → rx_indicate → sem_release
 *     --> 线程被唤醒 → 批量读取 → 行缓冲解析 → g_cc2530_data
 */
static void cc2530_recv_thread(void *parameter)
{
    rt_device_t serial = (rt_device_t)parameter;
    char ch;
    uint8_t skip_line = 0;

    LOG_I(TAG, "CC2530 receiver thread started (semaphore mode)");

    while (1)
    {
        rt_sem_take(cc2530_rx_sem, RT_WAITING_FOREVER);

        while (rt_device_read(serial, 0, &ch, 1) == 1)
        {
            if (ch == '\n')
            {
                if (!skip_line && g_cc2530_parser.line_pos > 0)
                {
                    g_cc2530_parser.line_buf[g_cc2530_parser.line_pos] = '\0';

                    if (g_cc2530_parser.line_pos >= 8)
                    {
                        LOG_D(TAG, "RX[%d]: %s", g_cc2530_parser.line_pos, g_cc2530_parser.line_buf);
                    }

                    cc2530_parse_line(g_cc2530_parser.line_buf);
                }
                g_cc2530_parser.line_pos = 0;
                skip_line = 0;
            }
            else if (ch == '\r')
            {
            }
            else if (skip_line)
            {
            }
            else if (g_cc2530_parser.line_pos < CC2530_LINE_BUF_SIZE - 1)
            {
                g_cc2530_parser.line_buf[g_cc2530_parser.line_pos++] = ch;
            }
            else
            {
                skip_line = 1;
                LOG_W(TAG, "Line too long, skipped (buf=%d)", CC2530_LINE_BUF_SIZE);
            }
        }
    }
}

/* ==================== CC2530 串口初始化 ==================== */
/*
 * cc2530_serial_init:
 *   打开 uart3 设备（对应 PB10-TX, PB11-RX），配置 9600-8N1，
 *   创建接收线程处理 CC2530 发来的传感器数据。
 *
 *   硬件引脚（由 CubeMX 预配置）：
 *     PB10 - USART3_TX -> CC2530 RX
 *     PB11 - USART3_RX -> CC2530 TX
 */
void cc2530_serial_init(void)
{
    rt_device_t serial = rt_device_find(CC2530_UART_DEVICE);

    if (serial == RT_NULL)
    {
        LOG_E(TAG, "Cannot find device: %s", CC2530_UART_DEVICE);
        return;
    }

    /* 以中断接收模式打开 */
    if (rt_device_open(serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK)
    {
        LOG_E(TAG, "Failed to open %s", CC2530_UART_DEVICE);
        return;
    }

    /* 配置串口参数：9600, 8N1 */
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    config.baud_rate = BAUD_RATE_9600;
    config.data_bits = DATA_BITS_8;
    config.stop_bits = STOP_BITS_1;
    config.parity    = PARITY_NONE;
    config.bufsz     = 1024;
    rt_device_control(serial, RT_DEVICE_CTRL_CONFIG, &config);

    /*
     * 创建信号量 + 注册中断回调 — 实现基于信号量的高效接收。
     * 串口硬件 RXNE 中断 → 驱动缓冲 → rx_indicate 释放信号量 → 线程消费。
     */
    cc2530_rx_sem = rt_sem_create("cc2530_rx", 0, RT_IPC_FLAG_FIFO);
    if (cc2530_rx_sem == RT_NULL)
    {
        LOG_E(TAG, "cc2530_rx sem create failed!");
        return;
    }
    rt_device_set_rx_indicate(serial, cc2530_rx_indicate);

    LOG_I(TAG, "%s opened at 9600 baud", CC2530_UART_DEVICE);

    /* 静态创建接收线程（栈不占堆）*/
    rt_thread_init(&cc2530_rx_thread,
                   "cc2530_rx",
                   cc2530_recv_thread,
                   serial,
                   cc2530_rx_stack,
                   sizeof(cc2530_rx_stack),
                   12, 10);
    rt_thread_startup(&cc2530_rx_thread);
    LOG_I(TAG, "Receiver thread started (prio=12, static stack=%d)", sizeof(cc2530_rx_stack));
}

/* ==================== 模块初始化 ==================== */
/*
 * app_logic_init:
 *   创建 RT-Thread 同步对象（互斥量、事件集），初始化硬件，
 *   并启动逻辑处理线程。
 *
 *   初始化序列体现了 RT-Thread 对象的创建顺序：
 *     1. 互斥量 — 最先创建，供后续所有线程使用
 *     2. 事件集 — 线程间通信基础
 *     3. 硬件外设 — BEEP、舵机、按键
 *     4. 逻辑线程 — 最后启动，开始等待事件
 */
void app_logic_init(void)
{
    /* 1. 创建互斥量 — 保护共享资源 g_app_state */
    data_lock = rt_mutex_create("data_lock", RT_IPC_FLAG_FIFO);
    if (data_lock == RT_NULL)
    {
        LOG_E(TAG, "mutex create failed!");
        return;
    }

    /* 2. 创建事件集 — 用于传感器→逻辑线程的异步通知 */
    rt_event_init(&evt_alarm, "evt_alarm", RT_IPC_FLAG_FIFO);

    /* 3. 初始化硬件 */
    rt_pin_mode(BEEP_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(BEEP_PIN, PIN_LOW);

    servo_init();
    app_key_init();

    /* 4. 初始化 CC2530 ZigBee 串口接收 */
    cc2530_serial_init();

    /* 5. 启动逻辑处理线程 */
    rt_thread_t tid_logic = rt_thread_create("logic",
                                              logic_thread_entry,
                                              RT_NULL,
                                              3072,
                                              8,
                                              10);
    if (tid_logic)
    {
        rt_thread_startup(tid_logic);
        LOG_I(TAG, "Init OK T:%.1f PM25:%.0f MQ2:%u Flame:%u",
              g_app_state.temp_threshold,
              g_app_state.pm25_threshold,
              g_app_state.mq2_threshold, g_app_state.flame_threshold);
    }
}
