# Rolling Sky Cortex-M0 开发交接指南

本文面向刚加入项目的开发者或协作 AI，说明仓库内容、开发环境、固件与 SD 卡资源的生成方式、当前验证状态和协作约束。项目当前代码以 `main` 分支为准；开始工作前先拉取最新提交，并检查本指南引用的状态日期。

## 1. 项目概况

- 私有仓库：`DBW051333/rolling-sky-cortexm0-road`。
- 目标硬件：Anlogic EG4S20BG256 板卡；Cortex-M0 运行于 32 MHz；240×320 LCD；SDHC/FAT32 卡读取关卡与美术包。
- 游戏逻辑、绘制、关卡和 SD 驱动由 Cortex-M0 固件执行；FPGA 工程提供 M0 SoC、AHB 外设、PLL、按键、LCD 与 SD 接口。
- 当前包括选关、ROM 备用关卡、SD 关卡包、Hills 路线、普通/短跳、低矮障碍、完整阻挡、玻璃踩踏动画、空中与地面伤害体，以及失败/通关状态显示。
- ARM Cortex-M0 DesignStart 核心文件 `rtl/cortexm0ds_logic.v` 已经在私有仓库中，供获许可的协作者使用。每位使用者仍须遵守适用的 ARM 许可条款；不得将仓库改为公开或把核心另行转发给未获许可人员。
- 仿真与基准目录 `modelsim/`、`bench/` 按项目所有者的要求不在仓库中。`VALIDATION.md` 中的历史数字不能仅凭当前仓库重跑。

## 2. 新协作者需要的环境

### 必需工具

1. Git 与可访问该私有 GitHub 仓库的个人账号/SSH key。
2. Windows PowerShell、Python 3。
3. Keil ARM Compiler 6，包含 `armclang.exe`、`armasm.exe`、`armlink.exe`、`fromelf.exe`。历史记录使用 ARMCLANG 6.22；建议先使用相同版本以减少编译差异。
4. Anlogic TD，包含 `td_commands_prompt.exe` 和 EG4S20BG256 对应器件数据库。仓库工程元数据为 TD 5.6 系列；项目原构建脚本中的已知安装路径版本为 `5.6.5_119222_SP7_198.377`。
5. 生成 SD 美术包需要 Python Pillow。`tools/make_appearance.py` 会打开仓库中的 `assets/hills-background.png`。
6. 要在板上运行，需要 EG4S20BG256 板卡、可用的 JTAG 下载器、LCD 和 FAT32 SDHC 卡。

不同机器的工具路径不要写进仓库。使用当前用户会话的环境变量配置：

```powershell
$env:ARMCLANG_BIN = 'C:\path\to\ARMCLANG\bin'
$env:TD_COMMANDS_PROMPT = 'C:\path\to\TD\bin\td_commands_prompt.exe'
```

`ARMCLANG_BIN` 应是包含 ARM 工具程序的目录；`TD_COMMANDS_PROMPT` 应是可执行文件的完整路径。若生成美术包时报缺少 `PIL`，请在该 Python 环境安装 Pillow。

## 3. 克隆与固件/FPGA 构建

```powershell
git clone git@github.com:DBW051333/rolling-sky-cortexm0-road.git
Set-Location rolling-sky-cortexm0-road
git switch main
git pull --ff-only
```

配置上述两个环境变量后，在仓库根目录运行：

```powershell
./build-fpga.ps1
```

该脚本先从美术源数据生成 `firmware/appearance_data.h` 和 `sd_payload/RSART.BIN`，再编译 Cortex-M0 固件，最后调用 TD 综合、布局布线并生成 bitstream。主要输出包括：

- `firmware/code.hex`：固件映像。
- `td/rolling_road.bit`：FPGA 配置映像。
- `td/*.area`、`td/*.timing`、`td/*.db`、`td/*.log`：本地构建结果和日志。

这些输出按 `.gitignore` 排除，不应提交。SRAM 下载断电后不会保留配置。仓库内已包含 `td/rolling_road.al`、TCL 构建脚本、引脚/时序约束和 PLL 源码。

如果只需编译 Cortex-M0 固件，可在 `firmware/` 中运行：

```powershell
./build.ps1
```

此步骤仍需要 ARM Compiler 6。完整 FPGA 构建另需 TD 和器件数据库。

## 4. SD 卡上必须放什么

**正常运行需要在 FAT32 SDHC 卡的根目录放置以下两个文件，文件名必须保持大写：**

```text
<SD 卡根目录>\RSLEVELS.BIN
<SD 卡根目录>\RSART.BIN
```

- `RSLEVELS.BIN`：关卡判定数据。生成源为 `levels/levels.json`；生成命令是 `python tools/make_sd_levels.py`。
- `RSART.BIN`：背景、颜色配置、场景绑定和模型数据。生成源为 `levels/appearance.json` 与 `assets/hills-background.png`；生成命令是 `python tools/make_appearance.py`。该命令也会更新固件中的 `firmware/appearance_data.h`。
- 无卡或判定包无法加载时，固件可使用 ROM 中的三关备用目录；但要获得当前四关 SD 目录和详细背景/模型，必须准备这两个文件。
- SD 接口支持 SDHC/FAT32，不支持 exFAT、FAT16、SDSC 或长文件名查找。不要把文件放进子目录。
- `.BIN` 文件是生成物并被 Git 忽略。不要通过强制添加二进制包来代替提交 JSON、PNG 和生成器。

### 重新生成当前 SD 文件

在仓库根目录运行：

```powershell
python tools/make_appearance.py
python tools/make_sd_levels.py
```

输出位置分别是 `sd_payload/RSART.BIN` 和 `sd_payload/RSLEVELS.BIN`。命令只在电脑本地生成文件，不会访问或格式化任何磁盘。构建完成后可检查大小与 SHA-256：

```powershell
Get-Item .\sd_payload\RSLEVELS.BIN, .\sd_payload\RSART.BIN |
  Select-Object Name, Length
Get-FileHash .\sd_payload\RSLEVELS.BIN, .\sd_payload\RSART.BIN -Algorithm SHA256
```

### 当前已记录的包与板上状态

`VALIDATION.md` 顶部记录的最新判定包为 13312 B，SHA-256：

```text
RSLEVELS.BIN  0EB27C02C3D7C344B6BCBD16CD23F7E758569CFC53F92C76C57EC961B494A1EE
RSART.BIN     0874590DC02131CAB8253BD2F8215E8D49D7EEB6DEA569D015FB0D8CA0081371
```

美术包大小为 7680 B。以上是 2026-10-16 验证记录中的文件值，不是本次在新协作者机器上重新生成/复核的结果。生成后若哈希不同，先不要覆盖 SD 卡上的现有文件；先确认检出的提交、`levels/levels.json`、`levels/appearance.json`、背景 PNG 和生成器版本一致。

向 SD 卡复制时请确认盘符后再操作，只把两个文件复制到根目录；安全弹出后插回板卡。开机后按 KEY4 重新读卡，右下角显示绿色 SD 表示关卡目录成功加载；随后用 KEY6/KEY14 选关、KEY8 开始。绿色 SD 只证明关卡目录加载，不足以证明美术包成功；需观察 Hills 背景/模型，或检查调试变量 `art_sd_loaded`。新包写入卡的历史源/目标哈希一致，但最新固件下载后重新插卡读取、显示及完整游玩仍需板上复核。

## 5. 操作按键与基本使用

- KEY6 / KEY14：菜单左右选关；游戏内按住对应键向左/右移动，松开停止。
- KEY8：菜单开始；游戏中暂停/继续。
- KEY4：菜单重新读 SD；游戏中重开当前关卡。
- KEY12：游戏中返回选关页。
- 关卡目录最多 24 项；关卡 ID 用于稳定绑定美术，不能把目录顺序当成美术 ID。

具体关卡数据格式、硬件接口和限制见 `SD_CARD.md`、`MECHANICS.md`、`HILLS.md`。

## 6. 代码地图与修改入口

| 路径 | 内容与修改入口 |
|---|---|
| `firmware/main.c` | 主状态机、菜单、按键与帧调度 |
| `firmware/road.c`, `road.h` | 透视道路、移动、显示与碰撞主逻辑 |
| `firmware/level.c`, `level.h` | 关卡抽象、ROM 备用关卡与选择 |
| `firmware/obstacle.c`, `obstacle.h` | 通用障碍盒体与球体碰撞判定 |
| `firmware/sd_levels.c`, `sd_levels.h` | SD/FAT32 文件读取、关卡与美术数据载入 |
| `firmware/walls.c`, `glass.c` | 障碍投影与逐格玻璃动画 |
| `firmware/appearance.c`, `appearance.h` | 美术包校验、缓存、背景和模型绘制 |
| `levels/levels.json` | 当前关卡及 Hills 数据源 |
| `levels/appearance.json`, `assets/hills-background.png` | 颜色、模型、场景绑定与 Hills 背景源 |
| `tools/make_hills.py` | 重建 Hills JSON 与 ROM 关卡头文件；会修改源数据，运行前先看差异 |
| `tools/make_sd_levels.py` | 从 JSON 生成 `RSLEVELS.BIN`，并执行格式、支撑和可见面预算约束 |
| `tools/make_appearance.py` | 生成 ROM 小型美术表和 `RSART.BIN` |
| `tools/make_ball.py`, `make_menu_title.py` | 生成球与标题的小型固件头文件；标题生成器使用 Windows Segoe UI 字体路径 |
| `rtl/` | M0 SoC、AHB 外设、LCD、键盘、SD 与 ARM Cortex-M0 核心 |
| `td/` | EG4S20BG256 项目、约束、PLL 和 TD 构建 TCL |
| `VALIDATION.md` | 按日期记录的综合、下载、仿真与物理观察证据 |

关卡判定和美术严格分离：判定在 `levels.json`，美术在 `appearance.json` 与图像源中。改关卡时同时考虑 ROM fallback 与 SD 包是否需要更新；改美术时重新生成 `appearance_data.h` 与 `RSART.BIN`。执行 `make_hills.py` 会重写关卡 JSON 和 ROM 头文件，不要把它当只读验证工具。

## 7. 当前进度与验证边界

最新已记录内容见 `VALIDATION.md` 顶部（记录日期 2026-10-16；交接指南编写日期 2026-10-21）。当前主线包含最新版 Hills 障碍布局、M0 固件、TD 项目和许可核心。最新记录报告：固件编译、TD 构建、setup/hold 检查和 JTAG SRAM 下载成功；SD 判定包写卡时源/目标 SHA-256 一致。

必须区分以下几类证据：

1. **源代码与生成器检查**：证明 JSON/静态约束格式通过；不证明连续操作一定能通关。
2. **编译和 TD 构建**：证明固件/FPGA 映像生成，不能证明实体屏幕和按键行为正确。
3. **RTL 仿真**：历史性能数字来自 `modelsim/`、`bench/` 中未上传的文件；当前仓库不能独立重跑这些仿真或基准。
4. **JTAG 下载**：历史记录证明下载命令和芯片校验成功；不等于断电保持（配置在 SRAM）或所有游戏玩法通过。
5. **实体运行**：最新 SD 文件重插后的加载、美术呈现、所有关卡通关、异常卡兼容性和当前帧率仍应分别由板上实际观察验证。旧版本曾确认过 SD 目录读取和第三关开局，不能自动外推为最新包所有行为已验证。

不要把历史日志引用成当前重新运行的结果。最新验证前先在报告中写清楚仓库提交号、工具版本、输入包哈希和使用的板卡。

## 8. 协作 AI 的工作规则

1. 开始改动前读取本指南、`README.md`、`MECHANICS.md`、`HILLS.md`、`SD_CARD.md`、`LEVEL_DESIGN.md` 和 `VALIDATION.md`，确认当前 `main` 和工作区状态。
2. 以 JSON/PNG/RTL/C/约束源文件为主；不要编辑生成的 BIN、HEX、BIT、map、时序报告或临时输出来代替改源。
3. 没有明确需要时不要加入仿真、基准、备份、工具生成物或缓存；本仓库目前有意不包含 `modelsim/`、`bench/`。
4. 不要擅自更改 SD 二进制格式、稳定关卡 ID、BRAM 布局、引脚或 ARM 核心；涉及这些边界时先追踪全部读写方并记录兼容影响。
5. 修改关卡或渲染预算后，运行对应生成器并保留其约束检查输出；修改固件后运行编译；修改 RTL/约束后运行 TD 构建。未运行的检查要明确标注未运行。
6. 不得把成功编译、静态检查、综合或 JTAG program 命令写成“游戏已通关”或“实体画面已确认”。实体结果需要视频/观察记录或明确的用户反馈。
7. 使用 GitHub Flow：从最新 `main` 建 `feature/<简短主题>` 分支，提交源文件改动，推送后开 PR。PR 描述应列明做了什么、运行了哪些检查、哪些仿真/板上行为仍未验证。
8. 使用自己的 GitHub 账号和 SSH key。ARM 核心即使在私有仓库中也只供许可范围覆盖的人员使用。

## 9. 已知限制与待验证事项

- 仿真/基准源码未随仓库提供；如要让另一位开发者重跑历史性能结果，需要项目所有者另行提供并确认分享范围。
- `sd_payload/*.BIN`、固件 `.hex`、FPGA `.bit`、TD 日志/报告均是本地生成物，不由 Git 管理。
- ROM 有三关 fallback；当前四关目录通过 SD 包提供。完整 Hills 美术也需要 `RSART.BIN`。
- 设计预算和生成器的静态 route/gate 检查不能证明状态空间唯一解、任意操作序列可通关或真实玩家体验。
- LCD 每帧的像素与刷新表现、玻璃/障碍碰撞手感、长关连续游玩、SD 异常与热拔等仍需实体设备测试。
- 详细的外设限制与未实现机制见 `SD_CARD.md`、`MECHANICS.md` 和 `LEVEL_DESIGN.md`。
