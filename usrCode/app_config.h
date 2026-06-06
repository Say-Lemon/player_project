/**
 * @file app_config.h
 * @brief 全项目公共宏与系统头文件集合
 *
 * 各业务模块通过包含本头文件，统一获得 POSIX 常用接口声明，
 * 并共享 MPlayer 控制管道的路径常量。
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <fcntl.h>      /* open, O_RDWR */
#include <sys/stat.h>   /* mkfifo */
#include <dirent.h>     /* opendir, readdir */
#include <time.h>
#include <math.h>

/**
 * MPlayer slave 模式控制管道的文件路径（命名管道 FIFO）。
 *
 * 启动参数：mplayer ... -input file=/tmp/mplayer_control
 * UI 与控制模块通过 write(fd_mplayer, "命令\n", ...) 向该 FIFO 写入文本命令；
 * MPlayer 从同一 FIFO 读取并执行。
 *
 * 使用 O_RDWR 打开可避免“仅有写端、无读端时 open 阻塞”的问题。
 */
#define FIFO_PATH "/tmp/mplayer_control"

#endif /* APP_CONFIG_H */
