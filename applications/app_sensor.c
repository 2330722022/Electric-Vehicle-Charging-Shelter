#include "app_sensor.h"
#include "app_logic.h"
#include "app_net.h"
#include <board.h>
#include <rtdevice.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "aht10.h"
#include "ap3216c.h"
#include "icm20608.h"

#define TAG "sensor"

/*
 * 模块：传感器采集 (app_sensor.c) — 电动车充电棚环境监测
 * 版本：v2.1 充电棚专版 (事件集同步)
 * 日期：2026-05-20
 * OS 概念体现：
 *   1. 多线程调度 — 采集线程设为优先级22（低于 OneNET 上传15 和逻辑处理8），
 *      RT-Thread 抢占式调度确保高优先级线程就绪时立即抢占 CPU。
 *   2. 事件集 — 传感器线程启动时阻塞等待 EVENT_MQTT_OK，
 *      确保 MQTT 连接就绪后才开始采集循环，避免告警事件无法上传。
 *   3. 总线驱动管理 — AHT10 挂载 I2C3，ICM20608 挂载 I2C2，RT-Thread 设备框架
 *      通过统一的 rt_device 接口管理不同总线设备，上层无需关心底层时序。
 */

/*
 * ==================== 硬件选型说明 ====================
 *
 * [AHT10 温湿度传感器 — I2C3 总线]
 *   选型依据：
 *     - 数字输出，无需额外 ADC，精度 ±0.3°C / ±2%RH
 *     - I2C 接口仅需 2 根线（SCL/SDA），节省引脚
 *     - RT-Thread AHT10 驱动已适配，直接调用 aht10_read_temperature/humidity
 *   为何 I2C3：
 *     - I2C1 被 AP3216C 和 ICM20608 共享（同总线多设备）
 *     - I2C2 留给扩展
 *     - I2C3 独立挂载 AHT10，避免总线冲突
 *
 * [AP3216C 环境光/接近传感器 — I2C2 总线]
 *   选型依据：
 *     - 集成 ALS（环境光）+ PS（接近检测），单芯片双功能
 *     - I2C 地址 0x1E，与 ICM20608(0x68) 不冲突，可共享 I2C2
 *
 * [ICM20608 6轴IMU — I2C2 总线]
 *   选型依据：
 *     - 集成 3轴加速度计 + 3轴陀螺仪，适合姿态检测
 *     - 加速度计用于计算倾斜角（atan2），陀螺仪用于检测振动
 *     - I2C 地址 0x68，与 AP3216C(0x1E) 不冲突
 *   为何 I2C 而非 SPI：
 *     - SPI2 已被 RW007 WiFi 模块独占，不可复用
 *     - ICM20608 支持 I2C 接口，共享 I2C2 总线减少引脚占用
 *     - I2C 轮询速率（100kHz）足够满足环境监测的 5 秒采样周期
 *
 * [总线资源分配总结]
 *   I2C1: 保留
 *   I2C2: AP3216C (0x1E) + ICM20608 (0x68) — 同总线双设备，地址隔离
 *   I2C3: AHT10 (0x38) — 独立总线
 *   SPI2: RW007 WiFi — 独占
 */

/* ==================== ICM20608倾斜监测配置 ==================== */
/* 充电棚场景：固定安装，倾倒检测已禁用 */
//#define TILT_THRESHOLD     15
//#define TILT_SENSITIVITY   2

/* ==================== 全局变量定义 ==================== */
struct sensor_data shared_data;
rt_mutex_t data_mutex = RT_NULL;

char status_json[256];

void *aht20_dev = RT_NULL;
void *ap3216c_dev = RT_NULL;
void *icm20608_dev = RT_NULL;

/* ==================== 倾斜角度计算 ==================== */
/* 充电棚场景：固定安装，倾倒检测已禁用 */
#if 0
static void calculate_tilt_angle(rt_int16_t accel_x, rt_int16_t accel_y, rt_int16_t accel_z,
                                  float *angle_x, float *angle_y)
{
    float g = sqrt(accel_x * accel_x + accel_y * accel_y + accel_z * accel_z);
    if (g < 100)
    {
        *angle_x = 0;
        *angle_y = 0;
        return;
    }
    *angle_x = atan2((float)accel_y, (float)accel_z) * 180 / 3.14159f;
    *angle_y = atan2((float)accel_x, (float)accel_z) * 180 / 3.14159f;
}
#endif

/* ==================== 传感器采集线程 ==================== */
/*
 * 优先级说明：
 *   采集线程优先级设为 22（最低），确保不干扰：
 *     - 显示线程(20) — LVGL 需要及时刷新屏幕
 *     - 逻辑线程(8) — 告警响应需要低延迟
 *     - Web Server(18) — 网络 I/O 需要及时处理
 *   RT-Thread 抢占式调度确保高优先级线程就绪时立即抢占 CPU。
 */
static void sensor_thread_entry(void *parameter)
{
    struct sensor_data data;
    //rt_int16_t accel_x, accel_y, accel_z;  /* 倾倒检测已禁用 */
    rt_int16_t gyro_x, gyro_y, gyro_z;
    //static int tilt_warning_count = 0;  /* 倾倒检测已禁用 */
    static int loop_count = 0;
    rt_uint32_t events;
#define VIBRATION_THRESHOLD 1000

    LOG_I(TAG, "Thread entry, waiting for EVENT_MQTT_OK...");
    rt_event_recv(&sys_event, EVENT_MQTT_OK,
                  RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                  RT_WAITING_FOREVER, &events);
    LOG_I(TAG, "EVENT_MQTT_OK received, starting sensor loop");

    while (1)
    {
        loop_count++;

        rt_memset(&data, 0, sizeof(struct sensor_data));

        /* 读取温度 & 湿度 (I2C3 — AHT10) */
        if (aht20_dev != RT_NULL)
        {
            float t = aht10_read_temperature((aht10_device_t)aht20_dev);
            float h = aht10_read_humidity((aht10_device_t)aht20_dev);
            if (t > -50.0f && t < 125.0f && h >= 0.0f && h <= 100.0f)
            {
                data.temperature = t;
                data.humidity    = h;
                LOG_D(TAG, "T=%.1f H=%.1f", t, h);
            }
            else
            {
                LOG_W(TAG, "AHT10 invalid: T=%.1f H=%.1f", t, h);
            }
        }

        /* 读取光照 & 接近 (I2C2 — AP3216C) */
        if (ap3216c_dev != RT_NULL)
        {
            float als = ap3216c_read_ambient_light((ap3216c_device_t)ap3216c_dev);
            uint16_t ps = ap3216c_read_ps_data((ap3216c_device_t)ap3216c_dev);

            if (als >= 0.0f && als < 100000.0f)
            {
                data.light = als;
            }
            else
            {
                LOG_W(TAG, "AP3216C ALS invalid: %.0f", als);
                data.light = 0;
            }

            if (ps < 0x1000)
            {
                data.proximity = ps;
            }
            else
            {
                LOG_W(TAG, "AP3216C PS invalid: %u", ps);
                data.proximity = 0;
            }

            LOG_D(TAG, "Light=%.0f Lux PS=%u", data.light, data.proximity);
        }

        /* 读取加速度计 + 陀螺仪 (I2C2 — ICM20608) */
        if (icm20608_dev != RT_NULL)
        {
            /* 充电棚场景：固定安装，倾倒检测已禁用 */
#if 0
            rt_err_t acc_ret = icm20608_get_accel((icm20608_device_t)icm20608_dev,
                                   &accel_x, &accel_y, &accel_z);
            if (acc_ret == RT_EOK)
            {
                calculate_tilt_angle(accel_x, accel_y, accel_z,
                                     &data.tilt_angle_x, &data.tilt_angle_y);

                if (abs(data.tilt_angle_x) > TILT_THRESHOLD ||
                    abs(data.tilt_angle_y) > TILT_THRESHOLD)
                {
                    tilt_warning_count++;
                    if (tilt_warning_count >= TILT_SENSITIVITY)
                    {
                        data.tilt_alarm = 1;
                        rt_event_send(&evt_alarm, EVENT_TILT_ALARM);
                        LOG_W(TAG, "ALERT: Device tilted! X:%.1f Y:%.1f",
                              data.tilt_angle_x, data.tilt_angle_y);
                    }
                }
                else
                {
                    tilt_warning_count = 0;
                    data.tilt_alarm = 0;
                }
            }
            else
            {
                LOG_E(TAG, "ICM20608 accel read err=%d", acc_ret);
                data.tilt_angle_x = 0;
                data.tilt_angle_y = 0;
                data.tilt_alarm = 0;
            }
#endif

            rt_err_t gyro_ret = icm20608_get_gyro((icm20608_device_t)icm20608_dev,
                                  &gyro_x, &gyro_y, &gyro_z);
            if (gyro_ret == RT_EOK)
            {
                int vib = (abs(gyro_x) > VIBRATION_THRESHOLD ||
                           abs(gyro_y) > VIBRATION_THRESHOLD ||
                           abs(gyro_z) > VIBRATION_THRESHOLD) ? 1 : 0;
                data.vibration_detected = vib;

                if (vib)
                {
                    rt_event_send(&evt_alarm, EVENT_VIBRATION_ALARM);
                    LOG_W(TAG, "ALERT: Vibration detected!");
                }
            }
            else
            {
                LOG_E(TAG, "ICM20608 gyro read err=%d", gyro_ret);
                data.vibration_detected = 0;
            }
        }

        /* 处理按键事件（WK_UP/DOWN 调整阈值）*/
        process_key_events();

        /* 调用业务逻辑中心进行温度阈值判定 */
        logic_handle(data.temperature);

        /* 电动车充电棚专属：CC2530 环境数据告警判定 — 临界区快照读取 */
        {
            uint16_t mq2_local = 0, flame_local = 0, pm25_local = 0;
            uint8_t env_valid = 0, pm_valid = 0;

            rt_mutex_take(data_lock, RT_WAITING_FOREVER);
            env_valid = g_cc2530_data.env.valid;
            if (env_valid)
            {
                mq2_local   = g_cc2530_data.env.mq2;
                flame_local = g_cc2530_data.env.flame;
            }
            pm_valid = g_cc2530_data.pm.valid;
            if (pm_valid)
            {
                pm25_local = g_cc2530_data.pm.pm2_5;
            }
            rt_mutex_release(data_lock);

            if (env_valid)
                logic_handle_cc2530_env(mq2_local, flame_local);
            if (pm_valid)
                logic_handle_cc2530_pm(pm25_local);
        }

        /* 同步 actuator 状态 */
        rt_mutex_take(data_lock, RT_WAITING_FOREVER);
        data.actuator_status = g_app_state.beep_status;
        float local_threshold = g_app_state.temp_threshold;
        uint8_t fan_en = g_app_state.fan_en;
        uint8_t alarm_type = g_app_state.alarm_type;
        rt_mutex_release(data_lock);

        /*
         * 风扇自动策略（优先级从高到低）：
         *   1. 火灾/烟雾告警 → 强制关闭（安全优先，防止助燃）
         *   2. 手动使能(fan_en=1) → 强制开启（远程/本地手动控制）
         *   3. 温度偏高但未达告警 → 自动通风（阈值 - 5°C 为通风触发点）
         *      - 演示场景：按键将阈值调到 30°C，室温 ~25°C 即触发通风
         *      - 实际场景：阈值默认 80°C，>75°C 时提前通风散热
         *   4. 其他情况 → 关闭
         */
        {
            uint8_t fan_final;
            if (alarm_type == 4 || alarm_type == 5)
            {
                fan_final = 0;
            }
            else if (fan_en)
            {
                fan_final = 1;
            }
            else if (data.temperature > local_threshold - 5.0f && data.temperature < local_threshold)
            {
                fan_final = 1;
            }
            else
            {
                fan_final = 0;
            }

            /* 写 FAN_PIN 前重新加锁：防止与 logic 线程的火灾/烟雾告警竞态 */
            rt_mutex_take(data_lock, RT_WAITING_FOREVER);
            if (g_app_state.alarm_type == 4 || g_app_state.alarm_type == 5)
                fan_final = 0;
            data.fan_status = fan_final;
            rt_pin_write(FAN_PIN, fan_final ? PIN_HIGH : PIN_LOW);
            rt_mutex_release(data_lock);
        }

        /* 更新共享数据 — 临界区保护 */
        rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
        shared_data = data;

        /* 构建 /get_status JSON — 供 HTTP/OneNET 零拷贝引用 */
        {
            int alarm_state = (data.temperature > local_threshold)
                              || /*data.tilt_alarm ||*/ data.vibration_detected;  /* 倾倒检测已禁用 */
            int _th_i = (int)local_threshold;
            int _th_d = (int)((local_threshold - _th_i) * 10);
            if (_th_d < 0) _th_d = -_th_d;
            rt_snprintf(status_json, sizeof(status_json),
                        "{\"alarm_state\":%d,\"beep\":%u,\"fan_status\":%u,"
                        "\"work_mode\":%d,\"temp_threshold\":%d.%d}",
                        alarm_state, g_app_state.beep_status, data.fan_status, 1,
                        _th_i, _th_d);
        }

        rt_mutex_release(data_mutex);

        LOG_D(TAG, "#%d T=%.1f H=%.1f L=%.0f", loop_count, data.temperature, data.humidity, data.light);

        /* 5 秒采样周期 */
        rt_thread_mdelay(5000);
    }
}

/* ==================== 传感器初始化 ==================== */
/*
 * app_sensor_init:
 *   创建互斥量保护共享数据，初始化传感器硬件，启动采集线程。
 *
 *   初始化序列：
 *     1. 创建 data_mutex — 保护 shared_data、status_json
 *     2. 初始化 I2C 总线传感器（AHT10、AP3216C、ICM20608）
 *     3. 校准 ICM20608（消除零偏）
 *     4. 启动采集线程（优先级 25，低优先级）
 */
void app_sensor_init(void)
{
    /* 1. 创建互斥量 */
    data_mutex = rt_mutex_create("data_mutex", RT_IPC_FLAG_FIFO);
    if (data_mutex == RT_NULL)
    {
        LOG_E(TAG, "data_mutex create failed!");
        return;
    }

    /* 2. 初始化传感器硬件 */
    aht20_dev = (void *)aht10_init("i2c3");
    if (aht20_dev == RT_NULL)
        LOG_W(TAG, "AHT10 init failed");
    else
        LOG_I(TAG, "AHT10 OK (i2c3)");

    ap3216c_dev = (void *)ap3216c_init("i2c2");
    if (ap3216c_dev == RT_NULL)
        LOG_W(TAG, "AP3216C init failed");
    else
        LOG_I(TAG, "AP3216C OK (i2c2)");

    icm20608_dev = (void *)icm20608_init("i2c2");
    if (icm20608_dev != RT_NULL)
    {
        /* 3. 校准 — 采集 100 个样本计算零偏 */
        icm20608_calib_level((icm20608_device_t)icm20608_dev, 100);
        LOG_I(TAG, "ICM20608 calibrated");
    }
    else
    {
        LOG_W(TAG, "ICM20608 init failed");
    }

    /* 4. 启动采集线程 — 优先级22（低于LVGL线程20，高于CC2530接收线程24）*/
    rt_thread_t tid = rt_thread_create("sensor",
                                        sensor_thread_entry,
                                        RT_NULL,
                                        4096,
                                        22,
                                        10);
    if (tid)
    {
        rt_thread_startup(tid);
        LOG_I(TAG, "Thread started (prio=22, interval=5s)");
    }
    else
    {
        LOG_E(TAG, "Failed to create thread!");
    }
}
