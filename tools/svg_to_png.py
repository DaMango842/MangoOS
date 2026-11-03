#!/usr/bin/env python3
"""tools/svg_to_png.py - render a small subset of SVG into a 32x32 RGBA
PNG stored in the assets/ resource directory.

Supported shapes: rect (with rx/ry), circle, ellipse, path with absolute
and relative commands M/L/C/Q/Z.  Fills only (no strokes, no gradients).
Supersampled 4x for antialiasing.  The PNG keeps its alpha channel; the
background blend happens later in tools/png_to_c.py.

Offline tool: both the SVG source and the generated PNG are committed,
so the kernel build has no dependency on this script or on any SVG
library.
"""

import os
import re
import sys

SIZE = 32              # icon size in pixels
SS = 4                 # supersampling factor
W = H = SIZE * SS


def parse_path(d):
    """Return a list of polylines (each a list of (x, y) tuples)."""
    tokens = re.findall(r"[MmLlCcQqZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", d)
    polys = []
    cur = None
    cx = cy = 0.0
    sx = sy = 0.0
    i = 0
    while i < len(tokens):
        t = tokens[i]
        if t in "Mm":
            args = []
            i += 1
            while i < len(tokens) and tokens[i] not in "MmLlCcQqZz":
                args.append(float(tokens[i])); i += 1
            for k in range(0, len(args) - 1, 2):
                x, y = args[k], args[k + 1]
                if t == "m":
                    x += cx; y += cy
                cx, cy = x, y
                sx, sy = x, y
                cur = [(x, y)]
                polys.append(cur)
        elif t in "Ll":
            args = []
            i += 1
            while i < len(tokens) and tokens[i] not in "MmLlCcQqZz":
                args.append(float(tokens[i])); i += 1
            for k in range(0, len(args) - 1, 2):
                x, y = args[k], args[k + 1]
                if t == "l":
                    x += cx; y += cy
                cur.append((x, y))
                cx, cy = x, y
        elif t in "Cc":
            args = []
            i += 1
            while i < len(tokens) and tokens[i] not in "MmLlCcQqZz":
                args.append(float(tokens[i])); i += 1
            for k in range(0, len(args) - 1, 5):
                x1, y1, x2, y2, x, y = args[k:k + 6]
                if t == "c":
                    x1 += cx; y1 += cy; x2 += cx; y2 += cy; x += cx; y += cy
                _bezier(cur, cx, cy, x1, y1, x2, y2, x, y, 16)
                cx, cy = x, y
        elif t in "Qq":
            args = []
            i += 1
            while i < len(tokens) and tokens[i] not in "MmLlCcQqZz":
                args.append(float(tokens[i])); i += 1
            for k in range(0, len(args) - 1, 3):
                x1, y1, x, y = args[k:k + 4]
                if t == "q":
                    x1 += cx; y1 += cy; x += cx; y += cy
                _quad(cur, cx, cy, x1, y1, x, y, 16)
                cx, cy = x, y
        elif t in "Zz":
            cur.append((sx, sy))
            i += 1
    return polys


def _bezier(poly, x0, y0, x1, y1, x2, y2, x3, y3, n):
    for k in range(1, n + 1):
        t = k / n
        u = 1 - t
        x = u**3 * x0 + 3 * u**2 * t * x1 + 3 * u * t**2 * x2 + t**3 * x3
        y = u**3 * y0 + 3 * u**2 * t * y1 + 3 * u * t**2 * y2 + t**3 * y3
        poly.append((x, y))


def _quad(poly, x0, y0, x1, y1, x2, y2, n):
    for k in range(1, n + 1):
        t = k / n
        u = 1 - t
        x = u**2 * x0 + 2 * u * t * x1 + t**2 * x2
        y = u**2 * y0 + 2 * u * t * y1 + t**2 * y2
        poly.append((x, y))


def fill_poly(poly, mask, scale, ox, oy):
    """Even-odd scanline fill into a W x H byte mask."""
    raw = [y for _, y in poly]
    for y in range(int(min(raw)), int(max(raw)) + 1):
        cuts = []
        for i in range(len(poly)):
            (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % len(poly)]
            if (y1 <= y < y2) or (y2 <= y < y1):
                t = (y - y1) / (y2 - y1)
                cuts.append(x1 + t * (x2 - x1))
        cuts.sort()
        my = int(y * scale + oy)
        for k in range(0, len(cuts) - 1, 2):
            xa = max(0, int(cuts[k] * scale + ox))
            xb = min(W - 1, int(cuts[k + 1] * scale + ox))
            for x in range(xa, xb + 1):
                mask[my * W + x] = 1


def render_shapes(shapes, width, height, viewbox):
    """Return 32x32 list of (r, g, b, a) tuples with real alpha."""
    order = []
    vb = [float(v) for v in viewbox.split()] if viewbox else [0, 0, width, height]
    sx = W / (vb[2] - vb[0])
    sy = H / (vb[3] - vb[1])
    ox = -vb[0] * sx
    oy = -vb[1] * sy
    for el in shapes:
        fill = el.get("fill", "#000000")
        if fill.startswith("#") and len(fill) == 7:
            rgb = (int(fill[1:3], 16), int(fill[3:5], 16), int(fill[5:7], 16))
        else:
            continue
        tag = el.tag.split("}")[-1]
        m = bytearray(W * H)
        if tag == "rect":
            x = float(el.get("x", 0)); y = float(el.get("y", 0))
            w = float(el.get("width", 0)); h = float(el.get("height", 0))
            rx = float(el.get("rx", 0) or 0)
            poly = []
            if rx > 0:
                poly = [(x + rx, y), (x + w - rx, y), (x + w, y + rx),
                        (x + w, y + h - rx), (x + w - rx, y + h),
                        (x + rx, y + h), (x, y + h - rx), (x, y + rx)]
            else:
                poly = [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
            fill_poly(poly, m, sx, ox, oy)
        elif tag == "circle":
            cx = float(el.get("cx", 0)); cy = float(el.get("cy", 0))
            r = float(el.get("r", 0))
            poly = [(cx + r * __import__("math").cos(k * 2 * 3.14159 / 32),
                     cy + r * __import__("math").sin(k * 2 * 3.14159 / 32))
                    for k in range(33)]
            fill_poly(poly, m, sx, ox, oy)
        elif tag == "ellipse":
            cx = float(el.get("cx", 0)); cy = float(el.get("cy", 0))
            rx = float(el.get("rx", 0)); ry = float(el.get("ry", 0))
            poly = [(cx + rx * __import__("math").cos(k * 2 * 3.14159 / 32),
                     cy + ry * __import__("math").sin(k * 2 * 3.14159 / 32))
                    for k in range(33)]
            fill_poly(poly, m, sx, ox, oy)
        elif tag == "path":
            for poly in parse_path(el.get("d", "")):
                fill_poly(poly, m, sx, ox, oy)
        else:
            continue
        order.append((rgb, m))

    out = []
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = 0, 0, 0
            a = 0.0
            for rgb, m in order:
                cov = sum(m[(yy * W + xx)] for yy in range(y * SS, (y + 1) * SS)
                          for xx in range(x * SS, (x + 1) * SS)) / (SS * SS)
                if cov > 0:
                    r = r * (1 - cov) + rgb[0] * cov
                    g = g * (1 - cov) + rgb[1] * cov
                    b = b * (1 - cov) + rgb[2] * cov
                    a = a + cov * (1 - a)
            out.append((int(r), int(g), int(b), int(a * 255)))
    return out


def main():
    import xml.etree.ElementTree as ET

    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), "svg", "mango.svg")
    root = ET.parse(src).getroot()
    width = float(root.get("width", "512"))
    height = float(root.get("height", "512"))
    viewbox = root.get("viewBox")
    pix = render_shapes(list(root.iter()), width, height, viewbox)

    out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.normpath(
        os.path.join(os.path.dirname(__file__), "..", "assets",
                     os.path.splitext(os.path.basename(src))[0] + ".png"))
    from PIL import Image
    img = Image.new("RGBA", (SIZE, SIZE))
    img.putdata(pix)
    img.save(out_path, "PNG")
    print("wrote", out_path)


if __name__ == "__main__":
    main()