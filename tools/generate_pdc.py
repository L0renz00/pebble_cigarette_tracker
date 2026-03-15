#!/usr/bin/env python3
"""
generate_pdc.py — generates cigarette_smoke.pdc, a PDC sequence showing a
cigarette burning down while smoke rises from the tip.

Run from anywhere:
    python3 tools/generate_pdc.py
Outputs: resources/cigarette_smoke.pdc

PDC binary format reference:
  developer.repebble.com/guides/app-resources/pdc-format/
All multi-byte fields are little-endian.
"""

import os, struct

# ---------------------------------------------------------------------------
# Binary helpers
# ---------------------------------------------------------------------------
def u8(v):      return struct.pack('<B', int(v) & 0xFF)
def u16(v):     return struct.pack('<H', int(v) & 0xFFFF)
def s16(v):     return struct.pack('<h', int(v))
def u32(v):     return struct.pack('<I', int(v) & 0xFFFFFFFF)

# ---------------------------------------------------------------------------
# GColor8  0b11RRGGBB  (R,G,B each 0–3)
# ---------------------------------------------------------------------------
def gc(r, g, b):
    return (0b11 << 6) | ((r & 3) << 4) | ((g & 3) << 2) | (b & 3)

CLEAR  = 0x00
BLACK  = gc(0, 0, 0)   # 0xC0
WHITE  = gc(3, 3, 3)   # 0xFF
ORANGE = gc(3, 2, 0)   # 0xF8  — filter
EMBER  = gc(3, 1, 0)   # 0xF4  — burning tip
LGRAY  = gc(2, 2, 2)   # 0xEA  — light smoke
MGRAY  = gc(1, 1, 1)   # 0xD5  — fresh smoke (slightly darker)

# ---------------------------------------------------------------------------
# Draw command builders
# ---------------------------------------------------------------------------
def path_cmd(pts, fill, stroke=BLACK, sw=1, open_path=False):
    """Closed (or open) straight-line path."""
    buf  = u8(1)                        # type = path
    buf += u8(0)                        # flags
    buf += u8(stroke)                   # stroke colour
    buf += u8(sw)                       # stroke width
    buf += u8(fill)                     # fill colour
    buf += u16(1 if open_path else 0)   # path open flag
    buf += u16(len(pts))                # num points
    for x, y in pts:
        buf += s16(x) + s16(y)
    return buf

def rect_cmd(x1, y1, x2, y2, fill, stroke=BLACK, sw=1):
    return path_cmd([(x1,y1),(x2,y1),(x2,y2),(x1,y2)], fill, stroke, sw)

def circle_cmd(cx, cy, r, fill, stroke=CLEAR, sw=0):
    """Circle; radius goes in the path-open field (same offset, per spec)."""
    buf  = u8(2)        # type = circle
    buf += u8(0)        # flags
    buf += u8(stroke)   # stroke colour
    buf += u8(sw)       # stroke width
    buf += u8(fill)     # fill colour
    buf += u16(r)       # radius  (replaces path-open field)
    buf += u16(1)       # num points = 1 (centre)
    buf += s16(cx) + s16(cy)
    return buf

# ---------------------------------------------------------------------------
# Command list  &  frame
# ---------------------------------------------------------------------------
def cmd_list(cmds):
    return u16(len(cmds)) + b''.join(cmds)

def frame_bytes(duration_ms, cmds):
    return u16(duration_ms) + cmd_list(cmds)

# ---------------------------------------------------------------------------
# Geometry
#
#  ViewBox: 80 × 60
#  Cigarette sits at y = 45…55, runs left→right
#  Filter (always present): x = 55…70
#  Paper body:              x = tip_x…55  (shrinks each frame)
#  Ember circle:            (tip_x, 50)  r=3
#  Smoke rises from above the tip, clouds tracked per-frame
# ---------------------------------------------------------------------------
VB_W, VB_H = 80, 60
FX1, FY1, FX2, FY2 = 55, 45, 70, 55   # filter rect

def filter_cmd():
    return rect_cmd(FX1, FY1, FX2, FY2, ORANGE, stroke=BLACK, sw=2)

def paper_cmd(tip_x):
    if tip_x >= FX1:
        return None
    return rect_cmd(tip_x, 45, FX1, 55, WHITE, stroke=BLACK, sw=2)

def ember_cmd(tip_x, r=3):
    return circle_cmd(tip_x, 50, r, EMBER)

# Each smoke entry: (cx, cy, radius, colour)
FRAME_DEFS = [
    # (tip_x, duration_ms, [(cx, cy, r, col), ...])
    # Frame 0 — cigarette lit, no smoke yet
    ( 5, 270, []),
    # Frame 1 — puff A born near tip
    (13, 230, [(13, 35, 3, MGRAY)]),
    # Frame 2 — A drifts left+up; puff B born
    (21, 210, [(10, 26, 4, MGRAY), (21, 35, 3, MGRAY)]),
    # Frame 3 — A fades high-left; B drifts right+up; puff C born
    (29, 210, [(13, 19, 5, LGRAY), (24, 25, 4, MGRAY), (29, 35, 3, MGRAY)]),
    # Frame 4 — A gone; B fades high-right; C drifts left+up
    (37, 210, [( 9, 13, 6, LGRAY), (21, 17, 5, LGRAY), (27, 26, 4, MGRAY)]),
    # Frame 5 — B gone; C fades; puff D born near new tip
    (45, 210, [(12,  8, 7, LGRAY), (25, 10, 6, LGRAY), (30, 18, 5, LGRAY), (45, 35, 3, MGRAY)]),
    # Frame 6 — C gone; D drifts right
    (53, 210, [(22,  4, 7, LGRAY), (27, 10, 6, LGRAY), (48, 26, 4, MGRAY)]),
    # Frame 7 — Cigarette fully burned; last wisps fade
    (55, 500, [(31,  3, 7, LGRAY), (45, 18, 5, LGRAY)]),
]

def build_frame(tip_x, duration_ms, smoke):
    cmds = []
    p = paper_cmd(tip_x)
    if p:
        cmds.append(p)
    cmds.append(filter_cmd())
    ember_r = 2 if tip_x >= FX1 else 3
    cmds.append(ember_cmd(tip_x, ember_r))
    for cx, cy, r, col in smoke:
        cmds.append(circle_cmd(cx, cy, r, col))
    return frame_bytes(duration_ms, cmds)

# ---------------------------------------------------------------------------
# Assemble PDC sequence file
# ---------------------------------------------------------------------------
frames_data = b''.join(build_frame(*fd) for fd in FRAME_DEFS)

# Sequence struct (sits at file offset 8)
seq  = u8(1)            # version
seq += u8(0)            # reserved
seq += u16(VB_W)        # viewbox width
seq += u16(VB_H)        # viewbox height
seq += u16(1)           # play count — play once
seq += u16(len(FRAME_DEFS))  # frame count
seq += frames_data

# PDCS file wrapper
pdc  = b'PDCS'
pdc += u32(len(seq))    # sequence size (bytes after the 8-byte file header)
pdc += seq

# ---------------------------------------------------------------------------
# Write output
# ---------------------------------------------------------------------------
script_dir = os.path.dirname(os.path.abspath(__file__))
out_path   = os.path.join(script_dir, '..', 'resources', 'cigarette_smoke.pdc')
out_path   = os.path.normpath(out_path)

with open(out_path, 'wb') as f:
    f.write(pdc)

print("Written %d bytes → %s" % (len(pdc), out_path))
print("  %d frames, viewbox %dx%d, loops forever" % (len(FRAME_DEFS), VB_W, VB_H))
