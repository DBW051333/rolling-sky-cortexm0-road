# 霓虹街景首页与角色皮肤（2026-11-09）

> 当前版本已改为 12 款纯小球皮肤，人物皮肤及 `player_pixels.h` 已删除。
> `tools/make_players.py` 现在生成 `firmware/player_skins.h` 名称和调色板，纹样在 `firmware/player.c` 中计算。
> 当前清单及实现见 [README](../README.md)，构建和实物进展见 [VALIDATION](../VALIDATION.md)。以下保留早期 8 款皮肤交付记录，非当前实现。

本次将静态蓝色卡片菜单替换为原创霓虹像素街景，并把 8 款可选角色接入实际道路渲染。画面预览来自编译运行本仓库 C 渲染器的输出，不是设计稿或板卡实拍。

## 操作与外观

- KEY2 在 LEVEL 与 SKIN 间切换，焦点边框分别为亮青色和亮粉色。
- 当前倒装配置：实体 KEY14 上一项，实体 KEY6 下一项；关卡有边界，皮肤首尾循环。两键同时按下不切换。
- SKIN 中 KEY8 应用并回到 LEVEL；LEVEL 中 KEY8 启动关卡。
- 菜单 KEY4 重新读 SD 卡，KEY12 轮换 ADC 通道，KEY0/1 调整音量。
- 返回首页、重开和切换关卡保留皮肤；重新配置 FPGA 恢复默认金球，尚无跨断电存储。

背景含双层横向视差楼群、像素霓虹商铺、窗格、雨线、路灯、地面反光和经过街道的飞行出租车。滚动位置来自真实毫秒，跨楼块边界延续世界索引。刷新调度间隔 100 ms，实际刷新率需要板上观察。每帧直接逐行合成后输出，不先清屏。

| 编号 | 皮肤 | 设计 |
| --- | --- | --- |
| 01 | Gold Core | 原有金色球，保留默认外观 |
| 02 | Ion Cyan | 青色金属球，洋红光带 |
| 03 | Void Orb | 紫色能量球，青色赤道环 |
| 04 | Pink Nova | 粉色球体，多道明亮纬线 |
| 05 | GPT Muse | 翡翠双马尾、悬浮电路光环、白色科技裙 |
| 06 | DeepSeek | 海蓝短发、鲸鳍耳机、蓝白水手科技服 |
| 07 | Claude | 珊瑚暖色头发、蝴蝶结、奶油色披风与星形饰物 |
| 08 | GLM Neko | 紫色头发、几何猫耳天线、青色机甲外套 |

四位像素少女是按品牌主题与配色创作的原创拟人形象，并非官方角色，也未拷贝网络同人画。美术源为 `tools/make_players.py` 中可逐像素编辑的 17×17 图案；生成 RLE 后以 4 倍整数放大显示。球体沿用原有球面阴影，通过调色板和发光纹带区分。

角色保持原有 68×68 显示范围、中心与碰撞逻辑。每条扫描线只取一次皮肤游程，角色绘制与钻石遮挡共用同一份轮廓；所有皮肤支持抬升、坠落、撞击闪红与消失。

## 源码与资源

- `firmware/menu.c`：程序化街景、字体、面板、角色预览和连续扫描线输出。
- `firmware/player.c`：皮肤状态、调色板、球体装饰、角色逐行 RLE 读取。
- `firmware/main.c`：焦点/按键、皮肤应用、首页状态与刷新调度。SD 机关数仅在换关或重新读卡后读取，动画刷新不重复读 SD。
- `firmware/road.c`：共享皮肤轮廓与钻石遮挡，向菜单借出已有扫描线缓存。
- `tools/make_players.py` → `firmware/player_pixels.h`：原创像素美术与确定性生成器，完整构建自动调用。

菜单与游戏串行执行，共用原有 480 B 扫描线，没有新增全屏帧缓冲。霓虹球的临时行缓存仅 12 B，少女游程直接从 ROM 读取。将墙面投影准备移到独立的绘制入口，再调用禁止内联的扫描线函数，避免两个大栈帧同时存在；菜单局部状态也放在独立函数中，不占用游戏常驻栈。

ARM Compiler 6.24 当前构建：

| 项目 | 占用 | 限额 / 余量 |
| --- | --- | --- |
| 代码及只读数据 | 28,192 B | 28,672 B，余 480 B |
| 静态数据 RAM | 7,540 B | 8,192 B，留栈 652 B |
| 链接器静态最长调用栈 | 520 B | 另补未解析的回调与异常入栈 |

链接器最长路径为 `main → road_draw → draw → backdrop_line → composite_segment → ball_segment → put_buffered`。手动补查直接输出的 `span` 回调、8 B 汇编中断包装、硬件异常栈及对齐、KEY ISR 后，当前保守预算约 580 B，小于 652 B。该值是当前工具链的静态核查，不是板上实测栈水位。构建会保留 `firmware/Output/code.callgraph.html` 供后续修改复查。

内存地址、代码/美术缓存分区、RTL、引脚、SD 格式均沿用现有项目。这次资源放入固件 ROM，无需向 SD 卡写入新文件。

## 已完成的软件验证

`tools/verify_cyber.c` 编译的是实际菜单、角色、道路、关卡、墙、玻璃和美术模块，SD 访问用 ROM 测试桩替代：

- 8 款皮肤的 544 条源扫描线，检查非零游程及每行总宽 68。
- 64 帧动画菜单，覆盖 SD、ROM、错误状态和 24 关目录边界。
- 40 帧角色场景：正常、跳跃、撞击闪红、坠落出屏、结束消失；检查可见不透明像素保留角色颜色。
- 8 款皮肤 × 3 个 ROM 关卡 × 37 个前进阶段 × 5 条横向位置 × 2 种投影，共 8,880 帧；缓存与实时计算投影逐像素摘要一致。
- 所有帧断言 320 行，每行 240 像素，总计 76,800 像素，禁止越界或零长输出。

复现（PowerShell，需 GCC 和 Python/Pillow）：

```powershell
New-Item -ItemType Directory -Force preview/cyber | Out-Null
gcc -std=c99 -O2 -Wall -Wextra -DROAD_BENCHMARK tools/verify_cyber.c firmware/menu.c firmware/player.c firmware/road.c firmware/level.c firmware/obstacle.c firmware/walls.c firmware/glass.c firmware/appearance.c -o preview/verify_cyber.exe
./preview/verify_cyber.exe preview/cyber
python tools/preview_cyber.py
```

生成 `preview/cyber/home.png`、`skins.png`、`game.png`、`menu.gif`，均为本地忽略的可再生预览。游戏预览直接调用道路渲染器，不包含主循环的 HUD 覆盖层。

## FPGA 与实体

ARM Compiler 6.24、TD V5.6.71036 完成固件编译、综合、布局布线与位流生成；没有扩大内存分区。资源为 LUT 7,871、寄存器 2,604、DSP 5、9K BRAM 8、32K BRAM 8、ADC 1、PLL 1。32 MHz 主时钟报告 setup slack +2.588 ns、hold slack +0.088 ns；原工程仍有 SWCLK 与音频派生时钟的未约束警告，此结果不表示全时钟域签核。

2026-11-09 14:10（本机 Asia/Shanghai 时间）经现有 AL-Link Mini 执行 SRAM JTAG 下载，工具确认 `Chip validation success: EAGLE_S20_BG256`，program 命令约 6.58 s 完成，download 命令约 6.80 s 完成。未执行 SPI Flash 固化，断电后仍需重新下载。

当前已下载文件：

```text
td/rolling_road.bit  676183 bytes
SHA256 D29628156580FC6FFD46402C3096E8E9F23A872EB7FCC447A2A3C224B170EE24

firmware/code.hex
SHA256 E922AFFC997FC45C808D08542E8657C93A4F699CE62CEEF1A7722A13A4B3ACD0
```

构建/下载日志在 `td/cyber-build.log`、`td/build.log`、`td/cyber-download.log`，按仓库规则本地忽略。加入调用图输出后重编译固件，HEX 的 SHA-256 与生成并下载位流时完全相同。

已向用户请求观察滚动街景，以及 KEY2 → 切换少女皮肤 → KEY8 应用 → KEY8 启动游戏。板上 LCD 外观、动画速度及新皮肤按键尚待本轮用户现场确认；此前用户确认的是旧版选关页与 KEY8 开始游戏。
