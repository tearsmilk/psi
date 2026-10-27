#!/usr/bin/env python3
"""Renders the Ψ app icon as an .iconset folder (PNG set) using only the standard library.

Usage: make_icon.py Psi.iconset
Then: iconutil -c icns Psi.iconset -o Psi.icns
"""
import math
import os
import struct
import sys
import zlib

ACCENT = (0xD9, 0x77, 0x57)
IVORY = (0xFA, 0xF8, 0xF3)


def png(width: int, height: int, rows: list[bytes]) -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + r for r in rows)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def render(size: int) -> bytes:
    """Signed-distance rendering: one distance per pixel, anti-aliased over one pixel."""
    s = size
    r = s * 0.22          # corner radius
    stroke = max(1.5, s * 0.075)
    cx = 0.5
    stem_top, stem_bottom = 0.22, 0.80
    bowl_top, bowl_bottom = 0.28, 0.58
    bowl_w = 0.22
    segs = [(cx, stem_top, cx, stem_bottom), (cx - 0.16, stem_bottom, cx + 0.16, stem_bottom)]
    n = 20
    pts = [(cx - bowl_w * math.cos(math.pi * i / n), bowl_top + (bowl_bottom - bowl_top) * math.sin(math.pi * i / n)) for i in range(n + 1)]
    segs += [(ax, ay, bx, by) for (ax, ay), (bx, by) in zip(pts, pts[1:])]
    segs = [(ax * s, ay * s, bx * s, by * s) for ax, ay, bx, by in segs]
    pre = [(ax, ay, bx - ax, by - ay, (bx - ax) ** 2 + (by - ay) ** 2) for ax, ay, bx, by in segs]

    def inside_rounded(x: float, y: float) -> float:
        qx = abs(x - s / 2) - (s / 2 - r)
        qy = abs(y - s / 2) - (s / 2 - r)
        return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - r

    def glyph_dist(x: float, y: float) -> float:
        best = 1e9
        for ax, ay, dx, dy, l2 in pre:
            t = ((x - ax) * dx + (y - ay) * dy) / l2
            t = 0.0 if t < 0 else 1.0 if t > 1 else t
            d = math.hypot(x - ax - t * dx, y - ay - t * dy)
            if d < best:
                best = d
        return best

    rows = []
    for py in range(s):
        row = bytearray()
        y = py + 0.5
        for px in range(s):
            x = px + 0.5
            bg = inside_rounded(x, y)
            if bg > 0.5:
                row += b"\x00\x00\x00\x00"
                continue
            a = min(1.0, 0.5 - bg)
            g = glyph_dist(x, y) - stroke / 2
            t = min(1.0, max(0.0, 0.5 - g))
            col = tuple(int(ACCENT[i] * (1 - t) + IVORY[i] * t) for i in range(3))
            row += bytes((col[0], col[1], col[2], int(255 * a)))
        rows.append(bytes(row))
    return png(s, s, rows)


def main() -> int:
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for base in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            size = base * scale
            name = f"icon_{base}x{base}" + ("@2x" if scale == 2 else "") + ".png"
            with open(os.path.join(out, name), "wb") as f:
                f.write(render(size))
    print("iconset written to", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
