/**
 * @file player_ui.c
 * @brief 播放器 LVGL 界面：主菜单、播放控制条、列表、FIFO 命令下发
 *
 * 界面层次：
 *   ui_show_main_menu()         → 主菜单（flex + video 按钮）
 *   ui_player_open_cb()         → mkfifo/open + ui_create_player_screen()
 *   ui_create_player_screen()   → 800x480 播放页
 *   ui_create_close_button()    → 右上角关闭钮
 *
 * 与系统编程相关：
 *   mkfifo / open / write(fd_mplayer) — MPlayer slave 控制
 *   pthread_kill + killall -19/-18 — seek/暂停时冻结读线程与 mplayer 进程
 *   pthread_cond_signal(&cond)    — 与 playback_io_sync 读线程配合
 *
 * 线程：本文件回调均在 LVGL 主线程执行；勿长时间阻塞。
 */

#include "player_ui.h"
#include "app_config.h"
#include "media_catalog.h"
#include "playback_controller.h"
#include "playback_monitor.h"
#include "playback_io_sync.h"

static void ui_transport_btn_cb(lv_event_t *e);

/* ---------- 跨模块共享的 LVGL 对象 ---------- */
lv_obj_t *t0 = NULL;
lv_obj_t *volume_slider = NULL;
lv_obj_t *bright_slider = NULL;
lv_obj_t *play_slider = NULL;
lv_obj_t *time_pos_label = NULL;
lv_obj_t *time_length_label = NULL;

/* ---------- 仅本模块使用的控件 ---------- */
static lv_obj_t *cont = NULL;
static lv_obj_t *video_list = NULL;
static lv_obj_t *volume_label = NULL;
static lv_obj_t *bright_label = NULL;

/** 1=列表可见，0=隐藏；由 "List" 按钮切换 */
static bool list_flag = true;

/** 播放/暂停 toggle 按钮控件指针 */
static lv_obj_t *btn_play_pause = NULL;

/** 当前是否已暂停（用于 toggle 图标和状态判断） */
static bool is_paused = false;


/** @brief 播放控制按钮统一事件处理（暂停/继续/seek/切歌/列表显隐） */
static void ui_transport_btn_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    char *msg = lv_event_get_user_data(e);

    if (code != LV_EVENT_CLICKED)
        return;

    if (strcmp(msg, "play_pause") == 0) {
        if (!start)
            return;

        if (!is_paused) {
            /* 当前正在播放 → 暂停 */
            pthread_kill(tid_read, 10);
            system("killall -19 mplayer");
            lv_obj_set_style_bg_img_src(btn_play_pause, LV_SYMBOL_PLAY, 0);
            is_paused = true;
        } else {
            /* 当前已暂停 → 继续播放 */
            system("killall -18 mplayer");
            pthread_cond_signal(&cond);
            usleep(1000);
            pthread_cond_signal(&cond);
            lv_obj_set_style_bg_img_src(btn_play_pause, LV_SYMBOL_PAUSE, 0);
            is_paused = false;
        }
    }

    if (strcmp(msg, "forward") == 0) {
        if (!start)
            return;
        usleep(1000);
        write(fd_mplayer, "seek +10\n", strlen("seek +10\n"));
        write(fd_mplayer, "get_percent_pos\n", strlen("get_percent_pos\n"));
    }

    if (strcmp(msg, "back") == 0) {
        if (!start)
            return;
        usleep(10000);
        write(fd_mplayer, "seek -10\n", strlen("seek -10\n"));
        write(fd_mplayer, "get_percent_pos\n", strlen("get_percent_pos\n"));
    }

    if (strcmp(msg, "next_music") == 0) {
        usleep(1000);
        if (++video_index >= video_num)
            video_index = 0;
        playback_play_current();
    }

    if (strcmp(msg, "prev_music") == 0) {
        usleep(1000);
        if (--video_index < 0)
            video_index = video_num - 1;
        playback_play_current();
    }

    if (strcmp(msg, "music_show_list") == 0) {
        list_flag = !list_flag;
        if (list_flag)
            lv_obj_clear_flag(video_list, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(video_list, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 创建底部播放控制按钮（播放/暂停/seek/上下曲/列表显隐）
 * @note user_data 传入字符串常量，在 ui_transport_btn_cb 中 strcmp 区分功能
 */
static void ui_create_transport_controls(void)
{
    lv_obj_t *label;

    /* 播放/暂停 toggle 按钮（二合一，居中 x=0） */
    btn_play_pause = lv_btn_create(cont);
    lv_obj_add_event_cb(btn_play_pause, ui_transport_btn_cb, LV_EVENT_ALL, "play_pause");
    lv_obj_align(btn_play_pause, LV_ALIGN_BOTTOM_MID, 0, 10);
    lv_obj_set_size(btn_play_pause, 60, 60);
    lv_obj_set_style_bg_img_src(btn_play_pause, LV_SYMBOL_PAUSE, 0);  /* 默认显示暂停图标 */

    lv_obj_t *btn_forward = lv_btn_create(cont);
    lv_obj_add_event_cb(btn_forward, ui_transport_btn_cb, LV_EVENT_ALL, "forward");
    lv_obj_set_size(btn_forward, 60, 60);
    lv_obj_align(btn_forward, LV_ALIGN_BOTTOM_MID, 100, 10);
    lv_obj_set_style_bg_img_src(btn_forward, LV_SYMBOL_RIGHT LV_SYMBOL_RIGHT, 0);

    lv_obj_t *btn_back = lv_btn_create(cont);
    lv_obj_add_event_cb(btn_back, ui_transport_btn_cb, LV_EVENT_ALL, "back");
    lv_obj_set_size(btn_back, 60, 60);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, -100, 10);
    lv_obj_set_style_bg_img_src(btn_back, LV_SYMBOL_LEFT LV_SYMBOL_LEFT, 0);

    lv_obj_t *btn_next = lv_btn_create(cont);
    lv_obj_add_event_cb(btn_next, ui_transport_btn_cb, LV_EVENT_ALL, "next_music");
    lv_obj_set_size(btn_next, 60, 60);
    lv_obj_align(btn_next, LV_ALIGN_BOTTOM_MID, 200, 10);
    lv_obj_set_style_bg_img_src(btn_next, LV_SYMBOL_NEXT, 0);

    lv_obj_t *btn_prev = lv_btn_create(cont);
    lv_obj_add_event_cb(btn_prev, ui_transport_btn_cb, LV_EVENT_ALL, "prev_music");
    lv_obj_set_size(btn_prev, 60, 60);
    lv_obj_align(btn_prev, LV_ALIGN_BOTTOM_MID, -200, 10);
    lv_obj_set_style_bg_img_src(btn_prev, LV_SYMBOL_PREV, 0);

    lv_obj_t *btn_list = lv_btn_create(cont);
    lv_obj_add_event_cb(btn_list, ui_transport_btn_cb, LV_EVENT_ALL, "music_show_list");
    lv_obj_set_size(btn_list, 50, 30);
    lv_obj_align(btn_list, LV_ALIGN_BOTTOM_RIGHT, 12, -45);
    label = lv_label_create(btn_list);
    lv_label_set_text(label, "List");
    lv_obj_center(label);
}

/**
 * @brief 滑条事件：音量/亮度实时写 FIFO；进度条在松开时 seek
 *
 * seek 流程（信号 + 条件变量）：
 *   1. pthread_kill(tid_read, 10) → playback_io_pause_reader_on_sig 阻塞读/写
 *   2. killall -19 mplayer → SIGSTOP 暂停解码进程
 *   3. 根据滑条百分比计算相对 seek 秒数，write "seek N\n"
 *   4. killall -18 恢复 mplayer，pthread_cond_signal 唤醒读线程
 */
static void ui_playback_slider_cb(lv_event_t *e)
{
    if (!start)
        return;

    char *msg = lv_event_get_user_data(e);

    if (strcmp(msg, "volume") == 0) {
        lv_obj_t *slider = lv_event_get_target(e);
        char buf[8];
        int volume = (int)lv_slider_get_value(slider);
        lv_snprintf(buf, sizeof(buf), "%d", volume);
        lv_label_set_text(volume_label, buf);
        lv_obj_align_to(volume_label, slider, LV_ALIGN_OUT_TOP_RIGHT, 8, -5);
        usleep(100);

        char cmd[1024] = {0};
        sprintf(cmd, "volume %d 1\n", volume);
        write(fd_mplayer, cmd, strlen(cmd));
    }

    if (strcmp(msg, "brightness") == 0) {
        lv_obj_t *slider1 = lv_event_get_target(e);
        char buf[8];
        int brightness = (int)lv_slider_get_value(slider1);
        lv_snprintf(buf, sizeof(buf), "%d", brightness);
        lv_label_set_text(bright_label, buf);
        lv_obj_align_to(bright_label, slider1, LV_ALIGN_OUT_TOP_RIGHT, 8, -5);
        usleep(100);

        char cmd[1024] = {0};
        sprintf(cmd, "brightness %d 1\n", brightness);
        write(fd_mplayer, cmd, strlen(cmd));
    }

    if (strcmp(msg, "play") == 0) {
        /* LV_EVENT_RELEASED：仅在手松开时跳转，避免拖动过程中连续 seek */
        if (start) {
            pthread_kill(tid_read, 10);
            system("killall -19 mplayer");
        }

        int rate = (int)lv_slider_get_value(play_slider);
        float new_time = time_length * rate * 0.01f;
        int seek_time = (int)(new_time - time_pos);

        char cmd[1024] = {0};
        sprintf(cmd, "seek %d\n", seek_time);
        write(fd_mplayer, cmd, strlen(cmd));

        system("killall -18 mplayer");
        pthread_cond_signal(&cond);
        usleep(1000);
        pthread_cond_signal(&cond);
    }
}

/** @brief 创建音量、亮度、进度滑条及两侧时间标签 */
static void ui_create_playback_sliders(void)
{
    volume_slider = lv_slider_create(cont);
    lv_obj_align(volume_slider, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_width(volume_slider, 100);
    lv_obj_add_event_cb(volume_slider, ui_playback_slider_cb, LV_EVENT_VALUE_CHANGED, "volume");
    lv_slider_set_value(volume_slider, 100, LV_ANIM_OFF);

    volume_label = lv_label_create(cont);
    lv_label_set_text(volume_label, "100");
    lv_obj_align_to(volume_label, volume_slider, LV_ALIGN_OUT_TOP_RIGHT, 8, -5);

    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, "volume");
    lv_obj_align_to(label, volume_slider, LV_ALIGN_OUT_TOP_LEFT, -5, -5);

    bright_slider = lv_slider_create(cont);
    lv_obj_align(bright_slider, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(bright_slider, ui_playback_slider_cb, LV_EVENT_VALUE_CHANGED, "brightness");
    lv_obj_set_width(bright_slider, 100);
    lv_slider_set_value(bright_slider, 20, LV_ANIM_OFF);

    bright_label = lv_label_create(cont);
    lv_label_set_text(bright_label, "20");
    lv_obj_align_to(bright_label, bright_slider, LV_ALIGN_OUT_TOP_RIGHT, 8, -5);

    lv_obj_t *label_bright = lv_label_create(cont);
    lv_label_set_text(label_bright, "bright");
    lv_obj_align_to(label_bright, bright_slider, LV_ALIGN_OUT_TOP_LEFT, -5, -5);

    play_slider = lv_slider_create(cont);
    lv_obj_align(play_slider, LV_ALIGN_CENTER, 0, 150);
    lv_obj_set_width(play_slider, 600);
    lv_obj_add_event_cb(play_slider, ui_playback_slider_cb, LV_EVENT_RELEASED, "play");
    lv_slider_set_value(play_slider, 0, LV_ANIM_OFF);
    lv_slider_set_range(play_slider, 0, 100);

    time_length_label = lv_label_create(cont);
    lv_label_set_text(time_length_label, "0:00");
    lv_obj_align_to(time_length_label, play_slider, LV_ALIGN_OUT_RIGHT_BOTTOM, 0, 20);

    time_pos_label = lv_label_create(cont);
    lv_label_set_text(time_pos_label, "0:00");
    lv_obj_align_to(time_pos_label, play_slider, LV_ALIGN_OUT_LEFT_BOTTOM, 0, 20);
}

/** @brief 列表项点击：设置 video_index 并调用 playback_play_current */
static void ui_playlist_item_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);

    if (code == LV_EVENT_CLICKED) {
        printf("Clicked %s\n", lv_list_get_btn_text(video_list, obj));
        video_index = (int)(intptr_t)lv_event_get_user_data(e);
        playback_play_current();
    }
}

/**
 * @brief 根据 media_catalog_scan 结果创建右侧播放列表
 * @note strtok(tmp, ".") 会修改 tmp，仅用于去掉扩展名显示
 */
static void ui_create_playlist(void)
{
    video_list = lv_list_create(cont);
    lv_obj_set_size(video_list, 100, 260);
    lv_obj_align(video_list, LV_ALIGN_RIGHT_MID, 15, -25);
    lv_list_add_text(video_list, "list");

    for (int i = 0; i < video_num; i++) {
        char tmp[100] = {0};
        char *p = video_path[i];
        p = strrchr(p, '/');
        strcpy(tmp, ++p);

        lv_obj_t *btn = lv_list_add_btn(video_list, NULL, strtok(tmp, "."));
        lv_obj_add_event_cb(btn, ui_playlist_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

/**
 * @brief 关闭播放页：终止 mplayer 并删除播放容器
 * @note user_data 为关闭按钮自身，其父对象即 cont
 */
void ui_player_close_cb(lv_event_t *e)
{
    system("killall -9 mplayer");
    lv_obj_del(lv_obj_get_parent((lv_obj_t *)(e->user_data)));
}

/** @brief 创建播放页右上角关闭按钮 */
static void ui_create_close_button(void)
{
    lv_obj_t *btn_close = lv_btn_create(cont);
    lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, 0, 15);  /* 下移 20px 避开触摸死角 */
    lv_obj_set_style_radius(btn_close, 5, 0);
    lv_obj_set_size(btn_close, 40, 40);

    lv_obj_t *label = lv_label_create(btn_close);
    lv_label_set_text(label, "X");
    lv_obj_center(label);

    lv_obj_add_event_cb(btn_close, ui_player_close_cb, LV_EVENT_RELEASED, btn_close);
}

/** @brief 组装播放页：关闭钮 + 控制钮 + 列表 + 滑条 */
static void ui_create_player_screen(void)
{
    cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, 800, 480);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_close_button();
    ui_create_transport_controls();
    ui_create_playlist();
    ui_create_playback_sliders();
}

/**
 * @brief 从主菜单进入播放器：创建 FIFO 并打开，再绘制播放界面
 *
 * access+mkfifo：命名管道 IPC
 * O_RDWR：避免仅写打开时因无读端而阻塞
 * 注意：此时尚未自动播放，需用户在列表中点击某一集
 */
void ui_player_open_cb(lv_event_t *e)
{
    (void)e;

    if (access(FIFO_PATH, F_OK))  /* 不存在则创建 FIFO */
        mkfifo(FIFO_PATH, 0644);

    fd_mplayer = open(FIFO_PATH, O_RDWR);
    if (fd_mplayer < 0) {
        perror("打开管道文件失败");
        exit(0);
    }

    ui_create_player_screen();
}

/**
 * @brief 应用启动后的主菜单
 *
 * 流程：创建 flex 容器 → media_catalog_scan() → "video" 按钮注册 ui_player_open_cb
 */
void ui_show_main_menu(void)
{
    t0 = lv_obj_create(lv_scr_act());
    lv_obj_set_size(t0, 800, 460);
    lv_obj_align(t0, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_pad_all(t0, 60, LV_PART_MAIN);
    lv_obj_set_style_pad_row(t0, 60, LV_PART_MAIN);
    lv_obj_set_style_pad_column(t0, 60, LV_PART_MAIN);
    lv_obj_clear_flag(t0, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_flex_flow(t0, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(t0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_SPACE_EVENLY, 0);

    media_catalog_scan();

    lv_obj_t *btn01 = lv_btn_create(t0);
    lv_obj_set_size(btn01, 150, 150);
    lv_obj_t *label01 = lv_label_create(btn01);
    lv_label_set_text(label01, "video");
    lv_obj_center(label01);
    lv_obj_add_event_cb(btn01, ui_player_open_cb, LV_EVENT_RELEASED, (void *)t0);

    printf("扫描到视频数量: %d\n", video_num);
}
