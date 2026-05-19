#include "app_logic.h"
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
    .temp_threshold  = 55.0f,
    .pm25_threshold  = 75.0f,
    .flame_threshold = 300,
    .mq2_threshold   = 500,
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
        rt_kprintf("[logic] Servo: find %s failed!\n", SERVO_PWM_DEV);
        return -1;
    }

    rt_pwm_set(servo_pwm_dev, SERVO_PWM_CHANNEL, SERVO_PERIOD_NS, SERVO_MIN_PULSE_NS);
    rt_pwm_enable(servo_pwm_dev, SERVO_PWM_CHANNEL);

    servo_last_angle = SERVO_ANGLE_MIN;
    rt_kprintf("[logic] Servo: initialized on %s channel %d\n", SERVO_PWM_DEV, SERVO_PWM_CHANNEL);
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
    rt_kprintf("[logic] Servo: set angle %d\n", angle);
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

    while (rt_mutex_take(data_lock, RT_WAITING_NO) != RT_EOK)
        rt_thread_mdelay(1);

    /* 临界区：原子读取阈值 */
    float threshold = g_app_state.temp_threshold;

    if (temperature > threshold)
    {
        should_alarm = 1;
        g_app_state.alarm_state = 1;
        rt_kprintf("[logic] Temp %d.%d > threshold %d.%d, ALARM\n",
                   (int)temperature, abs((int)((temperature - (int)temperature) * 10)),
                   (int)threshold, abs((int)((threshold - (int)threshold) * 10)));
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
    }
    else
    {
        rt_event_send(&evt_alarm, EVENT_ALARM_CLEAR);
    }
}

/* ==================== CC2530环境数据判定 ==================== */
/*
 * logic_handle_cc2530:
 *   电动车充电棚场景专属判定：
 *     PM2.5 > 75  → 空气污染告警（扬尘/烟雾）
 *     MQ2 > 500   → 可燃气体/烟雾告警（电池热失控前兆）
 *     Flame < 300 → 火焰告警（明火，值越低越危险）
 *   优先级: 火焰 > 烟雾 > PM2.5
 */
void logic_handle_cc2530(uint16_t pm2_5, uint16_t mq2, uint16_t flame)
{
    int should_alarm = 0;
    uint8_t alarm_type = 0;

    while (rt_mutex_take(data_lock, RT_WAITING_NO) != RT_EOK)
        rt_thread_mdelay(1);

    uint16_t flame_th = g_app_state.flame_threshold;
    uint16_t mq2_th = g_app_state.mq2_threshold;
    float pm25_th = g_app_state.pm25_threshold;
    rt_mutex_release(data_lock);

    if (flame < flame_th)
    {
        should_alarm = 1;
        alarm_type = 5;
        rt_kprintf("[logic] FIRE ALARM! flame=%u < %u\n", flame, flame_th);
    }
    else if (mq2 > mq2_th)
    {
        should_alarm = 1;
        alarm_type = 4;
        rt_kprintf("[logic] SMOKE ALARM! mq2=%u > %u\n", mq2, mq2_th);
    }
    else if (pm2_5 > (uint16_t)pm25_th)
    {
        should_alarm = 1;
        alarm_type = 6;
        rt_kprintf("[logic] PM2.5 ALARM! pm2_5=%u > %u\n", pm2_5, (uint16_t)pm25_th);
    }

    if (should_alarm)
    {
        rt_mutex_take(data_lock, RT_WAITING_FOREVER);
        g_app_state.alarm_state = 1;
        g_app_state.alarm_type = alarm_type;
        rt_mutex_release(data_lock);

        if (alarm_type == 5)
            rt_event_send(&evt_alarm, EVENT_FIRE_ALARM);
        else if (alarm_type == 4)
            rt_event_send(&evt_alarm, EVENT_SMOKE_ALARM);
        else
            rt_event_send(&evt_alarm, EVENT_PM25_ALARM);
    }
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

    rt_kprintf("[logic] Logic thread started, waiting for events...\n");

    while (1)
    {
        /*
         * 阻塞等待任意告警事件。RT_WAITING_FOREVER 使线程挂起，不消耗 CPU。
         * 当传感器线程发送事件时，内核唤醒本线程。
         */
        if (rt_event_recv(&evt_alarm,
                          EVENT_ALL,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER, &events) == RT_EOK)
        {
            rt_mutex_take(data_lock, RT_WAITING_FOREVER);   /* 进入临界区 */

            if (events & EVENT_FIRE_ALARM)
            {
                g_app_state.beep_status = 1;
                g_app_state.alarm_state = 1;
                g_app_state.alarm_type = 5;
                rt_pin_write(BEEP_PIN, PIN_HIGH);
                servo_set_angle(90);
                rt_kprintf("[logic] EVENT: FIRE_ALARM -> beep ON, servo 90\n");
            }
            else if (events & EVENT_SMOKE_ALARM)
            {
                g_app_state.beep_status = 1;
                g_app_state.alarm_state = 1;
                g_app_state.alarm_type = 4;
                rt_pin_write(BEEP_PIN, PIN_HIGH);
                servo_set_angle(90);
                rt_kprintf("[logic] EVENT: SMOKE_ALARM -> beep ON, servo 90\n");
            }
            else if (events & EVENT_PM25_ALARM)
            {
                g_app_state.alarm_state = 1;
                g_app_state.alarm_type = 6;
                rt_kprintf("[logic] EVENT: PM25_ALARM -> alert only\n");
            }
            else if (events & EVENT_TEMP_ALARM)
            {
                g_app_state.beep_status = 1;
                g_app_state.alarm_state = 1;
                g_app_state.alarm_type = 1;
                rt_pin_write(BEEP_PIN, PIN_HIGH);
                servo_set_angle(90);
                rt_kprintf("[logic] EVENT: TEMP_ALARM -> beep ON, servo 90\n");
            }
            else if (events & EVENT_TILT_ALARM)
            {
                g_app_state.alarm_state = 1;
                g_app_state.alarm_type = 2;
                rt_pin_write(BEEP_PIN, PIN_HIGH);
                rt_kprintf("[logic] EVENT: TILT_ALARM -> beep ON\n");
            }
            else if (events & EVENT_VIBRATION_ALARM)
            {
                g_app_state.alarm_state = 1;
                g_app_state.alarm_type = 3;
                rt_kprintf("[logic] EVENT: VIBRATION_ALARM\n");
            }
            else if (events & EVENT_ALARM_CLEAR)
            {
                g_app_state.beep_status = 0;
                g_app_state.alarm_state = 0;
                g_app_state.alarm_type = 0;
                rt_pin_write(BEEP_PIN, PIN_LOW);
                servo_set_angle(0);
            }

            rt_mutex_release(data_lock);                     /* 退出临界区 */
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

    rt_mutex_take(data_lock, RT_WAITING_FOREVER);           /* 进入临界区 */

    if (flags & KEY_EVENT_WKUP_SHORT)
    {
        g_app_state.temp_threshold += 0.5f;
        if (g_app_state.temp_threshold > 80.0f)
            g_app_state.temp_threshold = 80.0f;
        rt_kprintf("[key] WK_UP: threshold +0.5 -> %d.%d\n",
                   (int)g_app_state.temp_threshold,
                   abs((int)((g_app_state.temp_threshold - (int)g_app_state.temp_threshold) * 10)));
    }
    if (flags & KEY_EVENT_DOWN)
    {
        g_app_state.temp_threshold -= 0.5f;
        if (g_app_state.temp_threshold < 20.0f)
            g_app_state.temp_threshold = 20.0f;
        rt_kprintf("[key] DOWN: threshold -0.5 -> %d.%d\n",
                   (int)g_app_state.temp_threshold,
                   abs((int)((g_app_state.temp_threshold - (int)g_app_state.temp_threshold) * 10)));
    }

    rt_mutex_release(data_lock);                             /* 退出临界区 */

    update_threshold_display();
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

    rt_kprintf("[logic] Key init done (WK_UP=PC5, DOWN=PC4)\n");
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
    uint16_t pm1_0, pm2_5, pm10;
    uint16_t adc0, mq2, flame;
    char addr[16];

    if (strlen(line) == 0) return;

    /* 格式1: [COORD] ADC: ADC0=143(0.12V) */
    if (strstr(line, "[COORD] ADC:") == line)
    {
        if (sscanf(line, "[COORD] ADC: ADC0=%hu", &adc0) == 1)
        {
            g_cc2530_data.coord_adc0 = adc0;
            g_cc2530_data.coord_adc_valid = 1;
            rt_kprintf("[cc2530] Coord ADC: %hu\n", adc0);
        }
        return;
    }

    /* 格式2: [COORD] RX [END0x38A1] PMS7003: PM1.0=21 PM2.5=32 PM10=37 */
    if (strstr(line, "[COORD] RX [") == line && strstr(line, "PMS7003:"))
    {
        if (sscanf(line, "[COORD] RX [%15[^]]] PMS7003: PM1.0=%hu PM2.5=%hu PM10=%hu",
                   addr, &pm1_0, &pm2_5, &pm10) == 4)
        {
            g_cc2530_data.pm.pm1_0 = pm1_0;
            g_cc2530_data.pm.pm2_5 = pm2_5;
            g_cc2530_data.pm.pm10 = pm10;
            g_cc2530_data.pm.last_update_tick = rt_tick_get();
            g_cc2530_data.pm.valid = 1;
            rt_kprintf("[cc2530] PM from %s: %hu,%hu,%hu\n", addr, pm1_0, pm2_5, pm10);
        }
        return;
    }

    /* 格式3: [COORD] RX [END0x7CD0] ADC0=145 MQ2=281 Flame=2038 */
    if (strstr(line, "[COORD] RX [") == line && strstr(line, "ADC0="))
    {
        if (sscanf(line, "[COORD] RX [%15[^]]] ADC0=%hu MQ2=%hu Flame=%hu",
                   addr, &adc0, &mq2, &flame) == 4)
        {
            strncpy(g_cc2530_data.env.end_addr, addr, sizeof(g_cc2530_data.env.end_addr) - 1);
            g_cc2530_data.env.adc0 = adc0;
            g_cc2530_data.env.mq2 = mq2;
            g_cc2530_data.env.flame = flame;
            g_cc2530_data.env.last_update_tick = rt_tick_get();
            g_cc2530_data.env.valid = 1;
            rt_kprintf("[cc2530] Env from %s: ADC=%hu MQ2=%hu Flame=%hu\n",
                       addr, adc0, mq2, flame);
        }
        return;
    }

    /* 格式4: [END0x7CD0] 21,32,36,281,2038 (推荐 — 5字段CSV) */
    if (line[0] == '[' && strstr(line, "END") == line + 1)
    {
        /* 尝试5字段: PM1.0,PM2.5,PM10,MQ2,Flame */
        if (sscanf(line, "[%15[^]]] %hu,%hu,%hu,%hu,%hu",
                   addr, &pm1_0, &pm2_5, &pm10, &mq2, &flame) == 6)
        {
            g_cc2530_data.pm.pm1_0 = pm1_0;
            g_cc2530_data.pm.pm2_5 = pm2_5;
            g_cc2530_data.pm.pm10 = pm10;
            g_cc2530_data.pm.last_update_tick = rt_tick_get();
            g_cc2530_data.pm.valid = 1;
            strncpy(g_cc2530_data.env.end_addr, addr, sizeof(g_cc2530_data.env.end_addr) - 1);
            g_cc2530_data.env.mq2 = mq2;
            g_cc2530_data.env.flame = flame;
            g_cc2530_data.env.last_update_tick = rt_tick_get();
            g_cc2530_data.env.valid = 1;
            rt_kprintf("[cc2530] CSV from %s: PM=%hu,%hu,%hu MQ2=%hu Flame=%hu\n",
                       addr, pm1_0, pm2_5, pm10, mq2, flame);
            return;
        }
        /* 兼容3字段: [END0xXXXX] PM1.0,PM2.5,PM10 */
        if (sscanf(line, "[%15[^]]] %hu,%hu,%hu",
                   addr, &pm1_0, &pm2_5, &pm10) == 4)
        {
            g_cc2530_data.pm.pm1_0 = pm1_0;
            g_cc2530_data.pm.pm2_5 = pm2_5;
            g_cc2530_data.pm.pm10 = pm10;
            g_cc2530_data.pm.last_update_tick = rt_tick_get();
            g_cc2530_data.pm.valid = 1;
            rt_kprintf("[cc2530] PM from %s: %hu,%hu,%hu\n", addr, pm1_0, pm2_5, pm10);
        }
        return;
    }

    /* 格式5(旧): 纯CSV — "PM1.0,PM2.5,PM10" 向后兼容 */
    if (sscanf(line, "%hu,%hu,%hu", &pm1_0, &pm2_5, &pm10) == 3)
    {
        g_cc2530_data.pm.pm1_0 = pm1_0;
        g_cc2530_data.pm.pm2_5 = pm2_5;
        g_cc2530_data.pm.pm10 = pm10;
        g_cc2530_data.pm.last_update_tick = rt_tick_get();
        g_cc2530_data.pm.valid = 1;
        rt_kprintf("[cc2530] PM: %hu,%hu,%hu\n", pm1_0, pm2_5, pm10);
    }
}

/* ==================== CC2530 串口接收线程（静态分配） ==================== */
static rt_uint8_t cc2530_rx_stack[1536];
static struct rt_thread cc2530_rx_thread;
/*
 * cc2530_recv_thread:
 *   从 uart3 逐字节读取 CC2530 发来的数据，按行缓冲。
 *   收到 '\n' 时触发行解析，提取传感器数值。
 *
 *   数据流：
 *     CC2530 --UART(9600bps)--> USART3(PB10/PB11) --> 逐字节读取
 *     --> 行缓冲 --> '\n' 触发解析 --> g_cc2530_data
 */
static void cc2530_recv_thread(void *parameter)
{
    rt_device_t serial = (rt_device_t)parameter;
    char ch;
    uint8_t skip_line = 0;

    rt_kprintf("[cc2530] Receiver thread started\n");

    while (1)
    {
        if (rt_device_read(serial, -1, &ch, 1) == 1)
        {
            if (ch == '\n')
            {
                if (!skip_line)
                {
                    g_cc2530_parser.line_buf[g_cc2530_parser.line_pos] = '\0';
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
                rt_kprintf("[cc2530] Line too long, skipped (buf=%d)\n", CC2530_LINE_BUF_SIZE);
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
        rt_kprintf("[cc2530] Cannot find device: %s\n", CC2530_UART_DEVICE);
        return;
    }

    /* 以中断接收模式打开 */
    if (rt_device_open(serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK)
    {
        rt_kprintf("[cc2530] Failed to open %s\n", CC2530_UART_DEVICE);
        return;
    }

    /* 配置串口参数：9600, 8N1 */
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    config.baud_rate = BAUD_RATE_9600;
    config.data_bits = DATA_BITS_8;
    config.stop_bits = STOP_BITS_1;
    config.parity    = PARITY_NONE;
    config.bufsz     = 64;
    rt_device_control(serial, RT_DEVICE_CTRL_CONFIG, &config);

    rt_kprintf("[cc2530] %s opened at 9600 baud\n", CC2530_UART_DEVICE);

    /* 静态创建接收线程（栈不占堆）*/
    rt_thread_init(&cc2530_rx_thread,
                   "cc2530_rx",
                   cc2530_recv_thread,
                   serial,
                   cc2530_rx_stack,
                   sizeof(cc2530_rx_stack),
                   24, 10);
    rt_thread_startup(&cc2530_rx_thread);
    rt_kprintf("[cc2530] Receiver thread started (prio=24, static stack=%d)\n", sizeof(cc2530_rx_stack));
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
        rt_kprintf("[logic] FATAL: mutex create failed!\n");
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
                                              2048,
                                              8,
                                              10);
    if (tid_logic)
    {
        rt_thread_startup(tid_logic);
        rt_kprintf("[logic] Init OK T:%.1f PM25:%.0f MQ2:%u Flame:%u\n",
                   g_app_state.temp_threshold, g_app_state.pm25_threshold,
                   g_app_state.mq2_threshold, g_app_state.flame_threshold);
    }
}
