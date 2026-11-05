# 桌面洞洞板（desk.exe）

> 一个进程、一块板、若干张可自由摆放的模块卡片。本文是模块作者和排错的主要文档；
> 视觉/交互参考见 [README](../README.md)。

## 1. 设计原则

- **板是容器，模块是卡片**：不在一个面板里硬塞所有内容；每张卡有自己的标题条、
  尺寸、刷新节奏和设置。
- **一个进程**：一个分层窗口、一张 DIB、一个定时器、一个托盘、一套热键。
  卡片是板内矩形，不是独立 HWND。
- **自由但有边界**：卡片在板内自由拖动，8px 吸附；松手遇到重叠会落到最近空位。
- **视觉统一**：模块只画 body，板负责外框/标题条/间距/状态色；所有排版走
  `src/widget/skin.*`。
- **不整窗穿透**：卡片和控件可点；没有卡片的空白点交给桌面。
- **置底**：始终压在普通窗口之下，点击不抢前台焦点。

## 2. 运行结构

```
desk.exe
└─ Board（src/widget/board.cpp）一个 wg.desk 分层窗口
   ├─ BoardChrome：tint + 16px 点阵 + 边框 + 标题/拖动手柄 + 缩放把手
   ├─ 卡片层：标题条 30px + body + 10px 底边距
   ├─ 调度：1 个 SetTimer 对齐最近到期；每模块最多 1 个 worker
   ├─ 命中：卡片/标题条/缩放手柄/Add 槽 → HTCLIENT
   │        空白点 → HTTRANSPARENT（桌面可点；board.solid=1 可关）
   ├─ 菜单/托盘/热键/持久化/自启
   └─ Module 注册表（src/apps/desk/main.cpp）
       ├─ AccountModule（Account 卡）
       └─ ServersModule（Servers 卡）
```

数据流与旧 `WidgetT` 相同：`LoadData` 一次；`RefreshData` 在 worker 只写
staging；`Commit` 在 UI 线程 swap；`PaintBodyOf` 只读 shown；Board 保证
refresh 与 paint 不同时执行，所以模块代码不需要锁。

## 3. 交互

| 操作 | 行为 |
|---|---|
| 拖卡片标题条 | 移动卡片；按 `board.grid`（默认 8px）吸附；松开避让 |
| 点标题条右侧 28px | 切换 `expanded`；展开后高度增加，位置尽量保持 |
| 拖板标题条 | 移动整块板（限制在工作区） |
| 拖右下角 | 缩放板（最小 400×420，最大工作区） |
| 左键 Add 槽 | 打开板菜单 |
| 右键卡片 | 模块菜单 + Details / Refresh / Remove card |
| 右键板标题条 / 托盘 | Add module / Tidy / Refresh all / Show grid / Empty area passes clicks / Start with Windows / Exit |
| `Ctrl+Alt+W` | 板菜单 |
| `Ctrl+Alt+R` | 刷新所有可见卡片 |
| `Ctrl+Alt+D` | 全部展开/收起 |
| `Ctrl+Alt+Q` | 退出 |
| 托盘双击 | 全部展开/收起 |

**空白板面不接收右键**（HTTRANSPARENT），所以板菜单放在标题条/托盘/Add 槽上；
`board.solid=1` 时整板接收点击，空白处右键也能打开板菜单。

## 4. 尺寸与视觉令牌

| 项 | 值 |
|---|---|
| 默认板 | 760×800（工作区右侧，`min(760, workW*0.55)`） |
| 板内边距 | 14px |
| 板标题条 | 26px |
| 板边框 / 圆角 | 2px / 10px 像素圆角 |
| 卡片边框 / 圆角 | 1px / 6px 像素圆角 |
| 点阵 | 16px 间距，1px 点 |
| 卡片标题条 | 30px |
| 卡片内边距 | 左右 12 / 上 8 / 下 10 |
| 行距 | 32px |
| 进度条 | 4px |
| Add 槽 | 340×118，虚线 4/4 |
| 字号 | 标题/行 1.5；区块/状态/备注 1.0；详情大数 3.0 |

| 卡片 | 收起 | 展开 |
|---|---|---|
| Account（3 行） | 144 | 269（+6 gap +119 详情） |
| Servers（4 台） | 176 | 222（+6 gap +40 详情） |
| Servers（8 台） | 304 | 350 |

卡片高度 = `30 + 8 + BodyHeight() + 10`；`BodyHeight` 已经包含展开详情。

## 5. 配置与持久化

配置目录：exe 同目录；不可写则 `%APPDATA%\Desktop Desk`（全部文件一起回退）。

`desk.ini`：

| 键 | 说明 |
|---|---|
| `board.x/y/w/h` | 板屏幕矩形 |
| `board.grid` | 1 = 显示点阵（视觉；吸附步长由 `BoardOptions::grid` 决定） |
| `board.solid` | 1 = 空白板面也接收点击 |
| `card.<id>.x/y` | 卡片在板内的位置 |
| `card.<id>.visible` | 0 = 移除；仍保留位置，Add module 可恢复 |
| `card.<id>.expanded` | 展开详情 |

模块数据仍是 `aimeter.ini` / `server.ini`，不迁移。首次运行（没有
`desk.ini`）会：计算默认板/卡位置；移除旧的 `AiMeter` / `Server Status` /
`SSH Pad` 自启值；按需写 `Desktop Desk` 自启。

**新旧不要同时运行**：同一份 `aimeter.ini` 的当日快照会被两边同时推进。

## 6. 模块契约

`src/widget/module.h`：

```cpp
struct ModuleSpec {
    std::wstring id;          // "account"：配置键/菜单键/目录 id
    std::wstring title;       // "ACCOUNT"
    std::wstring configFile;  // "aimeter.ini"；空则 "<id>.ini"
    int width = 340;
    int refreshSeconds = 0;   // 0 = 只手动
    bool hasDetails = false;
    bool expandedByDefault = false;
    std::vector<Hotkey> hotkeys;  // 本地 id 1..99
};

class Module {
public:
    virtual ModuleSpec Describe() const = 0;   // 不要用 store()
    virtual void Load() = 0;      // UI，一次
    virtual void Refresh() = 0;   // worker，可阻塞
    virtual void Commit() = 0;    // UI，swap 后
    virtual int BodyHeight() const = 0;
    virtual void PaintBody(Canvas&, const Rect& body) = 0;
    virtual StatusChip Status() const;
    virtual std::vector<MenuItem> Menu() const;
    virtual void OnMenu(int id);
    virtual bool OnHotkey(int localId);
    virtual void OnClick(int x, int y);   // body 局部坐标
    virtual void OnHover(int x, int y);
};

template <class Data>
class ModuleT : public Module { /* Load/Refresh/Commit/BodyHeight/PaintBody */ };
```

- `RefreshData` 只写 staging；`PaintBodyOf` 只读 shown；不要从 worker 碰
  Canvas / 窗口。
- 卡片宽度由 `ModuleSpec::width` 决定；当前两个模块都是 340。高度由
  `BodyHeight` 决定，板会负责夹取/裁剪。
- `Status()` 返回标题条右侧的文字和颜色；颜色为 0 用 `theme.faint`。
- `Menu()` 的条目会出现在卡片右键菜单；`OnMenu` 收到条目 id。
- `hotkeys` 的本地 id 必须 1..99；Board 注册为 `1000 + index*100 + id`。
- 模块自己的配置通过 `store()` 读写；Board 负责打开/绑定。

### 新增模块

```bat
powershell -ExecutionPolicy Bypass -File tools\new-panel.ps1 -Name weather -Title WEATHER
rem 编辑 src\panels\weather\weather.cpp
rem 在 src\apps\desk\main.cpp 加一行 modules.push_back(CreateWeatherModule());
build.bat
```

构建脚本会自动发现 `src/panels/*` 并编进 `desk.exe`；模块自动出现在
**Add module** 菜单里。

### 视觉清单

- 只使用 `skin::Rows` / `skin::Divider` / `skin::FitText` 以及规范里的
  字号/颜色；不画外框、不嵌套卡片、不建窗口、不硬编码偏移。
- 空状态给“代码 + 下一步”（例如 `NO HOSTS` + `SSH HOSTS...`）。
- 状态色只用 `theme.ok` / `warn` / `bad`。
- 详情块每个最多一个 3.0 字号的大数，其余 1.0/1.5。

## 7. 布局与命中（src/widget/board_layout.*）

纯函数，无 HWND，可被 `tests/board_layout.cpp` 直接测试：

- `Snap(v, grid)`：四舍五入到网格。
- `CardRect / CardBody / CardHeight`：标题条、body、底边距的同一套常量。
- `ClampCard`：把卡片夹在板内；卡片比板大时贴左上。
- `ResolveDrop`：网格吸附 + `FindFreeSpot` 找最近空位；没有空位时保留落点
  （允许重叠），下次拖动/Tidy 再解决。
- `ClampBoard`：板的最小尺寸和工作区夹取。
- `HitTest`：Resize → BoardTitle → CardTitle/Card → AddSlot → Empty。

命中结果驱动 `WM_NCHITTEST`：Empty 且 `!board.solid` → `HTTRANSPARENT`，
其余 `HTCLIENT`。这不是 `WS_EX_TRANSPARENT` 整窗穿透。

## 8. 构建、测试与签名

```bat
build.bat            rem 框架 + desk.exe + srvctl.exe
build.bat test       rem 无窗口测试；签名测试 exe 后运行
```

测试：

- `tests/board_layout.cpp`：吸附/夹取/避让/命中/Canvas 裁剪/FitText。
- `tests/desk_render.cpp`：760×800 整板渲染（Account + Servers 展开态），
  断言 Account 269、Servers 222，输出 `build\desk-board-preview.bmp`。
- 既有 `tests/render.cpp` / `tests/cli_tests.cpp` 保持不变；旧的
  `tests/server_status.cpp` 随旧 Server 组件归档。
- `build.bat test` 会调用 `tools\sign-local.ps1 -Path "build\*-test.exe"`；
  Smart App Control 下这是测试能启动的前提。

构建后运行 `tools\sign-local.ps1` / `-Path build\bin\desk.exe` 给主程序签名。

## 9. 性能

本机实测（Windows 11，2026-11-05，板 760×800，Account 收起 / Servers 1 台）：

| 指标 | 值 |
|---|---|
| 进程 / 窗口 / 托盘 / 定时器 | 1 / 1 / 1 / 1 |
| 工作集 | 19.9 MB 空闲 / 20.4 MB 刷新中 |
| 私有内存 | 5.1 MB 空闲 / 6.4 MB 刷新中 |
| 线程 | 7 空闲 / 11 刷新中（含后台保存线程） |
| GDI / USER | 9 / 6 |
| 空闲 CPU | 10s 内 0 ms |

采集：`tools\bench-desktop.ps1 -ProcessName desk -Seconds 10`。
拖动按高精度时钟的 16ms 节拍重绘（60 fps），不重建 DIB、不创建 HWND；`SaveState` 只做快照，落盘在后台线程。

## 10. 兼容与迁移

- `srvctl.exe` 不变：仍读 `server.ini`，仍使用 `providers.cpp`；
  Servers 卡片的 `SSH Hosts...` 写回同一文件。
- 旧独立组件已归档到 `archive/standalone`，不再构建；旧 exe 在 `archive/bin`。
- 首次运行清理旧自启项；之后 `desk.ini` 存在就不再动注册表。
- 旧的 `aimeter.ini` / `server.ini` 数据直接复用；`desk.ini` 只保存板/卡
  布局，互不覆盖。

## 11. 限制

- 单显示器、固定像素、无 DPI 缩放。
- 空白板面默认交给桌面；板菜单在标题条/托盘/Add 槽。
- 一个模块类型一张卡；卡片不能自由缩放；卡片比板高会被裁切。
- `Win+D` 会把板一起隐藏。
- 退出时若 SSH 探测在飞，最多等 8s。
- Servers 仅 Linux，最多 8 台；`srvctl` 最多 32 台。
- 不并入 SSH Pad / Clock / AI Mini；终端卡需要更宽的板，另行评估。
- CommandCode `/alpha` 接口变化时该行 `NO DATA`。
