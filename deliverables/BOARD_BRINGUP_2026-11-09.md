# 2026-11-09 接手检查与首次上板记录

## 结论与证据

从提交 `05ed57a`（`main`，初始工作区干净）接手，在本地分支 `feature/local-board-bringup` 修正三项问题后，重新编译固件、综合布局布线，并下载到当前连接的安路开发板。随后按用户要求将本次改动整理到 `dev/ligan` 分支。

- Windows 下载器：`Anlogic AL-Link Mini`，设备状态 OK。
- TD 实际识别：`Chip validation success: EAGLE_S20_BG256`。
- 2026-11-09 13:32:51 开始 JTAG 下载，正常完成，约 6.8 秒；证据 `td/bringup-download.log`。
- 用户随后明确反馈：**“已显示，KEY8 可进入游戏”**。因此本次已确认选关页显示、KEY8 输入和进入游戏的实体路径。
- 当前是 **JTAG SRAM 配置**，断电不保持。未写入 SPI Flash。
- 尚未确认右下角 SD/ROM、SD 美术加载、全部关卡、滑条实际行程、声音听感、实体帧率与长时间稳定性。

本记录引用的构建、哈希和下载为本次机器实际结果。仓库历史 `VALIDATION.md` 与讲稿中的数字不是本次测量。

## 系统如何运行

1. `td/pll_ip.v` 将板上 R7 的 50 MHz 晶振变为 32 MHz。`rtl/top.v` 在时钟启动后计数 65535 个周期，再释放 SoC 复位。
2. `rtl/CortexM0_SoC.v` 实例化 ARM Cortex-M0 DesignStart 核心、AHB 互连、程序与数据 BRAM，以及键盘、ADC、LCD、SPI SD 和 PWM 音频外设。
3. 程序 BRAM 在生成位流时读取 `firmware/code.hex`。因此只更新 HEX 不会更新板上程序，必须重新生成并下载 BIT，或另行使用已验证的处理器调试下载流程。
4. 复位向量给出栈顶 `0x20002000`；`startup.s` 的 `Reset_Handler` 直接调用 `main`，没有普通 C 运行库的数据复制/清零流程。新增全局状态必须留意初始化，不能仅依赖非零静态初值。
5. `main.c` 初始化道路、SysTick（31999 重装，1 ms）、LCD 和声音，挂载 SD，再进入选关循环。读卡失败会保留 ROM 三关，允许无卡启动。
6. 进入关卡后，每帧读取按键与 ADC，推进逻辑，更新声音、SD 行缓存及 HUD，绘制 320 行，再等待帧间隔达到至少 33 ms。33 ms 是调度下限，不是本次测得帧率。

### 地址与存储边界

| 地址 | 实际用途 |
|---|---|
| `0x00000000..0x00006FFF` | 固件代码、只读表，28 KiB |
| `0x00007000..0x00007FFF` | SD 美术缓存，4 KiB；与代码共用 32 KiB 程序 BRAM |
| `0x20000000..0x20001FFF` | 数据 RAM 和向下增长的栈，合计 8 KiB |
| `0x40000000` / `0x40000004` | 键盘事件/清除、按住状态 |
| `0x40000008..0x40000020` | ADC 通道 1..7，每通道一个 32 位寄存器 |
| `0x40000100..` | UART 寄存器，115200 波特率；本版游戏不是靠串口显示 |
| `0x40050000..` | LCD GPIO、16 位数据、颜色段发送器 |
| `0x40060000..` | SD SPI 字节引擎、片选与分频 |
| `0x40070000..` | 四声道音频控制、音高、滑音、波形/音量与状态 |

AHB 译码窗口比物理 RAM 宽；不能因注释标成 64 KiB 就把可用程序或数据 RAM 当成 64 KiB。

### 逻辑、绘制和数据关系

- 世界使用 Q8 定点坐标，每格 256 单位；道路每毫秒推进 3/4 Q8，按键每毫秒横移 2 Q8。逻辑以最多 200 次的 1 ms 小步处理一次帧间隔。
- 普通跳跃当前为 1366 ms，接触跳板行前沿触发；短跳为 768 ms。逻辑高度独立于屏幕像素抬升。普通跳距约四行。
- 静态障碍和动态机关统一产生世界坐标盒体，用球与盒体距离判断碰撞；渲染读取相同机关姿态函数。机关相位随道路距离推进，暂停时冻结。
- 道路按屏幕扫描线反查世界深度，深度/缩放预先缓存；整帧没有 240×320 帧缓冲。HUD、钻石、传送门使用 240 像素行缓冲，其他行直接发送颜色段。
- `walls.c` 缓存 44 个面，静态场景优先占 32 个，再添加机关。远景面可能被预算剔除，但碰撞数据仍存在。
- 玻璃按踩过的单格播放下落，保留地面支撑；不能当作踩后删除地面的陷阱。
- 美术与判定独立：`levels.json` 生成 `RSLEVELS.BIN`；`appearance.json` 生成固件备用调色表与 `RSART.BIN`。当前背景由固件生成，原始背景 PNG 不再参与实际美术包编码。
- SD 支持 SDHC、512 字节扇区、FAT32、根目录 8.3 文件名。行数据双扇区缓存共 1024 B；美术在挂载时一次载入 4 KiB 缓存。
- 音频由 FPGA 输出 F3 PWM，32 MHz / 1024 = 31.25 kHz；CPU 写音符寄存器，三声部音乐与第四声道音效分开。菜单/暂停/失败/通关时音乐关闭。

## 文件导航

### 固件

| 文件 | 责任 |
|---|---|
| `firmware/main.c` | LCD 初始化与发送、菜单、ADC 自标定、键盘方向转换、音量、HUD、帧调度 |
| `firmware/road.c`, `road.h` | 道路状态和定点投影、背景、球、钻石、传送门、扫描线合成 |
| `firmware/level.c`, `level.h` | ROM 关卡注册、关卡重置、跳跃、拾取、碰撞和关卡推进 |
| `firmware/obstacle.c`, `obstacle.h` | 统一球/盒碰撞、激光/移动体/摆锤/滚轮/飞锤姿态 |
| `firmware/walls.c`, `walls.h` | 模型投影、面缓存、扫描线颜色区间和前后遮挡标记 |
| `firmware/glass.c`, `glass.h` | 12 格玻璃动画状态、离行触发与回收 |
| `firmware/sd_levels.c`, `sd_levels.h` | SD 初始化、FAT32、目录、V1..V4 关卡格式、缓存及独立美术文件读取 |
| `firmware/appearance.c`, `appearance.h` | 美术 CRC/范围检查、模型与调色查询、场景范围查询及备用美术 |
| `firmware/sound.c`, `sound.h` | 毫秒音乐序列、音效、音量及音频 MMIO |
| `firmware/startup.s`, `code.sct` | 中断向量、直接启动、代码/数据分区 |
| `firmware/build.ps1` | 编译九个 C 单元、汇编、链接、导出 HEX/映射/反汇编 |
| `firmware/ball_sprite.h`, `menu_title.h` | 球和菜单标题的离线压缩图形表 |
| `firmware/hills_level.h`, `appearance_data.h` | ROM Hills 数据与小型备用美术表；应从生成器维护 |

### RTL 与 TD

| 文件 | 责任 |
|---|---|
| `rtl/top.v` | 顶层 IO、PLL、上电复位延迟 |
| `rtl/CortexM0_SoC.v` | CPU、存储器、外设集成与 0x4005/6/7 子译码 |
| `rtl/cortexm0ds_logic.v` | ARM 许可核心；本次未修改 |
| `rtl/AHBlite_Interconnect.v`, `AHBlite_Decoder.v`, `AHBlite_SlaveMUX.v` | 地址分发、数据阶段返回选择 |
| `rtl/AHBlite_Block_RAM.v`, `Block_RAM.v` | AHB RAM 接口、按字节写入、同步读和程序初始化 |
| `rtl/Keyboard.v`, `keyboard_filter.v` | 4×4 矩阵扫描、同步、约 20 ms 消抖、事件与保持状态 |
| `rtl/eg_adc.v` | 内置 ADC 时钟与通道 1..7 连续轮询 |
| `rtl/AHBlite_Key_UART.v` | 键盘、ADC 与 UART 的寄存器窗口 |
| `rtl/AHBlite_UART_LED.v`, `uart.v` | UART 寄存器和串行收发；LED 寄存器输出本工程未接至顶层 |
| `rtl/AHBlite_LCD.v` | LCD 手动 GPIO 与硬件颜色段，每像素五个 SoC 时钟 |
| `rtl/AHBlite_SD.v` | SPI 模式字节传输；协议和 FAT32 位于固件 |
| `rtl/audio_synth.v` | 四声道波形、滑音、混音与 10 位 PWM |
| `td/rolling_road.al`, `build.tcl` | 工程源文件清单与综合、布局、布线、时序、位流流程 |
| `td/contr.adc`, `contr.sdc`, `pll_ip.v` | 引脚、时钟约束和 32 MHz PLL |
| `build-fpga.ps1` | 美术生成、正式固件编译和完整 FPGA 构建入口 |

### 关卡、资源、工具及文档

| 文件/目录 | 责任与使用边界 |
|---|---|
| `levels/levels.json` | 当前五关判定源，03 Long road、04 Hills、05 Massif |
| `levels/appearance.json` | 调色、模型、关卡 ID 到场景的范围绑定 |
| `levels/designs/forced_jump_chain.json` | 独立设计草案，不是当前五关输入 |
| `assets/hills-background.png` | 美术参考图，本版背景实际由代码生成 |
| `tools/make_sd_levels.py` | 生成 V4 关卡包并检查格式、支撑、机关、路线和预算 |
| `tools/make_appearance.py` | 生成美术包和备用表，检查模型/调色布局与预算 |
| `tools/make_hills.py` | 会重写 Hills JSON 与 ROM 头文件；不是只读检查 |
| `tools/make_massif.py` | 从仓库外的参考数据转换 Massif；正常构建不需要外部参考文件 |
| `tools/tune_levels.py` | 会修改地图和机关；本次未执行 |
| `tools/audit_mechs.py` | 机关数学与地面可达性离线检查，不能替代实体通关 |
| `tools/make_ball.py`, `make_menu_title.py` | 生成球和标题表；标题生成依赖 Windows 字体 |
| `tools/preview_frame.py` | Python 离线画面预览；不是实体截图 |
| `tools/tb_audio.v` | 音频 RTL 常驻回归，本次已执行 |
| `tools/verify_road.c` | 历史桌面检查，仍含旧速度 625 Q8/s 断言；不能直接当作当前版本通过证据 |
| `tools/make_midterm_ppt.py` | 依赖原作者电脑上的外部 PPT，和硬件构建无关 |
| `deliverables/中期答辩_3min_讲稿.md` | 讲稿，数字反映历史版本 |
| `README.md`, `HANDOFF_GUIDE.md` | 总体操作与原环境交接；部分参数已滞后 |
| `SD_CARD.md`, `MECHANICS.md`, `HILLS.md`, `LEVEL_DESIGN.md` | 格式、机制和设计说明；应与当前源码交叉核对 |
| `VALIDATION.md` | 历次实验记录；本次实测记录见本文和 `td/bringup-*` 日志 |
| `.gitattributes`, `.gitignore` | LF 规则，以及位流/HEX/包/仿真/基准等生成物排除规则 |

## 板卡资料核对

在 `D:/weiqian/CortexM0_SoC-main/code25/原理图/` 找到并读取核心板与综合底板原理图，另参考本机课程开发工具入门资料。

- 核心板型号 EG4S20BG256；时钟 R7；低有效复位 E3；矩阵键盘行为低有效。
- 当前 LCD 是右侧并口扩展的 16 位 TFT 通路；综合底板原理图还画有另一种 LCD/RGB/VGA 接口，不能把它们当成同一个显示后端。HDMI 本版未实现。
- SD 使用 F15 CLK、F13 CS、F14 MOSI、D14 MISO；F3 是 PWM 音频。
- 内置 ADC 通道从核心板 P2 引出。综合底板另外画有 ADC081S021，本工程使用的是 FPGA 内置 ADC，不是该外部 ADC。
- 手头底板原理图的功放与滤波元件版本与仓库文字不完全相同；共同点是 F3 单线 PWM 通路。本次未据旧文字调整电路或音量。

## 本次三项修正

1. **ARM Compiler 6.24 链接超限。** 原工程历史使用 6.22，代码空间仅剩很少。本机 6.24 按原参数链接超出 0x230 字节。`build.ps1` 仅对 `road.c` 增加 `-fno-unroll-loops`，保持 `-O2`；没有扩大代码区或压缩美术缓存。独立对象对比显示循环展开是主要体积来源。
2. **当前美术包被拒绝。** 当前树模型使用 flags bit2（值 4，远景细节剔除）；生成器和渲染器都支持 0..7，加载器却只允许 0..3。`appearance.c` 改为允许三个位。修复前用实际 1024 B 包复现 `art_sd_loaded=0`，修复后为 1，损坏 CRC 仍被拒绝。
3. **启动音量没有可靠初始化。** `startup.s` 不复制 `.data`，原来 `master=10` / `scale_q8=256` 仅作为非零静态初值落在代码加载映像尾部。改为由 `sound_init()` 明确赋值；修复前模拟 RAM 中两值为零时，初始化后仍为零，修复后为 10/256。最终映像没有需要加载的 RW 初值段。

## 本次验证结果

| 检查 | 结果 |
|---|---|
| ARM Compiler for Embedded 6.24 / MDK Plus 5.43 | 正式固件编译链接成功；armasm 一条既有弃用提示 |
| 代码/加载映像 | `0x67A0` = 26528 / 28672 B，剩 2144 B |
| 静态数据 | `0x1D54` = 7508 / 8192 B，栈与运行时临时空间剩 684 B；未测峰值栈占用 |
| 美术加载检查 | 当前树包可加载；损坏包和无卡回退正常 |
| 声音软件检查 | 显式初值、音乐寄存器、暂停关闭声部和音量边界通过 |
| 本机渲染检查 | 三个 ROM 关卡、五个横向位置，共 1110 帧调用；缓存/实时投影一致，每帧恰为 76800 像素 |
| 循环参数图像对比 | 本机默认/禁用循环展开结果摘要均为 `4BDE32C2`；不是 ARM 实体性能测量 |
| 音频 RTL | `PASS: waveform present, 196/1024 duty swing` |
| 关卡包生成 | 五关 72/80/1024/288/519 行，钻石 8/6/16/12/20，机关 2/4/27/0/21 |
| 第五关离线检查 | `fully passable on the ground (519 rows)`，孤立不可达地面区域 0；不是完整玩法通关证明 |
| TD V5.6.71036 | 综合、布局布线、位流成功 |
| 资源 | LUT 7871/19600、寄存器 2604/19600、DSP 5/29、9K BRAM 8/64、32K BRAM 8/16、ADC 1、PLL 1 |
| 受约束的 32 MHz 主时钟 | setup/hold 违例均 0；最小裕量 +2.588 ns / +0.088 ns |
| JTAG 下载 | 芯片匹配，program/download 正常完成 |
| 实体反馈 | 用户确认显示选关页，KEY8 可进入游戏 |

时序检查边界：TD 仍提示 SWCLK 和一个优化命名为音频 tick 的派生时钟网未约束；外部板级输入/输出延时也没有完整约束。本次“无违例”限于现有 SDC 覆盖范围，不能写成所有时钟/IO 已完成时序签核。既有 HRESP 位宽和 ADC 保留位未驱动提示本次未改动。

检查日志位于 `td/bringup-build.log`、`build.log`、`bringup-art-check.log`、`bringup-sound-check.log`、`bringup-audio.log`、`bringup-mechanisms.log`、`bringup-render-default.log`、`bringup-render-compact.log`。本机临时检查源在被忽略的 `tmp/bringup/`，不作为固件输入。未重跑仓库缺失的历史 `modelsim/`、`bench/` 性能工程。

### 最终产物 SHA-256

| 文件 | 大小 | SHA-256 |
|---|---:|---|
| `td/rolling_road.bit` | 676183 B | `EB0122FDC7032F33D0A3C8F1B77361C69839431BFFAD89024DAE9E9412432ED2` |
| `firmware/code.hex` | 66320 B | `9AC1B144681E242D7A4EF8C9B8B7E38FDCE5BB80870FFCCAE2CAE887BA75368B` |
| `sd_payload/RSLEVELS.BIN` | 22528 B | `D7DD70DAE779021271BF50E815BF1557BD3B2E1E8C5F2AE69F39E00760709144` |
| `sd_payload/RSART.BIN` | 1024 B | `4C4354E36246BB83963F09975C445544F46E12C7DD55652B0D53F506389DD2BC` |

## 后续复现与验收

按交接约定，工具目录通过会话环境变量 `ARMCLANG_BIN`、`TD_COMMANDS_PROMPT` 设置，不把个人安装目录写入工程脚本。已在本机被忽略的 `td/local-build.ps1` 保存实际路径，供本机再次执行。

```powershell
python tools/make_sd_levels.py
./build-fpga.ps1
# 在工程根目录下载当前位流：
@('download -cable 0 -bit td/rolling_road.bit','exit') | & $env:TD_COMMANDS_PROMPT
```

本次电脑只枚举到固定磁盘 C:/D:，未发现可供复制的 SD 卡盘符，因此两个包只在电脑生成，**没有声明已写卡**。要使用五关及完整树模型，将本次两个 BIN 放入 FAT32 SDHC 卡根目录，再插回板卡，菜单按 KEY4 重读。绿色 SD 只证明关卡目录加载，仍需实际观察树模型或读取 `art_sd_loaded` 验证美术。

操作：KEY8 开始/暂停，KEY4 重读卡或重开，KEY12 返回菜单（菜单中用于手动选 ADC 通道），KEY0/KEY1 音量减/加。当前程序为倒装外壳配置，LCD `MADCTL=0xC8` 翻转 180°，KEY6/KEY14 在输入处交换，滑条方向也同步翻转；裸板观察的方向可能与旧 README 的简略键名说明不同。滑条上电后需全行程扫一次建立标定。

后续实体验收应依次补充：SD/ROM 状态、两侧横移和重开/暂停/返回、滑条标定、关卡中声音和音量、五关及长关换缓存、长时间运行。需要断电自启动时，再根据实际 Flash 器件和启动拨码安排固化流程。
