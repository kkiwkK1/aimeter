# 小组件框架（src/widget）

这套框架是写 AiMeter 时顺手抽出来的：把“桌面常驻小组件”里跟业务无关的部分——
分层窗口、像素渲染、托盘、热键、拖动、配置、定时刷新、后台线程——固化下来，
让下一个组件只关心**数据从哪来**和**画成什么样**。

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

```
src/widgets/<name>/<name>.cpp   业务：Data + LoadData / RefreshData / HeightOf / PaintData
                 providers.cpp  可选：HTTP、文件解析等数据源
        │  只依赖 widget.h / view.h（以及 store/http/secrets/prompt）
        ▼
src/widget/host.*               宿主：单实例、窗口、托盘、热键、拖动、定时器、worker、持久化
src/widget/widget.h             契约：Widget + WidgetT<Data>（双缓冲）
src/widget/view.*               DrawList / ListHeight
src/widget/canvas.*             Canvas（DIB + 像素字体 + 面板/进度条）、Theme
src/widget/store.*              <id>.ini，UTF-16 key=value
src/widget/http.*               阻塞式 HTTPS GET
src/widget/secrets.*            Credential Manager
src/widget/prompt.*             模态单行输入框（像素风）
```

## 写一个组件

用脚手架生成：

```bat
powershell -ExecutionPolicy Bypass -File tools\new-widget.ps1 -Name weather -Title "Weather"
```

生成 `src/widgets/weather/weather.cpp`（最小可编译版本），`build.bat` 会自动发现并
编译成 `build\bin\weather.exe`。手写的话，一个完整组件长这样：

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
  ├─ 画第一帧 -> Refresh() 在 worker 线程跑
  └─ 消息循环
        ├─ 定时器 / 菜单“刷新” / 设置保存后 -> Refresh()
        ├─ worker 完成 -> PostMessage(WM_APP+1)
        │     -> join、Commit()（swap staging/shown）、按需改高度、重绘
        ├─ Ctrl+Alt+W / 托盘双击 -> 编辑模式（去掉点击穿透，可拖）
        ├─ 拖动结束 -> 写 x/y
        └─ 关闭 -> 移除托盘、join worker、写一次配置、退出
```

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

## API 速查

### WidgetT<Data>

| 函数 | 线程 | 说明 |
|---|---|---|
| `Describe()` | UI | 返回 `{id, title, width}`；id 决定窗口类、互斥量、ini 名 |
| `LoadData(Data&)` | UI | 只跑一次，适合读 `store()`、凭据 |
| `RefreshData(Data&)` | worker | 可阻塞；定时器/刷新/设置后触发 |
| `HeightOf(const Data&)` | UI | 每次重绘前调用，返回高度；变化时窗口自动缩放 |
| `PaintData(const Data&, Canvas&)` | UI | 画一帧，使用 `Canvas` |
| `TrayIcon(Canvas&)` | UI | 可选，画 16×16 托盘图标；返回 false 用默认 |
| `HasSettings()` / `OnSettings()` | UI | 可选，托盘菜单项与处理 |
| `HasDetails()` / `ShowDetails()` | UI | 可选，托盘 `Details...` 与 `Ctrl+Alt+D` |
| `OnCommit()` | UI | swap 之后调用，适合刷新组件自己的弹窗 |
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
    // label, value, note, frac, accent, noteLeft, noteColor
    {L"DEEPSEEK", L"CNY 6.75", L"1.24M TOK  LOCAL", -1.0f, 0,
     L"TODAY -0.45", wg::Rgba(0xF2F2F6)},
    {L"CODEX",    L"6% / 1%",  L"",                  0.06f, 0},
};
const int height = wg::ListHeight(style, rows.size());
wg::DrawList(canvas, rows, style, theme, editing());
```

`note` / `noteLeft` 任一非空时，这一行画文字而不画进度条；`noteColor` 为 0
时用 `theme.faint`。`Row::accent` 传 0 表示用 `theme.ok`。

### 其他

- `wg::HttpGet(host, path, bearer, ua)` → `{transport, status, body}`
- `wg::LoadSecret / SaveSecret / DeleteSecret(target)` → Credential Manager
- `wg::PromptText(spec, out)` → 模态输入框（自动掩码、粘贴、ESC/Enter）
- `wg::Options`：`refreshSeconds`、`startWithWindows`（仅首次运行）、
  `clickThrough`、编辑/退出/明细热键（明细热键只在 `HasDetails()` 时注册）

## 约定与坑

- **id**：小写、稳定。它同时是窗口类名、互斥量名、ini 文件名；
  改名等于换一个组件（配置和单实例都会断）
- **ini 保留键**：`x`、`y` 由宿主读写，组件不要用这两个名字
- **字体只有 ASCII**：`Canvas::Text` 对未知字符输出空格。中文目前画不出来，
  要么用英文，要么换字体源（见路线图）
- **宿主不把主窗口 HWND 交给组件**：需要输入用 `PromptText`，需要只读面板
  就自己建一个 layered window（参考 `src/widgets/aimeter/details.cpp`）
- **Describe 里不要用 `store()`**：宿主还没有绑定
- **RefreshData 可能被连续调用**：它的入参是上一帧的副本，可增量更新，但
  别假设某个字段一定是新的
- **高度可以变**：`Data` 的行数变了，`HeightOf` 跟着变，窗口会自动缩放并保持
  下边缘在屏幕内
- **设置后刷新**：`OnSettings` 返回后宿主会自动再 Refresh 一次
- **多个组件同时跑**：各自一个进程；默认热键相同会冲突，记得在 `Options` 里改
- **单实例**按 id 生效：同 id 的第二个进程直接退出

## 调试

- `build.bat test` 会编译 `tests/render.cpp`，不创建窗口，直接把标准列表布局
  渲染到 `build\preview.bmp`。改字体、间距、对齐时先看这张图
- 托盘 → 刷新；`Ctrl+Alt+W` 进入编辑模式后可拖动、右键菜单
- 想看布局线框，可以临时在 `PaintData` 里加 `c.Rect(...)`

## 演进路线（按需，不预先实现）

1. **多组件单进程**：`Widget` 接口本身与进程无关，加一个注册表 + 共享托盘 +
   每组件独立 ini/凭据即可；收益是内存和托盘图标数量
2. **字体源抽象**：把 `FindGlyph` 抽成 `GlyphSource`，再挂一个 CJK 点阵或
   GDI 字形缓存，就能显示中文，同时保留“无字体文件”的默认实现
3. **配置表单**：`PromptText` 升级为多字段表单，支持下拉/开关
4. **Provider 缓存**：多个组件共享一个 HTTP 结果缓存（例如余额）
5. **DPI**：按 `GetDpiForWindow` 缩放 `Canvas` 的绘制比例
