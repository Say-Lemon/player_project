# player_project / usrCode 软件架构与运行流程

本文档描述 `usrCode` 业务代码的模块关系、线程模型，以及**细化到函数**的调用流程。  
配合 Mermaid 图在支持渲染的编辑器（VS Code、Cursor、GitHub）中查看。

---

## 0. 模块文件命名对照表

| 旧文件名 | 新文件名 | 职责 |
|----------|----------|------|
| `common.h` | `app_config.h` | 应用公共宏与 IPC 路径常量 |
| `ui_video.c/h` | `player_ui.c/h` | LVGL 播放器界面与事件回调 |
| `video_scanner.c/h` | `media_catalog.c/h` | 本地视频目录扫描与列表 |
| `mplayer_control.c/h` | `playback_controller.c/h` | MPlayer 进程启停、切歌 |
| `mplayer_status.c/h` | `playback_monitor.c/h` | 解析 slave 应答、刷新进度 |
| `thread_sync.c/h` | `playback_io_sync.c/h` | 播放 I/O 读写线程与同步 |
| `main.c` | `main.c` | 程序入口（保持不变） |

## 0.1 函数命名对照表

| 旧名称（练手命名） | 当前专业命名 | 模块 |
|--------|--------|------|
| `Myplay_video` | `ui_show_main_menu` | player_ui |
| `video_playback` | `ui_player_open_cb` | player_ui |
| `Back_btn` | `ui_player_close_cb` | player_ui |
| `(原 display_interface2 内联关闭钮)` | `ui_create_close_button` | player_ui |
| `display_interface2` | `ui_create_player_screen` | player_ui |
| `show_button_tv` | `ui_create_transport_controls` | player_ui |
| `show_list2` | `ui_create_playlist` | player_ui |
| `show_slider_tv` | `ui_create_playback_sliders` | player_ui |
| `btn_handler2` | `ui_transport_btn_cb` | player_ui |
| `slider_event_cb2` | `ui_playback_slider_cb` | player_ui |
| `event_handler_video_list` | `ui_playlist_item_cb` | player_ui |
| `get_video_path` | `media_catalog_scan` | media_catalog |
| `kill_mplayer` | `playback_stop_all` | playback_controller |
| `play_one_video` | `playback_play_current` | playback_controller |
| `play_video_task` | `playback_launch_thread` | playback_controller |
| `read_mplayer_task` | `playback_status_reader_thread` | playback_monitor |
| `write_mplayer_task` | `playback_io_query_writer_thread` | playback_io_sync |
| `signal_10_task` | `playback_io_pause_reader_on_sig` | playback_io_sync |
| `signal_12_task` | `playback_io_pause_writer_on_sig` | playback_io_sync |
| `custom_tick_get` | `app_get_tick_ms` | main |

命名约定：`ui_*` 界面与 LVGL 回调；`media_catalog_*` 媒体目录；`playback_*` 播放控制；`playback_status_*` 状态监控；`playback_io_*` I/O 线程与同步。模块文件见 §0 对照表。

---

## 1. 总体软件架构（分层 + 模块）

```mermaid
flowchart TB
    subgraph HW["Linux 硬件层"]
        FB["/dev/fb0<br/>framebuffer"]
        EV["/dev/input/event0<br/>触摸屏"]
    end

    subgraph DRV["lv_drivers（第三方）"]
        fbdev_init --> FB
        fbdev_flush --> FB
        evdev_init --> EV
        evdev_read --> EV
    end

    subgraph LVGL["LVGL 库"]
        lv_init
        lv_timer_handler
        lv_tick_inc
        lv_obj_xxx["lv_obj_* / lv_btn_* / lv_slider_* ..."]
    end

    subgraph MAIN["main.c"]
        main
        app_get_tick_ms["app_get_tick_ms()<br/>（预留，当前未用）"]
    end

    subgraph UI["player_ui.c"]
        ui_show_main_menu
        ui_player_open_cb
        ui_create_player_screen
        ui_create_close_button
        ui_create_transport_controls
        ui_create_playlist
        ui_create_playback_sliders
        ui_transport_btn_cb
        ui_playback_slider_cb
        ui_playlist_item_cb
        ui_player_close_cb
    end

    subgraph SCAN["media_catalog.c"]
        media_catalog_scan
    end

    subgraph CTRL["playback_controller.c"]
        playback_play_current
        playback_launch_thread
        playback_stop_all
    end

    subgraph STAT["playback_monitor.c"]
        playback_status_reader_thread
    end

    subgraph SYNC["playback_io_sync.c"]
        playback_io_query_writer_thread
        playback_io_pause_reader_on_sig
        playback_io_pause_writer_on_sig
    end

    subgraph PROC["外部进程"]
        MP["mplayer 子进程<br/>-slave -input file=FIFO"]
    end

    subgraph IPC["IPC"]
        FIFO["/tmp/mplayer_control<br/>命名管道 FIFO"]
        PIPE["popen 匿名管道<br/>读 stdout"]
    end

    main --> lv_init
    main --> fbdev_init
    main --> evdev_init
    main --> ui_show_main_menu
    main --> lv_timer_handler

    ui_show_main_menu --> media_catalog_scan
    ui_show_main_menu --> lv_obj_xxx

    ui_player_open_cb --> ui_create_player_screen
    ui_create_player_screen --> ui_create_close_button
    ui_create_player_screen --> ui_create_transport_controls
    ui_create_player_screen --> ui_create_playlist
    ui_create_player_screen --> ui_create_playback_sliders

    ui_playlist_item_cb --> playback_play_current
    ui_transport_btn_cb --> playback_play_current
    ui_transport_btn_cb --> write

  playback_play_current --> playback_stop_all
    playback_play_current --> playback_launch_thread
    playback_play_current --> playback_status_reader_thread
    playback_play_current --> playback_io_query_writer_thread

    playback_launch_thread --> popen
    popen --> MP
    playback_launch_thread --> write
    write --> FIFO
    FIFO --> MP

    playback_io_query_writer_thread --> write
    playback_status_reader_thread --> fgets
    fgets --> PIPE
    PIPE --> MP
    playback_status_reader_thread --> lv_obj_xxx

    ui_transport_btn_cb --> write
    ui_playback_slider_cb --> write
    ui_playback_slider_cb --> pthread_kill
    ui_playback_slider_cb --> playback_io_pause_reader_on_sig

    MP --> PIPE
    MP --> FIFO
```

---

## 2. 线程与进程模型

```mermaid
flowchart LR
    subgraph T0["主线程 (main)"]
        LOOP["while(1)<br/>lv_timer_handler()<br/>lv_tick_inc(5)<br/>usleep(5000)"]
        EVT["LVGL 事件回调<br/>（均在主线程触发）"]
    end

    subgraph T1["播放线程 (每次切歌新建, detached)"]
        PVT["playback_launch_thread()<br/>popen(mplayer)<br/>write(get_time_length/pos)"]
    end

    subgraph T2["读线程 (仅首次 playback_play_current 创建, detached)"]
        RMT["playback_status_reader_thread()<br/>fgets → 解析 ANS_*<br/>mutex_lv + 更新 UI"]
    end

    subgraph T3["写线程 (仅首次创建, detached)"]
        WMT["playback_io_query_writer_thread()<br/>循环 write get_time_pos<br/>get_percent_pos"]
    end

    subgraph P1["子进程"]
        MP["mplayer"]
    end

    EVT --> T1
    T1 --> P1
    T2 --> P1
    T3 --> P1
    T2 -.->|mutex_lv| EVT
```

| 线程/进程 | 入口函数 | 创建时机 | 作用 |
|-----------|----------|----------|------|
| 主线程 | `main()` | 程序启动 | LVGL、触摸事件、控件创建 |
| 播放线程 | `playback_launch_thread()` | 每次 `playback_play_current()` | `popen` 启动 mplayer |
| 读线程 | `playback_status_reader_thread()` | 首次 `playback_play_current()` 且 `start==0` | 解析 slave 应答，更新进度 |
| 写线程 | `playback_io_query_writer_thread()` | 同上 | 定时向 FIFO 发查询命令 |
| mplayer | `exec` via `popen` | `playback_launch_thread` 内 | 解码播放 |

---

## 3. 模块依赖（头文件 include 关系）

```mermaid
flowchart TD
    app_cfg["app_config.h<br/>FIFO_PATH"]
    main_c["main.c"] --> player_ui_h["player_ui.h"]
    ui_c["player_ui.c"] --> app_cfg
    ui_c --> media_catalog_h
    ui_c --> playback_controller_h
    ui_c --> playback_monitor_h
    ui_c --> playback_io_sync_h
    playback_ctrl_c["playback_controller.c"] --> app_cfg
    playback_ctrl_c --> media_catalog_h
    playback_ctrl_c --> playback_io_sync_h
    playback_ctrl_c --> playback_monitor_h
    playback_mon_c["playback_monitor.c"] --> playback_monitor_h
    playback_sync_c["playback_io_sync.c"] --> playback_io_sync_h
    playback_sync_c --> playback_ctrl_c
    media_catalog_c["media_catalog.c"] --> media_catalog_h
```

---

## 4. 静态函数调用关系总图（usrCode 内部）

```mermaid
flowchart TD
    main["main()"]

    main --> ui_show_main_menu
    main --> lv_init
    main --> fbdev_init
    main --> lv_disp_draw_buf_init
    main --> lv_disp_drv_init
    main --> lv_disp_drv_register
    main --> evdev_init
    main --> lv_indev_drv_init
    main --> lv_indev_drv_register
    main --> lv_timer_handler
    main --> lv_tick_inc
    main --> usleep

    ui_show_main_menu --> media_catalog_scan
    ui_show_main_menu --> lv_obj_create
    ui_show_main_menu --> lv_btn_create
    ui_show_main_menu --> lv_obj_add_event_cb

    lv_obj_add_event_cb -.->|RELEASED| ui_player_open_cb

    ui_player_open_cb --> access
    ui_player_open_cb --> mkfifo
    ui_player_open_cb --> open
    ui_player_open_cb --> ui_create_player_screen

    ui_create_player_screen --> lv_obj_create
    ui_create_player_screen --> ui_create_close_button
    ui_create_player_screen --> ui_create_transport_controls
    ui_create_player_screen --> ui_create_playlist
    ui_create_player_screen --> ui_create_playback_sliders

    ui_create_close_button --> lv_btn_create
    ui_create_close_button --> lv_obj_add_event_cb

    lv_obj_add_event_cb -.->|RELEASED| ui_player_close_cb

    ui_create_transport_controls --> lv_btn_create
    ui_create_transport_controls --> lv_obj_add_event_cb

    lv_obj_add_event_cb -.->|CLICKED| ui_transport_btn_cb

    ui_create_playlist --> lv_list_create
    ui_create_playlist --> lv_list_add_btn
    ui_create_playlist --> lv_obj_add_event_cb

    lv_obj_add_event_cb -.->|CLICKED| ui_playlist_item_cb

    ui_create_playback_sliders --> lv_slider_create
    ui_create_playback_sliders --> lv_obj_add_event_cb

    lv_obj_add_event_cb -.->|VALUE_CHANGED / RELEASED| ui_playback_slider_cb

    ui_playlist_item_cb --> playback_play_current

    ui_transport_btn_cb --> playback_play_current
    ui_transport_btn_cb --> pthread_kill
    ui_transport_btn_cb --> system
    ui_transport_btn_cb --> write
    ui_transport_btn_cb --> pthread_cond_signal

    ui_playback_slider_cb --> pthread_kill
    ui_playback_slider_cb --> system
    ui_playback_slider_cb --> write
    ui_playback_slider_cb --> pthread_cond_signal
    ui_playback_slider_cb --> lv_slider_get_value

    ui_player_close_cb --> system
    ui_player_close_cb --> lv_obj_del

    playback_play_current --> playback_stop_all
    playback_play_current --> usleep
    playback_play_current --> pthread_create
    playback_play_current --> pthread_detach
    playback_play_current --> sleep
    playback_play_current --> pthread_cond_signal
    playback_play_current --> write
    playback_play_current --> lv_slider_get_value

    pthread_create -.->|playback_launch_thread| playback_launch_thread
    pthread_create -.->|playback_status_reader_thread| playback_status_reader_thread
    pthread_create -.->|playback_io_query_writer_thread| playback_io_query_writer_thread

    playback_stop_all --> system

    playback_launch_thread --> popen
    playback_launch_thread --> write
    playback_launch_thread --> perror

    playback_status_reader_thread --> signal
    playback_status_reader_thread --> fgets
    playback_status_reader_thread --> strstr
    playback_status_reader_thread --> sscanf
    playback_status_reader_thread --> pthread_mutex_lock
    playback_status_reader_thread --> lv_label_set_text
    playback_status_reader_thread --> lv_slider_set_value
    playback_status_reader_thread --> pthread_mutex_unlock
    playback_status_reader_thread --> playback_play_current

    playback_io_query_writer_thread --> signal
    playback_io_query_writer_thread --> write
    playback_io_query_writer_thread --> usleep

    pthread_kill -.->|SIG 10| playback_io_pause_reader_on_sig
    pthread_kill -.->|SIG 12| playback_io_pause_writer_on_sig

    playback_io_pause_reader_on_sig --> pthread_kill
    playback_io_pause_reader_on_sig --> pthread_cond_wait
    playback_io_pause_reader_on_sig --> pthread_cond_signal

    playback_io_pause_writer_on_sig --> pthread_cond_wait
```

> 虚线箭头：由 LVGL 事件系统或 `pthread_create` / `pthread_kill` **间接**调用，非直接 C 调用。

---

## 5. 运行流程一：程序启动

```mermaid
sequenceDiagram
    autonumber
    participant main as main()
    participant lv as LVGL / lv_drivers
    participant ui as ui_show_main_menu()
    participant scan as media_catalog_scan()

    main->>lv: lv_init()
    main->>lv: fbdev_init()
    main->>lv: lv_disp_draw_buf_init()
    main->>lv: lv_disp_drv_init() / register(fbdev_flush)
    main->>lv: evdev_init()
    main->>lv: lv_indev_drv_init() / register(evdev_read)
    main->>ui: ui_show_main_menu()
    ui->>scan: media_catalog_scan()
    scan->>scan: opendir("./video")
    loop 每个文件
        scan->>scan: readdir()
        scan->>scan: strstr(.avi/.mp4/.mkv)
        scan->>scan: sprintf(video_path[i])
    end
    scan->>scan: closedir()
    ui->>lv: lv_obj_create / lv_btn_create
    ui->>lv: lv_obj_add_event_cb(btn01, ui_player_open_cb)
    loop 永久
        main->>lv: lv_timer_handler()
        main->>lv: lv_tick_inc(5)
        main->>main: usleep(5000)
    end
```

---

## 6. 运行流程二：进入播放界面（尚未播放）

用户点击主菜单 **video** 按钮。

```mermaid
sequenceDiagram
    autonumber
    participant user as 用户触摸
    participant lv as lv_timer_handler
    participant vp as ui_player_open_cb()
    participant di as ui_create_player_screen()
    participant cb as ui_create_close_button()
    participant sb as ui_create_transport_controls()
    participant sl as ui_create_playlist()
    participant ss as ui_create_playback_sliders()

    user->>lv: 触摸释放
    lv->>vp: ui_player_open_cb(e)  [LV_EVENT_RELEASED]
    vp->>vp: access(FIFO_PATH, F_OK)
    alt FIFO 不存在
        vp->>vp: mkfifo(FIFO_PATH, 0644)
    end
    vp->>vp: open(FIFO_PATH, O_RDWR) → fd_mplayer
    vp->>di: ui_create_player_screen()
    di->>di: lv_obj_create(cont) 800×480
    di->>cb: ui_create_close_button()
    cb->>cb: lv_btn_create + lv_obj_add_event_cb(ui_player_close_cb)
    di->>sb: ui_create_transport_controls()
    sb->>sb: lv_btn_create ×6 + lv_obj_add_event_cb(ui_transport_btn_cb)
    di->>sl: ui_create_playlist()
    sl->>sl: lv_list_create + lv_list_add_btn(循环 video_num)
    sl->>sl: lv_obj_add_event_cb(ui_playlist_item_cb)
    di->>ss: ui_create_playback_sliders()
    ss->>ss: lv_slider_create(volume/bright/play)
    ss->>ss: lv_obj_add_event_cb(ui_playback_slider_cb)
    Note over vp: 此时未调用 playback_play_current，无声音画面
```

---

## 7. 运行流程三：点击列表项开始播放（首次）

假设 `start == 0`，`video_index` 被设为列表下标 `i`。

```mermaid
sequenceDiagram
    autonumber
    participant user as 用户
    participant lv as LVGL
    participant eh as ui_playlist_item_cb()
    participant pov as playback_play_current()
    participant kill as playback_stop_all()
    participant pvt as playback_launch_thread()
    participant rmt as playback_status_reader_thread()
    participant wmt as playback_io_query_writer_thread()
    participant fifo as FIFO fd_mplayer
    participant mp as mplayer 进程

    user->>lv: 点击列表项
    lv->>eh: ui_playlist_item_cb(e)
    eh->>eh: video_index = user_data(i)
    eh->>pov: playback_play_current()

    alt play_flag != 0
        pov->>kill: playback_stop_all()
        kill->>kill: system("killall -9 mplayer")
        pov->>pov: usleep(200000)
    end
    pov->>pov: play_flag = 1
    pov->>pov: pthread_create → playback_launch_thread
    pov->>pov: pthread_detach

    par 播放线程
        pvt->>pvt: snprintf(mplayer 命令行)
        pvt->>mp: popen(cmd, "r") → fp_mplayer
        pvt->>fifo: write("get_time_length\n")
        pvt->>fifo: write("get_time_pos\n")
    and 主线程继续
        alt !start 首次
            pov->>pov: sleep(1)
            pov->>pov: pthread_create → playback_status_reader_thread
            pov->>pov: pthread_detach(tid_read)
            pov->>pov: pthread_create → playback_io_query_writer_thread
            pov->>pov: pthread_detach(tid_write)
            pov->>pov: start = 1
        end
        pov->>pov: pthread_cond_signal(&cond)
        pov->>fifo: write(volume / brightness)
    end

    Note over rmt,wmt: 读线程内 signal(10, playback_io_pause_reader_on_sig)
    Note over wmt: 写线程内 signal(12, playback_io_pause_writer_on_sig)

    loop 写线程永久
        wmt->>fifo: write("get_time_pos\n")
        wmt->>wmt: usleep(400ms)
        wmt->>fifo: write("get_percent_pos\n")
        wmt->>wmt: usleep(400ms)
    end

    loop 读线程永久
        mp-->>rmt: stdout ANS_* 行
        rmt->>rmt: fgets(fp_mplayer)
        rmt->>rmt: 解析 ANS_TIME_POSITION / PERCENT / LENGTH
        rmt->>rmt: pthread_mutex_lock(mutex_lv)
        rmt->>lv: lv_label_set_text / lv_slider_set_value
        rmt->>rmt: pthread_mutex_unlock(mutex_lv)
    end
```

---

## 8. 运行流程四：播放中拖动进度条（seek）

触发：`ui_playback_slider_cb`，事件 `LV_EVENT_RELEASED`，`user_data == "play"`。

```mermaid
sequenceDiagram
    autonumber
    participant user as 用户
    participant lv as LVGL
    participant se as ui_playback_slider_cb()
    participant read as playback_status_reader_thread
    participant s10 as playback_io_pause_reader_on_sig()
    participant write as playback_io_query_writer_thread
    participant s12 as playback_io_pause_writer_on_sig()
    participant fifo as fd_mplayer
    participant mp as mplayer

    user->>lv: 松开进度滑条
    lv->>se: ui_playback_slider_cb(e)
    se->>se: if (!start) return
    se->>read: pthread_kill(tid_read, 10)
    read->>s10: playback_io_pause_reader_on_sig(10)
    s10->>write: pthread_kill(tid_write, 12)
    write->>s12: playback_io_pause_writer_on_sig(12)
    s12->>s12: pthread_cond_wait(&cond1, &mutex1)
    s10->>s10: pthread_cond_wait(&cond, &mutex)
    se->>mp: system("killall -19 mplayer")  SIGSTOP
    se->>se: rate = lv_slider_get_value(play_slider)
    se->>se: seek_time = time_length*rate*0.01 - time_pos
    se->>fifo: write("seek N\n")
    se->>mp: system("killall -18 mplayer")  SIGCONT
    se->>se: pthread_cond_signal(&cond) ×2
    s10->>s10: 从 cond 唤醒，pthread_cond_signal(&cond1)
    s12->>s12: 从 cond1 唤醒
    Note over read,write: 继续 fgets / write 查询循环
```

---

## 9. 运行流程五：暂停 / 继续

| 按钮 | 回调 | 关键调用 |
|------|------|----------|
| 暂停 | `ui_transport_btn_cb` ("pause") | `pthread_kill(tid_read,10)` → `system("killall -19 mplayer")` |
| 播放 | `ui_transport_btn_cb` ("play") | `system("killall -18 mplayer")` → `pthread_cond_signal(&cond)` ×2 |

```mermaid
flowchart TD
    A[ui_transport_btn_cb CLICKED] --> B{msg?}
    B -->|pause| C[pthread_kill tid_read 10]
    C --> D[playback_io_pause_reader_on_sig 阻塞读/写线程]
    C --> E[killall -19 SIGSTOP mplayer]
    B -->|play| F[killall -18 SIGCONT mplayer]
    F --> G[pthread_cond_signal cond ×2]
    G --> H[读线程从 cond 唤醒]
```

---

## 10. 运行流程六：快进 / 快退 / 上一曲 / 下一曲

```mermaid
flowchart TD
    H[ui_transport_btn_cb] --> F{msg}
    F -->|forward| W1[write seek +10]
    W1 --> W2[write get_percent_pos]
    F -->|back| W3[write seek -10]
    W3 --> W4[write get_percent_pos]
    F -->|next_music| I1[video_index++ 循环]
    F -->|prev_music| I2[video_index-- 循环]
    I1 --> POV[playback_play_current]
    I2 --> POV
    POV --> K[playback_stop_all 若已在播]
    POV --> PVT[pthread_create playback_launch_thread]
```

---

## 11. 运行流程七：自动下一首（播放到 99%）

在 `playback_status_reader_thread()` 内解析 `ANS_PERCENT_POSITION`：

```mermaid
sequenceDiagram
    participant wmt as playback_io_query_writer_thread
    participant fifo as FIFO
    participant mp as mplayer
    participant rmt as playback_status_reader_thread
    participant pov as playback_play_current

    wmt->>fifo: get_percent_pos
    mp-->>rmt: ANS_PERCENT_POSITION=99
    rmt->>rmt: percent_pos = 99
    rmt->>rmt: lv_slider_set_value(play_slider)
    rmt->>rmt: video_index++
    rmt->>pov: playback_play_current()
    Note over pov: kill 旧 mplayer → 新 playback_launch_thread → 新 popen
```

---

## 12. 运行流程八：关闭播放页

用户点击右上角 **X**。

```mermaid
sequenceDiagram
    participant user as 用户
    participant lv as LVGL
    participant bb as ui_player_close_cb()
    participant mp as mplayer

    user->>lv: 释放关闭钮
    lv->>bb: ui_player_close_cb(e)
    bb->>mp: system("killall -9 mplayer")
    bb->>lv: lv_obj_del(父容器 cont)
    Note over lv: 读/写线程仍在运行，fp_mplayer 可能失效后 fgets 空转
```

---

## 13. 全局变量与函数对照（跨模块）

| 变量 | 定义位置 | 主要读 | 主要写 |
|------|----------|--------|--------|
| `video_path[]`, `video_num` | media_catalog.c | ui, control | media_catalog_scan |
| `fd_mplayer` | playback_controller.c | ui, sync, control | ui_player_open_cb, playback_launch_thread |
| `fp_mplayer` | playback_controller.c | playback_monitor | playback_launch_thread |
| `video_index` | playback_controller.c | ui, control, status | ui 列表/按钮, status 自动下一首 |
| `start`, `play_flag` | playback_controller.c | ui, control | playback_play_current |
| `time_pos`, `time_length`, `percent_pos` | playback_monitor.c | ui seek | playback_status_reader_thread |
| `tid_read`, `tid_write` | playback_io_sync.c | ui, sync | playback_play_current |
| `cond`, `cond1`, `mutex*` | playback_io_sync.c | ui, sync | playback_io_pause_* / slider / btn |

---

## 14. 函数清单（usrCode 业务）

| 文件 | 函数 | 可见性 | 调用者 |
|------|------|--------|--------|
| main.c | `main` | 全局 | 系统 |
| main.c | `app_get_tick_ms` | 全局 | （未链接到 LV_TICK_CUSTOM） |
| media_catalog.c | `media_catalog_scan` | 全局 | ui_show_main_menu |
| player_ui.c | `ui_show_main_menu` | 全局 | main |
| player_ui.c | `ui_player_open_cb` | 全局 | LVGL 事件 |
| player_ui.c | `ui_player_close_cb` | 全局 | LVGL 事件 |
| player_ui.c | `ui_create_player_screen` | static | ui_player_open_cb |
| player_ui.c | `ui_create_close_button` | static | ui_create_player_screen |
| player_ui.c | `ui_create_transport_controls` | static | ui_create_player_screen |
| player_ui.c | `ui_create_playlist` | static | ui_create_player_screen |
| player_ui.c | `ui_create_playback_sliders` | static | ui_create_player_screen |
| player_ui.c | `ui_transport_btn_cb` | static | LVGL 事件 |
| player_ui.c | `ui_playback_slider_cb` | static | LVGL 事件 |
| player_ui.c | `ui_playlist_item_cb` | static | LVGL 事件 |
| playback_controller.c | `playback_stop_all` | 全局 | playback_play_current, ui_player_close_cb(间接) |
| playback_controller.c | `playback_play_current` | 全局 | player_ui, playback_monitor |
| playback_controller.c | `playback_launch_thread` | 全局 | pthread_create |
| playback_monitor.c | `playback_status_reader_thread` | 全局 | pthread_create |
| playback_io_sync.c | `playback_io_query_writer_thread` | 全局 | pthread_create |
| playback_io_sync.c | `playback_io_pause_reader_on_sig` | 全局 | signal / pthread_kill |
| playback_io_sync.c | `playback_io_pause_writer_on_sig` | 全局 | signal / pthread_kill |

---

*文档版本与 `usrCode` 源码同步；若增删函数请一并更新本文件。*
