"""Generates src/app.ico: a rounded touchpad with edge-zone bars.

Run from the repository root:  python tools/make_icon.py
The icon is written as multi-size 32bpp DIB entries so it needs no image
library and renders crisply in Explorer, the taskbar and Alt-Tab.
"""

import math
import struct
from pathlib import Path

SIZES = (16, 20, 24, 32, 40, 48, 64, 256)
SUPERSAMPLE = 4

BODY = (60, 140, 231)     # medium blue: readable on light and dark taskbars
BODY_EDGE = (36, 98, 175)
BAR = (255, 255, 255)
BUTTON_LINE = (222, 238, 255)


def rounded_rect(px, py, x0, y0, x1, y1, radius):
    """Signed-distance style coverage for a rounded rectangle."""
    cx = (x0 + x1) / 2.0
    cy = (y0 + y1) / 2.0
    hx = (x1 - x0) / 2.0 - radius
    hy = (y1 - y0) / 2.0 - radius
    qx = abs(px - cx) - hx
    qy = abs(py - cy) - hy
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    inside = min(max(qx, qy), 0.0)
    return outside + inside - radius


def sample(x, y, size):
    """Returns RGBA for one supersampled point in pixel units."""
    unit = size / 16.0
    margin = 1.4 * unit
    body = rounded_rect(x, y, margin, margin * 0.8, size - margin, size - margin * 1.2,
                        3.0 * unit)
    if body > 0.0:
        return None

    bar_top = 4.5 * unit
    bar_bottom = size - 5.5 * unit
    bar_width = max(1.0, 1.35 * unit)
    left_bar = (1.9 * unit, 2.9 * unit)
    right_bar = (size - 2.9 * unit, size - 1.9 * unit)

    if bar_top <= y <= bar_bottom:
        if left_bar[0] <= x <= left_bar[1] or right_bar[0] <= x <= right_bar[1]:
            return BAR

    button_y = size - 4.6 * unit
    if abs(y - button_y) <= max(0.6, 0.75 * unit) and 3.2 * unit <= x <= size - 3.2 * unit:
        return BUTTON_LINE

    # Slightly darker rim so the shape stays readable at 16 px.
    if body > -0.9 * unit:
        return BODY_EDGE
    return BODY


def render(size):
    rows = []
    for py in range(size):
        row = []
        for px in range(size):
            r = g = b = a = 0.0
            for sy in range(SUPERSAMPLE):
                for sx in range(SUPERSAMPLE):
                    x = px + (sx + 0.5) / SUPERSAMPLE
                    y = py + (sy + 0.5) / SUPERSAMPLE
                    colour = sample(x, y, size)
                    if colour is None:
                        continue
                    r += colour[0]
                    g += colour[1]
                    b += colour[2]
                    a += 255.0
            total = SUPERSAMPLE * SUPERSAMPLE
            if a == 0.0:
                row.append((0, 0, 0, 0))
            else:
                coverage = a / total
                row.append((
                    int(round(r / (a / 255.0))),
                    int(round(g / (a / 255.0))),
                    int(round(b / (a / 255.0))),
                    int(round(coverage)),
                ))
        rows.append(row)
    return rows


def dib(rows, size):
    header = struct.pack(
        "<IiiHHIIiiII",
        40,            # biSize
        size,          # biWidth
        size * 2,      # biHeight: XOR + AND masks
        1,             # biPlanes
        32,            # biBitCount
        0,             # biCompression (BI_RGB)
        size * size * 4,
        0, 0, 0, 0,
    )

    xor = bytearray()
    for row in reversed(rows):
        for (r, g, b, a) in row:
            xor += bytes((b, g, r, a))

    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray()
    for row in reversed(rows):
        bits = bytearray(mask_stride)
        for x, (_, _, _, a) in enumerate(row):
            if a == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits

    return header + bytes(xor) + bytes(mask)


def main():
    images = [(size, dib(render(size), size)) for size in SIZES]

    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for size, data in images:
        out += struct.pack(
            "<BBBBHHII",
            size if size < 256 else 0,
            size if size < 256 else 0,
            0, 0, 1, 32, len(data), offset,
        )
        offset += len(data)
    for _, data in images:
        out += data

    target = Path(__file__).resolve().parent.parent / "src" / "app.ico"
    target.write_bytes(bytes(out))
    print(f"wrote {target} ({len(out)} bytes, {len(images)} sizes)")


if __name__ == "__main__":
    main()
