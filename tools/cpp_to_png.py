#!/usr/bin/env python3
"""
Render LVGL pixel-art .cpp files as scaled PNGs (each pixel → scale×scale square).
Usage: python3 tools/cpp_to_png.py src/sprites/grot_0.cpp [scale]
  scale  Pixel scale (default 8 → 8×8 blocks).
Output: src/sprites/grot_0_8x.png (or similar alongside the .cpp).
"""

import re
import sys
from pathlib import Path
from PIL import Image


def parse_cpp(path):
    text = path.read_text()
    # Find array name and bytes: static const ... uint8_t _NAME_map[] = { 0x00, ... };
    arr_match = re.search(r"uint8_t _(\w+)_map\[\] = \{([^}]+)\}", text, re.DOTALL)
    # Find dimensions: .w = N, .h = M
    w_match = re.search(r"\.w\s*=\s*(\d+)", text)
    h_match = re.search(r"\.h\s*=\s*(\d+)", text)
    if not (arr_match and w_match and h_match):
        raise SystemExit(f"Could not parse {path}")
    name = arr_match.group(1)
    w, h = int(w_match.group(1)), int(h_match.group(1))
    hex_str = arr_match.group(2)
    bytes_list = []
    for m in re.finditer(r"0x([0-9A-Fa-f]{2})", hex_str):
        bytes_list.append(int(m.group(1), 16))
    return name, w, h, bytes(bytes_list)


def rgb565_to_rgb(lo, hi):
    val = lo | (hi << 8)
    r = (val >> 11) << 3
    g = ((val >> 5) & 0x3F) << 2
    b = (val & 0x1F) << 3
    return (r, g, b)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    cpp_path = Path(sys.argv[1])
    scale = int(sys.argv[2]) if len(sys.argv) >= 3 else 8
    if not cpp_path.is_file():
        raise SystemExit(f"File not found: {cpp_path}")

    name, w, h, data = parse_cpp(cpp_path)
    n_pixels = w * h
    rgb_size = n_pixels * 2
    if len(data) < rgb_size + n_pixels:
        raise SystemExit(f"Data too short for {w}x{h} RGB565A8")
    rgb_bytes = data[:rgb_size]
    alpha_bytes = data[rgb_size : rgb_size + n_pixels]

    pixels = []
    for i in range(n_pixels):
        r, g, b = rgb565_to_rgb(rgb_bytes[i * 2], rgb_bytes[i * 2 + 1])
        a = alpha_bytes[i]
        pixels.append((r, g, b, a))

    img = Image.new("RGBA", (w, h))
    img.putdata(pixels)

    # Scale: each pixel → scale×scale block (nearest neighbor)
    out_w, out_h = w * scale, h * scale
    out = Image.new("RGBA", (out_w, out_h))
    for y in range(h):
        for x in range(w):
            box = (x * scale, y * scale, (x + 1) * scale, (y + 1) * scale)
            out.paste(img.getpixel((x, y)), box)
    out_path = cpp_path.with_name(f"{name}_{scale}x.png")
    out.save(out_path)
    print(f"Saved {out_path} ({out_w}x{out_h})")


if __name__ == "__main__":
    main()
