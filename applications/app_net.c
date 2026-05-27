#include "app_net.h"
#include "app_sensor.h"
#include "app_logic.h"
#include <board.h>
#include <rtdevice.h>
#include <wlan_mgnt.h>
#include <onenet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

/*
 * 模块：网络通信 (app_net.c) — 电动车充电棚环境监测
 * 版本：v4.0 充电棚专版 (snprintf分段上报 + 事件集同步)
 * 日期：2026-05-20
 * OS 概念体现：
 *   1. 信号量 (Semaphore) — net_ready_sem 实现 HTTP Server 与 WiFi 的线程间同步。
 *   2. 事件集 (Event) — sys_event 控制全局启动时序：
 *      WiFi 就绪 → EVENT_WIFI_OK → OneNET 初始化 MQTT → EVENT_MQTT_OK → 传感器采集。
 *   3. snprintf 安全序列化 — 所有 MQTT 报文通过 snprintf 静态构建，
 *      零 malloc，杜绝内存泄漏和碎片。
 *   4. ISR安全命令处理 — onenet_cmd_rsp_cb 仅写 volatile 标志位，
 *      由上传线程在线程上下文中安全消费。
 *   5. 异步 HTTP 处理 — 每个 HTTP 请求创建独立线程处理。
 */

#define TAG "net"

/* ==================== 配置 ==================== */
#define WLAN_SSID       "whu"
#define WLAN_PASSWORD   "99999999"
#define HTTP_PORT       80

/* RW007复位引脚（PG15 = GET_PIN(G, 15)）*/
#define RW007_RST_PIN   111

/* LED引脚 */
#define LED_R_PIN       GET_PIN(F, 12)

/* ==================== 全局变量 ==================== */
/*
 * net_ready_sem:
 *   信号量 — WiFi 就绪同步原语。
 *   初始值 = 0：消费者（HTTP Server）调用 rt_sem_take() 将阻塞，
 *   直到生产者（WiFi 连接成功）调用 rt_sem_release() 唤醒。
 */
rt_sem_t net_ready_sem = RT_NULL;

/*
 * sys_event:
 *   全局事件集 — 控制启动时序。
 *   EVENT_WIFI_OK: WiFi 线程获取 IP 后发送
 *   EVENT_MQTT_OK: MQTT 线程初始化成功后发送
 *   传感器线程等待 EVENT_MQTT_OK 后才开始采集循环。
 */
struct rt_event sys_event;

int wifi_connected = 0;

#define HEARTBEAT_QOS0  0

/* HTTP响应头 */
static const char http_header_200[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: %d\r\n\r\n";
static const char http_header_503[] = "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";

/*
 * 下行命令标志位（ISR安全 — volatile存储，回调只写、线程只读）
 *   说明：OneNET 命令回调可能从 MQTT 内部线程上下文调用，
 *   不能使用 rt_mutex_take (RT_WAITING_FOREVER) 等阻塞操作。
 *   解决办法：回调仅置 volatile 标志位，由上传线程在主循环中消费。
 *
 *   ⚠ 使用独立 uint8_t 而非位域 — 位域共享内存字节，多线程读-改-写会互相覆盖。
 *      ARM Cortex-M 单字节 volatile 读/写是原子的，无竞态。
 */
static volatile uint8_t net_led_pending;
static volatile uint8_t net_led_on;
static volatile uint8_t net_beep_pending;
static volatile uint8_t net_beep_on;
static volatile uint8_t net_fan_pending;
static volatile uint8_t net_fan_on;
static volatile uint8_t net_servo_pending;
static volatile int    net_servo_angle;
static volatile uint8_t net_threshold_pending;
static volatile float   net_threshold_val;

#define MQTT_TOPIC "$sys/67k36rzgOO/test1/thing/property/post"

#define PAYLOAD_BUF_SIZE 512

static float last_valid_temperature = -99.0f;
static float last_valid_humidity = -1.0f;

static int is_valid_json_payload(const char *payload, int len)
{
    if (len <= 0 || len >= PAYLOAD_BUF_SIZE)
        return 0;
    if (payload[0] != '{' || payload[len - 1] != '}')
        return 0;
    for (int i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)payload[i];
        if (c < 0x20 && c != '\t' && c != '\r' && c != '\n')
            return 0;
    }
    return 1;
}

static float clamp_temperature(float t)
{
    if (t < -40.0f || t > 80.0f)
        return last_valid_temperature;
    last_valid_temperature = t;
    return t;
}

static float clamp_humidity(float h)
{
    if (h < 0.0f || h > 100.0f)
        return last_valid_humidity;
    last_valid_humidity = h;
    return h;
}

rt_err_t publish_temp_threshold(void)
{
    char payload[PAYLOAD_BUF_SIZE];
    rt_memset(payload, 0, sizeof(payload));

    rt_mutex_take(data_lock, RT_WAITING_FOREVER);
    float threshold = g_app_state.temp_threshold;
    rt_mutex_release(data_lock);

    LOG_I(TAG, "Publish threshold: %d.%d", (int)threshold, abs((int)((threshold - (int)threshold) * 10)));

    int t_int = (int)threshold;
    int t_dec = abs((int)((threshold - t_int) * 10 + 0.5f));
    int len = rt_snprintf(payload, sizeof(payload),
        "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{\"temp_threshold\":{\"value\":%d.%d}},\"method\":\"thing.property.post\"}",
        (unsigned long)rt_tick_get(), t_int, t_dec);

    if (len >= (int)sizeof(payload))
    {
        LOG_W(TAG, "threshold payload truncated, dropped");
        return -RT_ERROR;
    }

    return onenet_mqtt_publish(MQTT_TOPIC, (uint8_t *)payload, strlen(payload));
}

static void post_block_fire_safety(const struct sensor_data *data)
{
    char payload[PAYLOAD_BUF_SIZE];

    if (data == RT_NULL) return;

    rt_memset(payload, 0, sizeof(payload));

    rt_mutex_take(data_lock, RT_WAITING_FOREVER);
    int app_alarm_type = g_app_state.alarm_type;
    float threshold = g_app_state.temp_threshold;
    rt_mutex_release(data_lock);

    int alarm_state = 0;
    if (app_alarm_type >= 4)
        alarm_state = app_alarm_type;
    else if (data->temperature > threshold)
        alarm_state = 1;
    else if (data->vibration_detected)
        alarm_state = 2;

    rt_mutex_take(data_lock, RT_WAITING_FOREVER);
    int mq2 = (int)g_cc2530_data.env.mq2;
    int flame = (int)g_cc2530_data.env.flame;
    rt_mutex_release(data_lock);

    int len = rt_snprintf(payload, sizeof(payload),
        "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
        "\"alarm_state\":{\"value\":%d},"
        "\"mq2\":{\"value\":%d},"
        "\"flame\":{\"value\":%d}"
        "},\"method\":\"thing.property.post\"}",
        (unsigned long)rt_tick_get(), alarm_state, mq2, flame);

    if (len >= (int)sizeof(payload))
    {
        LOG_W(TAG, "Block A truncated, dropped");
        return;
    }
    if (!is_valid_json_payload(payload, len))
    {
        LOG_W(TAG, "Block A invalid payload, dropped");
        return;
    }

    rt_err_t ret = onenet_mqtt_publish(MQTT_TOPIC, (uint8_t *)payload, strlen(payload));
    if (ret == 0)
        LOG_I(TAG, "A(fire) alarm=%d mq2=%d flame=%d", alarm_state, mq2, flame);
    else
        LOG_W(TAG, "A(fire) FAIL ret=%d", ret);
}

static void post_block_environment(const struct sensor_data *data)
{
    char payload[PAYLOAD_BUF_SIZE];

    if (data == RT_NULL) return;

    rt_memset(payload, 0, sizeof(payload));

    rt_mutex_take(data_lock, RT_WAITING_FOREVER);
    int pm25_valid = g_cc2530_data.pm.valid;
    int pm25 = (pm25_valid) ? (int)g_cc2530_data.pm.pm2_5 : 0;
    rt_mutex_release(data_lock);

    float temp_val = clamp_temperature(data->temperature);
    float humi_val = clamp_humidity(data->humidity);
    int temp_is_null = (data->temperature < -40.0f || data->temperature > 80.0f);
    int humi_is_null = (data->humidity < 0.0f || data->humidity > 100.0f);

    int temp_int = (int)temp_val;
    int temp_dec = abs((int)((temp_val - temp_int) * 10 + 0.5f));
    int humi_int = (int)humi_val;
    int humi_dec = abs((int)((humi_val - humi_int) * 10 + 0.5f));

    int len;

    if (temp_is_null && humi_is_null && !pm25_valid)
    {
        len = rt_snprintf(payload, sizeof(payload),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
            "\"temperature\":{\"value\":null},"
            "\"humidity\":{\"value\":null},"
            "\"pm2_5\":{\"value\":null}"
            "},\"method\":\"thing.property.post\"}",
            (unsigned long)rt_tick_get());
    }
    else if (temp_is_null && humi_is_null)
    {
        len = rt_snprintf(payload, sizeof(payload),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
            "\"temperature\":{\"value\":null},"
            "\"humidity\":{\"value\":null},"
            "\"pm2_5\":{\"value\":%d}"
            "},\"method\":\"thing.property.post\"}",
            (unsigned long)rt_tick_get(), pm25);
    }
    else if (temp_is_null && !pm25_valid)
    {
        len = rt_snprintf(payload, sizeof(payload),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
            "\"temperature\":{\"value\":null},"
            "\"humidity\":{\"value\":%d.%d},"
            "\"pm2_5\":{\"value\":null}"
            "},\"method\":\"thing.property.post\"}",
            (unsigned long)rt_tick_get(), humi_int, humi_dec);
    }
    else if (temp_is_null)
    {
        len = rt_snprintf(payload, sizeof(payload),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
            "\"temperature\":{\"value\":null},"
            "\"humidity\":{\"value\":%d.%d},"
            "\"pm2_5\":{\"value\":%d}"
            "},\"method\":\"thing.property.post\"}",
            (unsigned long)rt_tick_get(), humi_int, humi_dec, pm25);
    }
    else if (humi_is_null && !pm25_valid)
    {
        len = rt_snprintf(payload, sizeof(payload),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
            "\"temperature\":{\"value\":%d.%d},"
            "\"humidity\":{\"value\":null},"
            "\"pm2_5\":{\"value\":null}"
            "},\"method\":\"thing.property.post\"}",
            (unsigned long)rt_tick_get(), temp_int, temp_dec);
    }
    else if (humi_is_null)
    {
        len = rt_snprintf(payload, sizeof(payload),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
            "\"temperature\":{\"value\":%d.%d},"
            "\"humidity\":{\"value\":null},"
            "\"pm2_5\":{\"value\":%d}"
            "},\"method\":\"thing.property.post\"}",
            (unsigned long)rt_tick_get(), temp_int, temp_dec, pm25);
    }
    else if (!pm25_valid)
    {
        len = rt_snprintf(payload, sizeof(payload),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
            "\"temperature\":{\"value\":%d.%d},"
            "\"humidity\":{\"value\":%d.%d},"
            "\"pm2_5\":{\"value\":null}"
            "},\"method\":\"thing.property.post\"}",
            (unsigned long)rt_tick_get(), temp_int, temp_dec, humi_int, humi_dec);
    }
    else
    {
        len = rt_snprintf(payload, sizeof(payload),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
            "\"temperature\":{\"value\":%d.%d},"
            "\"humidity\":{\"value\":%d.%d},"
            "\"pm2_5\":{\"value\":%d}"
            "},\"method\":\"thing.property.post\"}",
            (unsigned long)rt_tick_get(), temp_int, temp_dec, humi_int, humi_dec, pm25);
    }

    if (len >= (int)sizeof(payload))
    {
        LOG_W(TAG, "Block B truncated, dropped");
        return;
    }
    if (!is_valid_json_payload(payload, len))
    {
        LOG_W(TAG, "Block B invalid payload, dropped");
        return;
    }

    rt_err_t ret = onenet_mqtt_publish(MQTT_TOPIC, (uint8_t *)payload, strlen(payload));
    if (ret == 0)
        LOG_I(TAG, "B(env) T=%d.%d H=%d.%d PM2.5=%d",
              (int)temp_val, abs((int)((temp_val - (int)temp_val) * 10)),
              (int)humi_val, abs((int)((humi_val - (int)humi_val) * 10)), pm25);
    else
        LOG_W(TAG, "B(env) FAIL ret=%d", ret);
}

static void post_block_status(const struct sensor_data *data)
{
    char payload[PAYLOAD_BUF_SIZE];

    if (data == RT_NULL) return;
    rt_memset(payload, 0, sizeof(payload));

    rt_mutex_take(data_lock, RT_WAITING_FOREVER);
    int beep_val = g_app_state.beep_status;
    int fan_en_val = g_app_state.fan_en;
    int servo = g_app_state.servo_angle;
    rt_mutex_release(data_lock);

    int relay = data->actuator_status ? 1 : 0;
    int fan = data->fan_status ? 1 : 0;
    int vib = data->vibration_detected ? 1 : 0;

    int len = rt_snprintf(payload, sizeof(payload),
        "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
        "\"beep\":{\"value\":%d},"
        "\"relay\":{\"value\":%d},"
        "\"fan_en\":{\"value\":%s},"
        "\"fan_status\":{\"value\":%s},"
        "\"vibration\":{\"value\":%d},"
        "\"steeringstatus\":{\"value\":%d}"
        "},\"method\":\"thing.property.post\"}",
        (unsigned long)rt_tick_get(), beep_val, relay,
        fan_en_val ? "true" : "false",
        fan ? "true" : "false",
        vib, servo);

    if (len >= (int)sizeof(payload))
    {
        LOG_W(TAG, "Block C truncated, dropped");
        return;
    }
    if (!is_valid_json_payload(payload, len))
    {
        LOG_W(TAG, "Block C invalid payload, dropped");
        return;
    }

    rt_err_t ret = onenet_mqtt_publish(MQTT_TOPIC, (uint8_t *)payload, strlen(payload));
    if (ret == 0)
        LOG_I(TAG, "C(st) beep=%d relay=%d fan_en=%d fan=%d vib=%d servo=%d",
              beep_val, relay, fan_en_val, fan, vib, servo);
    else
        LOG_W(TAG, "C(st) FAIL ret=%d", ret);
}

static void post_block_config(void)
{
    char payload[PAYLOAD_BUF_SIZE];
    rt_memset(payload, 0, sizeof(payload));

    rt_mutex_take(data_lock, RT_WAITING_FOREVER);
    float temp_threshold = g_app_state.temp_threshold;
    float pm25_threshold = g_app_state.pm25_threshold;
    int mq2_threshold = (int)g_app_state.mq2_threshold;
    int flame_threshold = (int)g_app_state.flame_threshold;
    int pm_valid = g_cc2530_data.pm.valid;
    int pm1_0 = (pm_valid) ? (int)g_cc2530_data.pm.pm1_0 : 0;
    int pm10 = (pm_valid) ? (int)g_cc2530_data.pm.pm10 : 0;
    rt_mutex_release(data_lock);
    float light;
    {
        rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
        light = shared_data.light;
        rt_mutex_release(data_mutex);
    }

    int temp_th_int = (int)temp_threshold;
    int temp_th_dec = abs((int)((temp_threshold - temp_th_int) * 10 + 0.5f));
    int pm25_th_int = (int)pm25_threshold;
    int pm25_th_dec = abs((int)((pm25_threshold - pm25_th_int) * 10 + 0.5f));
    int light_int = (int)light;

    int len;
    if (!pm_valid)
    {
        len = rt_snprintf(payload, sizeof(payload),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
            "\"temp_threshold\":{\"value\":%d.%d},"
            "\"pm25_threshold\":{\"value\":%d.%d},"
            "\"mq2_threshold\":{\"value\":%d},"
            "\"flame_threshold\":{\"value\":%d},"
            "\"pm1_0\":{\"value\":null},"
            "\"pm10\":{\"value\":null},"
            "\"light\":{\"value\":%d}"
            "},\"method\":\"thing.property.post\"}",
            (unsigned long)rt_tick_get(),
            temp_th_int, temp_th_dec, pm25_th_int, pm25_th_dec,
            mq2_threshold, flame_threshold, light_int);
    }
    else
    {
        len = rt_snprintf(payload, sizeof(payload),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
            "\"temp_threshold\":{\"value\":%d.%d},"
            "\"pm25_threshold\":{\"value\":%d.%d},"
            "\"mq2_threshold\":{\"value\":%d},"
            "\"flame_threshold\":{\"value\":%d},"
            "\"pm1_0\":{\"value\":%d},"
            "\"pm10\":{\"value\":%d},"
            "\"light\":{\"value\":%d}"
            "},\"method\":\"thing.property.post\"}",
            (unsigned long)rt_tick_get(),
            temp_th_int, temp_th_dec, pm25_th_int, pm25_th_dec,
            mq2_threshold, flame_threshold, pm1_0, pm10, light_int);
    }

    if (len >= (int)sizeof(payload))
    {
        LOG_W(TAG, "Block D truncated, dropped");
        return;
    }
    if (!is_valid_json_payload(payload, len))
    {
        LOG_W(TAG, "Block D invalid payload, dropped");
        return;
    }

    rt_err_t ret = onenet_mqtt_publish(MQTT_TOPIC, (uint8_t *)payload, strlen(payload));
    if (ret == 0)
        LOG_I(TAG, "D(cfg) temp_th=%d.%d pm25_th=%d.%d mq2_th=%d flame_th=%d pm1=%d pm10=%d lux=%d",
              (int)temp_threshold, abs((int)((temp_threshold - (int)temp_threshold) * 10)),
              (int)pm25_threshold, abs((int)((pm25_threshold - (int)pm25_threshold) * 10)),
              mq2_threshold, flame_threshold, pm1_0, pm10, (int)light);
    else
        LOG_W(TAG, "D(cfg) FAIL ret=%d", ret);
}

/* ==================== HTTP请求结构体 ==================== */
/*
 * http_request:
 *   封装异步 HTTP 请求的上下文。
 *   主 acceptor 线程接收连接后，将此结构体传递给新创建的 worker 线程，
 *   主线程立即返回继续 accept，不会阻塞。
 */
struct http_request {
    int client_fd;
};

/* ==================== HTTP请求处理器（异步线程） ==================== */
/*
 * http_handler_thread:
 *   每个 HTTP 请求由独立线程处理，体现 OS 的异步事件处理能力。
 *   主 acceptor 线程不阻塞，可同时处理多个并发 HTTP 请求。
 */
static void http_handler_thread(void *parameter)
{
    struct http_request *req = (struct http_request *)parameter;
    int client_fd = req->client_fd;
    rt_free(req);

    char buffer[256];
    int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) { close(client_fd); return; }
    buffer[bytes_read] = '\0';

    if (strstr(buffer, "GET /api/data") != NULL)
    {
        /*
         * GET /api/data — 全量传感器物理量接口（uni-app/Android 图表数据源）
         *
         * 返回 JSON：temperature, humidity, light, mq2, flame, pm2_5, alarm_type
         * 浮点数通过 %d.%d 模式格式化为 1 位小数，兼容嵌入式 snprintf。
         *
         * 数据安全：依次获取 data_mutex(shared_data) 和 data_lock(g_cc2530_data)，
         * 避免嵌套持锁，防止与传感器/ZigBee 线程数据竞争。
         */
        float temperature, humidity, light;
        uint16_t mq2, flame, pm2_5;
        uint8_t mq2_valid, flame_valid, pm_valid;
        uint8_t alarm_type;

        rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
        temperature = shared_data.temperature;
        humidity    = shared_data.humidity;
        light       = shared_data.light;
        rt_mutex_release(data_mutex);

        rt_mutex_take(data_lock, RT_WAITING_FOREVER);
        mq2_valid   = g_cc2530_data.env.valid;
        mq2         = mq2_valid ? g_cc2530_data.env.mq2 : 0;
        flame_valid = g_cc2530_data.env.valid;
        flame       = flame_valid ? g_cc2530_data.env.flame : 0;
        pm_valid    = g_cc2530_data.pm.valid;
        pm2_5       = pm_valid ? g_cc2530_data.pm.pm2_5 : 0;
        alarm_type  = g_app_state.alarm_type;
        rt_mutex_release(data_lock);

        char json[256];
        rt_memset(json, 0, sizeof(json));

        int t_int = (int)temperature;
        int t_dec = abs((int)((temperature - t_int) * 10 + 0.5f));
        int h_int = (int)humidity;
        int h_dec = abs((int)((humidity - h_int) * 10 + 0.5f));
        int l_int = (int)light;

        int len = rt_snprintf(json, sizeof(json),
            "{\"temperature\":%d.%d,\"humidity\":%d.%d,\"light\":%d,"
            "\"mq2\":%u,\"flame\":%u,\"pm2_5\":%u,\"alarm_type\":%u}",
            t_int, t_dec, h_int, h_dec, l_int,
            mq2, flame, pm2_5, alarm_type);

        if (len >= (int)sizeof(json))
        {
            LOG_W(TAG, "/api/data JSON truncated");
            const char *e = "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            send(client_fd, e, strlen(e), 0);
        }
        else
        {
            char header[64];
            rt_snprintf(header, sizeof(header), http_header_200, len);
            send(client_fd, header, strlen(header), 0);
            send(client_fd, json, len, 0);
        }
    }
    else if (strstr(buffer, "GET /get_status") != NULL)
    {
        /*
         * GET /get_status — 系统全量状态接口
         *
         * 返回 g_app_state 全部字段 + 关键传感器数据，
         * 字段名、类型与 app_logic.h 中 struct app_state 严格对齐。
         *
         * 数据安全：依次获取 data_mutex(shared_data) 和 data_lock(g_app_state)，
         * 避免嵌套持锁造成死锁。512 字节缓冲区经计算足够容纳所有字段。
         */
        float temperature, humidity, light;
        int fan_status, servo_angle;
        float temp_threshold, pm25_threshold;
        uint16_t mq2_threshold, flame_threshold;
        uint8_t beep_status, alarm_type;
        int alarm_state;

        rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
        temperature = shared_data.temperature;
        humidity    = shared_data.humidity;
        light       = shared_data.light;
        fan_status  = shared_data.fan_status;
        servo_angle = shared_data.servo_angle;
        rt_mutex_release(data_mutex);

        rt_mutex_take(data_lock, RT_WAITING_FOREVER);
        alarm_state     = g_app_state.alarm_state;
        alarm_type      = g_app_state.alarm_type;
        beep_status     = g_app_state.beep_status;
        temp_threshold  = g_app_state.temp_threshold;
        pm25_threshold  = g_app_state.pm25_threshold;
        mq2_threshold   = g_app_state.mq2_threshold;
        flame_threshold = g_app_state.flame_threshold;
        rt_mutex_release(data_lock);

        char json[512];
        rt_memset(json, 0, sizeof(json));

        int t_int = (int)temperature;
        int t_dec = abs((int)((temperature - t_int) * 10 + 0.5f));
        int h_int = (int)humidity;
        int h_dec = abs((int)((humidity - h_int) * 10 + 0.5f));
        int l_int = (int)light;
        int th_int = (int)temp_threshold;
        int th_dec = abs((int)((temp_threshold - th_int) * 10 + 0.5f));
        int pm_int = (int)pm25_threshold;
        int pm_dec = abs((int)((pm25_threshold - pm_int) * 10 + 0.5f));

        int len = rt_snprintf(json, sizeof(json),
            "{\"alarm_state\":%d,\"alarm_type\":%u,"
            "\"beep\":%u,\"fan_status\":%d,\"work_mode\":%d,"
            "\"servo_angle\":%d,"
            "\"temperature\":%d.%d,\"humidity\":%d.%d,\"light\":%d,"
            "\"temp_threshold\":%d.%d,\"pm25_threshold\":%d.%d,"
            "\"mq2_threshold\":%u,\"flame_threshold\":%u}",
            alarm_state, alarm_type,
            beep_status, fan_status, 1,
            servo_angle,
            t_int, t_dec, h_int, h_dec, l_int,
            th_int, th_dec, pm_int, pm_dec,
            mq2_threshold, flame_threshold);

        if (len >= (int)sizeof(json))
        {
            LOG_W(TAG, "/get_status JSON truncated");
            const char *e = "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            send(client_fd, e, strlen(e), 0);
        }
        else
        {
            char header[64];
            rt_snprintf(header, sizeof(header), http_header_200, len);
            send(client_fd, header, strlen(header), 0);
            send(client_fd, json, len, 0);
        }
    }
    else if (strstr(buffer, "GET /api/set") != NULL)
    {
        const char *type_param = strstr(buffer, "type=");
        const char *val_param = strstr(buffer, "val=");

        if (type_param != NULL && val_param != NULL)
        {
            type_param += 5;
            val_param += 4;

            char type_buf[16] = {0};
            char val_buf[32] = {0};
            int i = 0;
            while (*type_param != '&' && *type_param != ' ' && *type_param != '\0' && i < 15)
                type_buf[i++] = *type_param++;
            i = 0;
            while (*val_param != '&' && *val_param != ' ' && *val_param != '\0' && i < 31)
                val_buf[i++] = *val_param++;

            if (strcmp(type_buf, "threshold") == 0)
            {
                /*
                 * [安全策略说明]
                 *
                 * 【HTTP 本地控制】阈值有效范围：20.0°C ~ 80.0°C
                 *   适用范围：运维人员通过 HTTP 直连网关进行本地调试/应急操作。
                 *   上限 80°C 的原因：
                 *     - 充电棚在夏季暴晒下内部环境温度可达 60-70°C，
                 *       若阈值上限卡死在 50°C 会导致持续误报，系统不可用。
                 *     - 本地运维有物理访问权限，误操作风险可控。
                 *
                 * 【OneNET 云端下行】阈值有效范围：20.0°C ~ 50.0°C
                 *   适用范围：远程管理人员通过 OneNET 云平台下发指令。
                 *   上限 50°C 的原因：
                 *     - 云端通道经过公网，需防范远程攻击者通过 OneNET API
                 *       恶意拉高阈值使告警失效。
                 *     - 50°C 是消防行业通用的环境温度告警起点，
                 *       超过此值应触发人工现场确认，而非远程调高阈值。
                 *   实现位置：onenet_cmd_rsp_cb() 中 net_threshold_val 判断。
                 *
                 *   两端策略差异化是刻意设计，非 Bug。
                 */
                float new_threshold = atof(val_buf);
                if (new_threshold >= 20.0f && new_threshold <= 80.0f)
                {
                    rt_mutex_take(data_lock, RT_WAITING_FOREVER);
                    g_app_state.temp_threshold = new_threshold;
                    rt_mutex_release(data_lock);

                    update_threshold_display();
                    publish_temp_threshold();

                    int _th_i = (int)new_threshold;
                    int _th_dec = abs((int)((new_threshold - _th_i) * 10));
                    char resp[64];
                    rt_snprintf(resp, sizeof(resp),
                                "{\"status\":\"ok\",\"type\":\"threshold\",\"value\":%d.%d}",
                                _th_i, _th_dec);
                    char header[64];
                    rt_snprintf(header, sizeof(header), http_header_200, strlen(resp));
                    send(client_fd, header, strlen(header), 0);
                    send(client_fd, resp, strlen(resp), 0);
                }
            }
            else if (strcmp(type_buf, "smoke") == 0)
            {
                int val = atoi(val_buf);
                if (val >= 100 && val <= 4000)
                {
                    rt_mutex_take(data_lock, RT_WAITING_FOREVER);
                    g_app_state.mq2_threshold = (uint16_t)val;
                    rt_mutex_release(data_lock);
                    LOG_I(TAG, "/api/set smoke -> %d", val);

                    char resp[64];
                    rt_snprintf(resp, sizeof(resp),
                                "{\"status\":\"ok\",\"type\":\"smoke\",\"value\":%d}", val);
                    char header[64];
                    rt_snprintf(header, sizeof(header), http_header_200, strlen(resp));
                    send(client_fd, header, strlen(header), 0);
                    send(client_fd, resp, strlen(resp), 0);
                }
            }
            else if (strcmp(type_buf, "flame") == 0)
            {
                int val = atoi(val_buf);
                if (val >= 50 && val <= 4000)
                {
                    rt_mutex_take(data_lock, RT_WAITING_FOREVER);
                    g_app_state.flame_threshold = (uint16_t)val;
                    rt_mutex_release(data_lock);
                    LOG_I(TAG, "/api/set flame -> %d", val);

                    char resp[64];
                    rt_snprintf(resp, sizeof(resp),
                                "{\"status\":\"ok\",\"type\":\"flame\",\"value\":%d}", val);
                    char header[64];
                    rt_snprintf(header, sizeof(header), http_header_200, strlen(resp));
                    send(client_fd, header, strlen(header), 0);
                    send(client_fd, resp, strlen(resp), 0);
                }
            }
            else if (strcmp(type_buf, "pm25") == 0)
            {
                float val = atof(val_buf);
                if (val >= 10.0f && val <= 500.0f)
                {
                    rt_mutex_take(data_lock, RT_WAITING_FOREVER);
                    g_app_state.pm25_threshold = val;
                    rt_mutex_release(data_lock);
                    LOG_I(TAG, "/api/set pm25 -> %d", (int)val);

                    char resp[64];
                    rt_snprintf(resp, sizeof(resp),
                                "{\"status\":\"ok\",\"type\":\"pm25\",\"value\":%d}", (int)val);
                    char header[64];
                    rt_snprintf(header, sizeof(header), http_header_200, strlen(resp));
                    send(client_fd, header, strlen(header), 0);
                    send(client_fd, resp, strlen(resp), 0);
                }
            }
            else if (strcmp(type_buf, "beep") == 0)
            {
                int new_beep = atoi(val_buf);
                if (new_beep == 0 || new_beep == 1)
                {
                    beep_set((uint8_t)new_beep);
                    char resp[64];
                    rt_snprintf(resp, sizeof(resp),
                                "{\"status\":\"ok\",\"type\":\"beep\",\"value\":%u}",
                                new_beep);
                    char header[64];
                    rt_snprintf(header, sizeof(header), http_header_200, strlen(resp));
                    send(client_fd, header, strlen(header), 0);
                    send(client_fd, resp, strlen(resp), 0);
                    LOG_I(TAG, "/api/set beep -> %s", new_beep ? "ON" : "OFF");
                }
                else
                {
                    const char *resp = "{\"status\":\"error\",\"msg\":\"invalid beep value (0 or 1)\"}";
                    char header[64];
                    rt_snprintf(header, sizeof(header), http_header_200, strlen(resp));
                    send(client_fd, header, strlen(header), 0);
                    send(client_fd, resp, strlen(resp), 0);
                }
            }
            else if (strcmp(type_buf, "fan") == 0)
            {
                int new_fan = atoi(val_buf);
                if (new_fan == 0 || new_fan == 1)
                {
                    fan_set((uint8_t)new_fan);
                    char resp[64];
                    rt_snprintf(resp, sizeof(resp),
                                "{\"status\":\"ok\",\"type\":\"fan\",\"value\":%u}",
                                new_fan);
                    char header[64];
                    rt_snprintf(header, sizeof(header), http_header_200, strlen(resp));
                    send(client_fd, header, strlen(header), 0);
                    send(client_fd, resp, strlen(resp), 0);
                    LOG_I(TAG, "/api/set fan -> %s", new_fan ? "ON" : "OFF");
                }
                else
                {
                    const char *resp = "{\"status\":\"error\",\"msg\":\"invalid fan value (0 or 1)\"}";
                    char header[64];
                    rt_snprintf(header, sizeof(header), http_header_200, strlen(resp));
                    send(client_fd, header, strlen(header), 0);
                    send(client_fd, resp, strlen(resp), 0);
                }
            }
            else if (strcmp(type_buf, "servo") == 0)
            {
                int new_angle = atoi(val_buf);
                if (new_angle >= 0 && new_angle <= 90)
                {
                    servo_set_angle(new_angle);
                    char resp[64];
                    rt_snprintf(resp, sizeof(resp),
                                "{\"status\":\"ok\",\"type\":\"servo\",\"value\":%d}",
                                new_angle);
                    char header[64];
                    rt_snprintf(header, sizeof(header), http_header_200, strlen(resp));
                    send(client_fd, header, strlen(header), 0);
                    send(client_fd, resp, strlen(resp), 0);
                    LOG_I(TAG, "/api/set servo -> angle %d", new_angle);
                }
                else
                {
                    const char *resp = "{\"status\":\"error\",\"msg\":\"invalid servo angle (0-90)\"}";
                    char header[64];
                    rt_snprintf(header, sizeof(header), http_header_200, strlen(resp));
                    send(client_fd, header, strlen(header), 0);
                    send(client_fd, resp, strlen(resp), 0);
                }
            }
            else
            {
                const char *resp = "{\"status\":\"error\",\"msg\":\"unknown type\"}";
                char header[64];
                rt_snprintf(header, sizeof(header), http_header_200, strlen(resp));
                send(client_fd, header, strlen(header), 0);
                send(client_fd, resp, strlen(resp), 0);
            }
        }
        else
        {
            const char *resp = "{\"status\":\"error\",\"msg\":\"missing parameters\"}";
            char header[64];
            rt_snprintf(header, sizeof(header), http_header_200, strlen(resp));
            send(client_fd, header, strlen(header), 0);
            send(client_fd, resp, strlen(resp), 0);
        }
    }
    else
    {
        const char *not_found = "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, not_found, strlen(not_found), 0);
    }
    close(client_fd);
}

/* ==================== Web Server 线程 ==================== */
/*
 * web_server_thread_entry:
 *   主 acceptor 线程 — 等待 net_ready_sem 后启动监听。
 *
 *   异步处理模式：
 *     每次 accept 新连接后，创建独立 worker 线程处理请求，
 *     主线程立即返回 accept 下一个连接，不被阻塞。
 *     这体现了 RTOS 处理并发异步事件的核心能力。
 */
static void web_server_thread_entry(void *parameter)
{
    /*
     * 等待信号量 — 阻塞直到 WiFi 获取 IP。
     * rt_sem_take 使本线程挂起，不消耗 CPU，内核在信号量可用时自动唤醒。
     */
    rt_sem_take(net_ready_sem, RT_WAITING_FOREVER);
    LOG_I(TAG, "HTTP server starting...");

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { LOG_E(TAG, "socket failed"); return; }
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(HTTP_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    { LOG_E(TAG, "bind failed"); close(server_fd); return; }
    if (listen(server_fd, 5) < 0)
    { LOG_E(TAG, "listen failed"); close(server_fd); return; }

    LOG_I(TAG, "HTTP Server started on port %d (async mode)", HTTP_PORT);

    while (1)
    {
        client_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd >= 0)
        {
            /*
             * 异步处理：为每个 HTTP 请求创建独立线程。
             * 主 acceptor 线程不阻塞，立即返回继续 accept。
             * 这确保了多个并发请求互不干扰。
             */
            struct http_request *req = rt_malloc(sizeof(struct http_request));
            if (req)
            {
                req->client_fd = client_fd;
                rt_thread_t tid = rt_thread_create("http",
                                                     http_handler_thread,
                                                     req,
                                                     4096,
                                                     23,
                                                     10);
                if (tid)
                {
                    rt_thread_startup(tid);
                }
                else
                {
                    rt_free(req);
                    close(client_fd);
                }
            }
            else
            {
                close(client_fd);
            }
        }
        rt_thread_mdelay(10);
    }
}

static void onenet_cmd_rsp_cb(uint8_t *recv_data, size_t recv_size,
                               uint8_t **resp_data, size_t *resp_size);

/* ==================== OneNET上传线程 ==================== */
/*
 * onenet_upload_thread_entry:
 *   等待 EVENT_WIFI_OK 事件 → 初始化 MQTT → 连接成功 → 发送 EVENT_MQTT_OK → 进入上传循环。
 *
 *   事件集同步关系：
 *     WiFi 线程（生产者）发送 EVENT_WIFI_OK → 本线程（消费者）等待后初始化 MQTT
 *     本线程（生产者）发送 EVENT_MQTT_OK → 传感器线程（消费者）等待后开始采集
 *
 *   命令处理：
 *     主循环中异步消费 net_xxx 标志位（由 ISR 安全的 onenet_cmd_rsp_cb 生产），
 *     使用 data_lock 互斥量保护 g_app_state 的写入。
 */
static void onenet_upload_thread_entry(void *parameter)
{
    rt_uint32_t events;

    LOG_I(TAG, "OneNET upload thread waiting for EVENT_WIFI_OK...");
    rt_event_recv(&sys_event, EVENT_WIFI_OK,
                  RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                  RT_WAITING_FOREVER, &events);
    LOG_I(TAG, "EVENT_WIFI_OK received, starting MQTT init...");

    int ret = onenet_mqtt_init();
    if (ret != 0)
    {
        LOG_E(TAG, "MQTT init failed: %d, waking sensor thread before exit", ret);
        rt_event_send(&sys_event, EVENT_MQTT_OK);
        return;
    }

    onenet_set_cmd_rsp_cb(onenet_cmd_rsp_cb);
    LOG_I(TAG, "MQTT init OK, waiting for PAHO connection...");

    {
        char payload[PAYLOAD_BUF_SIZE];
        int conn_wait = 0;
        while (conn_wait < 30)
        {
            rt_memset(payload, 0, sizeof(payload));
            int len = rt_snprintf(payload, sizeof(payload),
                "{\"id\":\"hb%d\",\"version\":\"1.0\",\"params\":{},\"method\":\"thing.property.post\"}",
                conn_wait);

            if (len < (int)sizeof(payload))
            {
                int pub_ret = onenet_mqtt_publish(MQTT_TOPIC, (uint8_t *)payload, strlen(payload));
                if (pub_ret == 0)
                {
                    LOG_I(TAG, "PAHO connected (#%d), entering upload loop", conn_wait);
                    break;
                }
            }
            conn_wait++;
            rt_thread_mdelay(1000);
        }
        if (conn_wait >= 30)
        {
            LOG_E(TAG, "PAHO connect timeout! waking sensor, exiting upload thread");
            rt_event_send(&sys_event, EVENT_MQTT_OK);
            return;
        }
    }

    rt_event_send(&sys_event, EVENT_MQTT_OK);
    LOG_I(TAG, "EVENT_MQTT_OK sent, upload loop starting");

    struct sensor_data data;
    rt_tick_t last_env_upload_time = 0;
    rt_tick_t last_fire_upload_time = 0;
    rt_tick_t last_config_upload_time = 0;
    rt_tick_t last_status_upload_time = 0;
    rt_tick_t last_heartbeat_time = 0;
    rt_tick_t current_time;

    int publish_fail_count = 0;

    while (1)
    {
        watchdog_feed();

        if (net_led_pending)
        {
            net_led_pending = 0;
            rt_pin_write(LED_R_PIN, net_led_on ? PIN_LOW : PIN_HIGH);
            LOG_I(TAG, "Deferred cmd: LED %s", net_led_on ? "ON" : "OFF");
        }

        if (net_beep_pending)
        {
            net_beep_pending = 0;
            uint8_t on = net_beep_on;
            rt_mutex_take(data_lock, RT_WAITING_FOREVER);
            g_app_state.beep_status = on;
            rt_pin_write(BEEP_PIN, on ? PIN_HIGH : PIN_LOW);
            rt_mutex_release(data_lock);
            LOG_I(TAG, "Deferred cmd: beep %s", on ? "ON" : "OFF");
        }

        if (net_fan_pending)
        {
            net_fan_pending = 0;
            fan_set(net_fan_on);
            LOG_I(TAG, "Deferred cmd: fan %s", net_fan_on ? "ON" : "OFF");
        }

        if (net_servo_pending)
        {
            net_servo_pending = 0;
            rt_mutex_take(data_lock, RT_WAITING_FOREVER);
            servo_set_angle(net_servo_angle);
            rt_mutex_release(data_lock);
            LOG_I(TAG, "Deferred cmd: servo angle %d", net_servo_angle);
        }

        if (net_threshold_pending)
        {
            net_threshold_pending = 0;
            float new_val = net_threshold_val;
            if (new_val >= 20.0f && new_val <= 50.0f)
            {
                rt_mutex_take(data_lock, RT_WAITING_FOREVER);
                g_app_state.temp_threshold = new_val;
                rt_mutex_release(data_lock);
                update_threshold_display();
                publish_temp_threshold();
                {
                    int _vi = (int)new_val;
                    int _vd = abs((int)((new_val - _vi) * 10));
                    LOG_I(TAG, "Deferred cmd: threshold=%d.%d", _vi, _vd);
                }
            }
        }

        current_time = rt_tick_get();

        if (!wifi_connected)
        {
            rt_thread_mdelay(2000);
            continue;
        }

        if (current_time - last_heartbeat_time >= 30000)
        {
            char payload[PAYLOAD_BUF_SIZE];
            rt_memset(payload, 0, sizeof(payload));
            int len = rt_snprintf(payload, sizeof(payload),
                "{\"id\":\"hb\",\"version\":\"1.0\",\"params\":{},\"method\":\"thing.property.post\"}");

            if (len < (int)sizeof(payload))
            {
                int mqtt_result = onenet_mqtt_publish(MQTT_TOPIC, (uint8_t *)payload, strlen(payload));

                if (mqtt_result != 0)
                {
                    publish_fail_count++;
                    LOG_W(TAG, "MQTT publish fail #%d", publish_fail_count);

                    if (publish_fail_count >= 3)
                    {
                        LOG_W(TAG, "3 consecutive publish failures, triggering MQTT reconnect...");
                        onenet_mqtt_init();
                        onenet_set_cmd_rsp_cb(onenet_cmd_rsp_cb);
                        rt_thread_mdelay(2000);

                        char repayload[PAYLOAD_BUF_SIZE];
                        rt_memset(repayload, 0, sizeof(repayload));
                        int relen = rt_snprintf(repayload, sizeof(repayload),
                            "{\"id\":\"reconnect\",\"version\":\"1.0\",\"params\":{},\"method\":\"thing.property.post\"}");
                        if (relen < (int)sizeof(repayload))
                        {
                            onenet_mqtt_publish(MQTT_TOPIC, (uint8_t *)repayload, strlen(repayload));
                        }
                        publish_fail_count = 0;
                    }
                }
                else
                {
                    publish_fail_count = 0;
                }
            }
            last_heartbeat_time = current_time;
        }

        rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
        data = shared_data;
        rt_mutex_release(data_mutex);

        if (current_time - last_fire_upload_time >= 5000)
        {
            post_block_fire_safety(&data);
            last_fire_upload_time = current_time;
            rt_thread_mdelay(2000);
            continue;
        }

        if (current_time - last_env_upload_time >= 10000)
        {
            post_block_environment(&data);
            last_env_upload_time = current_time;
            rt_thread_mdelay(2000);
            continue;
        }

        if (current_time - last_status_upload_time >= 20000)
        {
            post_block_status(&data);
            last_status_upload_time = current_time;
            rt_thread_mdelay(2000);
            continue;
        }

        if (current_time - last_config_upload_time >= 30000)
        {
            post_block_config();
            last_config_upload_time = current_time;
            rt_thread_mdelay(2000);
            continue;
        }

        rt_thread_mdelay(1000);
    }
}

/* ==================== OneNET下行控制回调 ==================== */
/*
 * onenet_cmd_rsp_cb:
 *   处理 OneNET 云端下发的控制命令（LED、BEEP、阈值）。
 *   ⚠ 此回调由 paho_mqtt 内部线程(mqtt0)上下文调用。
 *     禁止调用 rt_mutex_take / rt_sem_take 等阻塞 API，
 *     仅通过 volatile 标志位异步通知上传线程。
 *   ⚠ 必须验证 recv_data 有效性和长度，防止PAHO传入无效指针。
 */
static void onenet_cmd_rsp_cb(uint8_t *recv_data, size_t recv_size,
                               uint8_t **resp_data, size_t *resp_size)
{
    int request_id = 0;

    if (!recv_data || !recv_size || !resp_data || !resp_size)
    {
        if (resp_data) *resp_data = RT_NULL;
        if (resp_size) *resp_size = 0;
        return;
    }

    {
        const char *data_str = (const char *)recv_data;
        size_t scan_limit = recv_size < 512 ? recv_size : 512;
        const char *scan_end = data_str + scan_limit;

        const char *id_start = data_str;
        while (id_start < scan_end - 5 && strncmp(id_start, "\"id\":", 5) != 0)
            id_start++;
        if (id_start < scan_end - 5)
        {
            id_start += 5;
            while (id_start < scan_end && (*id_start == ' ' || *id_start == '\"'))
                id_start++;
            if (id_start < scan_end)
                request_id = atoi(id_start);
        }

        /* LED 控制 — 纯 volatile 写 */
        {
            const char *p = data_str;
            while (p < scan_end - 6 && strncmp(p, "\"led\":", 6) != 0)
                p++;
            if (p < scan_end - 6)
            {
                p += 6;
                while (p < scan_end && *p == ' ') p++;
                if (p < scan_end)
                {
                    net_led_on = (*p == 't' || *p == 'T' || *p == '1') ? 1 : 0;
                    net_led_pending = 1;
                    LOG_I(TAG, "OneNET cmd: LED %s (deferred)", net_led_on ? "ON" : "OFF");
                }
            }
        }

        /* BEEP 控制 — 纯 volatile 写 */
        {
            const char *p = data_str;
            while (p < scan_end - 7 && strncmp(p, "\"beep\":", 7) != 0)
                p++;
            if (p < scan_end - 7)
            {
                p += 7;
                while (p < scan_end && *p == ' ') p++;
                if (p < scan_end)
                {
                    net_beep_on = (*p == 't' || *p == 'T' || *p == '1') ? 1 : 0;
                    net_beep_pending = 1;
                    LOG_I(TAG, "OneNET cmd: beep %s (deferred)", net_beep_on ? "ON" : "OFF");
                }
            }
        }

        /* fan_en 控制 — 纯 volatile 写 */
        {
            const char *p = data_str;
            while (p < scan_end - 9 && strncmp(p, "\"fan_en\":", 9) != 0)
                p++;
            if (p < scan_end - 9)
            {
                p += 9;
                while (p < scan_end && *p == ' ') p++;
                if (p < scan_end)
                {
                    net_fan_on = (*p == 't' || *p == 'T' || *p == '1') ? 1 : 0;
                    net_fan_pending = 1;
                    LOG_I(TAG, "OneNET cmd: fan_en %s (deferred)", net_fan_on ? "ON" : "OFF");
                }
            }
        }

        /* 舵机控制 — 纯 volatile 写 */
        {
            const char *p = data_str;
            while (p < scan_end - 17 && strncmp(p, "\"steeringstatus\":", 17) != 0)
                p++;
            if (p < scan_end - 17)
            {
                p += 17;
                while (p < scan_end && *p == ' ') p++;
                if (p < scan_end)
                {
                    int angle = atoi(p);
                    if (angle >= 0 && angle <= 90)
                    {
                        net_servo_angle = angle;
                        net_servo_pending = 1;
                        LOG_I(TAG, "OneNET cmd: servo angle %d (deferred)", angle);
                    }
                }
            }
        }

        /* 阈值控制 — 纯 volatile 写 */
        {
            const char *p = data_str;
            while (p < scan_end - 17 && strncmp(p, "\"temp_threshold\":", 17) != 0)
                p++;
            if (p < scan_end - 17)
            {
                p += 17;
                while (p < scan_end && *p == ' ') p++;
                if (p < scan_end)
                {
                    float new_threshold = atof(p);
                    if (new_threshold >= 20.0f && new_threshold <= 50.0f)
                    {
                        net_threshold_val = new_threshold;
                        net_threshold_pending = 1;
                        {
                            int _ti = (int)new_threshold;
                            int _td = abs((int)((new_threshold - _ti) * 10));
                            LOG_I(TAG, "OneNET cmd: threshold=%d.%d (deferred)", _ti, _td);
                        }
                    }
                }
            }
        }
    }

    {
        char *cmd_resp_buf = rt_malloc(80);
        if (cmd_resp_buf == RT_NULL)
        {
            *resp_data = RT_NULL;
            *resp_size = 0;
            return;
        }
        rt_snprintf(cmd_resp_buf, 80,
                    "{\"id\":\"%d\",\"code\":200,\"msg\":\"success\"}", request_id);
        *resp_data = (uint8_t *)cmd_resp_buf;
        *resp_size = strlen(cmd_resp_buf);
    }
}

/* ==================== WiFi事件回调 ==================== */
static void wlan_event_callback(int event, struct rt_wlan_buff *buff, void *parameter)
{
    switch (event)
    {
        case RT_WLAN_EVT_STA_CONNECTED:
            LOG_I(TAG, "WiFi connected to AP");
            break;
        case RT_WLAN_EVT_STA_DISCONNECTED:
            LOG_W(TAG, "WiFi disconnected, reconnecting...");
            wifi_connected = 0;
            rt_wlan_connect(WLAN_SSID, WLAN_PASSWORD);
            break;
        default:
            break;
    }
}

/* ==================== WiFi连接 ==================== */
/*
 * wifi_connect:
 *   连接 WiFi 并通知所有等待的消费线程。
 *
 *   同步关系：
 *     生产者：本函数在 WiFi 获取 IP 后释放信号量(HTTP Server) + 发送事件(OneNET/Sensor)。
 *     消费者：HTTP Server 线程阻塞等待信号量，OneNET/Sensor 线程阻塞等待事件。
 */
int wifi_connect(void)
{
    LOG_I(TAG, "Connecting to WiFi: %s", WLAN_SSID);

    /*
     * RW007 硬件复位：
     *   拉低 RESET 引脚 200ms → 拉高，确保模块上电稳定。
     *   rt_thread_mdelay 使用系统延时，不阻塞其他线程。
     */
    rt_pin_mode(RW007_RST_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(RW007_RST_PIN, PIN_LOW);
    rt_thread_mdelay(200);
    rt_pin_write(RW007_RST_PIN, PIN_HIGH);
    LOG_I(TAG, "RW007 reset done, waiting 3s for stabilization...");
    rt_thread_mdelay(3000);

    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED, wlan_event_callback, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_DISCONNECTED, wlan_event_callback, RT_NULL);

    rt_wlan_connect(WLAN_SSID, WLAN_PASSWORD);

    for (int i = 0; i < 60; i++)
    {
        if (rt_wlan_is_connected())
        {
            LOG_I(TAG, "WiFi connected!");
            wifi_connected = 1;

            /*
             * 释放信号量 — 唤醒 HTTP Server 线程。
             */
            rt_sem_release(net_ready_sem);

            /*
             * 发送 EVENT_WIFI_OK — 唤醒 OneNET 上传线程。
             * 事件集中等待的系统通知方式，确保各线程按正确时序启动。
             */
            rt_event_send(&sys_event, EVENT_WIFI_OK);
            LOG_I(TAG, "EVENT_WIFI_OK sent");

            return 0;
        }
        rt_thread_mdelay(500);
    }

    LOG_E(TAG, "WiFi connect timeout!");
    return -1;
}

/* ==================== 模块初始化 ==================== */
/*
 * app_net_init:
 *   创建同步对象（信号量、事件集）并启动 Web Server 线程。
 *   HTTP Server 线程启动后立即阻塞等待 net_ready_sem，WiFi 连接成功后自动唤醒。
 *   sys_event 用于 OneNET 和传感器线程之间的启动时序控制。
 */
void app_net_init(void)
{
    /* 创建信号量 — 初始值 0，WiFi 连接成功后 release */
    net_ready_sem = rt_sem_create("net_ready", 0, RT_IPC_FLAG_FIFO);
    if (net_ready_sem == RT_NULL)
    {
        LOG_E(TAG, "semaphore create failed!");
        return;
    }

    /* 初始化事件集 — 控制网络/MQTT/传感器启动时序 */
    rt_event_init(&sys_event, "sys_event", RT_IPC_FLAG_FIFO);

    LOG_I(TAG, "net_ready_sem created (value=0), sys_event init OK");

    /* 启动 Web Server 线程 */
    rt_thread_t tid_web = rt_thread_create("web", web_server_thread_entry, RT_NULL,
                                           3072, 18, 10);
    if (tid_web)
        rt_thread_startup(tid_web);
}

/* ==================== OneNET初始化（由main在WiFi连接后调用）==================== */
/*
 * app_net_onenet_start:
 *   仅启动 OneNET 上传线程。MQTT 初始化、回调注册由线程自身完成。
 *   线程启动后先阻塞等待 EVENT_WIFI_OK 事件（WiFi 就绪），然后执行 init。
 *   这确保了 onenet_mqtt_init() 在正确的线程上下文（而非 main 线程）中运行。
 */
void app_net_onenet_start(void)
{
    /*
     * 启动 OneNET 上传线程。
     * 线程内部先阻塞等待 EVENT_WIFI_OK 事件集，
     * 然后初始化 MQTT、注册回调、进入上传循环。
     * 优先级 15 — 高于传感器(22)和HTTP worker(23)，
     * 确保下行命令及时消费，同时不抢占逻辑处理(8)。
     */
    rt_thread_t onenet_thread = rt_thread_create("onenet",
                                                   onenet_upload_thread_entry,
                                                   RT_NULL,
                                                   4096,
                                                   15,
                                                   10);
    if (onenet_thread)
    {
        rt_thread_startup(onenet_thread);
        LOG_I(TAG, "OneNET upload thread started (MQTT init deferred)");
    }
}
