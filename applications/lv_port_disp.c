/*
 * LVGL Display Port for ST7789 LCD
 */

#include <lvgl.h>
#include <drv_lcd.h>
#include <rtthread.h>

#define TAG "lvgl"

/* Log level definitions */
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

/* LCD buffer for LVGL — 15 rows = 1/16 screen, flush from 16 slices */
/* buf1 置于 CCMRAM (RAM2 @ 0x10000000)，释放 ~7KB RAM1 空间 */
static lv_disp_draw_buf_t disp_buf;
#define DISP_BUF_LINES 15
static lv_color_t buf1[LCD_W * DISP_BUF_LINES] __attribute__((section(".ccmram")));  /* 240 * 15 * 2 = 7200 bytes */

/* LCD device handle */
static rt_device_t lcd_dev_handle = RT_NULL;

/* Flush callback: copy buffer to LCD */
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    static int flush_count = 0;
    flush_count++;
    
    if (flush_count <= 5)
    {
        LOG_D(TAG, "flush #%d: area(%d,%d)->(%d,%d), pixels=%d",
                   flush_count, 
                   area->x1, area->y1, area->x2, area->y2,
                   (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));
    }
    
    /* Use lcd_fill_array to write pixel data to LCD */
    lcd_fill_array(area->x1, area->y1, area->x2, area->y2, color_p);
    
    /* Tell LVGL we are ready with the flushing */
    lv_disp_flush_ready(disp_drv);
}

void lv_port_disp_init(void)
{
    /* Initialize LCD hardware */
    rt_device_t lcd_dev = rt_device_find("lcd");
    if (lcd_dev != RT_NULL)
    {
        rt_device_open(lcd_dev, RT_DEVICE_OFLAG_RDWR);
        lcd_dev_handle = lcd_dev;
        LOG_I(TAG, "LCD device opened successfully");
    }
    
    /* Clear LCD screen to white (match LVGL background) */
    lcd_clear(WHITE);
    LOG_I(TAG, "LCD cleared to white");
    
    /* Initialize display buffer */
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, LCD_W * DISP_BUF_LINES);
    LOG_I(TAG, "LVGL display buffer initialized (%u bytes)",
               (unsigned)(LCD_W * DISP_BUF_LINES * sizeof(lv_color_t)));
    
    /* Initialize display driver */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    
    /* Set basic parameters */
    disp_drv.hor_res = LCD_W;
    disp_drv.ver_res = LCD_H;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = disp_flush;
    
    /* Register the display driver */
    lv_disp_drv_register(&disp_drv);
    LOG_I(TAG, "LVGL display driver registered");
}

/* Empty input driver (no touch) */
void lv_port_indev_init(void)
{
    /* No touch screen */
}

/* Empty GUI init */
void lv_user_gui_init(void)
{
    /* GUI initialized elsewhere */
}
