# -*- coding: utf-8 -*-
"""把开题报告改造成 3 分钟中期答辩版（9 页）。

原则：不重做版式，只改文字与页序——原文件的母版、配色、图片、表格样式全部沿用。
删除开题专属的页面（问题详解、实施顺序、风险预案），保留可作为进度证据的页面。

用法：
    python tools/make_midterm_ppt.py
"""
import copy
import os
import shutil

from pptx import Presentation
from pptx.oxml.ns import qn

SRC = r"C:\Users\27775\Downloads\滚动的天空_开题报告_动图与封面更新版.pptx"
OUT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "deliverables")
DST = os.path.join(OUT_DIR, "滚动的天空_中期答辩_3min.pptx")

# 1-based：保留这些页，其余删除
KEEP = [1, 2, 5, 8, 9, 11, 16, 19, 21]


# ---------------------------------------------------------------- 文本工具
def _template(tf):
    """取第一段第一 run 的 rPr 与第一段的 pPr 作为格式模板。"""
    rpr = ppr = None
    for p in tf.paragraphs:
        if rpr is None and p.runs:
            e = p.runs[0]._r.find(qn("a:rPr"))
            if e is not None:
                rpr = copy.deepcopy(e)
        if ppr is None:
            e = p._p.find(qn("a:pPr"))
            if e is not None:
                ppr = copy.deepcopy(e)
    return rpr, ppr


def write(tf, lines):
    """按行重写 text_frame，沿用原有字符与段落格式。"""
    if isinstance(lines, str):
        lines = lines.split("\n")
    rpr, ppr = _template(tf)
    for p in list(tf.paragraphs)[1:]:
        p._p.getparent().remove(p._p)
    for i, line in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        for r in list(p.runs):
            r._r.getparent().remove(r._r)
        old = p._p.find(qn("a:pPr"))
        if old is not None:
            p._p.remove(old)
        if ppr is not None:
            p._p.insert(0, copy.deepcopy(ppr))
        run = p.add_run()
        run.text = line
        cur = run._r.find(qn("a:rPr"))
        if cur is not None:
            run._r.remove(cur)
        if rpr is not None:
            run._r.insert(0, copy.deepcopy(rpr))


def by_id(slide, shape_id):
    for sh in slide.shapes:
        if sh.shape_id == shape_id:
            return sh
    raise KeyError("shape id %s not in slide" % shape_id)


def put(slide, shape_id, text):
    write(by_id(slide, shape_id).text_frame, text)


def cell(slide, table_idx, row, col, text):
    tb = [sh for sh in slide.shapes if sh.has_table][table_idx].table
    write(tb.cell(row, col).text_frame, text)


# ---------------------------------------------------------------- 各页改稿
def fix_cover(s):
    put(s, 2, "中期检查报告  ·  2026.11")
    put(s, 11, "2026 年 11 月")


def fix_toc(s):
    items = [
        ("进度总览", "七项基础与四项进阶，逐条对照"),
        ("3D 道路渲染", "逐行深度映射与柱体投影，已实机"),
        ("SD 关卡包 V4", "五关 1,983 行，钻石与机关扩展表"),
        ("音视频与判定", "TFT 直写通路、四声道合成、机关系统"),
        ("资源占用", "逻辑、存储、时序与帧时间余量"),
        ("剩余 7 天", "五件事，HDMI 优先"),
    ]
    title_ids = [8, 12, 16, 20, 24, 28]
    sub_ids = [9, 13, 17, 21, 25, 29]
    for (t, d), ti, si in zip(items, title_ids, sub_ids):
        put(s, ti, t)
        put(s, si, d)


def fix_progress(s):
    put(s, 2, "03 / 进度总览")
    put(s, 3, "十一项目标：七项达成，三项在途，一项未启")
    head = ["层级", "目标", "完成状态", "证据"]
    for c, v in enumerate(head):
        cell(s, 0, 0, c, v)
    rows = [
        ("基础", "道路 3D 视觉", "已完成　逐行深度映射、柱体顶点投影", "实机录像"),
        ("基础", "SD 关卡文件", "已完成　V4 五关，钻石表与机关表", "解析与切关"),
        ("基础", "美术与判定分离", "已完成　两包分离，八套场景配色", "换包对比"),
        ("基础", "TFT／HDMI 音视频", "部分　TFT 240×320＋喇叭已通；HDMI 未接", "帧计数 / 录像"),
        ("基础", "菜单与操作", "已完成　选关—暂停—重开闭环", "固定操作序列"),
        ("基础", "直滑 ADC", "已完成　七通道自选＋行程自标定", "端点与读数"),
        ("基础", "碰撞、掉落与跳跃", "已完成　六类机关、玻璃与传送门", "边界测试"),
        ("进阶", "关卡制作工具", "部分　命令行打包 / 审计 / 预览，网页 GUI 未做", "新关上板"),
        ("进阶", "画面性能提升", "TFT 已超指标；HDMI 320×240 未做", "同场景帧计数"),
        ("进阶", "复杂判定扩展", "已完成　激光、飞锤、滚轮、移动方块", "逐状态测试"),
        ("进阶", "外壳设计与实装", "未开始　关键尺寸与孔位已确认", "实物装配"),
    ]
    for r, (lv, name, stat, ev) in enumerate(rows, start=1):
        cell(s, 0, r, 0, lv)
        cell(s, 0, r, 1, name)
        cell(s, 0, r, 2, stat)
        cell(s, 0, r, 3, ev)


def fix_road(s):
    put(s, 34, "02 / 已完成")
    put(s, 2, "道路与柱体的 3D 效果：均已实机")
    put(s, 16, "相机前进改变世界行号，横移改变投影中心；逐行填充颜色区间，整帧 29.2 ms（仿真，30 fps 调度）。")
    put(s, 33, "实机画面 · 240×320")


def fix_level(s):
    put(s, 38, "03 / 已完成")
    put(s, 2, "V4 关卡包：五关 1,983 行，22,528 B")
    put(s, 6, "判定包 22,528 B · 五关 · 1,983 行 · 小端存储")
    put(s, 8, "01 Warmup\n72 行")
    put(s, 9, "02 Changing lanes\n80 行")
    put(s, 10, "03 Long road\n1,024 行")
    put(s, 11, "04 Hills\n288 行")
    put(s, 18, "RSLEVEL4 ｜ 关数=5 ｜ 每行8B\n目录：(72,1)、(80,3)、(1024,5)、(288,21)、(519,26)")
    put(s, 19, "每行 8 B 判定位掩码；V4 另附钻石表与机关表")
    put(s, 36, "读法：1F（11111）五列都有地面，04（00100）中央第 3 列有跳板。\n05 Massif 519 行、20 钻石、21 机关，位于扇区 26 起。")


def fix_av(s):
    put(s, 39, "04 / 已完成")
    put(s, 2, "TFT 直写通路与四声道音频合成")
    tb = [sh for sh in s.shapes if sh.has_table][0].table
    write(tb.cell(0, 1).text_frame, "开题指标")
    write(tb.cell(0, 2).text_frame, "完成情况")
    write(tb.cell(1, 1).text_frame, "240×320，RGB565，≥15fps")
    write(tb.cell(1, 2).text_frame, "已完成，29.2 ms/帧")
    write(tb.cell(2, 1).text_frame, "160×120 → 320×240，≥30fps")
    write(tb.cell(2, 2).text_frame, "未接入，方案已定")
    put(s, 21, "HDMI：设计已定，11/4 起实现")
    put(s, 36, "音频：4 声道波表合成器 · 31.25 kHz 载波 · 11 级主音量")
    put(s, 37, "三声部 8 小节循环（Am–F–C–G）＋鼓组，第 4 声道留给音效；PWM 经两级 RC 低通进 PAM8403。")
    put(s, 38, "音乐只在关卡内播放，菜单、暂停与结算无声，音效照常；合成在 FPGA 内完成，CPU 每帧最多写 12 个寄存器。")


def fix_data(s):
    put(s, 2, "04 / 资源占用")
    put(s, 3, "逻辑宽松、存储吃紧：程序区已用到 99.7%")
    head = ["项目", "容量 / 要求", "实测占用", "余量"]
    for c, v in enumerate(head):
        cell(s, 0, 0, c, v)
    rows = [
        ("逻辑单元 LUT", "19,600", "7,929（40.5%）", "余 11,671"),
        ("触发器 FF", "19,600", "2,623（13.4%）", "宽松"),
        ("DSP 乘法器", "29", "5（17.2%）", "余 24"),
        ("9K BRAM", "64", "8（12.5%）", "余 56"),
        ("程序 BRAM", "28,672 B", "28,576 B（99.7%）", "余 96 B"),
        ("数据 RAM", "8,192 B", "7,508 B（91.7%）", "栈余 684 B"),
        ("时序 @ 32 MHz", "Setup / Hold > 0", "+3.18 ns / +0.15 ns", "通过"),
        ("TFT 帧时间", "≤ 66 ms（15fps）", "29.2 ms，近墙 34.1 ms", "超 2 倍"),
    ]
    for r, (a, b, c, d) in enumerate(rows, start=1):
        cell(s, 0, r, 0, a)
        cell(s, 0, r, 1, b)
        cell(s, 0, r, 2, c)
        cell(s, 0, r, 3, d)


def fix_plan(s):
    put(s, 2, "06 / 剩余计划")
    put(s, 3, "11/3 已完成与 11/4—11/10 待完成")
    put(s, 7, "已完成（截至 11/3）")
    put(s, 8, "待完成（11/4—11/10）")
    put(s, 9, "基础七项：六项已完成，仅差 HDMI √")
    put(s, 10, "TFT 原生 240×320，29.2 ms/帧   √")
    put(s, 11, "TFT＋喇叭出声，四声道合成   √")
    put(s, 12, "进阶：复杂判定扩展超额完成 √")
    put(s, 19, "外壳方案与关键尺寸已确认 √")
    put(s, 14, "HDMI 通路：160×120 先行，再冲 320×240")
    put(s, 15, "网页关卡工具：图形化编辑与一键导出")
    put(s, 16, "机关与画面、判定一致性回归")
    put(s, 17, "连续 30 min 运行与端到端联测")
    put(s, 20, "外壳 3D 打印与整机装配")


def fix_end(s):
    put(s, 7, "敬请批评指正　·　11/10 终期带来完整演示")


# ---------------------------------------------------------------- 主流程
def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    shutil.copyfile(SRC, DST)
    prs = Presentation(DST)
    slides = list(prs.slides)

    fix_cover(slides[0])
    fix_toc(slides[1])
    fix_progress(slides[4])
    fix_road(slides[7])
    fix_level(slides[8])
    fix_av(slides[10])
    fix_data(slides[15])
    fix_plan(slides[18])
    fix_end(slides[20])

    # 删页：只留 KEEP 中的页
    sld_id_lst = prs.slides._sldIdLst
    for i, sld in enumerate(list(sld_id_lst), start=1):
        if i not in KEEP:
            sld_id_lst.remove(sld)

    # 页脚统一 + 页码重编
    for n, slide in enumerate(prs.slides, start=1):
        for sh in slide.shapes:
            if not sh.has_text_frame:
                continue
            t = sh.text_frame.text.strip()
            if t == "滚动的天空  /  项目开题":
                put(slide, sh.shape_id, "滚动的天空  /  中期答辩")
            elif t.isdigit() and len(t) == 2 and sh.top > prs.slide_height * 0.9:
                put(slide, sh.shape_id, "%02d" % n)

    prs.save(DST)
    print("pages:", len(prs.slides._sldIdLst), "->", DST)


if __name__ == "__main__":
    main()
