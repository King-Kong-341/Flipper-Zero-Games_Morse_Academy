"""Renders every Morse Academy screen with the real Flipper fonts (port of the
C draw functions, same coordinates) into a PNG gallery for layout review."""
import os
from PIL import Image, ImageDraw
from flipsim import Canvas
from sim_ui import *

import sys

# optional argument: output folder (default: ./shots next to this script)
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "shots")
os.makedirs(OUT, exist_ok=True)
SHOTS = []


def shot(name, fn):
    c = Canvas()
    fn(c)
    path = os.path.join(OUT, name + ".png")
    c.save(path, scale=3)
    SHOTS.append((name, path))


# ---------------- menus ----------------
MENU = ["Learn", "Practice", "Games", "Free Key", "Alphabet", "Stats", "Settings", "Help"]
MENU_ICONS = ["IconLearn", "IconPractice", "IconGames", "IconKey", "IconAlphabet", "IconStats", "IconSettings", "IconHelp"]


def draw_row(c, y, w, icon, label, info, selected, locked):
    c.set_color(1)
    if selected:
        c.rbox(1, y, w, 12, 3)
        c.set_color(0)
    ui_icon(c, 4, y + 2, icon)
    c.set_font("primary")
    c.str(17, y + 10, label)
    c.set_font("secondary")
    if locked:
        ui_lock(c, w - 9, y + 3)
    elif info:
        c.str_aligned(w - 3, y + 10, "right", "bottom", info)
    c.set_color(1)


def scrollbar(c, x, y, h, pos, count):
    if count <= 1:
        return
    for yy in range(y, y + h, 2):
        c.dot(x + 1, yy)
    thumb = max(h // count, 4)
    ty = y + (h - thumb) * pos // (count - 1)
    c.box(x, ty, 3, thumb)


def menu(sel=0, scroll=0, streak=12, goal_met=True):
    def f(c):
        c.set_font("primary")
        c.str(3, 10, "MORSE ACADEMY")
        buf = str(streak)
        c.set_font("secondary")
        nw = c.str_width(buf)
        c.str_aligned(127, 10, "right", "bottom", buf)
        ui_flame(c, 127 - nw - 9, 2, goal_met)
        c.line(0, 13, 127, 13)
        for i in range(4):
            idx = scroll + i
            info = "L5" if idx == 0 else ("Lv3" if idx == 5 else "")
            draw_row(c, 15 + i * 12, 120, MENU_ICONS[idx], MENU[idx], info, sel == idx, False)
        scrollbar(c, 124, 15, 48, sel, 8)
    return f


def sub_header(c, title):
    c.set_font("primary")
    c.str_aligned(64, 7, "center", "center", title)
    c.line(0, 13, 127, 13)


PRAC = ["Listen", "Send", "Hear words", "Send words", "Weak spots", "Callsigns"]
PRAC_DESC = ["Hear a sign, pick it", "See a sign, key it", "Hear a word, pick it", "Key whole words", "Train your weakest signs", "Copy radio callsigns"]
PRAC_ICONS = ["IconEar", "IconKey", "IconWords", "IconSend", "IconTarget", "IconRadio"]


def practice(sel=0, scroll=0, locked=()):
    def f(c):
        sub_header(c, "PRACTICE")
        for i in range(3):
            idx = scroll + i
            draw_row(c, 15 + i * 12, 120, PRAC_ICONS[idx], PRAC[idx], "", sel == idx, idx in locked)
        scrollbar(c, 124, 15, 36, sel, 6)
        c.line(0, 52, 127, 52)
        c.set_font("secondary")
        desc = PRAC_DESC[sel] if sel not in locked else "Unlocks after lesson 16"
        c.str_aligned(64, 61, "center", "bottom", desc)
    return f


def games(sel=0):
    def f(c):
        sub_header(c, "GAMES")
        names = ["Morse Rush", "Sound Sprint", "Echo Chain"]
        icons = ["IconRocket", "IconBolt", "IconChain"]
        best = ["240", "31", ""]
        for i in range(3):
            draw_row(c, 15 + i * 12, 126, icons[i], names[i], best[i], sel == i, False)
        c.line(0, 52, 127, 52)
        c.set_font("secondary")
        c.str_aligned(64, 61, "center", "bottom", ["Key falling letters!", "60 s of fast listening", "Repeat longer chains"][sel])
    return f


def node_x(i):
    return [14, 28, 42, 28][i % 4]


DX16 = [7, 6, 5, 3, 0, -3, -5, -6, -7, -6, -5, -3, 0, 3, 5, 6]
DY16 = [0, 3, 5, 6, 7, 6, 5, 3, 0, -3, -5, -6, -7, -6, -5, -3]


def course(sel=4, unlocked=4, stars=None, anim=0):
    stars = stars or [3, 2, 3, 1] + [0] * 15

    def f(c):
        for d in range(-3, 4):
            i = sel + d
            if i < 0 or i >= 19:
                continue
            cx = node_x(i)
            cy = 32 + d * 19
            if i + 1 < 19:
                nx = node_x(i + 1)
                ny = cy + 19
                op = i + 1 <= unlocked
                for k in range(1, 10):
                    if not op and k % 2:
                        continue
                    px = cx + int((nx - cx) * k / 10)
                    py = cy + 7 + int((ny - cy - 14) * k / 10)
                    c.dot(px, py)
            done = stars[i] > 0
            op = i <= unlocked
            c.set_color(1)
            if done:
                c.disc(cx, cy, 7)
                c.set_color(0)
            elif op:
                c.circle(cx, cy, 7)
                c.circle(cx, cy, 6)
            else:
                for a in range(16):
                    c.dot(cx + DX16[a], cy + DY16[a])
            if i == 18:
                ui_star(c, cx - 3, cy - 3, True)
            elif not op:
                ui_lock(c, cx - 3, cy - 3)
            else:
                c.set_font("secondary")
                c.str_aligned(cx, cy + 1, "center", "center", str(i + 1))
            c.set_color(1)
            if d == 0 and (anim // 8) % 2 == 0:
                c.circle(cx, cy, 9)
        c.set_color(0)
        c.box(56, 0, 72, 64)
        c.set_color(1)
        c.rframe(58, 1, 69, 62, 4)
        c.set_font("secondary")
        if sel == 18:
            c.str_aligned(92, 10, "center", "bottom", "FINAL EXAM")
            c.set_font("primary")
            c.str_aligned(92, 23, "center", "center", "All signs")
        else:
            c.str_aligned(92, 10, "center", "bottom", f"LESSON {sel + 1}")
            chars = LESSONS[sel][0]
            n = len(chars)
            w = n * 10 + (n - 1) * 4
            x = 92 - w // 2
            for k, ch in enumerate(chars):
                ui_big_char(c, x + k * 14, 14, ch, 2)
        for s in range(3):
            ui_star(c, 79 + s * 9, 33, s < stars[sel])
        if sel <= unlocked:
            ui_button(c, 64, 45, 57, 13, "OK Again" if stars[sel] else "OK Start", (anim // 12) % 2 == 0)
        else:
            ui_lock(c, 67, 48)
            c.set_font("secondary")
            c.str(78, 55, "Locked")
    return f


SET_LABELS = ["Sound", "Volume", "Tone", "Vibration", "LED", "Speed", "Spacing", "Keyer", "Letter pause", "Hints", "Daily goal", "Screen on", "Test speed", "Placement test", "Reset progress"]
SET_VALUES = ["ON", None, "700 Hz", "ON", "ON", "15 WPM", "8 WPM", "Paddle", "Normal", "ON", "50 XP", "ON", "OK", "OK", "OK"]
SET_DESC = ["Beeps for dits and dahs", "How loud the tone is", "Pitch of the beep", "Buzz when Flipper sends", "Colors show what happens", "Speed of each sign (WPM)", "Extra pause between signs", "Paddle: < > / Straight: OK", "Pause that ends a letter", "Down key shows the pattern", "XP to earn every day", "Keep the backlight on", "OK plays a test word", "OK: find your level again", "OK: delete all progress"]


def settings(sel=0, scroll=0, volume=80):
    def f(c):
        ui_frame(c, "SETTINGS")
        for i in range(3):
            idx = scroll + i
            y = 18 + i * 11
            selected = sel == idx
            c.set_color(1)
            if selected:
                c.rbox(4, y, 114, 11, 3)
                c.set_color(0)
            c.set_font("secondary")
            c.str(8, y + 8, SET_LABELS[idx])
            if idx == 1:
                for b in range(10):
                    bh = 3 + b // 2
                    bx = 70 + b * 4
                    by = y + 9 - bh
                    if b < volume // 10:
                        c.box(bx, by, 3, bh)
                    else:
                        c.dot(bx + 1, y + 8)
            else:
                buf = SET_VALUES[idx]
                adj = idx < 12
                if selected and adj:
                    c.str_aligned(114, y + 8, "right", "bottom", ">")
                    w = c.str_width(buf)
                    c.str_aligned(108, y + 8, "right", "bottom", buf)
                    c.str_aligned(104 - w, y + 8, "right", "bottom", "<")
                else:
                    c.str_aligned(114, y + 8, "right", "bottom", buf)
            c.set_color(1)
        scrollbar(c, 121, 18, 33, sel, 15)
        c.line(4, 52, 123, 52)
        c.set_font("secondary")
        c.str_aligned(64, 61, "center", "bottom", SET_DESC[sel])
    return f


def confirm(reset=True, yes=False):
    def f(c):
        ui_frame(c, "RESET PROGRESS?" if reset else "PLACEMENT TEST?")
        c.set_font("secondary")
        ui_wrap(c, 6, 25, 116, 9, "Deletes lessons, XP, stars, badges and stats. Settings stay." if reset else "A short listening test. Lessons you already know get unlocked.", 0, 3)
        ui_button(c, 18, 49, 40, 12, "Yes", yes)
        ui_button(c, 70, 49, 40, 12, "No", not yes)
    return f


def alphabet(ch="A", mastery=60, learned=True, playing_el=None):
    def f(c):
        code = CODE[ch]
        c.rframe(1, 1, 37, 49, 4)
        ui_big_char(c, 7, 8, ch, 5)
        c.set_font("secondary")
        kind = "LETTER" if ch.isalpha() else ("NUMBER" if ch.isdigit() else "SIGN")
        c.str(43, 9, kind)
        c.str_aligned(126, 9, "right", "bottom", f"{ORDER.index(ch) + 1}/41")
        playing = playing_el is not None
        ui_pattern(c, 84, 20, code, True, playing_el if playing else 127, playing)
        buf = ui_phonetic(code)
        if c.str_width(buf) <= 82:
            c.str_aligned(84, 33, "center", "bottom", buf)
        else:
            cut = len(buf) // 2
            while cut < len(buf) and buf[cut] != "-":
                cut += 1
            c.str_aligned(84, 31, "center", "bottom", buf[:cut])
            c.str_aligned(84, 40, "center", "bottom", buf[cut + 1:])
        sim = "N" if ch == "A" else "Q"
        c.str(43, 49, f"Like: {sim}")
        if learned:
            c.str_aligned(126, 49, "right", "bottom", f"{mastery}%")
            ui_progress(c, 74, 44, 26, 5, mastery, 100)
        else:
            c.str_aligned(126, 49, "right", "bottom", "new")
        c.line(0, 52, 127, 52)
        c.str(2, 61, "OK: play")
        c.str_aligned(126, 61, "right", "bottom", "< browse >")
    return f


def draw_medal(c, cx, cy, r, got):
    if got:
        c.disc(cx, cy, r)
    else:
        dx = [8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3, 0, 3, 6, 7]
        dy = [0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3]
        for a in range(16):
            c.dot(cx + int(dx[a] * r / 8), cy + int(dy[a] * r / 8))


def stats_header(c, title):
    ui_frame(c, title)
    ui_arrow(c, 6, 8, 3, 3)
    ui_arrow(c, 121, 8, 1, 3)


def stats(page=0):
    def f(c):
        if page == 0:
            stats_header(c, "OVERVIEW")
            c.set_font("primary")
            c.str(6, 26, "Lv 3 Radio Pro")
            c.set_font("secondary")
            c.str_aligned(122, 26, "right", "bottom", "4860 XP")
            ui_progress(c, 6, 29, 116, 5, 300, 540)
            ui_flame(c, 6, 37, True)
            c.str(16, 45, "Streak 12  (best 30)")
            c.str(6, 53, "Today 40/50 XP")
            ui_progress(c, 84, 48, 38, 5, 40, 50)
            c.str(6, 61, "Lessons 19/19   100% right")
        elif page == 1:
            stats_header(c, "SIGNS")
            c.set_font("secondary")
            for i, ch in enumerate(ORDER):
                x = 4 + (i % 11) * 11
                y = 18 + (i // 11) * 11
                m = (i * 37) % 101
                learned = i < 30
                c.set_color(1)
                if learned and m >= 80:
                    c.rbox(x, y, 10, 10, 2)
                    c.set_color(0)
                    c.str_aligned(x + 5, y + 7, "center", "bottom", ch)
                else:
                    c.str_aligned(x + 5, y + 7, "center", "bottom", ch)
                    if learned:
                        bw = max(m * 9 // 100, 1)
                        c.line(x + 1, y + 9, x + bw, y + 9)
                    else:
                        c.dot(x + 5, y + 9)
                c.set_color(1)
        elif page == 2:
            stats_header(c, "BADGES")
            got = {0, 1, 3, 5, 8}
            sel = 3 if not hasattr(stats, "sel") else stats.sel
            for i in range(12):
                cx = 9 + i * 10
                draw_medal(c, cx, 22, 3, i in got)
                if i == sel:
                    ui_arrow(c, cx, 30, 0, 3)
            c.line(4, 33, 123, 33)
            g = sel in got
            draw_medal(c, 15, 46, 9, g)
            if g:
                c.set_color(0)
                ui_star(c, 12, 43, True)
                c.set_color(1)
            else:
                c.set_font("primary")
                c.str_aligned(15, 47, "center", "center", "?")
            c.set_font("primary")
            ui_str_fit(c, 29, 44, 96, "left", BADGES[sel][0])
            c.set_font("secondary")
            ui_str_fit(c, 29, 53, 96, "left", BADGES[sel][1])
            c.str(29, 61, ("Earned!" if g else "Not yet") + "  5/12")
        elif page == 99:
            got = {0, 1, 3, 5, 8}
            sel = 3
            dx12 = [6, 5, 3, 0, -3, -5, -6, -5, -3, 0, 3, 5]
            dy12 = [0, 3, 5, 6, 5, 3, 0, -3, -5, -6, -5, -3]
            for i in range(12):
                cx = 14 + (i % 6) * 20
                cy = 24 + (i // 6) * 15
                if i in got:
                    c.disc(cx, cy, 6)
                    c.set_color(0)
                    ui_star(c, cx - 3, cy - 3, True)
                    c.set_color(1)
                else:
                    for a in range(12):
                        c.dot(cx + dx12[a], cy + dy12[a])
                    c.set_font("secondary")
                    c.str_aligned(cx, cy + 1, "center", "center", "?")
                if i == sel:
                    c.rframe(cx - 9, cy - 8, 19, 17, 3)
            c.set_font("secondary")
            c.str(6, 52, BADGES[sel][0] + " *")
            c.str_aligned(122, 52, "right", "bottom", "5/12")
            c.str(6, 61, BADGES[sel][1])
        else:
            stats_header(c, "RECORDS")
            for y, l, v in [(25, "Morse Rush", "240"), (33, "Sound Sprint", "31"), (41, "Echo Chain", "9"), (49, "Best run", "27"), (57, "Words right", "112")]:
                ui_dotted_row(c, y, l, v)
    return f


HELP = [
    ("WHAT IS MORSE?", "Morse code turns letters into short and long beeps. Short = dit (.), long = dah (-). Radio hams, pilots and sailors use it."),
    ("YOUR MORSE KEY", "Left = dit, Right = dah. Hold a key to repeat it. Pause a moment (or press OK) and the letter is done. Up clears."),
]


def help_screen(page=0, scroll=0):
    def f(c):
        t, text = HELP[page]
        ui_frame(c, t)
        c.set_font("secondary")
        lines = ui_wrap(c, 6, 25, 112, 9, text, scroll, 4)
        if scroll > 0:
            ui_scroll_arrow(c, 123, 21, True)
        if scroll + 4 < lines:
            ui_scroll_arrow(c, 123, 50, False)
        c.str_aligned(64, 62, "center", "bottom", f"< {page + 1}/10 >")
    return f


def draw_tower(c, x, base_y, anim):
    top = base_y - 34
    c.line(x, top, x - 9, base_y)
    c.line(x, top, x + 9, base_y)
    for k in range(1, 5):
        y = top + k * 7
        half = 9 * k * 7 // 34
        c.line(x - half, y, x + half, y)
        if k < 4:
            half2 = 9 * (k + 1) * 7 // 34
            c.line(x - half, y, x + half2, y + 7)
    c.line(x - 12, base_y, x + 12, base_y)
    c.disc(x, top - 1, 2)
    phase = (anim // 5) % 4
    for a in range(3):
        if a >= phase:
            continue
        r = 6 + a * 5
        for dy in range(-(r // 2), r // 2 + 1):
            dx = 0
            while (dx + 1) ** 2 + dy * dy <= r * r:
                dx += 1
            c.dot(x + dx, top - 1 + dy)
            c.dot(x - dx, top - 1 + dy)


def welcome(anim=15):
    def f(c):
        draw_tower(c, 22, 60, anim)
        c.set_font("primary")
        c.str(46, 12, "MORSE")
        c.str(46, 23, "ACADEMY")
        ui_pattern(c, 58, 30, "....", False)
        ui_pattern(c, 80, 30, "..", False)
        c.set_font("secondary")
        c.str(46, 41, "Learn Morse code")
        c.str(46, 49, "from zero to hero!")
        ui_button(c, 46, 52, 78, 11, "OK - Let's go", True)
    return f


def experience(sel=0):
    def f(c):
        ui_frame(c, "HOW MUCH MORSE?")
        for i, o in enumerate(["Not at all", "A little", "Very well"]):
            ui_button(c, 20, 18 + i * 12, 88, 11, o, sel == i)
        c.set_font("secondary")
        c.str_aligned(64, 62, "center", "bottom", "Redo it later in Settings")
    return f


TUT = ["Press LEFT once. A short beep is a dit.", "Now press RIGHT. A long beep is a dah.", "Key an A: dit then dah (Left, Right).", "Perfect! A letter ends when you pause. Ready!"]


def tutorial(step=2, keyed=1, on_dah=False):
    def f(c):
        ui_frame(c, "TRY YOUR KEY" if step < 3 else "YOU'RE READY")
        c.set_font("secondary")
        ui_wrap(c, 6, 25, 116, 9, TUT[step], 0, 2)
        if step < 3:
            target = [".", "-", ".-"][step]
            ui_pattern(c, 64, 43, target, True, keyed - 1, True)
            c.str(4, 61, "<dit")
            c.str_aligned(124, 61, "right", "bottom", "dah>")
            if on_dah:
                c.rframe(100, 52, 26, 11, 3)
        else:
            ui_pattern(c, 64, 44, ".-", True)
            ui_button(c, 34, 50, 60, 12, "OK - Start", True)
    return f


# ---------------- quiz ----------------
def quiz_tip(lesson=4, anim=0):
    def f(c):
        ui_top_progress(c, 0, 20)
        chars, tip = LESSONS[lesson]
        c.set_font("secondary")
        if lesson == 18:
            c.set_font("primary")
            c.str_aligned(64, 13, "center", "center", "FINAL EXAM")
        else:
            c.str(3, 13, f"L{lesson + 1}")
            c.str_aligned(125, 13, "right", "bottom", "NEW")
            n = len(chars)
            w = n * 10 + (n - 1) * 5
            x = 64 - w // 2
            for i, ch in enumerate(chars):
                ui_big_char(c, x + i * 15, 5, ch, 2)
        c.line(4, 21, 123, 21)
        c.set_font("secondary")
        n = ui_wrap(c, 4, 30, 120, 9, tip, 0, 3)
        if n > 3:
            print("!! tip too long for lesson", lesson + 1, n, "lines")
        if (anim // 12) % 2 == 0:
            c.rbox(96, 52, 28, 11, 3)
            c.set_color(0)
        else:
            c.rframe(96, 52, 28, 11, 3)
        c.str_aligned(110, 58, "center", "center", "OK >")
        c.set_color(1)
    return f


def quiz_intro(ch="R", lit=None):
    def f(c):
        ui_top_progress(c, 1, 20)
        code = CODE[ch]
        c.rframe(3, 6, 30, 36, 3)
        ui_big_char(c, 8, 10, ch, 4)
        c.set_font("secondary")
        kind = "LETTER" if ch.isalpha() else ("NUMBER" if ch.isdigit() else "SIGN")
        c.str_aligned(81, 12, "center", "bottom", "NEW " + kind)
        playing = lit is not None
        ui_pattern(c, 81, 24, code, True, lit if playing else 127, playing)
        c.set_font("secondary")
        ui_str_fit(c, 64, 51, 122, "center", ui_phonetic(code))
        ui_dotted_row(c, 61, "OK: hear again", "Next >")
    return f


def key_hints(c, straight=False, allow_hint=True):
    c.set_font("secondary")
    c.str(2, 63, "<dit")
    c.str_aligned(126, 63, "right", "bottom", "dah>")
    mid = "hold OK = key" if straight else ("^clear  v hint" if allow_hint else "^clear")
    c.str_aligned(64, 63, "center", "bottom", mid)


def live_pattern(c, pat, cy, cursor=True):
    w = pattern_width(pat, True)
    ui_pattern(c, 64, cy, pat, True)
    if cursor:
        cx = 64 + w // 2 + 4 if w > 0 else 64
        c.line(cx, cy - 4, cx, cy + 4)


def quiz_send(ch="R", guided=True, keyed=".-", state="ask", wrong="K", hinted=False):
    def f(c):
        ui_top_progress(c, 5, 20)
        code = CODE[ch]
        c.rframe(3, 6, 25, 29, 3)
        ui_big_char(c, 8, 10, ch, 3)
        c.set_font("primary")
        c.str(33, 14, "Your turn!" if guided else "Send it:")
        if guided or hinted or state == "wrong":
            lit = len(keyed) - 1 if code.startswith(keyed) else -1
            ui_pattern(c, 77, 27, code, True, 127 if state == "wrong" else lit, True)
        else:
            c.set_font("secondary")
            c.str(33, 29, "from memory")
        c.set_font("secondary")
        if state == "right":
            c.str_aligned(64, 44, "center", "center", "Correct!")
            ui_pattern(c, 64, 51, code, False)
        elif state == "wrong":
            c.str_aligned(64, 44, "center", "center", f"It's {ch} - OK to go on")
        elif state == "try":
            c.str_aligned(64, 44, "center", "center", f"That was {wrong} - try again")
            ui_pattern(c, 64, 51, CODE[wrong], False)
        else:
            live_pattern(c, keyed, 45)
        key_hints(c, False, not guided)
    return f


def quiz_word_send(word="MOON", typed=2, keyed="-.", state="ask", hinted=False):
    def f(c):
        ui_top_progress(c, 8, 20)
        n = len(word)
        c.set_font("secondary")
        c.str(3, 12, "Send the word:")
        w = n * 10 + (n - 1) * 4
        x = 64 - w // 2
        for i, ch in enumerate(word):
            lx = x + i * 14
            ui_big_char(c, lx, 16, ch, 2)
            if i < typed:
                c.box(lx, 32, 10, 2)
            elif i == typed:
                c.line(lx, 33, lx + 9, 33)
        target = word[typed] if typed < n else None
        if state == "right":
            c.str_aligned(64, 44, "center", "center", "Great word!")
        elif state == "wrong":
            c.str_aligned(64, 41, "center", "center", "Listen to it - OK to go on")
            ui_pattern(c, 64, 50, CODE[target], False)
        elif hinted:
            ui_pattern(c, 64, 40, CODE[target], False)
            live_pattern(c, keyed, 50)
        else:
            live_pattern(c, keyed, 46)
        key_hints(c)
    return f


def quiz_choice(opts=("K", "R", "N", "A"), answer=1, state="ask", chosen=None, words=False, run=4, cur=6, count=20, playing=True, anim=10, calls=False):
    def f(c):
        ui_top_progress(c, cur, count)
        for slot in range(4):
            if opts[slot] is None:
                continue
            filled = state == "right" and slot == answer
            cross = False
            if state == "wrong":
                filled = slot == answer
                cross = slot == chosen
            font = "primary" if not words else ("keyboard" if calls else "secondary")
            ui_choice_box(c, slot, opts[slot], filled, cross, font)
        c.set_font("secondary")
        c.str(2, 12, f"{cur + 1}/{count}")
        if run >= 3:
            ui_flame(c, 2, 14, True)
            c.str(11, 22, f"x{run}")
        c.str_aligned(126, 12, "right", "bottom", "OK:")
        c.str_aligned(126, 21, "right", "bottom", "replay")
        if state == "ask":
            ui_waves(c, 66, 33, anim, playing)
        else:
            ans = opts[answer]
            c.set_font("primary")
            if not words:
                c.str_aligned(64, 30, "center", "center", ans)
                ui_pattern(c, 64, 42, CODE[ans], False)
            else:
                c.set_font("secondary")
                c.str_aligned(64, 33, "center", "center", "Yes!" if state == "right" else "Hmm")
        c.set_font("secondary")
        if state == "right":
            c.str(2, 60, "Nice!")
        elif state == "wrong":
            c.str(2, 60, "Oops!")
            c.str_aligned(126, 60, "right", "bottom", "OK >")
    return f


def results_lesson(stars=2, acc=88, xp=130, passed=True, streak=3):
    def f(c):
        ui_frame(c, "LESSON DONE!" if passed else "ALMOST THERE")
        for i in range(3):
            ui_star_big(c, 39 + i * 18, 17, i < stars)
        c.set_font("secondary")
        c.str(8, 42, f"Accuracy {acc}%")
        c.str_aligned(120, 42, "right", "bottom", f"+{xp} XP")
        ui_flame(c, 8, 44, streak > 0)
        c.str(18, 51, f"{streak} day streak" if passed else "Need 70% - try again!")
        c.str_aligned(120, 60, "right", "bottom", "OK Next" if passed else "OK Retry")
        c.str(8, 60, "< Retry" if passed else "Back: Menu")
    return f


def results_practice(acc=87, right=17, total=20, xp=95):
    def f(c):
        ui_frame(c, "PRACTICE DONE")
        buf = str(acc)
        c.set_font("big")
        w = c.str_width(buf)
        c.str(64 - (w + 8) // 2, 34, buf)
        c.set_font("primary")
        c.str(64 - (w + 8) // 2 + w + 1, 34, "%")
        c.set_font("secondary")
        c.str(8, 42, f"{right}/{total} right")
        c.str_aligned(120, 42, "right", "bottom", f"+{xp} XP")
        ui_flame(c, 8, 44, True)
        c.str(18, 51, "3 day streak")
        c.str_aligned(120, 60, "right", "bottom", "OK Again")
        c.str(8, 60, "Back: Menu")
    return f


def results_placement(lesson=4):
    def f(c):
        ui_frame(c, "PLACEMENT DONE")
        c.set_font("secondary")
        c.str_aligned(64, 22, "center", "center", "Your start:")
        c.set_font("primary")
        c.str_aligned(64, 32, "center", "center", f"Lesson {lesson + 1}")
        chars = LESSONS[lesson][0]
        n = len(chars)
        w = n * 10 + (n - 1) * 4
        x = 64 - w // 2
        for i, ch in enumerate(chars):
            ui_big_char(c, x + i * 14, 38, ch, 2)
        c.set_font("secondary")
        c.str_aligned(124, 60, "right", "bottom", "OK >")
    return f


# ---------------- games ----------------
LANES = [14, 39, 64, 89, 114]


def hud(c, lives=2, score=120, level=2, combo=4):
    for i in range(3):
        ui_heart(c, 2 + i * 9, 1, i < lives)
    c.set_font("secondary")
    c.str_aligned(126, 8, "right", "bottom", str(score))
    c.str_aligned(64, 8, "center", "bottom", f"Lv{level}")
    if combo >= 3:
        c.str(80, 8, f"x{combo}")


def rush(keyed="", hint=True):
    def f(c):
        hud(c)
        c.line(0, 10, 127, 10)
        for x in range(0, 128, 2):
            c.dot(x, 50)
        fallers = [("K", 1, 38), ("M", 3, 22), ("S", 4, 16)]
        low = fallers[0]
        for ch, lane, cy in fallers:
            cx = LANES[lane]
            c.set_color(1)
            if (ch, lane, cy) == low:
                c.rbox(cx - 6, cy - 6, 13, 13, 3)
                c.set_color(0)
            else:
                c.rframe(cx - 6, cy - 6, 13, 13, 3)
            c.set_font("primary")
            c.str_aligned(cx + 1, cy + 1, "center", "center", ch)
            c.set_color(1)
        c.line(64, 53, 89, 28)
        if keyed:
            ui_pattern(c, 64, 58, keyed, True)
        elif hint:
            c.set_font("secondary")
            c.str(2, 61, "Hint K:")
            ui_pattern(c, 80, 58, CODE["K"], False)
        else:
            ui_icon(c, 60, 54, "IconRadio")
    return f


def sprint(state="ask"):
    def f(c):
        ui_top_progress(c, 41000, 60000)
        opts = ["U", "S", "H", "V"]
        ans = 1
        for slot in range(4):
            ui_choice_box(c, slot, opts[slot], state != "ask" and slot == ans, state == "wrong" and slot == 3, "primary")
        c.set_font("primary")
        c.str(2, 14, "23")
        c.set_font("secondary")
        c.str_aligned(126, 13, "right", "bottom", "41s")
        c.str_aligned(126, 22, "right", "bottom", "17wpm")
        ui_flame(c, 2, 16, True)
        c.str(11, 24, "x7")
        if state == "ask":
            ui_waves(c, 66, 33, 12, True)
        else:
            c.set_font("primary")
            c.str_aligned(64, 30, "center", "center", "S")
            ui_pattern(c, 64, 42, CODE["S"], False)
        c.set_font("primary")
        c.str(4, 58, "+3s!")
    return f


def echo(phase=2, seq="KMRSO", pos=2, keyed=".-"):
    def f(c):
        c.set_font("primary")
        c.str_aligned(64, 7, "center", "center", f"ROUND {len(seq)}")
        c.set_font("secondary")
        c.str_aligned(126, 8, "right", "bottom", "Best 9")
        c.line(0, 13, 127, 13)
        active = pos
        first = active - 4 if active > 4 else 0
        shown = min(len(seq) - first, 9)
        x0 = 64 - (shown * 13 - 2) // 2
        for i in range(shown):
            idx = first + i
            x = x0 + i * 13
            done = phase >= 2 and idx < pos
            is_active = idx == active and phase <= 2
            c.set_color(1)
            if is_active:
                c.rbox(x, 17, 11, 13, 2)
                c.set_color(0)
            else:
                c.rframe(x, 17, 11, 13, 2)
            t = seq[idx] if (done or phase >= 3) else "?"
            c.set_font("primary")
            c.str_aligned(x + 6, 24, "center", "center", t)
            c.set_color(1)
        c.set_font("secondary")
        if phase == 1:
            ui_waves(c, 66, 42, 12, True)
        elif phase == 2:
            if keyed:
                ui_pattern(c, 64, 42, keyed, True)
            else:
                c.str_aligned(64, 40, "center", "center", "Your turn - key it back!")
        elif phase == 4:
            c.str_aligned(64, 37, "center", "center", "It was R, not K")
            ui_pattern(c, 64, 46, CODE["R"], False)
        c.set_font("secondary")
        c.str(2, 63, "<dit")
        c.str_aligned(126, 63, "right", "bottom", "dah>")
    return f


def game_over(record=True, score=240):
    def f(c):
        ui_frame(c, "MORSE RUSH")
        for k in range(6):
            lx = 6 + (k * 5) % 26
            rx = 96 + (k * 5 + 3) % 26
            fy = 18 + ((7 + k * 4) % 20)
            c.dot(lx, fy)
            c.dot(rx, 38 - (fy - 18))
        c.set_font("secondary")
        c.str_aligned(64, 21, "center", "center", "* NEW RECORD *" if record else "Best: 300")
        c.set_font("big")
        c.str_aligned(64, 34, "center", "center", str(score))
        c.set_font("secondary")
        c.str_aligned(64, 47, "center", "center", "+24 XP")
        c.str(6, 60, "< Games")
        c.str_aligned(122, 60, "right", "bottom", "OK Again")
    return f


def freekey(text="HELLO WORLD CQ CQ DE FLIPPER K", keyed="-.-"):
    def f(c):
        c.set_font("primary")
        c.str(2, 9, "FREE KEY")
        c.set_font("secondary")
        c.str_aligned(126, 9, "right", "bottom", "15 WPM")
        c.rframe(0, 12, 128, 27, 3)
        c.set_font("keyboard")
        per = 20
        total = len(text) // per + 1
        first = total - 2 if total > 2 else 0
        for l in range(2):
            start = (first + l) * per
            if start > len(text):
                break
            line = text[start:start + per]
            c.str(4, 23 + l * 11, line)
            if first + l == total - 1:
                c.box(4 + len(line) * 6, 16 + l * 11, 2, 9)
        if keyed:
            ui_pattern(c, 56, 46, keyed, True)
            c.set_font("primary")
            dec = {v: k for k, v in CODE.items()}.get(keyed, "?")
            c.str_aligned(124, 46, "right", "center", "=" + dec)
        c.set_font("secondary")
        c.str(2, 63, "<dit")
        c.str_aligned(126, 63, "right", "bottom", "dah>")
        c.str_aligned(64, 63, "center", "bottom", "^del vspace")
    return f


def toast_over(base, title, text, icon):
    def f(c):
        base(c)
        ui_toast(c, title, text, icon)
    return f


def pause_over(base, q):
    def f(c):
        base(c)
        ui_yes_no(c, q, False)
    return f


if __name__ == "__main__":
    shot("01_welcome", welcome())
    shot("02_experience", experience(1))
    shot("03_tutorial", tutorial(2, 1, True))
    shot("04_tutorial_done", tutorial(3))
    shot("05_menu", menu(0, 0))
    shot("06_menu_scrolled", menu(5, 4, streak=128, goal_met=False))
    shot("07_course", course(4, 4))
    shot("08_course_final", course(18, 18, [3] * 18 + [0]))
    shot("09_course_locked", course(7, 4))
    shot("10_tip_L5", quiz_tip(4))
    shot("11_tip_L14", quiz_tip(13))
    shot("12_tip_L18", quiz_tip(17))
    shot("13_tip_final", quiz_tip(18))
    shot("14_intro_R", quiz_intro("R", 1))
    shot("15_intro_comma", quiz_intro(",", None))
    shot("16_guided", quiz_send("R", True, ".-"))
    shot("17_send_try", quiz_send("R", False, "", "try", "K"))
    shot("18_send_right", quiz_send("7", False, "", "right"))
    shot("19_send_wrong", quiz_send("?", False, "", "wrong"))
    shot("20_word_send", quiz_word_send("PLANET", 2, "-."))
    shot("21_word_send_hint", quiz_word_send("MOON", 1, "-", hinted=True))
    shot("22_word_wrong", quiz_word_send("MOON", 1, "", state="wrong"))
    shot("23_listen", quiz_choice())
    shot("24_listen_right", quiz_choice(state="right"))
    shot("25_listen_wrong", quiz_choice(opts=(".", ",", "?", "/"), answer=1, state="wrong", chosen=3))
    shot("26_listen_2opts", quiz_choice(opts=(None, "T", None, "E"), answer=1, run=0, cur=3))
    shot("27_words", quiz_choice(opts=("PLANET", "VALLEY", "OXYGEN", "PUZZLE"), answer=0, words=True))
    shot("28_calls", quiz_choice(opts=("HB9ABC", "WW9WMW", "W1AW", "K2QZM"), answer=0, words=True, state="wrong", chosen=2, calls=True))
    shot("29_pause", pause_over(quiz_choice(), "Quit practice?"))
    shot("30_results", results_lesson())
    shot("31_results_fail", results_lesson(0, 62, 40, False))
    shot("32_results_practice", results_practice())
    shot("33_results_place", results_placement(4))
    shot("34_practice_menu", practice(0))
    shot("35_practice_locked", practice(5, 3, locked=(5,)))
    shot("36_games_menu", games(1))
    shot("37_rush", rush())
    shot("38_rush_keying", rush(".-."))
    shot("39_sprint", sprint())
    shot("40_sprint_wrong", sprint("wrong"))
    shot("41_echo_turn", echo(2))
    shot("42_echo_listen", echo(1, pos=1))
    shot("43_echo_fail", echo(4))
    shot("44_gameover", game_over())
    shot("45_freekey", freekey())
    shot("46_alphabet_A", alphabet("A"))
    shot("47_alphabet_comma", alphabet(",", learned=False))
    shot("48_alphabet_0", alphabet("0", 100, True, 2))
    shot("49_stats0", stats(0))
    shot("50_stats1", stats(1))
    shot("51_stats2", stats(2))
    stats.sel = 10
    shot("51b_stats2_locked", stats(2))
    shot("52_stats3", stats(3))
    shot("53_settings", settings(1, 0))
    shot("54_settings2", settings(8, 7))
    shot("55_settings3", settings(12, 12))
    shot("56_help", help_screen(0))
    shot("57_confirm", confirm(True, False))
    shot("58_toast_badge", toast_over(menu(0, 0), "Badge unlocked!", "Number Cruncher", 0))
    shot("59_toast_goal", toast_over(results_lesson(), "Daily goal!", "65535 XP today!", 2))
    shot("60_toast_level", toast_over(results_lesson(), "Level up!", "Lv 10 - Master", 1))

    # contact sheet
    cols = 4
    tw, th = SHOTS and Image.open(SHOTS[0][1]).size
    pad = 26
    rows_n = (len(SHOTS) + cols - 1) // cols
    for part in range(0, rows_n, 5):
        sub = SHOTS[part * cols:(part + 5) * cols]
        if not sub:
            break
        sheet = Image.new("RGB", (cols * (tw + 10) + 10, ((len(sub) + cols - 1) // cols) * (th + pad) + 10), (30, 30, 30))
        d = ImageDraw.Draw(sheet)
        for i, (name, p) in enumerate(sub):
            x = 10 + (i % cols) * (tw + 10)
            y = 10 + (i // cols) * (th + pad)
            sheet.paste(Image.open(p), (x, y + 14))
            d.text((x, y), name, fill=(230, 230, 230))
        sheet.save(os.path.join(OUT, f"sheet_{part // 5 + 1}.png"))
    print("rendered", len(SHOTS))
