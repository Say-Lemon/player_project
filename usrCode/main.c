/**
 * @file main.c
 * @brief 程序入口：LVGL 与 Linux 显示/输入驱动初始化，主事件循环
 *
 * 职责：
 *   - 初始化 LVGL、framebuffer 显示（800x480）、evdev 触摸屏
 *   - 调用 ui_show_main_menu() 进入业务主菜单
 *   - 在 while(1) 中周期性调用 lv_timer_handler()（须在主线程执行）
 *
 * 注意：LVGL 非线程安全，除 playback_monitor 中带 mutex_lv 的少量更新外，
 *       所有控件创建与事件处理均在本线程完成。
 */

#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include "player_ui.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

/** 显示驱动单缓冲像素数量（与 lv_conf 色深配合，占用较大静态内存） */
#define DISP_BUF_SIZE (800 * 480 * 2)

int main(void)
{
    /* ---------- LVGL 核心与 framebuffer 显示 ---------- */
    lv_init();

    fbdev_init();
    evdev_init();

    static lv_color_t buf[DISP_BUF_SIZE];
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);


    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;   /* 将 LVGL 绘制结果刷到 framebuffer */
    disp_drv.hor_res  = 800;
    disp_drv.ver_res  = 480;
    lv_disp_drv_register(&disp_drv);    //注册显示设备

    /* ---------- 触摸屏输入（evdev） ---------- */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);  //注册输入设备

    /* ---------- 业务 UI：主菜单（扫描 video/、显示入口按钮） ---------- */
    ui_show_main_menu();

    /**
     * LVGL 主循环：约每 5ms 一轮。
     * lv_tick_inc(5) 与 usleep(5000) 配合，为动画与定时器提供时间基准。
     * （lv_conf.h 中 LV_TICK_CUSTOM 为 0，故在此手动递增 tick，未使用 app_get_tick_ms）
     */
    while (1) {
        lv_timer_handler(); //处理所有注册的 LVGL 任务
        lv_tick_inc(5);
        usleep(5000);
    }

    return 0;
}

/**
 * @brief 获取自上电/首次调用以来的毫秒数（相对时间）
 *
 * 若将 lv_conf.h 中 LV_TICK_CUSTOM 设为 1 并绑定本函数，可由 LVGL 内部自动取 tick。
 * 当前工程在主循环中手动 lv_tick_inc，本函数为预留接口。
 */
// uint32_t app_get_tick_ms(void)
// {
//     static uint64_t start_ms = 0;
//     if (start_ms == 0) {
//         struct timeval tv_start;
//         gettimeofday(&tv_start, NULL);
//         start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
//     }

//     struct timeval tv_now;
//     gettimeofday(&tv_now, NULL);
//     uint64_t now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

//     return (uint32_t)(now_ms - start_ms);
// }
