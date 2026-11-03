# MangoOS 图形化设计(MangoWin)规划

> 状态:**已实现并交付**。本文档是原始设计决策记录,代码见 `kernel32/gui*.c`
> 与 `kernel32/gui.h`。下方里程碑已全部完成,并且实际实现超出了本文的
> 规划范围——额外增加了任务栏、开始菜单、桌面图标、基于变化检测的脏矩形
> 刷新(`render.c` + `video_blit_rect_cmp*`),这些均未出现在原设计中。
>
> **未采纳的部分**:§5 提出的目录重组(`core/ console/ input/ storage/ misc/
> shell/`)没有执行。模块仍平铺在 `kernel32/` 下,只是各自补齐了头文件
> (`vga.h` `video.h` `serial.h` `keyboard.h` `mouse.h` `disk.h` `mangofs.h`
> `time.h` `random.h`),跨模块符号一律走头文件而不再就地 `extern`。
>
> 前置依赖:无(字体系统为后置阶段,本设计复用现有 8x16 字形)

## 1. 定位与兼容性红线

- GUI(MangoWin)是与 CLI console **并列的第二运行模式**;系统开机默认仍是命令行,与现状完全一致
- **唯一入口**:CLI 命令 `mangoWin`;退出(Esc / 窗口关闭按钮)后回到 CLI
- 现有 CLI、串口注入、screendump/P6+OCR 验证流程零影响
- 目录/命名延续 `docs/font-system.md §4.1` 模块化约定:归属 `console/` 子系统,`gui_` 前缀,不引入长命名空间

## 2. 定案决策

| # | 决策 | 定案 | 说明 |
| --- | --- | --- | --- |
| G1 | 进入命令 | `mangoWin` | CLI 唯一入口 |
| G2 | 退出方式 | Esc 键 / 串口 exit 直接退出;关闭按钮只关单个窗口,全部窗口关闭后才退回 CLI | 关闭按钮需鼠标(见 G5) |
| G3 | 串口 | 注入式 | 与 CLI 一致,可注入 GUI 命令 |
| G4 | 双缓冲 | 支持 | 静态 backbuffer 800x600x4 ≈ 1.92MB |
| G5 | 鼠标 | 改造 mouse 模块 | 像素级指针融入 mouse.c 本体 |
| G6 | 字体 | 后置阶段 | 本阶段复用现有 8x16(font16.c) |

## 3. 运行模式与状态恢复(兼容性核心)

```
CLI(console)  --mangoWin-->  MangoWin(GUI)
   ^                           |
   +-- Esc / 串口 exit / 全部窗口关闭 --+
```

- **进入**:保存 console 快照 —— `vga_row/vga_col`、`vga_attr`、`fb_cell/fb_cattr`(80x25x2 = 4KB)、光标状态
- **GUI 期间**:console 数据不动(GUI 全部绘制到独立 backbuffer,提交时才碰 LFB)
- **退出**:从快照完整恢复 console,逐格重绘,断言与进入前一致(验收项)
- 输入三通道:键盘 → GUI 键事件;鼠标 → 指针+点击事件;串口 → GUI 命令注入

## 4. 架构

### 4.1 目录与命名

```
kernel32/
  core/       entry.asm kernel.c linker.ld
  console/    vga.c  video.c  gui.c  gui.h    <- GUI 与字体同组(字体后置)
  input/      keyboard.c  mouse.c
  storage/    disk.c  mangofs.c
  misc/       serial.c  rtc.c
  shell/      shell.c
```

- 新模块:`console/gui.c` + `gui.h`(公开接口 + 事件/窗口描述符)
- 前缀:`gui_`(`gui_enter` / `gui_exit` / `gui_event_loop` / `gui_window_create` / `gui_window_close` / `gui_flip` …)

### 4.2 模块职责

| 模块 | 职责 | 接口示例 |
| --- | --- | --- |
| `video.c` | 帧缓冲原语(现有) | video_pixel/video_rect/video_line/video_clear |
| `vga.c` | console 终端(现有) | 快照由 GUI 直接读取其内部状态 |
| `font16.c` | 8x16 字形(现有,后置替换为 font 模块) | video_char |
| `gui.c`(新) | 窗口系统:事件循环、窗口(标题栏+内容区+关闭按钮)、遮挡重绘、双缓冲提交 | gui_* |
| `mouse.c`(改) | 文本/像素双模式指针 | mouse_set_mode/mouse_get_pos | 

### 4.3 双缓冲

- 静态 backbuffer:`unsigned int back[800*600]`(=1.92MB,启动期预留,bss 段)
- 流程:窗口绘制 → backbuffer;`gui_flip` 将整帧拷贝至 LFB(期初整帧,后续脏矩形优化)
- 退出 MangoWin 时丢弃 backbuffer 内容,以 console 快照重绘恢复

### 4.4 窗口模型(初期)

- 窗口 = 矩形:标题栏(1 行文字)+ 内容区 + 右上角关闭按钮(16x16 区域)
- 遮挡:初始 z-order 递增式重绘(后画覆盖先画),剪裁优化后置
- 事件:`gui_ev_key` / `gui_ev_mouse` / `gui_ev_close` / `gui_ev_cmd`(串口注入)

## 5. mouse 模块改造方案

- 保留现有文本光标路径(0xB8000)作为 text 模式
- 新增**像素模式**:`mouse_set_mode(0=text, 1=pixel)`;像素模式输出坐标+按钮事件供 GUI 使用
- PS/2 解析共享(`mouse_poll` 不变),双模式互斥切换
- MangoWin 进入 → pixel 模式;退出 → 恢复 text 模式并清除像素指针

## 6. 串口注入

- CLI 语义沿用(现有 `serial_getc` 通道)
- MangoWin 内命令语法:`gui <cmd> ...`,命令集实现期定义(如 `gui win <x> <y> <w> <h> <title>`、`gui exit`)
- 保证无鼠标环境可注入退出,兼作自动化验收通道

## 7. 实施路线

```
M0 骨架    gui.c/h 空壳;mangoWin/退出命令;console 快照+恢复;双缓冲 flip
M1 窗口    窗口创建/销毁;标题栏+内容区+关闭按钮;遮挡重绘
M2 输入    mouse 像素化改造;点击分发;关闭按钮关单个窗口,全部关闭后退出;Esc 直接退出
M3 串口    GUI 命令注入通道
M4 验收    screendump/P6+OCR;CLI 进出前后逐格一致性断言
后续      字体系统(font-system.md)作为独立后置阶段
```

## 8. 风险与注意

- backbuffer 1.92MB 静态占用:内存规划需确认(现 16MB 镜像/VM,应无压力,但需在文档留档)
- console 快照恢复完整性(attr/光标/滚屏状态)是兼容性成败点,验收必须逐格断言
- 鼠标双模式切换若在 PS/2 解析中引入分支,需防文本模式行为漂移(回归:现有 mouse 命令)
- GUI 无时钟/后台任务,事件循环为纯轮询;资源(窗口对象)数量静态上限

## 9. 待办与开放项

- [ ] 定案 `gui <cmd>` 注入命令集
- [ ] 确认 backbuffer 内存预算落点
- [ ] 窗口遮挡策略实现期裁定(先重绘后剪裁)
- [ ] 开工(本文档仅为规划,待本轮文档评审通过)