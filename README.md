# 嵌入式视频播放器 (Embedded Video Player)

基于 **LVGL v9** + **MPlayer** 的 ARM Linux 嵌入式视频播放器，运行在 framebuffer 设备（`/dev/fb0`）上，使用触摸屏（evdev）进行交互。

## 技术栈

| 组件 | 说明 |
|------|------|
| **GUI** | LVGL v9.0，通过 `/dev/fb0` 渲染，800×480 分辨率 |
| **输入** | evdev 触摸屏 (`/dev/input/event0`) |
| **解码** | MPlayer（外部进程），slave 模式 |
| **IPC** | 命名管道 FIFO + popen 匿名管道 |
| **编译** | `arm-linux-gcc` 交叉编译 |
| **平台** | ARM Linux（S5PV210 / 其他 ARM 开发板） |

## 功能

- 🎬 本地视频播放（支持 `.avi` / `.mp4` / `.mkv`）
- 🎛️ 播放/暂停 toggle、快进/快退、上一曲/下一曲
- 📜 右侧播放列表，点击切换视频
- 🎚️ 音量、亮度实时调节
- ⏩ 进度条拖拽 seek
- 🔄 播放到 99% 自动切下一首
- 🌙 暗色主题 UI

## 软件架构

```
usrCode/
├── main.c                   # 入口：LVGL + framebuffer/evdev 初始化，主循环
├── app_config.h             # 公共头文件（FIFO 路径、POSIX 头文件）
├── player_ui.c/h            # LVGL 界面：主菜单、播放页、按钮/滑条回调
├── media_catalog.c/h        # 视频扫描（opendir/readdir）
├── playback_controller.c/h  # MPlayer 进程管理（popen/切歌/停止）
├── playback_monitor.c/h     # 解析 MPlayer slave 应答（进度/时长）
└── playback_io_sync.c/h     # 后台 I/O 线程 + 信号/条件变量同步
```

### 线程模型

| 线程 | 入口 | 职责 |
|------|------|------|
| 主线程 | `main()` | LVGL UI 刷新 (200Hz)、触摸事件 |
| 播放线程 | `playback_launch_thread()` | 每次切歌新建，popen 启动 MPlayer |
| 读线程 | `playback_status_reader_thread()` | 第一次播放时创建，解析 slave 应答 |
| 写线程 | `playback_io_query_writer_thread()` | 第一次播放时创建，周期性查询进度 |
| MPlayer | 外部进程 | 实际解码渲染 |

### IPC 数据通路

```
UI 按钮/滑条 ──write()──→ FIFO (/tmp/mplayer_control) ──→ MPlayer (slave 模式)
MPlayer stdout ──fgets()──→ 读线程 ──mutex──→ LVGL 控件更新（进度条/时间）
写线程 ──write(get_time_pos)──→ FIFO ──→ MPlayer ──stdout──→ 读线程
```

## 构建

### 依赖

- `arm-linux-gcc` 交叉编译工具链
- 本仓库已包含 LVGL 和 lv_drivers 源码（无需额外下载）

### 编译

```bash
make clean
make
```

编译产物：`demo`（ARM 可执行文件）

### 部署到目标板

修改 `Makefile` 中 `send` 目标的 IP 地址，然后：

```bash
make send
```

或手动拷贝：

```bash
scp demo root@<开发板IP>:~/path/to/app/
```

## 使用

1. 将视频文件放入目标板的 `video/` 目录（支持 `.avi` / `.mp4` / `.mkv`）
2. 运行 `./demo`
3. 主菜单点击 **video** 按钮进入播放界面
4. 在右侧列表中点击视频名称开始播放
5. 底部控制栏：
   - ⏯ 播放/暂停 toggle
   - ⏪ ⏩ 快退/快进 10 秒
   - ⏮ ⏭ 上一曲/下一曲
   - 🎚️ 音量滑条（右下）/ 亮度滑条（左下）
   - 📊 进度条（拖动 seek）

## 目录结构

```
player_project/
├── usrCode/             # 业务代码
├── lvgl/                # LVGL 图形库
├── lv_drivers/          # 显示与输入驱动（fbdev/evdev）
├── lv_conf.h            # LVGL 配置（暗色主题、32 位色深）
├── lv_drv_conf.h        # 驱动配置
├── video/               # 测试视频（不纳入版本控制）
├── img/                 # 图片资源
├── Makefile             # 编译脚本
└── ARCHITECTURE.md      # 详细架构文档与函数调用图
```

## 相关文档

- [ARCHITECTURE.md](ARCHITECTURE.md) — 完整的函数调用关系图、线程模型、IPC 流程
- [LVGL 官方文档](https://docs.lvgl.io/)
- [MPlayer slave 模式文档](https://www.mplayerhq.hu/DOCS/tech/slave.txt)

## License

MIT License
