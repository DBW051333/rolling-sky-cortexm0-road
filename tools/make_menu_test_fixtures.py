"""Extract two real RLE frames for the menu decoder and HDMI RTL regressions."""
from pathlib import Path
from make_menu_rle import INDEX, ROOT, align_sector, decode


def main():
    destination = ROOT / "tmp/menu_player"
    destination.mkdir(parents=True, exist_ok=True)
    payloads = bytearray()
    pixels = bytearray()
    constants = []
    ring_start = 0x3FFE00
    with (ROOT / "sd_payload/RSMENU.RLE").open("rb") as source:
        for slot, frame in enumerate((10, 1210)):
            source.seek(512 + frame * INDEX.size)
            offset, length, _, crc = INDEX.unpack(source.read(INDEX.size))
            source.seek(offset)
            payload = source.read(length)
            ring_offset = (ring_start + len(payloads)) & 0x3FFFFF
            constants.append(
                f"localparam LENGTH{slot}={length}, CRC{slot}=32'h{crc:08x}, "
                f"OFFSET{slot}=22'h{ring_offset:06x}, TIME{slot}={frame * 1000 // 30};"
            )
            pixels.extend(decode(payload, 360 * 640))
            payloads.extend(payload)
            payloads.extend(bytes(align_sector(length) - length))
    constants.insert(0, f"localparam PAYLOAD_BYTES={len(payloads)};")
    (destination / "fixture.vh").write_text("\n".join(constants) + "\n")
    for filename, data in (("compressed.hex", payloads), ("decoded.hex", pixels)):
        (destination / filename).write_text("".join(f"{value:02x}\n" for value in data))
    print(f"Extracted {len(payloads)} compressed bytes and {len(pixels)} decoded pixels")


if __name__ == "__main__":
    main()
