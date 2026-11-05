# 小组件框架（src/widget）

这套框架是写 AiMeter 时顺手抽出来的：把“桌面常驻小组件”里跟业务无关的部分——
分层窗口、像素渲染、托盘、热键、拖动、配置、定时刷新、后台线程——固化下来，
让下一个组件只关心**数据从哪来**和**画成什么样**。

`desk.exe` 的洞洞板层（Module / Board）见下一节；下面的 `Widget` / `Host` 写法
对应已归档的 standalone 组件（`archive/framework`、`archive/standalone`）。

## 设计目标

- **一个组件一个 .cpp**：写一个 `Data`，实现 `LoadData` / `RefreshData` /
  `HeightOf` / `PaintData`，其余交给宿主
- **默认就好看**：提供 `DrawList` 这种“标签 / 数值 / 进度条”标准布局和一套深色主题
- **不卡界面**：所有网络/文件扫描在 worker 线程，UI 先画上一帧
- **零运行时依赖**：只有 Win32 + WinHTTP，静态链接，单 exe
- **能拆得开**：`Canvas` / `Store` / `HttpGet` / 凭据 / 输入框都可以单独用

非目标：跨平台、矢量字体排版、多显示器 DPI 缩放、通用 UI 控件库。
这些等真的有第二个、第三个组件需要时再说（见文末路线图）。

## 分层

> 下面这棵旧树里的 `src/widgets/*`、`src/widget/host.*`、`src/widget/widget.h`
> 已归档；活跃的洞洞板分层见上一节，共享数据在 `src/data/`。

```
src/widgets/<name>/<name>.cpp   业务：Data + LoadData / RefreshData / HeightOf / PaintData
                 providers.cpp  可选：HTTP、文件解析等数据源
        │  只依赖 widget.h / view.h（以及 store/http/secrets/prompt）
        ▼
src/widget/host.*               宿主：单实例、窗口、托盘、热键、拖动、定时器、worker、
                                持久化、流式重绘（WM_APP+3）
src/widget/layout.*             多组件互不遮挡：枚举 wg.<id>、找空位、命名互斥量
src/widget/widget.h             契约：Widget + WidgetT<Data>（双缓冲）+ RequestRepaint
src/widget/view.*               DrawList / ListHeight
src/widget/canvas.*             Canvas（DIB + 像素字体 + 面板/进度条）、Theme
src/widget/cjk.*                CJK 字形缓存（按需加载系统字体；ASCII 不碰）
src/widget/store.*              <id>.ini，UTF-16 key=value
src/widget/http.*               阻塞式 HTTPS GET / JSON POST
src/widget/secrets.*            Credential Manager
src/widget/prompt.*             模态单行输入框（像素风，支持中文/IME/粘贴）
src/widget/proc.*               子进程：RunCapture（阻塞取全量）/ RunInherited（接管道）
                                / Child（长驻，双输出管道，kill-on-close job）
```

## 桌面洞洞板（Module / Board）

`desk.exe` 不是“多个 Widget 叠在一起”，而是**一块板 + 独立卡片**：

```
src/apps/desk/main.cpp      注册模块（一行一个）
src/panels/<name>/          卡片实现（Module 子类 + 纯视图函数）
src/widget/board.*          板宿主：窗口、调度、拖动、菜单、托盘、持久化
src/widget/board_layout.*   纯布局：Snap / ClampCard / ResolveDrop / HitTest
src/widget/skin.*           板面/卡片/行/详情/Add 槽的视觉令牌
src/widget/module.h         Module / ModuleT<Data> 契约
```

- 一个 `wg.desk` 分层窗口、一张 DIB、一个 `SetTimer`、一个托盘、一套热键。
- 卡片是板内矩形；拖动只移动矩形，不创建/移动 HWND。
- 空白点返回 `HTTRANSPARENT` 交给桌面；卡片/标题条/缩放手柄/Add 槽返回
  `HTCLIENT`。不是 `WS_EX_TRANSPARENT` 整窗穿透。
- 始终置底（`HWND_BOTTOM` + 前台事件钩子），点击不抢焦点。
  唯一的例外是"需要键盘的卡片"：`Module::WantsKeyboard()` 认领的点击会让板
  `SetForegroundWindow` 拿键盘（拿之前先 `PinToDesktop()`，所以仍在最底层），
  `WM_MOUSEACTIVATE` 对其它所有点击继续回答 `MA_NOACTIVATE`。

### Module 契约

```cpp
struct ModuleSpec {
    std::wstring id, title, configFile;
    int width = 340;
    int refreshSeconds = 0;
    bool hasDetails = false;
    bool expandedByDefault = false;
    std::vector<Hotkey> hotkeys;
};

template <class Data>
class ModuleT : public Module {
protected:
    virtual void LoadData(Data&);                 // UI，一次
    virtual void RefreshData(Data&) = 0;          // worker，可阻塞
    virtual int  BodyHeightOf(const Data&) const = 0;
    virtual void PaintBodyOf(const Data&, Canvas&, const Rect& body) = 0;
    virtual void OnCommit();
    const Data& shown() const;
};
```

键盘（默认全部不参与，卡片照旧"点了也不抢焦点"）：

```cpp
virtual bool WantsKeyboard(int x, int y) const;   // 认领这次点击
virtual void OnFocusChanged(bool focused);
virtual bool OnKey(const KeyEvent& key);          // VK + ctrl/alt/shift
virtual void OnChar(wchar_t c);
virtual void OnPaste(std::wstring_view text);     // 板的 Ctrl+V 代读剪贴板
virtual bool CaretPos(int* x, int* y) const;      // IME 候选窗锚点
```

- 双缓冲语义与 `WidgetT` 相同：`RefreshData` 只写 staging，`PaintBodyOf`
  只读 shown；Board 保证不同时执行，所以模块不用锁。
- 例外：**POWERSHELL 卡**（`src/panels/ps`）直接实现 `Module`，不套
  `ModuleT`。它的日志是一条流，由 `ps::Session` 自己加锁持有，`refreshSeconds=0`，
  读线程有新行时调 `RequestRepaint()`（80ms 节流），绘制时取尾部快照。
  这条路径只适合流式数据，别的新模块仍走 `ModuleT`。
- Board 负责卡片标题条/外框/裁剪；模块只画 body，并拿到 `body` 矩形。
- `Status()` 给标题条右侧的文字和颜色；`Menu()/OnMenu` 给卡片右键菜单；
  `hotkeys` 本地 id 1..99，由 Board 分配全局 id。
- 模块配置用 `store()`；Board 先 Bind 再 Load。

### 新增模块

```bat
powershell -ExecutionPolicy Bypass -File tools\new-panel.ps1 -Name weather -Title WEATHER
rem 编辑 src\panels\weather\weather.cpp
rem 在 src\apps\desk\main.cpp 加 modules.push_back(CreateWeatherModule());
build.bat
```

构建会自动发现 `src/panels/*`；模块自动进入 **Add module** 菜单。
细节、尺寸、配置与视觉规范见 [desk.md](desk.md)。

## 写一个组件

> 下面 `Widget` 写法对应已归档的 standalone 组件；新的洞洞板模块用上一节的 `Module`。

用脚手架生成：

```bat
powershell -ExecutionPolicy Bypass -File archive\tools\new-widget.ps1 -Name weather -Title "Weather"
```

归档的脚手架会生成 `src/widgets/weather/weather.cpp`（仅参考；当前 `build.bat`
不构建它）。手写的话，一个完整组件长这样：

```cpp
struct WeatherData {
    std::wstring temperature = L"--";
    std::wstring condition = L"LOADING";
};

class Weather : public wg::WidgetT<WeatherData> {
public:
    wg::Desc Describe() override { return wg::Desc{L"weather", L"Weather", 220}; }

    bool HasSettings() const override { return true; }          // 托盘菜单出现 Settings...
    void OnSettings() override {                                // UI 线程
        wg::PromptSpec spec;
        spec.label = L"CITY";
        std::wstring city;
        if (wg::PromptText(spec, city)) {
            store().Set(L"city", city);                         // 持久化
            store().Save();
        }
    }

protected:
    void LoadData(WeatherData& data) override {                 // UI 线程，只跑一次
        data.condition = store().Get(L"city", L"SHANGHAI");
    }

    void RefreshData(WeatherData& data) override {              // worker 线程，可以阻塞
        if (auto r = wg::HttpGet(L"api.example.com", L"/now", L""); r.ok()) {
            data.temperature = L"24 C";                         // 这里可以随便慢
        }
    }

    int HeightOf(const WeatherData&) const override { return 78; }

    void PaintData(const WeatherData& data, wg::Canvas& c) override {
        c.Panel(theme_, editing());                             // 编辑模式蓝色边框
        c.TextCentered(data.temperature, c.width() / 2, 12.0f, theme_.text, 3.0f);
        c.TextCentered(data.condition, c.width() / 2, 52.0f, theme_.faint, 1.0f);
    }

private:
    wg::Theme theme_;
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wg::Options options;
    options.refreshSeconds = 600;      // 组件自己的刷新节奏
    return wg::Run(std::make_unique<Weather>(), options);
}
```

## 生命周期

```
进程启动
  ├─ 单实例互斥量 <id>SingleInstance
  ├─ 读取 <id>.ini，首次运行按需开启开机自启
  ├─ Load()        -> LoadData(staging) -> shown
  ├─ 创建分层窗口（宽 = Desc::width，高 = HeightOf(shown)）
  ├─ 避开其他组件窗口（枚举 wg.<id> + 命名互斥量）
  ├─ 画第一帧 -> Refresh() 在 worker 线程跑
  └─ 消息循环
        ├─ 定时器 / 菜单“刷新” / 设置保存后 -> Refresh()
        ├─ worker 完成 -> PostMessage(WM_APP+1)
        │     -> join、Commit()（swap staging/shown）、按需改高度
        │     -> 重新检查重叠、重绘
        ├─ Ctrl+Alt+W / 托盘双击 -> 编辑模式（去掉点击穿透，可拖）
        ├─ 拖动结束 -> 写 x/y -> 重新检查重叠
        └─ 关闭 -> 移除托盘、join worker、写一次配置、退出
```

## 多组件位置协调

同一框架下的组件通过窗口类 `wg.<id>` 互相发现，不需要共享配置文件，
进程崩溃后也不会留下僵尸占位：

- 启动时枚举其他组件的主窗口（跳过 `wg.<id>.details` 和 `wg.Prompt`），
  在**当前显示器的工作区**内找一个与所有窗口至少间隔 8px 的位置
- 优先放在同一行的左侧/右侧，其次上方/下方，最后从右下角做网格扫描
- 放置过程持有 `Local\wgWidgetLayout` 命名互斥量，两个组件同时启动也不会
  抢到同一个空位
- 拖动结束、组件高度变化时重新检查；有空位就挪过去，没有就保持原位
- 结果写回各自 `<id>.ini` 的 `x` / `y`

限制：只协调同一桌面会话、同一套 `wg.` 窗口类；如果屏幕空间真的放不下，
仍可能重叠。详情面板是临时窗口，不参与布局。

## 线程模型（重要）

```
UI 线程                                   worker 线程
--------                                  ------------
Load()  ── LoadData(staging) ── shown
（空闲，可重绘 shown）                     Refresh() ── RefreshData(staging)
Commit() ── swap(shown, staging) <────── PostMessage 通知完成
Paint(shown)
```

规则只有三条：

1. `RefreshData` 里**只写传进来的 `Data&`**（也就是 staging），不要碰全局 UI；
2. `PaintData` 里只读 `const Data&`（shown）；
3. 宿主保证两者不同时执行，所以**不需要锁**。

这也意味着慢接口（8 秒超时）不会卡住托盘菜单、拖动或重绘：上一帧先画出来，
worker 回来了再换下一帧。

**流式组件是唯一例外**：worker 在 `RefreshData` 返回前就要把增量送到屏幕。
组件自己维护一份受锁保护的缓冲，每收到一段就调用 `RequestRepaint()`；宿主
`PostMessage(WM_APP+3)` 后在 UI 线程重绘。`PaintData` 仍然只读 shown，但可以
在锁内拷贝这份缓冲。不要从 worker 直接碰 Canvas。

## API 速查

### WidgetT<Data>

| 函数 | 线程 | 说明 |
|---|---|---|
| `Describe()` | UI | 返回 `{id, title, width, hotkeys}`；id 决定窗口类、互斥量、ini 名 |
| `LoadData(Data&)` | UI | 只跑一次，适合读 `store()`、凭据 |
| `RefreshData(Data&)` | worker | 可阻塞；定时器/刷新/设置后触发 |
| `HeightOf(const Data&)` | UI | 每次重绘前调用，返回高度；变化时窗口自动缩放 |
| `PaintData(const Data&, Canvas&)` | UI | 画一帧，使用 `Canvas` |
| `TrayIcon(Canvas&)` | UI | 可选，画 16×16 托盘图标；返回 false 用默认 |
| `HasSettings()` / `OnSettings()` | UI | 可选，托盘菜单项与处理 |
| `HasDetails()` / `ShowDetails()` | UI | 可选，托盘 `Details...` 与 `Ctrl+Alt+D` |
| `Actions()` / `OnAction(int)` | UI | 可选，动态托盘菜单项（如 Connect 主机列表） |
| `OnHotkey(int)` | UI | 可选，处理 `Desc::hotkeys` 注册的自定义热键 |
| `OnClick(x,y)` | UI | 可选，`Options::focusable` 时点击客户区 |
| `OnChar(wchar_t)` | UI | 可选，可聚焦组件的字符输入 |
| `OnKey(vk,ctrl,alt,shift)` | UI | 可选，返回 true 表示已处理特殊键 |
| `OnFocus(bool)` | UI | 可选，焦点变化（用于光标/提示） |
| `OnScroll(delta)` | UI | 可选，鼠标滚轮 |
| `OnCommit()` | UI | swap 之后调用，适合刷新组件自己的弹窗 |
| `RequestRepaint()` | worker | 流式时请求宿主重绘；调用方自己保证数据安全 |
| `editing()` | UI | 是否处于编辑模式（用于高亮边框） |
| `store()` | 任意 | `<id>.ini` 读写，内部有锁 |

`Data` 需要可默认构造、可交换（`std::swap`），通常就是几个 `std::wstring` /
`std::vector<wg::Row>`。

### Canvas

| 函数 | 说明 |
|---|---|
| `Resize(w,h)` | 分配/复用 DIB（尺寸不变时不重建） |
| `Clear(color)` | 清成透明或任意颜色 |
| `Fill / Rect / HLine` | 矩形、1px 边框、横线 |
| `Text / TextRight / TextCentered` | 5×7 点阵字体，`scale` 默认 1.5 |
| `TextWidth` | 文本像素宽度（用于右对齐） |
| `Bar` | 进度条（`frac < 0` 只画槽） |
| `Panel(theme, active, solid)` | 标准 2px 切角面板 |
| `Present(hwnd, x, y)` | `UpdateLayeredWindow`，宿主每帧调用 |
| `ToIcon()` | 转成 HICON（托盘） |

颜色是 **预乘 alpha** 的 `0xAARRGGBB`，用 `wg::Rgba(0xRRGGBB, alpha)` 构造。
`Canvas` 是单层画布：后来画的像素会**覆盖**而不是混合，透明区域由 PRGB 的 alpha 决定。

### DrawList

```cpp
std::vector<wg::Row> rows = {
    // label, value, note, frac, accent, noteLeft, noteColor, valueColor
    {L"DEEPSEEK", L"CNY 6.75", L"1.24M TOK  LOCAL", -1.0f, 0,
     L"TODAY -0.45", wg::Rgba(0xF2F2F6)},
    {L"CODEX",    L"6% / 1%",  L"",                  0.06f, 0},
};
const int height = wg::ListHeight(style, rows.size());
wg::DrawList(canvas, rows, style, theme, editing());
```

`note` / `noteLeft` 任一非空时，这一行画文字而不画进度条；`noteColor` 为 0
时用 `theme.faint`。`Row::accent` 传 0 表示用 `theme.ok`。`valueColor` 为 0
时数值用 `theme.text`，Server Status 用它把 `UP` / `TIMEOUT` 染成状态色。

### 其他

- `wg::HttpGet(host, path, bearer, ua)` → `{transport, status, body}`
- `wg::HttpPost(host, path, bearer, body, receiveTimeoutMs)` → 同上，JSON POST；
  `receiveTimeoutMs` 是单次读取超时（默认 30s），慢补全接口要显式调大
- `wg::HttpPostStream(host, path, bearer, body, onChunk, receiveTimeoutMs)` →
  SSE 流式 POST；`onChunk` 返回 false 可中止，成功路径不缓存整包
- `wg::cjk::Text(canvas, text, x, y, color, scale)` / `TextWidth` / `Wrap`：
  中文、日文、韩文与全角标点的按需字形缓存；ASCII 仍走 5×7 点阵。
  `Wrap` 只做像素宽度计算、不碰 GDI，可以在 worker 线程里换行
- `wg::LoadSecret / SaveSecret / DeleteSecret(target)` → Credential Manager
- `wg::PromptText(spec, out)` → 模态输入框（可选掩码、ESC/Enter，支持
  `WM_CHAR` / `WM_PASTE` 与中文 IME）；`spec.initial` 可预填并直接编辑已有值
- `wg::Options`：`refreshSeconds`、`startWithWindows`（仅首次运行）、
  `clickThrough`、`focusable`（点击聚焦、接收键盘）、编辑/退出/明细热键
  （明细热键只在 `HasDetails()` 时注册）
- `wg::Desc::hotkeys`：组件自己声明的全局热键，id 必须 ≥ 100；
  宿主负责注册/注销并把 `WM_HOTKEY` 转成 `Widget::OnHotkey(id)`

## 约定与坑

- **id**：小写、稳定。它同时是窗口类名、互斥量名、ini 文件名；
  改名等于换一个组件（配置和单实例都会断）
- **ini 保留键**：`x`、`y` 由宿主读写，组件不要用这两个名字
- **字体默认只有 ASCII**：`Canvas::Text` 对未知字符输出空格。要显示中文用
  `cjk::Text`：它把 ASCII 片段交回 `Canvas::Text`，只在遇到宽字符时按需
  创建系统字体、缓存 1bpp 字形；纯英文组件不会加载字体
- **宿主不把主窗口 HWND 交给组件**：需要输入用 `PromptText`，需要只读面板
  就自己建一个 layered window（参考 `archive/standalone/aimeter/details.cpp`）
- **Describe 里不要用 `store()`**：宿主还没有绑定
- **RefreshData 可能被连续调用**：它的入参是上一帧的副本，可增量更新，但
  别假设某个字段一定是新的
- **高度可以变**：`Data` 的行数变了，`HeightOf` 跟着变，窗口会自动缩放并保持
  下边缘在屏幕内
- **设置后刷新**：`OnSettings` 返回后宿主会自动再 Refresh 一次
- **多个组件同时跑**：各自一个进程；默认热键相同会冲突，记得在 `Options` 里改
- **多组件位置**：由 `layout.*` 自动协调，组件不要自己移动窗口；屏幕空间不足时仍可能重叠
- **流式数据**：worker 只追加自己的缓冲并 `RequestRepaint()`；UI 在锁内取快照，
  不要把 `Canvas` / `shown()` 交给 worker
- **单实例**按 id 生效：同 id 的第二个进程直接退出

## 调试

- `build.bat test` 会编译 `tests/render.cpp`，不创建窗口，直接把标准列表布局
  渲染到 `build\preview.bmp`。改字体、间距、对齐时先看这张图
- 托盘 → 刷新；`Ctrl+Alt+W` 进入编辑模式后可拖动、右键菜单
- 想看布局线框，可以临时在 `PaintData` 里加 `c.Rect(...)`

## 演进路线（按需，不预先实现）

1. **多组件单进程：已完成**：`desk.exe` 用 `Module` + `Board` 实现（见上一节），
   一个进程/窗口/托盘/定时器；旧的 `Widget` / `Host` 已归档。
2. **字形源继续抽象**：`cjk.*` 已经有了 GDI 字形缓存；下一步是把 `FontState`
   换成可替换的 `GlyphSource`（内嵌 Unifont 子集 / GDI / 系统点阵），
   并按 DPI 选择字号
3. **配置表单**：`PromptText` 升级为多字段表单，支持下拉/开关
4. **Provider 缓存**：多个组件共享一个 HTTP 结果缓存（例如余额）
5. **DPI**：按 `GetDpiForWindow` 缩放 `Canvas` 的绘制比例
