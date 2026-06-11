"""
Generate Aries Automation Tools icon:
  - src/icon_data.hpp   : 32x32 RGBA C array (kIconPixels)
  - assets/app.ico      : multi-size Windows ICO (16/24/32/48/64/128/256)

Design:
  Dark navy circle background, cyan/blue outer ring, amber play-arrow center.
"""
import math, struct, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ── pixel helpers ────────────────────────────────────────────────────────────

def make_canvas(W, H):
    return bytearray(W * H * 4)

def blend(pixels, W, H, x, y, r, g, b, a):
    if not (0 <= x < W and 0 <= y < H): return
    i = (y * W + x) * 4
    sa = a / 255.0
    da = pixels[i+3] / 255.0
    oa = sa + da * (1 - sa)
    if oa < 0.001: return
    pixels[i+0] = int((r * sa + pixels[i+0] * da * (1-sa)) / oa)
    pixels[i+1] = int((g * sa + pixels[i+1] * da * (1-sa)) / oa)
    pixels[i+2] = int((b * sa + pixels[i+2] * da * (1-sa)) / oa)
    pixels[i+3] = int(oa * 255)

def edge_fn(ax, ay, bx, by, px, py):
    return (bx-ax)*(py-ay) - (by-ay)*(px-ax)

def dist_to_seg(ax, ay, bx, by, px, py):
    dx, dy = bx-ax, by-ay
    l2 = dx*dx + dy*dy
    if l2 < 1e-9: return math.hypot(px-ax, py-ay)
    t = max(0.0, min(1.0, ((px-ax)*dx + (py-ay)*dy) / l2))
    return math.hypot(px-(ax+t*dx), py-(ay+t*dy))

# ── icon renderer ────────────────────────────────────────────────────────────

def render_icon(size):
    W = H = size
    p = make_canvas(W, H)
    s = W / 32.0
    cx = cy = W / 2.0

    outer_r = W / 2.0 - 0.5

    # --- background circle (dark navy gradient) ---
    for y in range(H):
        for x in range(W):
            dx = x + 0.5 - cx; dy = y + 0.5 - cy
            d = math.hypot(dx, dy)
            if d >= outer_r: continue
            aa = min(1.0, outer_r - d)
            t = (y + 0.5) / H                       # 0=top, 1=bottom
            br = int(13 + 6*(1-t)); bg = int(27 + 14*(1-t)); bb = int(55 + 22*(1-t))
            blend(p, W, H, x, y, br, bg, bb, int(aa * 255))

    # --- outer ring (cyan top -> blue bottom) ---
    ring_r = outer_r - 1.8*s
    ring_w = 1.3*s
    for y in range(H):
        for x in range(W):
            dx = x + 0.5 - cx; dy = y + 0.5 - cy
            d = math.hypot(dx, dy)
            dd = abs(d - ring_r)
            if dd >= ring_w + 0.5: continue
            aa = max(0.0, 1.0 - dd / ring_w)
            t = (y + 0.5) / H
            rr = int(32 + 24*(1-t)); gg = int(148 + 44*(1-t)); rb = int(238 - 18*t)
            blend(p, W, H, x, y, rr, gg, rb, int(aa * 200))

    # --- play arrow (amber, pointing right) ---
    # 32x32 reference vertices: (7.5, 9.5) (7.5, 22.5) (24.5, 16.0)
    p1x, p1y = 7.5*s, 9.5*s
    p2x, p2y = 7.5*s, 22.5*s
    p3x, p3y = 24.5*s, 16.0*s

    for y in range(H):
        for x in range(W):
            px, py = x + 0.5, y + 0.5
            e1 = edge_fn(p1x,p1y, p2x,p2y, px,py)
            e2 = edge_fn(p2x,p2y, p3x,p3y, px,py)
            e3 = edge_fn(p3x,p3y, p1x,p1y, px,py)
            if e1 >= 0 and e2 >= 0 and e3 >= 0:
                # lighter at tip (right)
                tt = max(0.0, min(1.0, (px - p1x) / max(0.001, p3x - p1x)))
                rr = int(251 - 20*(1-tt))
                gg = int(191 - 55*(1-tt))
                blend(p, W, H, x, y, rr, gg, 36, 255)
            else:
                d1 = dist_to_seg(p1x,p1y, p2x,p2y, px,py)
                d2 = dist_to_seg(p2x,p2y, p3x,p3y, px,py)
                d3 = dist_to_seg(p3x,p3y, p1x,p1y, px,py)
                md = min(d1, d2, d3)
                if md < 1.1:
                    aa = 1.0 - md / 1.1
                    blend(p, W, H, x, y, 251, 191, 36, int(aa * 255))

    return bytes(p)

# ── ICO builder ──────────────────────────────────────────────────────────────

def build_ico(sizes):
    images = [(sz, render_icon(sz)) for sz in sizes]
    n = len(images)
    hdr = struct.pack('<HHH', 0, 1, n)
    data_off = 6 + n * 16
    dirs = b''; datas = b''
    for sz, rgba in images:
        # convert RGBA -> BGRA bottom-up
        bgra = bytearray(sz*sz*4)
        for row in range(sz):
            for col in range(sz):
                si = (row*sz+col)*4; di = ((sz-1-row)*sz+col)*4
                bgra[di+0]=rgba[si+2]; bgra[di+1]=rgba[si+1]
                bgra[di+2]=rgba[si+0]; bgra[di+3]=rgba[si+3]
        bih = struct.pack('<IiiHHIIiiII', 40, sz, sz*2, 1, 32, 0, 0, 0, 0, 0, 0)
        mask = bytes(((sz+31)//32)*4 * sz)
        img = bih + bytes(bgra) + mask
        ico_sz = sz if sz < 256 else 0
        dirs += struct.pack('<BBBBHHII', ico_sz, ico_sz, 0, 0, 1, 32, len(img), data_off)
        datas += img; data_off += len(img)
    return hdr + dirs + datas

# ── write icon_data.hpp ──────────────────────────────────────────────────────

def write_hpp(rgba32):
    lines = [
        '// Auto-generated 32x32 RGBA icon. Do not edit manually.',
        '#pragma once',
        '#include <cstdint>',
        'static const uint8_t kIconPixels[32*32*4] = {',
    ]
    for row in range(32):
        vals = []
        for col in range(32):
            i = (row*32+col)*4
            vals += [rgba32[i], rgba32[i+1], rgba32[i+2], rgba32[i+3]]
        lines.append('    ' + ','.join(map(str, vals)) + ',')
    lines += ['};', '']
    out = os.path.join(ROOT, 'src', 'icon_data.hpp')
    with open(out, 'w') as f: f.write('\n'.join(lines))
    print(f'  written: {out}')

# ── main ─────────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    print('Generating icon...')
    rgba32 = render_icon(32)
    write_hpp(rgba32)

    ico = build_ico([16, 24, 32, 48, 64, 128, 256])
    ico_path = os.path.join(ROOT, 'assets', 'app.ico')
    with open(ico_path, 'wb') as f: f.write(ico)
    print(f'  written: {ico_path}  ({len(ico)} bytes)')
    print('Done.')
