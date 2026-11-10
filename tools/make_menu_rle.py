"""Render the four production flyover scenes to indexed, sector-aligned RLE.

python tools/make_menu_rle.py
python tools/make_menu_rle.py --verify sd_payload/RSMENU.RLE
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
HEADER_BYTES = 512
INDEX = struct.Struct("<IIII")
RAW_HEADER = struct.Struct("<8sHHHHII")
MAGIC = b"RSMENU01"
SCENES = ("space", "mountains", "city", "forest")


def align_sector(value):
    return (value + 511) & ~511


def read_exact(stream, count):
    chunks = bytearray()
    while len(chunks) < count:
        chunk = stream.read(count - len(chunks))
        if not chunk:
            raise ValueError(f"Truncated input: expected {count} bytes, got {len(chunks)}")
        chunks.extend(chunk)
    return bytes(chunks)


def encode(pixels):
    values = np.frombuffer(pixels, dtype=np.uint8)
    starts = np.r_[0, np.flatnonzero(values[1:] != values[:-1]) + 1]
    lengths = np.diff(np.r_[starts, len(values)])
    parts = (lengths + 254) // 255
    original = np.repeat(np.arange(len(starts)), parts)
    part_number = np.arange(int(parts.sum())) - np.repeat(np.cumsum(parts) - parts, parts)
    records = np.empty((len(original), 2), dtype=np.uint8)
    records[:, 0] = np.minimum(255, lengths[original] - part_number * 255)
    records[:, 1] = values[starts[original]]
    return records.tobytes()


def decode(payload, pixels_per_frame):
    if not payload or len(payload) & 1:
        raise ValueError("RLE payload must contain complete two-byte records")
    records = np.frombuffer(payload, dtype=np.uint8).reshape(-1, 2)
    if np.any(records[:, 0] == 0) or int(records[:, 0].sum()) != pixels_per_frame:
        raise ValueError("Invalid run length or decoded frame size")
    return np.repeat(records[:, 1], records[:, 0]).tobytes()


def crc32(data):
    return zlib.crc32(data) & 0xffffffff


def make_header(width, height, fps, frames, duration, data_offset, size, index_crc):
    header = bytearray(HEADER_BYTES)
    header[:8] = MAGIC
    struct.pack_into("<HHHHHH", header, 8, 1, HEADER_BYTES, width, height, fps, 1)
    struct.pack_into("<IIII", header, 20, frames, duration, width * height, HEADER_BYTES)
    struct.pack_into("<HBBIII", header, 36, INDEX.size, 1, 0, data_offset, size, index_crc)
    struct.pack_into("<I", header, 508, crc32(header[:508]))
    return header


def verify(path):
    path = Path(path)
    with path.open("rb") as stream:
        header = read_exact(stream, HEADER_BYTES)
        if header[:8] != MAGIC or crc32(header[:508]) != struct.unpack_from("<I", header, 508)[0]:
            raise ValueError("Invalid video signature or header CRC")
        version, header_size, width, height, fps, denominator = struct.unpack_from("<HHHHHH", header, 8)
        frames, duration, pixels, index_offset = struct.unpack_from("<IIII", header, 20)
        entry_size, pixel_format, order, data_offset, size, index_crc = struct.unpack_from("<HBBIII", header, 36)
        if (version, header_size, entry_size, pixel_format, order) != (1, 512, 16, 1, 0):
            raise ValueError("Unsupported format")
        if not width or not height or not fps or denominator != 1 or pixels != width * height:
            raise ValueError("Invalid dimensions or frame rate")
        if frames * 1000 != duration * fps or size != path.stat().st_size:
            raise ValueError("Invalid duration or file length")
        if index_offset != 512 or data_offset != align_sector(512 + frames * INDEX.size):
            raise ValueError("Invalid directory layout")
        table = read_exact(stream, frames * INDEX.size)
        if crc32(table) != index_crc:
            raise ValueError("Invalid directory CRC")
        expected_offset = data_offset
        sizes = []
        for number in range(frames):
            offset, length, raw_crc, encoded_crc = INDEX.unpack_from(table, number * INDEX.size)
            if offset != expected_offset or length <= 0 or length & 1 or offset + length > size:
                raise ValueError(f"Invalid frame directory entry {number}")
            stream.seek(offset)
            payload = read_exact(stream, length)
            if crc32(payload) != encoded_crc:
                raise ValueError(f"Compressed CRC mismatch in frame {number}")
            raw = decode(payload, pixels)
            if crc32(raw) != raw_crc:
                raise ValueError(f"Decoded CRC mismatch in frame {number}")
            expected_offset = align_sector(offset + length)
            if any(read_exact(stream, expected_offset - offset - length)):
                raise ValueError(f"Nonzero frame padding in frame {number}")
            sizes.append(length)
        if expected_offset != size:
            raise ValueError("Unexpected trailing data")
    return {"width": width, "height": height, "fps": fps, "frames": frames,
            "duration_ms": duration, "file_bytes": size,
            "mean_frame_bytes": sum(sizes) / frames, "max_frame_bytes": max(sizes)}


def build(fps, output):
    folder = ROOT / "tmp/menu_rle"
    folder.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which(os.environ.get("CC", "gcc"))
    if not compiler:
        raise RuntimeError("Set CC to an available host C compiler")
    executable = folder / ("render_menu_frames.exe" if os.name == "nt" else "render_menu_frames")
    subprocess.run([compiler, "-std=c99", "-O2", str(ROOT / "tools/render_menu_frames.c"),
                    str(ROOT / "firmware/flyover.c"), "-o", str(executable)], check=True)
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    groups = [[] for _ in SCENES]
    try:
        with (folder / "render.log").open("wb") as log:
            process = subprocess.Popen([str(executable), str(fps)], stdout=subprocess.PIPE, stderr=log)
            try:
                raw_header = read_exact(process.stdout, RAW_HEADER.size)
                magic, width, height, actual_fps, reserved, frames, duration = RAW_HEADER.unpack(raw_header)
                if magic != b"RSRAW001" or actual_fps != fps or reserved or duration != 80000:
                    raise ValueError("Unexpected renderer output")
                if (width, height) != (360, 640):
                    raise ValueError("Renderer must use the native 360x640 canvas")
                data_offset = align_sector(512 + frames * INDEX.size)
                table = bytearray(frames * INDEX.size)
                with temporary.open("w+b") as stream:
                    stream.write(bytes(data_offset))
                    for number in range(frames):
                        raw = read_exact(process.stdout, width * height)
                        payload = encode(raw)
                        if decode(payload, width * height) != raw:
                            raise ValueError(f"Encoder round-trip mismatch in frame {number}")
                        offset = stream.tell()
                        INDEX.pack_into(table, number * INDEX.size, offset, len(payload), crc32(raw), crc32(payload))
                        stream.write(payload)
                        stream.write(bytes(align_sector(stream.tell()) - stream.tell()))
                        groups[number // (20 * fps)].append(len(payload))
                        if (number + 1) % (20 * fps) == 0:
                            print(f"Encoded {number + 1}/{frames} frames", flush=True)
                    if process.stdout.read(1):
                        raise ValueError("Renderer emitted unexpected trailing bytes")
                    if process.wait() != 0:
                        raise RuntimeError(f"Renderer failed; inspect {folder / 'render.log'}")
                    size = stream.tell()
                    stream.seek(0)
                    stream.write(make_header(width, height, fps, frames, duration, data_offset, size, crc32(table)))
                    stream.write(table)
                    stream.flush()
                    os.fsync(stream.fileno())
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait()
                process.stdout.close()
        stats = verify(temporary)
        with temporary.open("rb") as source:
            digest = hashlib.file_digest(source, "sha256").hexdigest()
        temporary.replace(output)
        stats["sha256"] = digest
        stats["pixel_format"] = "RGB332"
        stats["pixel_order"] = "portrait row-major, top-left origin"
        stats["ui_included"] = False
        stats["scenes"] = [dict(name=name, mean_frame_bytes=sum(values)/len(values),
                                max_frame_bytes=max(values), duration_ms=20000)
                           for name, values in zip(SCENES, groups)]
        output.with_name(output.stem + "_INFO.json").write_text(
            json.dumps(stats, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(json.dumps(stats, indent=2, ensure_ascii=False))
        return stats
    finally:
        if temporary.exists():
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fps", type=int, default=30, choices=range(1, 61), metavar="1..60")
    parser.add_argument("--output", type=Path, default=ROOT / "sd_payload/RSMENU.RLE")
    parser.add_argument("--verify", type=Path)
    args = parser.parse_args()
    if args.verify:
        print(json.dumps(verify(args.verify), indent=2))
    else:
        build(args.fps, args.output)


if __name__ == "__main__":
    main()
