# Mango OS Image Builder（芒式操作系统镜像构建器）规划与 TODO

> 全称：**Mango OS Image Builder**（芒式操作系统镜像构建器），简称 **MIB**。
> 状态：规划中（本文件仅规划与设计，不涉及开发）。
> 定位：**全新设计的镜像构建器**，不是现有 `tools/build_img.ps1` 的移植或封装。
> 现有 PowerShell 脚本仅作为"行为兼容的回归基准"存在，不是新构建器的设计来源。
> 语言：**Go**（选型依据见 §3）。

## 1. 目标与设计原则

MIB 是 MangoOS 的官方镜像构建工具：把一个镜像的**所有信息**集中在一份配方
（recipe）里，由构建器完成布局规划、内容组装、校验与产物发布。

设计原则：

1. **配方即唯一事实源（Single Source of Truth）**：镜像大小、组件、顺序、
   FS 内容全部来自配方；构建器代码不硬编码任何布局。
2. **组件模型**：镜像 = 配方描述的组件集合（stage1/stage2/kernel/FS/裸文件），
   每个组件有类型、来源与放置约束；新增组件类型不改布局引擎。
3. **校验是一级功能**：verify 不是事后补丁，而是与 build 同级的第一公民子命令，
   构建流程默认"写入即校验"。
4. **可复现构建**：同一配方 + 同一输入 → 逐字节相同的镜像（固定时间戳、
   确定性 FS 布局）。
5. **兼容是契约不是设计**：引导链的 BootMap v2 等外部接口是**冻结契约**，
   MIB 严格遵守；契约之外可以自由设计。
6. **轻量且完整**：轻量指工具本身（单二进制、零运行时依赖、秒级构建镜像），
   完整指能力（一个轻量工具即可产出完整可启动的 MangoOS 镜像，含
   MangoFS 预格式化与预置文件）。

## 2. 外部契约（冻结，不可变更）

新构建器必须满足的引导链接口。这些不是设计选项，是硬约束。

### 2.1 镜像布局（16 MiB HD 基准）

| 位置 | 内容 |
|---|---|
| LBA 0 | stage1 引导扇区 |
| LBA 1 | BootMap v2 |
| LBA 2 .. | stage2（扇区对齐） |
| 其后 | KERNEL.BIN（raw blob，不走文件系统） |
| 其后 | MangoFS v2 区域（其余扇区） |

### 2.2 BootMap v2（LBA 1，512B）

| 偏移 | 字段 |
|---|---|
| +0 | `'MBOOT'` |
| +4 | `'T'`（占位） |
| +5 | 0x02 = BootMap 版本 2 |
| +6 | 0x01 = stage2 加载器版本 |
| +7 | stage2_lba (dword) |
| +11 | stage2_sec (dword) |
| +15 | fs_base（FS 区域起始 LBA）(dword) |
| +19 | kernel_lba (dword) |
| +23 | kernel_sec (dword) |

### 2.3 MangoFS v2（内核侧格式，mangofs.c 为权威实现）

```
fs_base            superblock（1 扇区）
fs_base+1 ..       bitmap（每扇区 1 bit，覆盖整个区域）
其后               root 目录（2 扇区，32 × 32B 表项）
其后               data 区
```

Superblock：`"MFSV2   "`、version=2、bitmap_lba/root_lba/root_sectors/data_lba
(word)、total_sectors/free_sectors (dword)。目录项：8.3 布局 32B。

内核侧常量（当前硬编码）：`DISK_TOTAL=32768`（16MiB）、`FS_MAX_FILES=32`、
`FS_MAX_FILE_SIZE=16384`、root 2 扇区 —— 属于**待解耦耦合点**（见 §7）。

## 3. 编程语言规划（Go）

### 3.1 选型准则

MIB 定位"轻量工具构建完整镜像"，语言必须同时满足：

1. **轻量**：产物为单个静态二进制，无解释器/运行时依赖；启动毫秒级；
   内存占用小；
2. **完整能力**：镜像构建所需的 JSON 配方解析、二进制块读写、SHA-256 校验、
   zip 打包、命令行参数——标准库原生覆盖，不引第三方依赖；
3. **跨平台一致**：Windows / MSYS2 / Linux 行为一致，交叉编译零配置；
4. **可维护**：强类型 + 结构体直接映射配方 schema，非法配方在编译期/解析期
   即可被类型系统约束；错误处理显式；
5. **增量集成**：与 makefile 协作时工具自身编译为秒级，不拖慢主构建。

### 3.2 候选对比

| 维度 | Go | Python 3 | C |
|---|---|---|---|
| 产物形态 | 单静态二进制 | 需解释器 | 单二进制 |
| 运行时依赖 | 无 | Python 环境 | 无 |
| 标准库能力 | JSON/zip/sha256/二进制 I/O 全有 | 全有 | 无（需手写/引库） |
| 启动/构建速度 | 秒级 | 秒级（解释） | 毫秒级 |
| 开发量 | 中 | 小 | 大（JSON 解析最耗时） |
| 工具链 | 需安装 Go（仅建工具时） | 已有（rom2c.py 同栈） | 已有（跨工具链） |
| 类型安全 | 强 | 弱 | 中 |

结论：Go 在"轻量（单二进制零依赖）"与"完整能力（标准库全覆盖）"两个
准则上同时最优；Python 的轻量是代码量而非产物，C 的能力需要额外开发量。

### 3.3 落地要点

- **模块布局**：`tools/mib/` 独立 Go 模块（`go.mod`，module 名
  `mango/mib`），与内核代码零耦合；
- **依赖策略**：仅 Go 标准库（`encoding/json`、`archive/zip`、
  `crypto/sha256`、`hash/crc32`、`os`、`flag`、`fmt`、`io`），
  锁定 go.mod，不引第三方；
- **交叉编译**：`GOOS=windows|linux GOARCH=amd64 go build` 产出
  `mib.exe` / `mib` 单文件；构建脚本固化在 `tools/mib/Makefile` 或
  `tools/mib/build.sh`；
- **与 makefile 集成**：`$(MIB)` 由 `go build` 增量产出（秒级），
  `$(DISK_IMG)` 规则先构建工具再执行 `mib build recipe.json`；
- **分发**：单文件可拷至任意机器直接运行；发布物附带 SHA-256 清单
  （§5 Phase 6）；
- **版本锁定**：go.mod 提交入库；升级 Go 版本在 CI 显式验证。

### 3.4 风险与对策

| 风险 | 对策 |
|---|---|
| 构建机需新装 Go 工具链 | 仅"构建 MIB"需要；产物不依赖 Go；MSYS2 用 `pacman -S mingw-w64-ucrt-x86_64-go`，文档记录一条命令 |
| 交叉编译目标平台差异 | 首版仅 Windows + Linux 两个产物；CI 各跑一次字节回归 |
| Go 版本漂移 | go.mod 锁定；升级显式验证 |

## 4. 新方案架构设计

### 4.1 总体架构

```
┌───────────────────────────── 配方 recipe.json ─────────────────────────────┐
│ target: 大小/扇区/目标类型 │ components: [stage1, stage2, kernel, fs, raw] │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   ▼
┌────────────────────────── MIB 核心管线 ──────────────────────────┐
│ 1. 解析器     配方 schema 校验（未知字段/缺字段报错）              │
│ 2. 布局引擎   顺序分配 region：对齐、填充、边界/重叠检查            │
│ 3. 组件装配   blob 读取 → 组件实例（含 FS 创作模块）               │
│ 4. 写入器     零填充 + 定位写入 + 尾部处理                         │
│ 5. 校验器     回读验证 BootMap/魔数/哈希/布局一致性                │
│ 6. 报告器     manifest.json + 人类可读布局日志                     │
└────────────────────────────────────────────────────────────────────┘
                                   ▼
                      disk.img / manifest.json / report
```

### 4.2 CLI 设计

```
mib <command> [options]

  mib init   <dir>       生成配方骨架 + 示例（脚手架）
  mib build  recipe.json  [--out disk.img] [--no-verify] [--report]
  mib verify recipe.json  [--img disk.img] 回读校验（含哈希清单）
  mib inspect recipe.json [--img disk.img] 只读布局/内容清单
  mib package recipe.json 产物打包（zip + SHA-256 清单）
```

build 默认行为 = 构建 + 写入后自动 verify（失败即非零退出）。

### 4.3 配方（recipe.json）草案

```jsonc
{
  "target": {
    "name": "mango-hd",
    "size_sectors": 32768,          // 16 MiB；不再从代码里猜
    "bootmap_version": 2,
    "stage2_loader_version": 1
  },
  "components": [
    { "type": "stage1",   "src": "image/stage1.bin" },
    { "type": "bootmap" },                          // 由布局引擎填充
    { "type": "stage2",   "src": "image/stage2.bin" },
    { "type": "kernel",   "src": "image/kernel.bin" },
    { "type": "mangofs",                            // 预格式化 + 预置
      "files": [
        { "src": "misc/help.txt", "name": "HELP   TXT" },
        { "src": "misc/welcome.txt", "name": "WELCOME TXT" }
      ]
    },
    { "type": "raw",      "lba": 1000, "src": "misc/bootlogo.bin" }
  ]
}
```

要点：

- `target` 定义镜像级属性；`components` 决定内容与放置；
- 未指定 `lba` 的组件由布局引擎**顺序分配**；指定 `lba` 的组件支持
  精确放置（覆盖检查由引擎负责）；
- `mangofs` 组件是 MIB 独有的**内容创作**单元（见 4.5）；
- `bootmap` 组件由引擎生成，字段来自 target + 实际布局结果。

### 4.4 组件模型

| 类型 | 来源 | 放置 | 说明 |
|---|---|---|---|
| stage1 | 文件 | LBA 0（固定） | 引导扇区 |
| bootmap | 生成 | LBA 1（固定） | BootMap v2，布局后回填 |
| stage2 | 文件 | 顺序 | 扇区对齐 |
| kernel | 文件 | 顺序 | raw blob |
| mangofs | 生成 + 文件 | 顺序 | FS 创作模块：格式化 + 预置文件 |
| raw | 文件 | 指定 lba / 顺序 | 通用裸数据（引导徽标等） |

新增组件类型 = 新增一个装配器函数，布局引擎与写入器零改动。

### 4.5 MangoFS 创作模块（与内核 mkfs 同构）

- 在 MIB 内实现 MangoFS v2 格式化器：superblock / bitmap / root / data 写入；
- 预置文件按配方放入 data 区并登记目录项（受内核上限约束，见 §7）；
- 确定性：目录项时间戳用配方给定的固定基准（默认 2000-01-01），
  保证同输入同输出；
- **回归锁定**：以"固定输入 → 内核 mkfs 结果字节相等"为验收标准，
  防止双实现漂移。

### 4.6 布局引擎规则

1. 顺序分配：按组件顺序分配 region，每 region 对齐到扇区边界；
2. 精确放置：`lba` 指定的组件插入固定位置，与相邻 region 做重叠检查；
3. 边界检查：任何 region 不得越出 `target.size_sectors`；
4. 填充：region 尾部补零；组件间空隙记录到报告（可告警）；
5. 结果物：每个 region 的 {lba, sec, offset, size, sha256} 写入 manifest.json，
   verify 时回读比对。

### 4.7 校验子系统

- BootMap：魔数、版本、各字段与布局结果一致性；
- 内容：每个组件回读哈希 == 写入前哈希；
- 边界：region 不重叠、不越界、镜像尾部队齐；
- 报告：通过/失败逐项列出，失败非零退出。

### 4.8 可复现构建

- 所有时间戳类字段由配方固定值决定（构建不读系统时钟）；
- 输出不包含绝对路径、构建机器信息；
- CI 场景可用 `--report` 生成 manifest 并入库比对。

## 5. 分阶段规划

### Phase 0 — 需求确认
- [x] 现状约束梳理（外部契约 §2）
- [x] 语言选型（Go，§3）
- [ ] 配方草案评审（§4.3）
- [ ] 确认目标范围：软盘 1.44MB 目标是否保留、预置文件上限预期

### Phase 1 — 新架构骨架（纯新代码，不移植）
- [ ] `tools/mib/` Go 模块搭建（go.mod + 交叉编译脚本）
- [ ] CLI 骨架（build 子命令优先）
- [ ] 配方解析 + schema 校验（Go 结构体映射）
- [ ] 布局引擎（顺序分配 + 对齐 + 边界/重叠检查）
- [ ] 写入器（零填充 + 定位写入）+ 报告器（manifest.json）
- [ ] 兼容回归：对同一批输入，MIB 产物与 build_img.ps1 产物逐字节一致；
      一致后 build_img.ps1 退役（保留在 git 历史）
- [ ] makefile：`$(MIB)` 增量构建 + `$(DISK_IMG)` 规则接入 `mib build`

### Phase 2 — 校验子系统
- [ ] `verify` 子命令（BootMap / 哈希 / 边界三项）
- [ ] build 默认写入后自动 verify
- [ ] `inspect` 子命令（只读布局报告）
- [ ] 失败退出码规范（1=输入错，2=布局错，3=校验失败）

### Phase 3 — MangoFS 创作模块
- [ ] MIB 侧 MangoFS v2 格式化器（与内核 mkfs 同构）
- [ ] 预置文件写入 + 目录项登记 + 固定时间戳
- [ ] 内核侧配合：`fs_init` 改为"superblock 有效则挂载，否则 mkfs"
- [ ] 字节相等回归 + QEMU 实测（`dir`/`type` 可见预置文件）

### Phase 4 — 多目标与常量解耦
- [ ] 1.44MB 软盘目标（BootMap 不变，stage1/stage2 兼容性评估）
- [ ] 16/32/64MB 镜像（内核 `DISK_TOTAL`/fs 区域读取改为来自 BootMap/bootinfo）
- [ ] `FS_MAX_FILES`/`FS_MAX_FILE_SIZE` 上限核对与必要放宽

### Phase 5 — 可复现构建
- [ ] 时间戳确定性验证（连续两次 build 输出相同）
- [ ] manifest 入库比对流程（CI 支持）

### Phase 6 — 发布
- [ ] `package` 子命令（zip + SHA-256 清单）
- [ ] 文档（用法、配方参考、迁移说明）

## 6. 技术决策记录

| 决策 | 选择 | 理由 |
|---|---|---|
| 语言 | Go，仅标准库 | 单二进制零依赖 + 标准库 JSON/zip/哈希全覆盖（§3） |
| 配方格式 | JSON（首版），JSONC 后议 | 标准库可解析；结构即文档 |
| 布局来源 | 配方 + 引擎，代码零硬编码 | 单一事实源原则 |
| 与旧脚本关系 | 仅回归基准，退役后移除 | 新架构不受旧实现约束 |
| 校验时机 | build 默认内置 verify | 校验是第一公民 |
| 时间戳 | 配方固定基准 | 可复现构建 |
| 双实现风险 | 字节相等回归锁定 | 防 MangoFS 漂移 |

## 7. 耦合点与风险

1. **内核硬编码常量**：`DISK_TOTAL`、`FS_MAX_FILES`、`FS_MAX_FILE_SIZE`、
   root 扇区数 —— Phase 4 解耦（改从 BootMap/bootinfo 读区域信息）。
2. **MangoFS 双实现**：MIB 格式化器 vs 内核 mkfs —— 用字节相等回归锁死。
3. **引导链契约**：BootMap v2 字段冻结；任何引导链改动须同步 bump
   版本字段并在配方声明。
4. **预置文件上限**：Phase 3 受 `FS_MAX_FILES=32` / `FS_MAX_FILE_SIZE=16384`
   约束；超限场景列为 Phase 4 工作项。
5. **兼容回归窗口**：Phase 1 期间两脚本并存，`make` 只调用 MIB；
   对比脚本手动执行，不进入 makefile。
6. **Go 工具链引入**：仅构建 MIB 时使用，产物零依赖；安装命令写入文档
   （MSYS2: `pacman -S mingw-w64-ucrt-x86_64-go`）。

## 8. TODO 汇总

- [ ] Phase 0：配方草案评审 + 目标范围确认
- [ ] Phase 1：Go 模块 / CLI / 配方解析 / 布局引擎 / 写入器 / 字节级回归 / makefile 接入
- [ ] Phase 2：verify + inspect + 退出码规范
- [ ] Phase 3：MangoFS 创作模块 + 内核挂载优先
- [ ] Phase 4：多目标镜像 + 内核常量解耦
- [ ] Phase 5：可复现构建验证
- [ ] Phase 6：package 发布 + 文档

> 每个 Phase 完成并验收后再进入下一个；验收标准见各节。