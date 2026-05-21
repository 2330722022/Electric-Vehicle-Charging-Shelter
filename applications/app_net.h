#ifndef APP_NET_H__
#define APP_NET_H__

#include <rtthread.h>

/*
 * net_ready_sem:
 *   信号量 — WiFi 获取 IP 后释放，通知 HTTP 服务启动。
 *   初始化值 = 0，WiFi 连接成功后 release，消费者 wait 获取。
 */
extern rt_sem_t net_ready_sem;

/*
 * sys_event:
 *   全局事件集 — 控制系统启动时序，确保网络/MQTT 就绪后才开始业务。
 *   标志位:
 *     EVENT_WIFI_OK  — WiFi 获取 IP 后发送
 *     EVENT_MQTT_OK  — MQTT 初始化并连接成功后发送
 */
#define EVENT_WIFI_OK       (1 << 0)
#define EVENT_MQTT_OK       (1 << 1)

extern struct rt_event sys_event;

/* WiFi连接状态 */
extern int wifi_connected;

/* 模块初始化 */
void app_net_init(void);

/* WiFi连接 */
int wifi_connect(void);

/* OneNET初始化（由main在WiFi连接后调用）*/
void app_net_onenet_start(void);

/* 立即上报温度阈值到 OneNET（按键/下行触发） */
rt_err_t publish_temp_threshold(void);

#endif
