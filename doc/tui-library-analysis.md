# DAEMONITOR TUI 库选型分析

> 版本: v1.0.0  
> 日期: 2026-06-10  
> 项目: DAEMONITOR — C++20 Windows 硬件监控内核  
> 决策依据: 二进制体积最小化 / 后台资源占用最小化 / 高性能渲染 / TUI 图表能力

---

## 1. 需求背景

DAEMONITOR 是一个 Windows 原生 C++20 硬件监控内核，需要在终端中渲染实时硬件仪表盘：

- **控件类型**：柱状图 (CPU/RAM/GPU 使用率)、网络速率折线图、磁盘卷标+填充条、GPU 显存 gauge
- **刷新率**：每秒 1 tick，60-tick 交互循环
- **约束**：
  - 二进制体积 < 500 KB（最终 EXE）
  - 后台 CPU 占用 < 0.5%（tick 间隔外）
  - 不依赖 MSYS2/Cygwin 等 POSIX 模拟层
  - 允许使用汇编优化渲染热路径
- **色彩**：已有 canonical palette（`#33BCB7` / `#D9D9D9` / `#E87040` / `#0A2625` / `#2E160D` / `#2B2B2B`）

---

## 2. 候选库概览

| 属性 | **FTXUI** | **notcurses** | **ImTui** | **termbox2** |
|---|---|---|---|---|
| 仓库 | ArthurSonzogni/FTXUI | dankamongmen/notcurses | ggerganov/imtui | termbox/termbox2 |
| 许可证 | MIT | Apache 2.0 | MIT | MIT |
| Stars | 10,200+ | 4,600+ | 3,600+ | 720+ |
| 最新版本 | v6.1.9 (2025-05) | v3.0.17 (2025-10) | v1.0.5 (2021-04) | v2.5.0 (2024-12) |
| 活跃度 | ⭐ 高 (持续维护) | ⭐ 高 (持续维护) | ❌ 停更 (2021) | ⭐ 中 (偶尔更新) |
| 语言 | C++17/20 | C17 (C++ wrapper) | C++ (Dear ImGui) | C99 |
| 依赖数 | **0** (header-only 可选) | 多 (terminfo, libunistring, ffmpeg 可选) | libncurses (Windows: PDCurses) | **0** (单文件 header) |
| Windows 支持 | ✅ 原生 | ⚠️ Win10 v1093+ | ⚠️ MSYS2+MinGW | ✅ 原生 |

---

## 3. 逐库深度分析

### 3.1 FTXUI — ⭐ 推荐

```
仓库: github.com/ArthurSonzogni/FTXUI
版本: v6.1.9 (2025-05) / v7.0.0 (amalgamated)
语言: C++17, 支持 C++20 modules
构建: CMake FetchContent / 单头文件 amalgamated
```

**核心能力：**

| 功能 | 说明 |
|---|---|
| `graph()` | 折线图，子字符精度（2×2 block characters / cell） |
| `gauge()` | 4 方向 gauge（上/下/左/右），颜色可配 |
| `Canvas` | `DrawPointLine()` / `DrawPointCircle()` / `DrawPointCircleFilled()` / `DrawPointEllipse()` |
| `spinner` | 加载动画（多种样式） |
| `separatorHSelector` / `separatorVSelector` | 选择器控件 |
| Layout 引擎 | Flexbox 风格，自动布局 |
| 事件系统 | 键盘/鼠标/Timer 事件 |
| 颜色 | 16 色 + 256 色 + True Color (24-bit) |
| 渲染 | 基于 `Screen` 像素缓冲区，diff-only 刷新 |

**DAEMONITOR 适用度：**

- `graph()` 直接用于网络速率折线图 → 1 行代码
- `gauge()` 直接用于 GPU 显存占用 → 1 行代码
- 柱状图可用 `gauge(direction::right)` 或自定义 `Canvas`
- `hflow`/`vflow` 布局 CPU/RAM/GPU/Net/Disk 5 个面板
- 定时器事件驱动 `tick()` 调用，无需 `Sleep()` 轮询

**二进制体积：**

- amalgamated 单头文件编译 → ~80-120 KB (LTO + -Os)
- 无任何外部链接依赖

**潜在问题：**

- 模板较深，编译时间较纯 C 方案长约 2-3s（可接受）
- Windows Console 需要 `ENABLE_VIRTUAL_TERMINAL_PROCESSING`（已启用）
- True Color 需要终端支持；Windows Terminal / ConPTY ≥ Win10 14393

**集成方式：**

```cmake
# CMakeLists.txt
include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v6.1.9
)
FetchContent_MakeAvailable(ftxui)
target_link_libraries(DAEMONITOR PRIVATE ftxui::screen ftxui::dom ftxui::component)
```

---

### 3.2 notcurses — ❌ 过度设计

```
仓库: github.com/dankamongmen/notcurses
版本: v3.0.17 (2025-10)
语言: C17 (C++ wrapper 可用)
```

**核心能力：** 多媒体终端渲染（图像/视频/字体/QR 码）

**不适合的原因：**

| 因素 | 详情 |
|---|---|
| 依赖栈 | `terminfo` + `libunistring` → Windows 上需 MSYS2/MinGW |
| 二进制 | 链接后 EXE > 800 KB（仅核心库 ~500 KB） |
| 定位 | 面向多媒体终端应用，DAEMONITOR 只需要文本图表 |
| 复杂度 | API 约 200+ 函数，学习曲线陡峭 |
| 图表示例 | 无内置 graph/gauge，需手动用 `ncplane` 逐像素绘制 |

**结论：** 除非需要终端内嵌图片/视频，否则不适用。DAEMONITOR 的纯文本图表需求用 notcurses 是杀鸡用牛刀。

---

### 3.3 ImTui — ❌ 停更且依赖重

```
仓库: github.com/ggerganov/imtui
版本: v1.0.5 (2021-04) [停更 5 年]
语言: C++ (Dear ImGui backend)
```

**核心能力：** 将 Dear ImGui 渲染到终端

**不适合的原因：**

| 因素 | 详情 |
|---|---|
| 活跃度 | 最后 release 2021 年，无人维护 |
| Windows | 需要 PDCurses，只能通过 MSYS2+MinGW 编译 |
| 二进制 | Dear ImGui + libncurses 链接后 ~250 KB |
| 图表 | 需自行实现 ImGui 自定义绘图，无内置 chart |
| 复杂度 | 引入 Dear ImGui 的 retained-mode 范式，与 DAEMONITOR 的每帧轮询模式不匹配 |

**结论：** 依赖链过长、已停更、且以 GUI 范式做 TUI 在资源占用上无优势。不推荐。

---

### 3.4 termbox2 — ⚠️ 仅终端 I/O 层

```
仓库: github.com/termbox/termbox2
版本: v2.5.0 (2024-12)
语言: C99
```

**核心能力：** 最简终端抽象层

| 功能 | 说明 |
|---|---|
| 文件 | 单一 header: `termbox2.h` (~3500 行) |
| 依赖 | 仅 libc |
| 二进制增量 | ~15 KB |
| API | `tb_init()` / `tb_clear()` / `tb_set_cell()` / `tb_present()` / `tb_printf()` |
| 图表 | ❌ 无 |
| 布局 | ❌ 无 |
| 事件 | 键盘/鼠标/resize |

**DAEMONITOR 适用度：**

- ✅ 最轻量，零依赖
- ❌ 无 graph/gauge/bar 控件，需全部手写
- ❌ 无 diff 渲染（需手动追踪脏区域）
- ❌ 无自动布局

**结论：** 适合作为渲染后端，但 DAEMONITOR 需要在此基础上实现全部 chart 控件，工作量相当于自建微型 TUI 框架。不适合直接使用。

---

## 4. DIY Windows Console API 方案

当前 `main.cpp` 已在使用的方案：

| 优势 | 劣势 |
|---|---|
| 零依赖 | 手工实现 layout/bar/graph 控件 |
| 最小二进制 (< 20 KB 增量) | `system("cls")` 全屏刷新，闪烁 |
| 完全控制 | 无 diff 渲染，功耗高 |
| Windows 原生 | 需手动管理颜色/光标 |

**当前实现评估：**

- `print_bar()` 柱状图 → 可用
- 折线图 → 未实现
- 无 layout 引擎 → 手工坐标
- 刷新方式 → `system("cls")` + 全量重绘

**改进路径：** 可自行实现双缓冲 + diff 渲染，但意味着维护一个微型 TUI 引擎 → 偏离项目核心目标。

---

## 5. 方案对比矩阵

| 维度 | FTXUI | notcurses | ImTui | termbox2 | DIY Console API |
|---|---|---|---|---|---|
| 依赖数 | 0 | 3+ | 2+ | 0 | 0 |
| 二进制增量 | ~100 KB | ~500 KB | ~250 KB | ~15 KB | ~5 KB |
| 内置 graph | ✅ | ❌ | ❌ | ❌ | ❌ |
| 内置 gauge | ✅ | ❌ | ❌ | ❌ | ❌ |
| Canvas 绘图 | ✅ | ✅ | ❌ | ❌ | ❌ |
| Diff 渲染 | ✅ | ✅ | ❌ | ❌ | ❌ |
| Layout 引擎 | ✅ | ❌ | ❌ | ❌ | ❌ |
| True Color | ✅ | ✅ | ❌ | ❌ | ⚠️ VT 序列 |
| 事件系统 | ✅ | ✅ | ✅ | ✅ | ❌ |
| Windows 原生 | ✅ | ⚠️ | ❌ | ✅ | ✅ |
| 维护活跃 | ✅ | ✅ | ❌ | ⚠️ | — |
| 上手成本 | 低 | 高 | 中 | 中 | 中 |
| 自制控件量 | 0 | 全部 | 全部 | 全部 | 全部 |

---

## 6. 推荐方案: FTXUI

### 6.1 选择理由

1. **零依赖** — 不需要 MSYS2/PDCurses/libncurses，Windows 原生支持
2. **内置控件** — `graph()` 折线图、`gauge()` 填充条开箱即用，DAEMONITOR 需要 0 行自绘代码
3. **Diff 渲染** — 只刷新变化像素，CPU 占用极低（< 0.1%）
4. **C++ 原生** — 与项目 C++20 风格一致，支持 C++20 modules
5. **活跃维护** — 2025 年仍在发版，10.2k+ stars 社区验证
6. **二进制可接受** — amalgamated 单头 + LTO/-Os 约 80-100 KB
7. **支持 canonical palette** — True Color 模式直接使用 `#33BCB7` 等 hex 值

### 6.2 替代方案: termbox2 + 自绘

如果 FTXUI 的 100 KB 增量不可接受，可选用 termbox2 + 手写：

- 增量 ~15 KB
- 需实现：bar 图 (~30 行)、折线图 (~80 行)、双缓冲 diff (~50 行)、layout (~40 行)
- 合计额外工作量约 200 行，可接受但重复造轮子

### 6.3 最终建议

**第一优先：FTXUI** — 功能最全、上手最快、二进制增量可接受。

**第二优先：termbox2 + 自绘** — 如果对二进制体积有极致要求 (< 200 KB 总 EXE)，手写 200 行控件代码。

**不推荐：** notcurses (过重)、ImTui (停更)、DIY Console API (维护负担)。

---

## 7. FTXUI 集成计划

### 7.1 CMake 配置

```cmake
# 新增于 CMakeLists.txt
include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v6.1.9
)
FetchContent_MakeAvailable(ftxui)
target_link_libraries(DAEMONITOR PRIVATE ftxui::dom ftxui::screen ftxui::component)
```

### 7.2 架构设想

```
console-sys.cpp
├── render_header()          → text("DAEMONITOR") | bold | color(Color::Cyan)
├── render_cpu_panel()       → hbox( gauge(), text(percent) )
├── render_ram_panel()       → hbox( gauge(), text(percent) )
├── render_gpu_panel()       → hbox( gauge(), text(percent), text(VRAM) )
├── render_net_panel()       → graph( rx_history ), graph( tx_history )
├── render_disk_panel()      → vbox( [hbox(label, gauge()) ...] )
└── render_all()             → vbox( header, hflow(cpu,ram,gpu,net,disk) )
```

### 7.3 色彩映射

```cpp
// canonical → FTXUI
ftxui::Color cpu_active   = ftxui::Color::RGB(0x33, 0xBC, 0xB7);  // #33BCB7
ftxui::Color ram_active   = ftxui::Color::RGB(0xD9, 0xD9, 0xD9);  // #D9D9D9
ftxui::Color gpu_active   = ftxui::Color::RGB(0xE8, 0x70, 0x40);  // #E87040
ftxui::Color dim_cyan     = ftxui::Color::RGB(0x0A, 0x26, 0x25);  // #0A2625
ftxui::Color dim_red      = ftxui::Color::RGB(0x2E, 0x16, 0x0D);  // #2E160D
ftxui::Color dim_gray     = ftxui::Color::RGB(0x2B, 0x2B, 0x2B);  // #2B2B2B
```

### 7.4 预期二进制增量

| 场景 | EXE 大小 |
|---|---|
| 当前 (DIY Console API) | ~120 KB |
| + FTXUI (debug, -O0) | ~2.5 MB |
| + FTXUI (release, -Os, LTO) | ~220-260 KB |
| + FTXUI (release, -Os, LTO, amalgamated) | ~200-240 KB |

---

## 8. 结论

**FTXUI 是 DAEMONITOR 项目的最佳 TUI 库选择。** 它在零外部依赖的前提下提供了 graph/gauge/Canvas 等开箱即用的图表控件，diff 渲染保证低资源占用，True Color 支持完美适配项目的 canonical palette，且维护活跃、社区成熟。预计增量 100 KB 在项目 500 KB 目标内有充足余量。

---

*本文档由 DAEMONITOR 项目组维护。最后更新: 2026-06-10*
