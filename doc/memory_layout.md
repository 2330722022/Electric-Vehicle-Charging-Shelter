# 电动车充电棚环境监测系统 — 内存布局与优化文档

> 硬件平台：星火一号 (STM32F407ZG)  
> 项目状态：v2.0 充电棚专版 (2026-05-19)  
> 用途：为后续功能升级（摄像头、TTS 语音、MQTT over TLS 等）提供内存规划参考

---

## 一、星火一号硬件平台概要

| 项目 | 规格 |
|------|------|
| **主控芯片** | STM32F407ZGT6 |
| **内核** | ARM Cortex-M4F，带 FPU，168MHz |
| **Flash** | 1024 KB (`0x08000000` ~ `0x080FFFFF`) |
| **SRAM1 (通用)** | 128 KB (`0x20000000` ~ `0x2001FFFF`) |
| **SRAM2 (CCMRAM)** | 64 KB (`0x10000000` ~ `0x1000FFFF`) |
| **外部 FSMC LCD RAM** | 1024 KB (`0x68000000` ~ `0x680FFFFF`) |
| **OS** | RT-Thread v4.1.0+, 抢占式调度 |

### 关键内存特性

- **CCMRAM (Core Coupled Memory)**：直连 CPU D-Bus，零等待访问，但不连接 DMA 总线。适合存放 CPU 密集访问的数据（堆栈、显示缓冲），不可用于 DMA 传输。
- **FSMC 外部 RAM**：通过 FSMC 总线映射，专用于 LCD 显存（ILI9341/ST7789 的 framebuffer），不占用内部 SRAM。

---

## 二、链接脚本内存布局 (link.lds)

```
MEMORY
{
    CODE   (rx): 0x08000000, 1024k    ← Flash (固件/常量)
    RAM1   (rw): 0x20000000,  128k    ← 主 SRAM (.data + .bss + RT-Thread 堆)
    RAM2   (rw): 0x10000000,   64k    ← CCMRAM (.stack + .ccmram LVGL 缓冲区)
    LCDRAM (rw): 0x68000000, 1024k    ← FSMC 外部 LCD 显存
}

SECTIONS
{
    .text         → CODE (Flash)
    .ARM.exidx    → CODE (Flash)
    .data         → RAM1  (已初始化全局变量)
    .bss          → RAM1  (未初始化/零初始化全局变量 + RT-Thread 堆)
    .stack        → RAM2  (1 KB 系统栈，已从 RAM1 迁移)
    .ccmram       → RAM2  (LVGL 显示缓冲 7.2 KB，D-Bus 高速访问)
    .MCUlcdgrambysram → LCDRAM (FSMC LCD 显存)
}
```

---

## 三、RAM1 (128 KB) 使用明细

| 段/区域 | 大小 | 说明 |
|---------|------|------|
| `.data` | ~2 KB | 已初始化全局变量（g_app_state、互斥量/信号量控制块等） |
| `.bss` | ~40-50 KB | BSS 段：静态全局数组 + RT-Thread small memory 堆管理器元数据 |
| **RT-Thread 堆** | ~70-80 KB | `rt_malloc` 动态分配区，各线程栈、JSON 缓冲区、MQTT 缓冲区等 |
| *合计已用* | *~53% (约68 KB)* | 启动后实测 |
| *剩余可用* | *~60 KB* | 供后续功能扩展 |

### 动态堆主要消费者

| 分配项 | 大小 | 来源 |
|--------|------|------|
| LVGL 工作线程栈 | 8 KB | `lv_rt_thread_port.c` |
| sensor 线程栈 | 3 KB | `app_sensor.c` |
| logic 线程栈 | 2 KB | `app_logic.c` |
| onenet 上传线程栈 | 2 KB | `app_net.c` |
| web server 线程栈 | 2 KB | `app_net.c` |
| http worker 线程栈 (per request) | 2 KB | `app_net.c` |
| json_buf (OneNET) | 2 KB | `app_net.c` |
| MQTT 内部缓冲 | ~4 KB | pahomqtt 包 |
| LWIP TCP/IP 栈 | ~8-12 KB | lwip-2.0.3 |
| WiFi (RW007) 驱动缓冲 | ~2 KB | rw007 包 |

---

## 四、RAM2 / CCMRAM (64 KB) — 优化前后对比

### 优化前（2026-05-18 及之前）

| 段 | 大小 | 状态 |
|----|------|------|
| *(无使用)* | 0 KB | **64 KB 完全闲置！**链接脚本定义了 RAM2，但没有任何段放置于此；`board.c` 虽有 memheap 注册，但无代码调用 `rt_memheap_alloc`，从未使用 |

### 优化后（2026-05-19）

| 段 | 大小 | 说明 |
|----|------|------|
| `.stack` | 1 KB | 系统主栈 (`_system_stack_size = 0x400`)，Cortex-M4 MSP/PSP 使用 |
| `.ccmram` | 7.2 KB | LVGL 显示缓冲 `buf1[240*15]`，`__attribute__((section(".ccmram")))` |
| *已使用* | *8.2 KB* | |
| *剩余可用* | **55.8 KB** | 零等待 D-Bus 访问，适合 CPU 密集型数据 |

### CCMRAM 剩余空间建议用途（优先级排序）

| 用途 | 预估大小 | 优先级 | 说明 |
|------|---------|--------|------|
| LVGL 双缓冲 (buf2) | 7.2 KB | ⭐⭐⭐ | 当前为单缓冲模式，加双缓冲可消除撕裂 |
| 传感器数据 FIFO 环形缓冲 | 2-4 KB | ⭐⭐ | 解耦采集与上传，应对网络波动 |
| 摄像头帧缓冲 (灰度) | 30-50 KB | ⭐ | 如后续接入 OV2640 做火焰识别，需要帧缓冲 |
| 日志环形缓冲 | 4 KB | ⭐ | 掉电前的告警日志持久化缓冲 |

---

## 五、各线程栈空间汇总

| 线程名 | 栈大小 | 优先级 | 栈位置 | 来源文件 |
|--------|--------|--------|--------|----------|
| `LVGL` | 8192 B | 20 | RAM1 (堆) | `lv_rt_thread_port.c` |
| `sensor` | 3072 B | 25 | RAM1 (堆) | [app_sensor.c](file:///d:/RT-ThreadStudio/workspace/onenet/applications/app_sensor.c) |
| `logic` | 2048 B | 8 | RAM1 (堆) | [app_logic.c](file:///d:/RT-ThreadStudio/workspace/onenet/applications/app_logic.c) |
| `onenet` | 2048 B | 15 | RAM1 (堆) | [app_net.c](file:///d:/RT-ThreadStudio/workspace/onenet/applications/app_net.c) |
| `web` | 2048 B | 18 | RAM1 (堆) | [app_net.c](file:///d:/RT-ThreadStudio/workspace/onenet/applications/app_net.c) |
| `http` (per req) | 2048 B | 23 | RAM1 (堆) | [app_net.c](file:///d:/RT-ThreadStudio/workspace/onenet/applications/app_net.c) |
| `cc2530_rx` | 1536 B | 24 | RAM1 (.bss) | [app_logic.c](file:///d:/RT-ThreadStudio/workspace/onenet/applications/app_logic.c) |
| `tshell` | 2048 B | 20 | RAM1 (堆) | RT-Thread 内核 |
| `tidle0` | 1024 B | 31 | RAM1 (堆) | RT-Thread 内核 |
| `paho_mqtt` | ~4096 B | 10 | RAM1 (堆) | pahomqtt 包 |
| `tcpip` | ~2048 B | — | RAM1 (堆) | lwip-2.0.3 |
| **系统主栈** | 1024 B | — | **RAM2 (CCMRAM)** | [link.lds](file:///d:/RT-ThreadStudio/workspace/onenet/board/linker_scripts/link.lds) |
| **LVGL 显存** | 7200 B | — | **RAM2 (CCMRAM)** | [lv_port_disp.c](file:///d:/RT-ThreadStudio/workspace/onenet/applications/lv_port_disp.c) |

---

## 六、关键内存风险与注意事项

### 1. CCMRAM 的 DMA 限制 ⚠️
CCMRAM (`0x10000000`) **不可被 DMA 访问**。当前 LVGL 的 ST7789 LCD 使用 FSMC 并行接口（非 DMA SPI），因此缓冲区放在 CCMRAM 是安全的。**若未来将 LCD 切换为 SPI+DMA 模式，必须将 `buf1` 移回 RAM1。**

### 2. 单缓冲撕裂
当前 LVGL 使用单缓冲模式（只有 `buf1`，无 `buf2`），在高速刷新场景可能出现画面撕裂。建议利用 CCMRAM 的剩余 55.8 KB 添加双缓冲。

### 3. 堆碎片化
RT-Thread small memory 分配器使用 First-Fit 算法，频繁的 `rt_malloc`/`rt_free`（如 HTTP worker 线程的创建与销毁）可能导致碎片化。建议：
- HTTP worker 改用线程池或静态分配
- JSON 缓冲区使用固定大小静态数组

### 4. 编译时验证
建议在 `rtconfig.h` 中添加以下宏，编译期检查内存是否溢出：
```c
#define RT_USING_MEMTRACE
```
或在启动日志中关注 `[MEM]` 输出，当 `Max Used` 超过 RAM1 的 85% (约 109 KB) 时应启动优化。

---

## 七、后续升级内存需求预估

| 功能 | 额外内存需求 | 可行性 |
|------|-------------|--------|
| 摄像头 (OV2640 JPEG 320×240) | ~30 KB (帧缓冲) | 需启用 CCMRAM 剩余空间或外扩 PSRAM |
| TTS 语音播报 | ~8 KB (音频缓冲) | RAM1 剩余空间可满足 |
| MQTT over TLS (mbedTLS) | ~20-30 KB | RAM1 紧张，需评估 |
| LVGL 双缓冲 | 7.2 KB | CCMRAM 剩余空间直接满足 |
| 文件系统 (LittleFS on SPI Flash) | ~4 KB (缓存) | RAM1 可满足 |
| OTA 固件升级 | ~8 KB (接收缓冲) | RAM1 可满足 |

> **总结**：当前系统 RAM1 使用约 53%，CCMRAM 使用约 13%。短期新增传感器和网络功能有充足空间。若要接入摄像头或 TLS，建议外扩 SPI PSRAM (如 IPS6404 8MB) 或启用片内 CCMRAM 剩余 ~55 KB 作为帧/加密缓冲。