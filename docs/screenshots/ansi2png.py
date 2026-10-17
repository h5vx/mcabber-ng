#!/usr/bin/env python3
"""Render a terminal capture to a PNG image (used for the screenshots of
the README).

    tmux capture-pane -p -e -N -t SESSION > shot.ansi
    ansi2png.py shot.ansi shot.png COLS ROWS [title]

Needs Pillow and fontTools, a Nerd Font (FONT_DIR, JetBrainsMono Nerd Font
Mono by default) and DejaVu Sans Mono for the other glyphs (FALLBACK_FONT).
"""
import os
import re
import sys
import unicodedata
from PIL import Image, ImageDraw, ImageFont

FONT_DIR = os.environ.get(
    "FONT_DIR", os.path.expanduser("~/.local/share/fonts/JetBrainsMonoNerd/"))
FONTS = {
    (False, False): "JetBrainsMonoNerdFontMono-Regular.ttf",
    (True, False): "JetBrainsMonoNerdFontMono-Bold.ttf",
    (False, True): "JetBrainsMonoNerdFontMono-Italic.ttf",
    (True, True): "JetBrainsMonoNerdFontMono-BoldItalic.ttf",
}
FALLBACK = os.environ.get(
    "FALLBACK_FONT", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf")
SCALE = 2
SIZE = 14 * SCALE

DEFAULT_FG = (0xc0, 0xca, 0xf5)
DEFAULT_BG = (0x1a, 0x1b, 0x26)
# Tango-like palette for the 16 base colors (black is the background)
BASE16 = ["1a1b26", "cc0000", "4e9a06", "c4a000", "3465a4", "75507b",
          "06989a", "d3d7cf", "555753", "ef2929", "8ae234", "fce94f",
          "729fcf", "ad7fa8", "34e2e2", "eeeeec"]
# DEC line drawing characters (after SO)
ACS = {"x": "│", "q": "─", "l": "┌", "k": "┐", "m": "└", "j": "┘",
       "t": "├", "u": "┤", "w": "┬", "v": "┴", "n": "┼"}


def palette(n):
    if n < 16:
        h = BASE16[n]
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    if n < 232:
        n -= 16
        lv = [0, 95, 135, 175, 215, 255]
        return (lv[n // 36], lv[(n // 6) % 6], lv[n % 6])
    v = 8 + (n - 232) * 10
    return (v, v, v)


class Cell:
    __slots__ = ("ch", "fg", "bg", "bold", "italic", "underline", "wide")

    def __init__(self):
        self.ch = " "
        self.fg = None
        self.bg = None
        self.bold = self.italic = self.underline = False
        self.wide = False


def parse(text, cols, rows):
    grid = [[Cell() for _ in range(cols)] for _ in range(rows)]
    state = dict(fg=None, bg=None, bold=False, italic=False,
                 underline=False, reverse=False)
    acs = False
    lines = text.split("\n")
    for y, line in enumerate(lines[:rows]):
        x = 0
        i = 0
        while i < len(line):
            m = re.match(r"\x1b\[([0-9;:]*)m", line[i:])
            if m:
                apply_sgr(state, m.group(1))
                i += m.end()
                continue
            if line[i] == "\x1b":       # Other sequences: skip
                m = re.match(r"\x1b[\[\]][^a-zA-Z]*[a-zA-Z]", line[i:])
                i += m.end() if m else 1
                continue
            ch = line[i]
            i += 1
            if ch == "\x0e":
                acs = True
                continue
            if ch == "\x0f":
                acs = False
                continue
            if acs:
                ch = ACS.get(ch, ch)
            if unicodedata.combining(ch):
                continue
            w = 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1
            if x + w > cols:
                break
            c = grid[y][x]
            c.ch = ch
            fg, bg = state["fg"], state["bg"]
            if state["reverse"]:
                fg, bg = (bg or DEFAULT_BG), (fg or DEFAULT_FG)
            c.fg, c.bg = fg, bg
            c.bold, c.italic = state["bold"], state["italic"]
            c.underline = state["underline"]
            c.wide = (w == 2)
            if w == 2:
                grid[y][x + 1].bg = bg
                grid[y][x + 1].ch = ""
            x += w
    return grid


def apply_sgr(st, params):
    p = [int(v) if v else 0 for v in re.split("[;:]", params)] if params \
        else [0]
    i = 0
    while i < len(p):
        v = p[i]
        if v == 0:
            st.update(fg=None, bg=None, bold=False, italic=False,
                      underline=False, reverse=False)
        elif v == 1:
            st["bold"] = True
        elif v == 3:
            st["italic"] = True
        elif v == 4:
            st["underline"] = True
        elif v == 7:
            st["reverse"] = True
        elif v == 22:
            st["bold"] = False
        elif v == 23:
            st["italic"] = False
        elif v == 24:
            st["underline"] = False
        elif v == 27:
            st["reverse"] = False
        elif 30 <= v <= 37:
            st["fg"] = palette(v - 30)
        elif 90 <= v <= 97:
            st["fg"] = palette(v - 90 + 8)
        elif 40 <= v <= 47:
            st["bg"] = palette(v - 40)
        elif 100 <= v <= 107:
            st["bg"] = palette(v - 100 + 8)
        elif v == 39:
            st["fg"] = None
        elif v == 49:
            st["bg"] = None
        elif v in (38, 48):
            key = "fg" if v == 38 else "bg"
            if i + 2 < len(p) + 1 and p[i + 1] == 5:
                st[key] = palette(p[i + 2])
                i += 2
            elif p[i + 1] == 2:
                st[key] = tuple(p[i + 2:i + 5])
                i += 4
        i += 1


# Box drawing: (up, down, left, right), "r" for rounded corners
BOX = {
    "─": (0, 0, 1, 1), "│": (1, 1, 0, 0), "┌": (0, 1, 0, 1),
    "┐": (0, 1, 1, 0), "└": (1, 0, 0, 1), "┘": (1, 0, 1, 0),
    "├": (1, 1, 0, 1), "┤": (1, 1, 1, 0), "┬": (0, 1, 1, 1),
    "┴": (1, 0, 1, 1), "┼": (1, 1, 1, 1),
    "╭": "r-dr", "╮": "r-dl", "╰": "r-ur", "╯": "r-ul",
}


def draw_box(d, ch, x, y, cw, chh, color):
    lw = max(1, SCALE)
    cx, cy = x + cw // 2, y + chh // 2
    spec = BOX[ch]
    if isinstance(spec, str):
        r = min(cw, chh) // 2
        dirs = spec[2:]
        # Lines from the arc to the cell borders
        vy = cy + r if "d" in dirs else cy - r
        if "d" in dirs:
            d.line([(cx, vy), (cx, y + chh)], fill=color, width=lw)
        else:
            d.line([(cx, y), (cx, vy)], fill=color, width=lw)
        hx = cx + r if "r" in dirs else cx - r
        if "r" in dirs:
            d.line([(hx, cy), (x + cw, cy)], fill=color, width=lw)
        else:
            d.line([(x, cy), (hx, cy)], fill=color, width=lw)
        # Arc
        bx = cx if "r" in dirs else cx - 2 * r
        by = cy if "d" in dirs else cy - 2 * r
        start = {"dr": 180, "dl": 270, "ur": 90, "ul": 0}[dirs]
        d.arc([bx, by, bx + 2 * r, by + 2 * r], start, start + 90,
              fill=color, width=lw)
        return
    up, down, left, right = spec
    if up:
        d.line([(cx, y), (cx, cy)], fill=color, width=lw)
    if down:
        d.line([(cx, cy), (cx, y + chh)], fill=color, width=lw)
    if left:
        d.line([(x, cy), (cx, cy)], fill=color, width=lw)
    if right:
        d.line([(cx, cy), (x + cw, cy)], fill=color, width=lw)


def render(grid, cols, rows, out, title):
    fonts = {k: ImageFont.truetype(FONT_DIR + v, SIZE) for k, v in
             FONTS.items()}
    fallback = ImageFont.truetype(FALLBACK, SIZE)
    from fontTools.ttLib import TTFont
    cmap = TTFont(FONT_DIR + FONTS[(False, False)]).getBestCmap()
    f = fonts[(False, False)]
    cw = round(f.getlength("M"))
    asc, desc = f.getmetrics()
    chh = asc + desc
    pad = 18 * SCALE
    bar = 34 * SCALE
    W = cols * cw + 2 * pad
    H = rows * chh + 2 * pad + bar
    # Transparent outer background
    img = Image.new("RGBA", (W + 40 * SCALE, H + 40 * SCALE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    ox, oy = 20 * SCALE, 20 * SCALE
    d.rounded_rectangle([ox, oy, ox + W, oy + H], radius=12 * SCALE,
                        fill=DEFAULT_BG + (255,),
                        outline=(0x2a, 0x2e, 0x42, 255), width=SCALE)
    # Title bar
    for i, col in enumerate([(0xff, 0x5f, 0x57), (0xfe, 0xbc, 0x2e),
                             (0x28, 0xc8, 0x40)]):
        cx = ox + pad + i * 22 * SCALE + 6 * SCALE
        cy = oy + bar // 2
        d.ellipse([cx - 6 * SCALE, cy - 6 * SCALE, cx + 6 * SCALE,
                   cy + 6 * SCALE], fill=col)
    if title:
        tf = fonts[(False, False)]
        tw = tf.getlength(title)
        d.text((ox + (W - tw) / 2, oy + bar / 2 - chh / 2), title,
               font=tf, fill=(0x73, 0x7a, 0xa2))
    x0, y0 = ox + pad, oy + bar + pad // 2
    # Backgrounds
    for y in range(rows):
        for x in range(cols):
            c = grid[y][x]
            if c.bg:
                d.rectangle([x0 + x * cw, y0 + y * chh,
                             x0 + (x + 1) * cw - 1, y0 + (y + 1) * chh - 1],
                            fill=c.bg)
    # Text
    for y in range(rows):
        for x in range(cols):
            c = grid[y][x]
            if not c.ch or c.ch == " ":
                continue
            color = c.fg or DEFAULT_FG
            px, py = x0 + x * cw, y0 + y * chh
            if c.ch in BOX:
                draw_box(d, c.ch, px, py, cw, chh, color)
                continue
            font = fonts[(c.bold, c.italic)]
            if ord(c.ch) not in cmap:
                font = fallback
            d.text((px, py), c.ch, font=font, fill=color)
            if c.underline:
                d.line([(px, py + chh - 2 * SCALE),
                        (px + cw, py + chh - 2 * SCALE)], fill=color,
                       width=SCALE)
    img.save(out)


def main():
    src, out, cols, rows = sys.argv[1], sys.argv[2], int(sys.argv[3]), \
        int(sys.argv[4])
    title = sys.argv[5] if len(sys.argv) > 5 else ""
    text = open(src, encoding="utf-8", errors="replace").read()
    render(parse(text, cols, rows), cols, rows, out, title)


if __name__ == "__main__":
    main()
