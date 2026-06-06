/**
 * @file playback_controller.c
 * @brief 播放引擎：MPlayer 子进程管理与播放控制逻辑
 *
 * 数据通路：
 *   控制：write(fd_mplayer) → FIFO → mplayer -input file=...
 *   状态：popen 匿名管道 ← mplayer stdout（ANS_* 行，由 playback_monitor 解析）
 *
 * 线程：
 *   playback_launch_thread — 每次切歌时 pthread_create，内部 popen 启动 mplayer
 *   读/写线程 — 仅在首次 playback_play_current 时各创建一次，之后复用
 */

#include "playback_controller.h"
#include "media_catalog.h"
#include "playback_io_sync.h"
#include "playback_monitor.h"
#include "app_config.h"
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

FILE *fp_mplayer = NULL;
int fd_mplayer = 0;
int video_index = -1;   /* 列表选中前为 -1，需先点列表项再播 */
bool play_flag = 0;
bool start = 0;

/**
 * @brief 强制结束所有名为 mplayer 的进程
 *
 * 使用 system+killall 是练手写法；未保存子进程 pid。
 */
void playback_stop_all(void)
{
    system("killall -9 mplayer");
}

/**
 * @brief 播放线程：popen 启动 mplayer，并查询总时长与当前位置
 *
 * -slave：文本命令/应答模式
 * -input file=FIFO_PATH：从命名管道读命令
 * -x/-y/-zoom：视频窗口大小（叠在 LVGL 界面中央）
 * 全局 fp_mplayer 供 playback_status_reader_thread 阻塞读取。
 */
void *playback_launch_thread(void *arg)
{
    (void)arg;

    printf("---- %ld 播放线程 ------------------\n", (long)pthread_self());

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "mplayer -quiet -slave -zoom -x 680 -y 362 -input file=%s \"%s\"",
             FIFO_PATH, video_path[video_index]);
    printf("命令：%s\n", cmd);

    fp_mplayer = popen(cmd, "r");
    if (fp_mplayer == NULL) {
        perror("popen fail");
        return NULL;
    }

    puts("---- 播放线程：mplayer 已启动 ----\n");

    /* 向 FIFO 查询，应答由读线程解析 */
    write(fd_mplayer, "get_time_length\n", strlen("get_time_length\n"));
    write(fd_mplayer, "get_time_pos\n", strlen("get_time_pos\n"));

    return NULL;
}

/**
 * @brief 播放当前 video_index 指向的视频（切歌入口）
 *
 * 1. 若已在播则 playback_stop_all 并短暂等待
 * 2. 新建 detached 线程执行 popen
 * 3. 首次调用时 sleep(1) 后创建读/写线程（等待 mplayer 与 FIFO 就绪）
 * 4. pthread_cond_signal 唤醒可能阻塞在 cond 上的读线程
 * 5. 按 UI 滑条同步音量、亮度
 */
void playback_play_current(void)
{
    if (play_flag != 0) {
        playback_stop_all();
        usleep(200000);
    }

    play_flag = 1;
    printf("视频索引 %d\n", video_index);

    pthread_t tid;
    if (pthread_create(&tid, NULL, playback_launch_thread, NULL) != 0) {
        perror("创建播放线程失败");
        return;
    }
    pthread_detach(tid);

    if (!start) {
        sleep(1);  /* 等待 mplayer 进程启动并打开 FIFO 读端 */

        pthread_create(&tid_read, NULL, playback_status_reader_thread, NULL);
        pthread_detach(tid_read);

        pthread_create(&tid_write, NULL, playback_io_query_writer_thread, NULL);
        pthread_detach(tid_write);

        printf("tid_read %ld  tid_write %ld\n", (long)tid_read, (long)tid_write);
        start = 1;
    }

    pthread_cond_signal(&cond);

    if (volume_slider) {
        int volume = (int)lv_slider_get_value(volume_slider);
        char cmd[64];
        sprintf(cmd, "volume %d 1\n", volume);
        write(fd_mplayer, cmd, strlen(cmd));
    }

    if (bright_slider) {
        int bright = (int)lv_slider_get_value(bright_slider);
        char cmd[64];
        sprintf(cmd, "brightness %d 1\n", bright);
        write(fd_mplayer, cmd, strlen(cmd));
    }
}
