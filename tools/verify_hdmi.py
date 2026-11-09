"""Independent TMDS reference, complete raster checks and vendor DDR simulation.

Run from the repo root: python tools/verify_hdmi.py
Requires Icarus Verilog and TD's unmodified functional EG_LOGIC_ODDR model.
"""
from pathlib import Path
import os
import random
import subprocess

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
(ROOT / "tmp").mkdir(exist_ok=True)
td_dir = Path(os.environ.get("TD_HOME", "D:/Program Files (x86)/alogic"))
vendor_io = td_dir / "sim_release/eg/eg_logic_io.v"
vendor_global = td_dir / "sim_release/eg/eg_phy_glbl.v"


def reference(byte, de, control, running):
    if not de:
        return (0b1101010100, 0b0010101011, 0b0101010100, 0b1010101011)[control], 0
    xnor = byte.bit_count() > 4 or (byte.bit_count() == 4 and byte % 2 == 0)
    bits = [byte & 1]
    for k in range(1, 8):
        bits.append(bits[-1] ^ ((byte >> k) & 1) ^ xnor)
    payload = sum(b << k for k, b in enumerate(bits))
    qm8 = int(not xnor)
    ones = sum(bits)
    if running == 0 or ones == 4:
        invert = not qm8
    else:
        invert = (running > 0 and ones > 4) or (running < 0 and ones < 4)
    symbol = (int(invert) << 9) | (qm8 << 8) | (payload ^ (255 if invert else 0))
    # Count actual emitted bits, independently of RTL disparity update formulae.
    running += 2 * symbol.bit_count() - 10
    assert abs(running) <= 8, running
    return symbol, running


rng = random.Random(20261109)
samples = [(0, 0, c) for c in range(4)]
samples += [(b, 1, 0) for b in range(256)] * 3
for b in range(256):
    samples += [(b, 1, 0)] * 12 + [(b, 0, b % 4)]
samples += [(rng.randrange(256), int(rng.random() > 0.05), rng.randrange(4)) for _ in range(5000)]
running = 0
lines = []
for byte, de, control in samples:
    symbol, running = reference(byte, de, control, running)
    lines.append(f"{(de << 20) | (control << 18) | (byte << 10) | symbol:06x}\n")
(ROOT / "tmp/hdmi_vectors.hex").write_text("".join(lines))


def run(top, *args):
    output = f"tmp/{top}.vvp"
    subprocess.run(["iverilog", "-g2012", "-s", top, "-s", "glbl", "-o", output,
                    "-DHDMI_SIM_RAM", "rtl/hdmi_test.v", "rtl/hdmi_game.v", "rtl/AHBlite_Video.v",
                    "tools/tb_hdmi.v", "tools/tb_hdmi_game.v", str(vendor_io), str(vendor_global)], check=True)
    result = subprocess.run(["vvp", output, *args], check=True, capture_output=True, text=True)
    print(result.stdout, end="")
    if "PASS " not in result.stdout or "FATAL" in result.stdout or "ERROR" in result.stdout:
        raise RuntimeError(result.stdout + result.stderr)


run("tb_hdmi_encoder", f"+COUNT={len(samples)}")
run("tb_hdmi_pattern")
for phase in (0, 0.7, 2.4, 4.9, 12.3):
    run("tb_hdmi_serializer", f"+PHASE={phase}")
run("tb_game_writer")
run("tb_game_scaler")
