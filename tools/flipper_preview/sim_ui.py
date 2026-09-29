"""Python port of morse_draw.c (1:1 coordinates) on top of flipsim.Canvas."""
import os
import re
from flipsim import Canvas

# repository root (two folders up from this file) - the C sources live there
SRC = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def _read(name):
    return open(os.path.join(SRC, name), encoding="utf-8").read()


_data = _read("morse_data.c")
MORSE = re.findall(r"\{'(.)', \"([.\-]+)\"\}", _data)
CODE = {c: k for c, k in MORSE}
ORDER = [c for c, _ in MORSE]
LESSONS = re.findall(r'\{"([^"]*)", "([^"]*)"\},', _data[_data.index("lessons[LESSON_COUNT]"):_data.index("badges[BADGE_COUNT]")])
BADGES = re.findall(r'\{"([^"]*)", "([^"]*)"\},', _data[_data.index("badges[BADGE_COUNT]"):_data.index("daily_goals")])
_g = _data[_data.index("glyph_5x7"):]
GLYPHS = [[int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", row)] for row in re.findall(r"\{(0x[^}]*)\}", _g)]

# icon rows parsed from morse_draw.c
_draw = _read("morse_draw.c")
ICONS = {}
for name, body in re.findall(r"\[(Icon\w+)\] =\s*\{([^}]*)\}", _draw):
    ICONS[name] = re.findall(r'"([.#]+)"', body)


def rows(c, x, y, rs):
    for r, row in enumerate(rs):
        for col, ch in enumerate(row):
            if ch == "#":
                c.dot(x + col, y + r)


def ui_frame(c, title):
    c.set_color(1)
    c.rframe(0, 0, 128, 64, 4)
    c.set_font("primary")
    c.str_aligned(64, 9, "center", "center", title)
    c.line(14, 15, 113, 15)


def ui_dotted_row(c, y, label, key):
    c.set_font("secondary")
    c.str(6, y, label)
    lw = c.str_width(label)
    kw = c.str_width(key)
    x = 6 + lw + 3
    end = 122 - kw - 2
    while x < end:
        c.dot(x, y - 2)
        x += 3
    c.str_aligned(122, y, "right", "bottom", key)


def ui_scroll_arrow(c, cx, cy, up):
    for k in range(3):
        row = cy - 1 + k if up else cy + 1 - k
        c.line(cx - k, row, cx + k, row)


def ui_arrow(c, cx, cy, d, size):
    for k in range(size):
        if d == 0:
            c.line(cx - k, cy - size + 1 + k, cx + k, cy - size + 1 + k)
        elif d == 2:
            c.line(cx - k, cy + size - 1 - k, cx + k, cy + size - 1 - k)
        elif d == 3:
            c.line(cx - size + 1 + k, cy - k, cx - size + 1 + k, cy + k)
        else:
            c.line(cx + size - 1 - k, cy - k, cx + size - 1 - k, cy + k)


def pattern_width(code, big):
    w = 0
    for i, e in enumerate(code):
        if i:
            w += 4 if big else 3
        w += (13 if big else 8) if e == "-" else (5 if big else 3)
    return w


def ui_pattern(c, cx, cy, code, big, lit=127, outline=False):
    x = cx - pattern_width(code, big) // 2 if pattern_width(code, big) >= 0 else cx
    x = cx - int(pattern_width(code, big) / 2)
    for i, e in enumerate(code):
        solid = (not outline) or i <= lit
        dah = e == "-"
        if big:
            if dah:
                (c.rbox if solid else c.rframe)(x, cy - 2, 13, 5, 2)
                x += 17
            else:
                (c.disc if solid else c.circle)(x + 2, cy, 2)
                x += 9
        else:
            w = 8 if dah else 3
            (c.box if solid else c.frame)(x, cy - 1, w, 3)
            x += w + 3


def ui_big_char(c, x, y, ch, scale):
    if ch not in ORDER:
        return
    g = GLYPHS[ORDER.index(ch)]
    for r in range(7):
        for col in range(5):
            if g[r] & (0x10 >> col):
                c.box(x + col * scale, y + r * scale, scale, scale)


def ui_wrap(c, x, y, width, lh, text, skip, max_lines):
    lines = []
    line = ""
    for para in text.split("\n"):
        for word in para.split(" "):
            if not word:
                continue
            cand = (line + " " + word) if line else word
            if line and c.str_width(cand) > width:
                lines.append(line)
                line = word
            else:
                line = cand
        if line:
            lines.append(line)
            line = ""
    for i, l in enumerate(lines):
        if skip <= i < skip + max_lines:
            c.str(x, y + (i - skip) * lh, l)
    return len(lines)


def ui_str_fit(c, x, y, max_w, h, text):
    buf = text[:31]
    while len(buf) > 1 and c.str_width(buf) > max_w:
        buf = buf[:-2] + "."
    w = c.str_width(buf)
    dx = x
    if h == "center":
        dx = x - w // 2
    if h == "right":
        dx = x - w
    c.str(dx, y, buf)


STAR_FULL = ["...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", ".#...#."]
STAR_EMPTY = ["...#...", "..#.#..", "##...##", ".#...#.", "..#.#..", ".##.##.", ".#...#."]
HEART_FULL = [".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."]
HEART_EMPTY = [".##.##.", "#..#..#", "#.....#", ".#...#.", "..#.#..", "...#..."]
FLAME_FULL = ["...#...", "...##..", "..###..", ".####.#", ".######", "#######", "###.###", "##...##", ".#####."]
FLAME_EMPTY = ["...#...", "...##..", "..#.#..", ".#..#.#", ".#...##", "#.....#", "#..#..#", "#.#.#.#", ".#####."]
LOCK = ["..###..", ".#...#.", ".#...#.", "#######", "###.###", "###.###", "#######"]


def ui_star(c, x, y, f):
    rows(c, x, y, STAR_FULL if f else STAR_EMPTY)


def ui_star_big(c, x, y, f):
    rs = STAR_FULL if f else STAR_EMPTY
    for r in range(7):
        for col in range(7):
            if rs[r][col] == "#":
                c.box(x + col * 2, y + r * 2, 2, 2)


def ui_heart(c, x, y, f):
    rows(c, x, y, HEART_FULL if f else HEART_EMPTY)


def ui_flame(c, x, y, f):
    rows(c, x, y, FLAME_FULL if f else FLAME_EMPTY)


def ui_lock(c, x, y):
    rows(c, x, y, LOCK)


def ui_icon(c, x, y, name):
    rows(c, x, y, ICONS[name])


def ui_waves(c, cx, cy, anim, active):
    c.box(cx - 9, cy - 2, 3, 5)
    for k in range(4):
        c.line(cx - 6 + k, cy - 2 - k, cx - 6 + k, cy + 2 + k)
    phase = (anim // 4) % 4
    for a in range(3):
        if active and a >= phase:
            continue
        r = 4 + a * 4
        for dy in range(-r + 2, r - 1):
            dx = 0
            while (dx + 1) ** 2 + dy * dy <= r * r:
                dx += 1
            c.dot(cx + dx - 2, cy + dy)


def ui_button(c, x, y, w, h, label, filled):
    c.set_font("secondary")
    c.set_color(1)
    if filled:
        c.rbox(x, y, w, h, 3)
        c.set_color(0)
    else:
        c.rframe(x, y, w, h, 3)
    c.str_aligned(x + w // 2, y + h // 2 + 1, "center", "center", label)
    c.set_color(1)


def ui_progress(c, x, y, w, h, val, mx):
    c.rframe(x, y, w, h, 1)
    if mx == 0:
        return
    val = min(val, mx)
    fill = (w - 2) * val // mx
    if fill > 0:
        c.box(x + 1, y + 1, fill, h - 2)


CHOICE = [(34, 5, 60, 13), (84, 26, 44, 13), (34, 49, 60, 13), (0, 26, 44, 13)]


def ui_choice_box(c, slot, label, filled, cross, font):
    x, y, w, h = CHOICE[slot]
    c.set_color(1)
    if filled:
        c.rbox(x, y, w, h, 3)
        c.set_color(0)
    else:
        c.rframe(x, y, w, h, 3)
    c.set_font(font)
    ui_str_fit(c, x + w // 2, y + 10, w - 6, "center", label)
    if cross:
        c.line(x + 3, y + 2, x + w - 4, y + h - 3)
        c.line(x + w - 4, y + 2, x + 3, y + h - 3)
    c.set_color(1)


def ui_top_progress(c, val, mx):
    fill = 128 * min(val, mx) // mx if mx else 0
    if fill > 0:
        c.box(0, 0, fill, 3)
    x = fill + 1
    while x < 128:
        c.dot(x, 1)
        x += 2


def ui_phonetic(code):
    parts = []
    for i, e in enumerate(code):
        parts.append("dah" if e == "-" else ("dit" if i + 1 == len(code) else "di"))
    return "-".join(parts)


def ui_yes_no(c, q, yes):
    c.set_color(0)
    c.rbox(10, 16, 108, 34, 4)
    c.set_color(1)
    c.rframe(10, 16, 108, 34, 4)
    c.set_font("primary")
    c.str_aligned(64, 25, "center", "center", q)
    ui_button(c, 20, 34, 38, 12, "Yes", yes)
    ui_button(c, 70, 34, 38, 12, "No", not yes)


def ui_toast(c, title, text, icon, y=1):
    c.set_color(0)
    c.box(0, y - 1, 128, 23)
    c.set_color(1)
    c.rframe(0, y, 128, 21, 4)
    c.rframe(1, y + 1, 126, 19, 3)
    c.disc(13, y + 10, 7)
    c.set_color(0)
    if icon == 2:
        ui_flame(c, 10, y + 6, True)
    elif icon == 1:
        ui_arrow(c, 13, y + 9, 0, 5)
        c.box(12, y + 9, 3, 4)
    else:
        ui_star(c, 10, y + 7, True)
    c.set_color(1)
    c.set_font("primary")
    ui_str_fit(c, 25, y + 10, 99, "left", title)
    c.set_font("secondary")
    ui_str_fit(c, 25, y + 18, 99, "left", text)
