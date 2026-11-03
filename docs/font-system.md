# MangoOS 字体系统规划

> 状态:规划中,已定案为「MangoWin 图形化后置阶段」(仅文档,不涉及任何代码改动)
> 动机:先明确目标与边界,再决定是否开工
> 关联:图形化设计见 `docs/graphics-design.md`(本规划在 MangoWin 落地后实施)

## 1. 背景与现状

### 1.1 当前实现

| 文件 | 内容 |
| --- | --- |
| `kernel32/font16.c` | 8x16 VGA 点阵字体,256 个字形(CP437),来源 SeaBIOS `vgafonts.c`(公有领域),共 4096 字节,格式 `font8x16[256*16]`,每字形 16 行、每行 8bit(高位为左) |
| `kernel32/video.c` | `video_char(x, y, ch, fg, bg)` — 以**像素坐标** (x,y) 为基准,逐像素把字形画到 LFB(fg/bg 纯色,无透明/无缩放) |
| `kernel32/vga.c` | 文本模式(80x25,0xB8000)独立的字符/光标路径,与 LFB 字体无关 |

### 1.2 已知限制

- 只有 8x16 一种尺寸,字形 1bit(单色)
- `video_char` 无返回值、无边界裁剪(像素可能越界写坏相邻区域)
- 无中文/Unicode 支持(CP437 上限 256 字符)
- 无背景透明、无反色/下划线/加粗等属性
- 无字形缓存、无缩放;字符终端(vga.c)与图形字体两套体系并存

## 2. 规划目标(候选,待定级)

1. 统一字符渲染路径(终端在图形模式下走同一套字体)
2. 支持多字号(8x16 / 8x8 / 16x16 / 任意缩放)
3. 支持 UTF-8 解码与基础 CJK 字形(中文字库体积大,需评估)
4. 字体数据外置(从磁盘加载,而非全部编译进内核)
5. 渲染属性:前景/背景色、透明背景、光标块、下划线

> 阶段关系:MangoWin 图形化(见 `docs/graphics-design.md §7`)完成后,再进入本规划实施;MangoWin 初期复用现有 8x16 字形(`font16.c`),不阻塞 GUI 进度。

> 目标需要排序:是「够用」还是「够好」,直接影响工作量。

## 3. 关键决策点(开工前必须确认)

| # | 决策 | 选项 | 影响 |
| --- | --- | --- | --- |
| D1 | 字形尺寸集合 | A. 仅 8x16(维持现状) B. 8x8+8x16 C. 8x16+16x16 | 字库体积、缩放逻辑复杂度 |
| D2 | 是否引入中文 | A. 不支持 B. GB2312 常用字(约 3-8K 字) C. 全 Unicode | 字库体积从 4KB 增至数 MB;内存与磁盘规划变化大 |
| D3 | 字体存放位置 | A. 编译进内核(现状) B. MangoFS 磁盘文件 + 运行时加载 | B 需要文件系统+加载器+缺字兜底 |
| D4 | 与 vga.c 文本模式关系 | A. 保留双轨 B. 图形模式专用 C. 图形模式统一终端 | 影响所有现有输出函数 |
| D5 | 目录与命名 | 见 §4.1(子系统分目录 + `font_` 前缀,非 `MangoOSAPI` 风格) | 影响后续所有模块 |
| D6 | 存量模块是否同批归位 | A. 仅新模块入子目录(推荐) B. 一并迁移现有模块 | A 改动面小,不阻断本次字体工作 |

## 4. 候选方案

### 4.1 目录结构与命名规约(模块化对齐)

**原则:与 Mango OS API 的模块化方式看齐——模块 = 文件主题,函数 = `前缀_动作` 小写下划线风格;目录按子系统分组,不把全部模块平铺塞进单一目录;不引入 `MangoOSAPI` / `MA_` / `mango_os_api_` 之类的长命名空间。**

当前既有模块的对照基准(现状为平铺,规划目标按子系统分组):

| 模块 | 现状文件 | API 前缀 | 规划归属子系统 |
| --- | --- | --- | --- |
| 视频 | `video.c` | `video_` | `console/`(显示/终端) |
| 文本 | `vga.c` | `vga_` | `console/` |
| 字体(规划) | `font16.c` → `font.c/h` | `font_` | `console/` |
| 文件系统 | `mangofs.c` | `fs_` | `storage/` |
| 磁盘 | `disk.c` | — | `storage/` |
| 键盘 | `keyboard.c` | `kbd_` | `input/` |
| 鼠标 | `mouse.c` | `mouse_` | `input/` |
| 串口 | `serial.c` | `serial_` | `misc/`(或 `driver/`) |
| 时钟 | `rtc.c` | `rtc_` | `misc/`(或 `driver/`) |
| 靴/main | `entry.asm` / `kernel.c` | — | `core/` |
| shell | `shell.c` | — | `shell/` |

**规划目标目录树**(示意,命名待定级):

```
kernel32/
  core/       entry.asm  kernel.c  linker.ld
  console/    vga.c  video.c  font.c  font.h   <- 字体归此处
  input/      keyboard.c  mouse.c
  storage/    disk.c  mangofs.c
  driver/     serial.c  rtc.c            (或并入 misc/)
  shell/      shell.c
```

**关于字体模块的落地:**

- **目录**:新建 `kernel32/console/` 子系统目录,字体 `font.c/font.h` 与 `video.c/vga.c` 同组,不直接平铺到 `kernel32/` 根部
- **命名空间**:`font_` 前缀(`font_init` / `font_load` / `font_glyph` / `font_char` / `font_size`);不采用长前缀或全局 `mango_` 品牌式命名
- **头文件**:`font.h` 提供公开接口声明与字形描述符结构体;既有模块的 `extern` 风格保留作为兼容手段,新接口一律走头文件
- **过渡策略(防过度工程)**:采用「新目录先行、存量逐步归位」——字体作为新模块直接落在 `console/`;现有 `video.c/vga.c` 等可后续再搬,不在本项目一次性大迁移
- **反例(避免)**:`MangoOSAPIFontChar`、`mango_os_api_font_*`、无节制的子目录堆叠(`kernel/font/engine/raster/…`)

### 方案 A:最小改动(推荐先做)
- 保留 8x16 单色字形,仅补齐:`video_char` 边界裁剪、背景透明 flag、简单缩放(整数倍)
- 工作量小,风险低,不改变现有架构
- 落地时按 §4.1:新能力以 `font_` 前缀收敛到 `font.c/font.h`,`video_char` 退化为薄封装

### 方案 B:统一终端
- 新建 `terminal.c`,图形模式下文本输出统一走字体渲染
- vga.c 只保留开机早期文本模式;后期 shell/串口输出全部走新终端
- 工作量中等,触及 shell/video/kernel 多处

### 方案 C:多字号 + 磁盘字库
- 新增字库文件格式(头部 + 字形表),MangoFS 加载
- 支持 UTF-8 解码(ASCII 直查 8x16,CJK 查扩展字库)
- 工作量最大,需要先完成磁盘读写(已具备)与缺字策略

## 5. 实施路线(仅规划,不执行)

```
阶段 0  整理现状:现有输出路径盘点(已完成,见 1.1)
阶段 1  定型:按 §4.1 确定 font.c/font.h 骨架与 font_ 前缀接口(只建桩,不写实现)
阶段 2  小修:video_char 裁剪 + 透明背景 + 整数倍缩放          (可选,半天)
阶段 3  定义 font 模块接口 + 数据结构(字形描述符/属性)
阶段 4  终端统一(若选方案 B)
阶段 5  字库外置 + UTF-8(若选方案 C)
阶段 6  验收:OCR 探针 + 像素级回归(沿用现有 probe 工具链)

前置条件:MangoWin(Mango OS 图形化)落地,见 docs/graphics-design.md §7
```

## 6. 风险与注意

- 中文字库体积与镜像大小(当前 disk.img 16MiB,FS 约 32K 扇区)——需先测算
- `video_char` 当前被 shell/绘制代码依赖,接口变更需同步
- 字体是性能热点(纯软件逐像素),整数倍缩放与缓存策略要提前定
- 与已有 screendump / OCR 探针的验证口径保持一致

## 7. 待办(待用户拍板)

- [x] 确认 D1-D6 的选择(字体后置,复用 8x16 起步)
- [ ] 确认目标优先级(够用 vs 够好)
- [ ] 确认是否开工(本文档仅为规划;预计在 MangoWin 落地后) 
