"""Minimal Flipper Zero canvas emulation (u8g2 fonts + primitives) for layout checks.

Fonts are the real u8g2 font blobs extracted from the SDK's libu8g2.a.
Primitives follow the u8g2 algorithms (rframe/rbox/disc/circle/line).
"""
import os
from PIL import Image

FONT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts")
FONT_FILES = {
    "primary": "helvB08_tr.bin",
    "secondary": "haxrcorp4089_tr.bin",
    "keyboard": "profont11_mr.bin",
    "big": "profont22_tn.bin",
}


class U8g2Font:
    def __init__(self, data: bytes):
        self.d = data
        h = data
        self.glyph_cnt = h[0]
        self.bits_0 = h[2]
        self.bits_1 = h[3]
        self.bits_w = h[4]
        self.bits_h = h[5]
        self.bits_x = h[6]
        self.bits_y = h[7]
        self.bits_dx = h[8]
        s8 = lambda v: v - 256 if v > 127 else v
        self.ascent_A = s8(h[13])
        self.descent_g = s8(h[14])
        self.start_A = (h[17] << 8) | h[18]
        self.start_a = (h[19] << 8) | h[20]

    def glyph(self, ch):
        enc = ord(ch)
        p = 23
        if enc >= ord("a"):
            p += self.start_a
        elif enc >= ord("A"):
            p += self.start_A
        while True:
            if self.d[p + 1] == 0:
                return None
            if self.d[p] == enc:
                return p + 2
            p += self.d[p + 1]

    class _Bits:
        def __init__(self, data, pos):
            self.data = data
            self.pos = pos
            self.bit = 0

        def u(self, cnt):
            val = self.data[self.pos] >> self.bit
            bpc = self.bit + cnt
            if bpc >= 8:
                s = 8 - self.bit
                self.pos += 1
                val |= self.data[self.pos] << s
                bpc -= 8
            val &= (1 << cnt) - 1
            self.bit = bpc
            return val

        def s(self, cnt):
            return self.u(cnt) - (1 << (cnt - 1))

    def header(self, ch):
        p = self.glyph(ch)
        if p is None:
            return None
        b = self._Bits(self.d, p)
        w = b.u(self.bits_w)
        h = b.u(self.bits_h)
        x = b.s(self.bits_x)
        y = b.s(self.bits_y)
        dx = b.s(self.bits_dx)
        return w, h, x, y, dx, b

    def width(self, s):
        w = 0
        dx = 0
        gw = 0
        gx = 0
        for ch in s:
            hd = self.header(ch)
            if hd is None:
                continue
            gw, _, gx, _, dx, _ = hd
            w += dx
        if gw != 0:
            w -= dx
            w += gw
            w += gx
        return w

    def draw(self, canvas, x, y, s, color):
        for ch in s:
            hd = self.header(ch)
            if hd is None:
                continue
            w, h, gx, gy, dx, b = hd
            if w > 0:
                tx = x + gx
                ty = y - h - gy
                lx = ly = 0
                while True:
                    a = b.u(self.bits_0)
                    c = b.u(self.bits_1)
                    while True:
                        for run, fg in ((a, False), (c, True)):
                            cnt = run
                            while True:
                                rem = w - lx
                                cur = rem if rem < cnt else cnt
                                if fg:
                                    for k in range(cur):
                                        canvas.px(tx + lx + k, ty + ly, color)
                                if cnt < rem:
                                    break
                                cnt -= rem
                                lx = 0
                                ly += 1
                            lx += cnt
                        if b.u(1) == 0:
                            break
                    if ly >= h:
                        break
            x += dx


def extract_fonts():
    """Pulls the real Flipper fonts out of the ufbt SDK (libu8g2.a)."""
    import glob, subprocess, tempfile
    home = os.path.expanduser("~")
    # ~/.ufbt/toolchain/<x86_64-windows | x86_64-linux | ...-darwin>/bin
    candidates = glob.glob(os.path.join(home, ".ufbt", "toolchain", "*", "bin"))
    if not candidates:
        raise SystemExit("ufbt toolchain not found - run 'python -m ufbt' once first")
    tc = candidates[0]
    lib = os.path.join(home, ".ufbt", "current", "lib", "libu8g2.a")
    os.makedirs(FONT_DIR, exist_ok=True)
    tmp = tempfile.mkdtemp()
    subprocess.check_call([os.path.join(tc, "arm-none-eabi-ar"), "x", lib, "u8g2_fonts.o"], cwd=tmp)
    for v in FONT_FILES.values():
        name = v[:-4]
        subprocess.check_call([os.path.join(tc, "arm-none-eabi-objcopy"), "-O", "binary",
                               "-j", ".rodata.u8g2_font_" + name, "u8g2_fonts.o",
                               os.path.join(FONT_DIR, v)], cwd=tmp)


if not all(os.path.exists(os.path.join(FONT_DIR, v)) for v in FONT_FILES.values()):
    extract_fonts()

FONTS = {k: U8g2Font(open(os.path.join(FONT_DIR, v), "rb").read()) for k, v in FONT_FILES.items()}

UPPER_RIGHT, UPPER_LEFT, LOWER_LEFT, LOWER_RIGHT = 1, 2, 4, 8
ALL = 15


class Canvas:
    W, H = 128, 64

    def __init__(self):
        self.buf = [[0] * self.W for _ in range(self.H)]
        self.color = 1
        self.font = FONTS["secondary"]
        self.overlaps = []

    # --- basic ---
    def px(self, x, y, color=None):
        if 0 <= x < self.W and 0 <= y < self.H:
            self.buf[y][x] = self.color if color is None else color

    def set_color(self, c):
        self.color = 1 if c in (1, "black") else 0

    def set_font(self, name):
        self.font = FONTS[name]

    def dot(self, x, y):
        self.px(x, y)

    def hline(self, x, y, w):
        for i in range(w):
            self.px(x + i, y)

    def vline(self, x, y, h):
        for i in range(h):
            self.px(x, y + i)

    def box(self, x, y, w, h):
        for j in range(h):
            self.hline(x, y + j, w)

    def frame(self, x, y, w, h):
        if w <= 0 or h <= 0:
            return
        self.hline(x, y, w)
        self.hline(x, y + h - 1, w)
        self.vline(x, y, h)
        self.vline(x + w - 1, y, h)

    def line(self, x1, y1, x2, y2):
        swap = False
        dx = abs(x2 - x1)
        dy = abs(y2 - y1)
        if dy > dx:
            swap = True
            dx, dy = dy, dx
            x1, y1 = y1, x1
            x2, y2 = y2, x2
        if x1 > x2:
            x1, x2 = x2, x1
            y1, y2 = y2, y1
        err = dx >> 1
        ystep = 1 if y2 > y1 else -1
        y = y1
        for x in range(x1, x2 + 1):
            if swap:
                self.px(y, x)
            else:
                self.px(x, y)
            err -= dy
            if err < 0:
                y += ystep
                err += dx

    # --- circles (u8g2 algorithms) ---
    def _circle_section(self, x, y, x0, y0, opt):
        if opt & UPPER_RIGHT:
            self.px(x0 + x, y0 - y)
            self.px(x0 + y, y0 - x)
        if opt & UPPER_LEFT:
            self.px(x0 - x, y0 - y)
            self.px(x0 - y, y0 - x)
        if opt & LOWER_RIGHT:
            self.px(x0 + x, y0 + y)
            self.px(x0 + y, y0 + x)
        if opt & LOWER_LEFT:
            self.px(x0 - x, y0 + y)
            self.px(x0 - y, y0 + x)

    def _disc_section(self, x, y, x0, y0, opt):
        if opt & UPPER_RIGHT:
            self.vline(x0 + x, y0 - y, y + 1)
            self.vline(x0 + y, y0 - x, x + 1)
        if opt & UPPER_LEFT:
            self.vline(x0 - x, y0 - y, y + 1)
            self.vline(x0 - y, y0 - x, x + 1)
        if opt & LOWER_RIGHT:
            self.vline(x0 + x, y0, y + 1)
            self.vline(x0 + y, y0, x + 1)
        if opt & LOWER_LEFT:
            self.vline(x0 - x, y0, y + 1)
            self.vline(x0 - y, y0, x + 1)

    def _arc(self, x0, y0, r, opt, fn):
        f = 1 - r
        ddx = 1
        ddy = -2 * r
        x = 0
        y = r
        fn(x, y, x0, y0, opt)
        while x < y:
            if f >= 0:
                y -= 1
                ddy += 2
                f += ddy
            x += 1
            ddx += 2
            f += ddx
            fn(x, y, x0, y0, opt)

    def circle(self, x0, y0, r, opt=ALL):
        self._arc(x0, y0, r, opt, self._circle_section)

    def disc(self, x0, y0, r, opt=ALL):
        self._arc(x0, y0, r, opt, self._disc_section)

    def rframe(self, x, y, w, h, r):
        xl, yu = x + r, y + r
        xr, yl = x + w - r - 1, y + h - r - 1
        self.circle(xl, yu, r, UPPER_LEFT)
        self.circle(xr, yu, r, UPPER_RIGHT)
        self.circle(xl, yl, r, LOWER_LEFT)
        self.circle(xr, yl, r, LOWER_RIGHT)
        ww, hh = w - r - r, h - r - r
        xl += 1
        yu += 1
        if ww >= 3:
            ww -= 2
            self.hline(xl, y, ww)
            self.hline(xl, y + h - 1, ww)
        if hh >= 3:
            hh -= 2
            self.vline(x, yu, hh)
            self.vline(x + w - 1, yu, hh)

    def rbox(self, x, y, w, h, r):
        xl, yu = x + r, y + r
        xr, yl = x + w - r - 1, y + h - r - 1
        self.disc(xl, yu, r, UPPER_LEFT)
        self.disc(xr, yu, r, UPPER_RIGHT)
        self.disc(xl, yl, r, LOWER_LEFT)
        self.disc(xr, yl, r, LOWER_RIGHT)
        ww, hh = w - r - r, h - r - r
        xl += 1
        yu += 1
        if ww >= 3:
            ww -= 2
            self.box(xl, y, ww, r + 1)
            self.box(xl, yl, ww, r + 1)
        if hh >= 3:
            hh -= 2
            self.box(x, yu, w, hh)

    # --- text ---
    def str_width(self, s):
        return self.font.width(s)

    def str(self, x, y, s):
        self.font.draw(self, x, y, s, self.color)

    def str_aligned(self, x, y, h, v, s):
        if h == "right":
            x -= self.font.width(s)
        elif h == "center":
            x -= self.font.width(s) // 2
        if v == "top":
            y += self.font.ascent_A
        elif v == "center":
            y += self.font.ascent_A // 2
        self.str(x, y, s)

    def save(self, path, scale=4):
        img = Image.new("RGB", (self.W * scale + 2 * scale, self.H * scale + 2 * scale), (60, 60, 60))
        on = (20, 20, 20)
        off = (255, 140, 0)  # Flipper orange backlight
        for y in range(self.H):
            for x in range(self.W):
                c = on if self.buf[y][x] else off
                for dy in range(scale):
                    for dx in range(scale):
                        img.putpixel((scale + x * scale + dx, scale + y * scale + dy), c)
        img.save(path)


if __name__ == "__main__":
    for k, f in FONTS.items():
        print(k, "ascent", f.ascent_A, "descent", f.descent_g, "w(MORSE ACADEMY)", f.width("MORSE ACADEMY"))
