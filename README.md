# AiMeter

> A tiny, pixel-art desktop widget for Windows 11 that keeps an eye on your AI
> service balances and quotas — **16 MB RAM, 0% idle CPU, no GPU, no web view**.

一个用 Win32 + 手写像素渲染实现的中文/英文桌面小组件，常驻在桌面层，
实时显示 DeepSeek 余额与今日消耗、Codex（ChatGPT Plus）的 5 小时 / 每周额度。

![screenshot](docs/screenshot.png)

## 特性

- **极低开销**：私有内存约 3 MB，工作集约 16 MB，空闲时 CPU 与 GPU 均为 0
- **零依赖渲染**：不用 Direct2D、不用 DirectWrite、不加载任何字体文件。
  一个 300×70 的 DIB 缓冲区 + 手写 5×7 点阵字体，逐像素写入
- **永远在桌面层**：窗口固定在 z 序最底部（紧跟 `Progman` 之上），
  不会盖住任何应用窗口，也永远不会抢焦点
- **点击穿透**：默认穿透，鼠标事件直接落到桌面；`Ctrl+Alt+W` 进入编辑模式后可拖动
- **系统托盘**：右键菜单提供刷新、设置、开机自启、退出
- **位置记忆 / 开机自启**：位置与每日余额快照存在 exe 同目录的 `aimeter.ini`
- **单实例**：重复启动会直接退出

## 支持的服务

| 服务 | 数据来源 | 状态 |
|---|---|---|
| DeepSeek | `GET https://api.deepseek.com/user/balance` | ✅ 余额 + 今日消耗 |
| Codex / ChatGPT Plus | 本地 `~/.codex/sessions/**/rollout-*.jsonl` 中的 `rate_limits` | ✅ 5h / 周额度百分比 |
| Z.AI Coding Plan | ZCode 内部 RPC，无公开接口 | ⏸ 代码中预留开关，默认隐藏 |

Codex 的额度**完全离线读取**，不产生任何网络请求：每次你使用 Codex CLI，
它会把 `primary`（5 小时窗口）和 `secondary`（每周窗口）的 `used_percent`
写进会话日志，AiMeter 只读最新那一条。

## 构建

需要 **MinGW-w64 GCC**（开发时使用 WinLibs GCC 16.2.0，UCRT + POSIX 线程）
和 Windows 10/11 x64。

```bat
build.bat
```

`build.bat` 默认从 `C:\Tools\mingw64\bin` 找 `g++`，改这一行即可指向你自己的工具链。
脚本会把中间文件放到 `C:\Temp\aimeter-build`——因为 MinGW 的工具链无法处理
非 ASCII 的 `%TEMP%`（中文用户名），这一步是必需的。

产物是单个 `aimeter.exe`（约 1.2 MB，静态链接，无运行时依赖）。

## 使用

| 操作 | 说明 |
|---|---|
| `Ctrl+Alt+W` | 切换编辑模式（可拖动、右键菜单） |
| `Ctrl+Alt+Q` | 退出 |
| 托盘图标双击 | 同 `Ctrl+Alt+W` |
| 托盘右键 | 刷新 / 设置 / 开机自启 / 退出 |

### 配置 API Key

`Ctrl+Alt+W` → 右键 → **Settings...**，粘贴 DeepSeek 的 API key 回车即可。

- key 通过 `CredWriteW` 存入 **Windows 凭据管理器**，目标名 `AiMeter/DeepSeek`，
  不落任何明文文件
- 查询余额是只读接口，不消耗 token、不扣费
- 换 key 时重新打开设置窗输入新的即可

### 配置文件

`aimeter.ini`（与 exe 同目录，UTF-16 编码）：

```ini
x=2240          ; 窗口位置
y=1300
day=2026-10-20  ; 当日快照日期
start=720       ; 当天开局余额（分）
last=687        ; 最近一次余额（分）
```

“今日消耗” = `start - 当前余额`。跨天时把 `last` 当作新一天的开局值，
所以即使深夜没开机，第二天也能算出准确差额。

## 为什么不用 Direct2D

第一版用 D2D 的 DC 渲染目标实现，功能正常，但**私有内存 51 MB**——
D2D 在 DC 目标上会拉起软件光栅化设备，光它一个就吃掉 40 MB。
改成“自己往 DIB 写 BGRA + `UpdateLayeredWindow`”之后降到 **2.9 MB**，
顺带砍掉了 D2D/DWrite 的 DLL 依赖，启动也更快。

像素风本身不需要抗锯齿和亚像素排版，这正好让“最省”和“最像”重合。

## 目录结构

```
src/app.cpp      # 全部实现：窗口、渲染、字体、HTTP、凭据、托盘、配置
build.bat        # MinGW 构建脚本
docs/            # 截图
```

## 已知限制

- 仅支持单显示器（主屏右下角，可拖动）
- 面板是普通顶层窗口，`Win+D`（显示桌面）会把它一起隐藏
- Z.AI 行暂未启用
- 只支持 Windows 10 1809+ / Windows 11

## License

[MIT](LICENSE)
