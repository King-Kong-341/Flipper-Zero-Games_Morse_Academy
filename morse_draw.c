#include "morse.h"

/* Shared drawing helpers. All small pictures are defined as '#'/'.' strings
 * so they are easy to read and tweak. */

static void draw_rows(Canvas* canvas, int16_t x, int16_t y, const char* const* rows, uint8_t n) {
    for(uint8_t r = 0; r < n; r++) {
        for(uint8_t c = 0; rows[r][c] != '\0'; c++) {
            if(rows[r][c] == '#') canvas_draw_dot(canvas, x + c, y + r);
        }
    }
}

/* ---------- Frames & rows ---------- */

void ui_frame(Canvas* canvas, const char* title) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 0, 0, 128, 64, 4);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 9, AlignCenter, AlignCenter, title);
    canvas_draw_line(canvas, 14, 15, 113, 15);
}

void ui_dotted_row(Canvas* canvas, int16_t y, const char* label, const char* key) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 6, y, label);
    uint16_t label_w = canvas_string_width(canvas, label);
    uint16_t key_w = canvas_string_width(canvas, key);
    int16_t dots_x = 6 + (int16_t)label_w + 3;
    int16_t dots_end = 122 - (int16_t)key_w - 2;
    for(int16_t x = dots_x; x < dots_end; x += 3) {
        canvas_draw_dot(canvas, x, y - 2);
    }
    canvas_draw_str_aligned(canvas, 122, y, AlignRight, AlignBottom, key);
}

void ui_scroll_arrow(Canvas* canvas, int16_t cx, int16_t cy, bool up) {
    for(int16_t k = 0; k < 3; k++) {
        int16_t row = up ? (cy - 1 + k) : (cy + 1 - k);
        canvas_draw_line(canvas, cx - k, row, cx + k, row);
    }
}

/* Solid triangle: dir 0 up, 1 right, 2 down, 3 left */
void ui_arrow(Canvas* canvas, int16_t cx, int16_t cy, uint8_t dir, uint8_t size) {
    for(int16_t k = 0; k < size; k++) {
        switch(dir) {
        case 0:
            canvas_draw_line(canvas, cx - k, cy - size + 1 + k, cx + k, cy - size + 1 + k);
            break;
        case 2:
            canvas_draw_line(canvas, cx - k, cy + size - 1 - k, cx + k, cy + size - 1 - k);
            break;
        case 3:
            canvas_draw_line(canvas, cx - size + 1 + k, cy - k, cx - size + 1 + k, cy + k);
            break;
        default:
            canvas_draw_line(canvas, cx + size - 1 - k, cy - k, cx + size - 1 - k, cy + k);
            break;
        }
    }
}

/* ---------- Dits & dahs ---------- */

#define BIG_DIT 5
#define BIG_DAH 13
#define BIG_GAP 4
#define SMALL_DIT 3
#define SMALL_DAH 8
#define SMALL_GAP 3

int16_t ui_pattern_width(const char* code, bool big) {
    int16_t w = 0;
    for(uint8_t i = 0; code[i] != '\0'; i++) {
        if(i > 0) w += big ? BIG_GAP : SMALL_GAP;
        if(code[i] == '-') {
            w += big ? BIG_DAH : SMALL_DAH;
        } else {
            w += big ? BIG_DIT : SMALL_DIT;
        }
    }
    return w;
}

/* Draws a pattern centered on cx/cy. Elements up to 'lit_upto' are solid;
 * if 'outline_rest' is set, the ones after it are drawn hollow (used to
 * follow playback element by element). */
void ui_pattern(
    Canvas* canvas,
    int16_t cx,
    int16_t cy,
    const char* code,
    bool big,
    int8_t lit_upto,
    bool outline_rest) {
    int16_t x = cx - ui_pattern_width(code, big) / 2;
    for(uint8_t i = 0; code[i] != '\0'; i++) {
        bool solid = !outline_rest || (int8_t)i <= lit_upto;
        bool dah = code[i] == '-';
        if(big) {
            if(dah) {
                if(solid) {
                    canvas_draw_rbox(canvas, x, cy - 2, BIG_DAH, 5, 2);
                } else {
                    canvas_draw_rframe(canvas, x, cy - 2, BIG_DAH, 5, 2);
                }
                x += BIG_DAH + BIG_GAP;
            } else {
                if(solid) {
                    canvas_draw_disc(canvas, x + 2, cy, 2);
                } else {
                    canvas_draw_circle(canvas, x + 2, cy, 2);
                }
                x += BIG_DIT + BIG_GAP;
            }
        } else {
            int16_t w = dah ? SMALL_DAH : SMALL_DIT;
            if(solid) {
                canvas_draw_box(canvas, x, cy - 1, w, 3);
            } else {
                canvas_draw_frame(canvas, x, cy - 1, w, 3);
            }
            x += w + SMALL_GAP;
        }
    }
}

/* Scaled 5x7 pixel letter; top-left at x/y, size 5*scale x 7*scale. */
void ui_big_char(Canvas* canvas, int16_t x, int16_t y, char c, uint8_t scale) {
    int idx = morse_index(c);
    if(idx < 0) return;
    for(uint8_t r = 0; r < 7; r++) {
        uint8_t bits = glyph_5x7[idx][r];
        for(uint8_t col = 0; col < 5; col++) {
            if(bits & (0x10 >> col)) {
                canvas_draw_box(canvas, x + col * scale, y + r * scale, scale, scale);
            }
        }
    }
}

/* ---------- Text ---------- */

/* Word-wraps 'text' into 'width' pixels with the current font. Lines before
 * 'skip' are not drawn, at most 'max_lines' are drawn (0 = only count).
 * '\n' forces a line break. Returns the total number of lines. */
uint8_t ui_wrap(
    Canvas* canvas,
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t line_h,
    const char* text,
    uint8_t skip,
    uint8_t max_lines) {
    char line[48];
    char cand[48];
    uint8_t ll = 0;
    uint8_t lines = 0;
    line[0] = '\0';
    const char* p = text;

#define EMIT()                                                                    \
    do {                                                                          \
        if(lines >= skip && lines < skip + max_lines) {                           \
            canvas_draw_str(canvas, x, y + (lines - skip) * line_h, line);         \
        }                                                                         \
        lines++;                                                                  \
        ll = 0;                                                                   \
        line[0] = '\0';                                                           \
    } while(0)

    while(*p != '\0') {
        if(*p == ' ') {
            p++;
            continue;
        }
        if(*p == '\n') {
            EMIT();
            p++;
            continue;
        }
        const char* ws = p;
        while(*p != '\0' && *p != ' ' && *p != '\n')
            p++;
        size_t wl = (size_t)(p - ws);
        if(wl > 30) wl = 30;
        /* cand = line + " " + word, always bounded */
        size_t cl = 0;
        if(ll > 0) {
            memcpy(cand, line, ll);
            cl = ll;
            cand[cl++] = ' ';
        }
        if(cl + wl > sizeof(cand) - 1) wl = sizeof(cand) - 1 - cl;
        memcpy(cand + cl, ws, wl);
        cand[cl + wl] = '\0';
        if(ll > 0 && (int16_t)canvas_string_width(canvas, cand) > width) {
            EMIT();
            memcpy(line, ws, wl);
            line[wl] = '\0';
            ll = (uint8_t)wl;
        } else {
            strncpy(line, cand, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
            ll = (uint8_t)strlen(line);
        }
    }
    if(ll > 0) EMIT();
#undef EMIT
    return lines;
}

/* Draws text on baseline y, shortened with '.' if it would be wider than
 * max_w - so nothing can ever run out of its box. */
void ui_str_fit(Canvas* canvas, int16_t x, int16_t y, int16_t max_w, Align h, const char* text) {
    char buf[32];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    size_t len = strlen(buf);
    while(len > 1 && (int16_t)canvas_string_width(canvas, buf) > max_w) {
        len--;
        buf[len - 1] = '.';
        buf[len] = '\0';
    }
    int16_t w = (int16_t)canvas_string_width(canvas, buf);
    int16_t dx = x;
    if(h == AlignCenter) dx = x - w / 2;
    if(h == AlignRight) dx = x - w;
    canvas_draw_str(canvas, dx, y, buf);
}

/* ---------- Little pictures ---------- */

void ui_star(Canvas* canvas, int16_t x, int16_t y, bool filled) {
    static const char* const full[] = {
        "...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", ".#...#."};
    static const char* const empty[] = {
        "...#...", "..#.#..", "##...##", ".#...#.", "..#.#..", ".##.##.", ".#...#."};
    draw_rows(canvas, x, y, filled ? full : empty, 7);
}

void ui_heart(Canvas* canvas, int16_t x, int16_t y, bool filled) {
    static const char* const full[] = {
        ".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
    static const char* const empty[] = {
        ".##.##.", "#..#..#", "#.....#", ".#...#.", "..#.#..", "...#..."};
    draw_rows(canvas, x, y, filled ? full : empty, 6);
}

void ui_flame(Canvas* canvas, int16_t x, int16_t y, bool filled) {
    static const char* const full[] = {
        "...#...",
        "...##..",
        "..###..",
        ".####.#",
        ".######",
        "#######",
        "###.###",
        "##...##",
        ".#####."};
    static const char* const empty[] = {
        "...#...",
        "...##..",
        "..#.#..",
        ".#..#.#",
        ".#...##",
        "#.....#",
        "#..#..#",
        "#.#.#.#",
        ".#####."};
    draw_rows(canvas, x, y, filled ? full : empty, 9);
}

void ui_lock(Canvas* canvas, int16_t x, int16_t y) {
    static const char* const rows[] = {
        "..###..", ".#...#.", ".#...#.", "#######", "###.###", "###.###", "#######"};
    draw_rows(canvas, x, y, rows, 7);
}

static const char* const icon_rows[IconCount][9] = {
    [IconLearn] =
        {".###.###.",
         "#...#...#",
         "#.#.#.#.#",
         "#...#...#",
         "#.#.#.#.#",
         "#...#...#",
         "#...#...#",
         ".###.###.",
         "........."},
    [IconPractice] =
        {"..#####..",
         ".#.....#.",
         "#..###..#",
         "#.#...#.#",
         "#.#.#.#.#",
         "#.#...#.#",
         "#..###..#",
         ".#.....#.",
         "..#####.."},
    [IconGames] =
        {".........",
         ".#######.",
         "#..#....#",
         "#.###.#.#",
         "#..#.#..#",
         "#.......#",
         ".##...##.",
         ".........",
         "........."},
    [IconKey] =
        {"....#....",
         "...###...",
         "....#....",
         "#########",
         "....#....",
         ".#######.",
         "#.......#",
         "#########",
         "........."},
    [IconAlphabet] =
        {"..#####..",
         ".#.....#.",
         ".#.....#.",
         ".#######.",
         ".#.....#.",
         ".#.....#.",
         ".........",
         ".#..####.",
         "........."},
    [IconStats] =
        {".......##",
         ".......##",
         "....##.##",
         "....##.##",
         ".##.##.##",
         ".##.##.##",
         ".##.##.##",
         "#########",
         "........."},
    [IconSettings] =
        {"....#....",
         ".#.###.#.",
         "..#####..",
         ".##...##.",
         "###...###",
         ".##...##.",
         "..#####..",
         ".#.###.#.",
         "....#...."},
    [IconHelp] =
        {"..#####..",
         ".#.....#.",
         "#..###..#",
         "#.....#.#",
         "#....#..#",
         "#...#...#",
         "#.......#",
         ".#..#..#.",
         "..#####.."},
    [IconEar] =
        {".........",
         "...#...#.",
         "..##.#..#",
         "####..#.#",
         "####..#.#",
         "####..#.#",
         "..##.#..#",
         "...#...#.",
         "........."},
    [IconSend] =
        {"....#....",
         "...###...",
         "..#.#.#..",
         "....#....",
         "....#....",
         "#...#...#",
         "#.......#",
         "#########",
         "........."},
    [IconWords] =
        {".........",
         "#########",
         "#.......#",
         "#.#.###.#",
         "#.......#",
         "#########",
         "..##.....",
         ".#.......",
         "........."},
    [IconTarget] =
        {"....#....",
         "..#####..",
         ".#..#..#.",
         ".#.....#.",
         "###.#.###",
         ".#.....#.",
         ".#..#..#.",
         "..#####..",
         "....#...."},
    [IconRadio] =
        {"#.......#",
         ".#.....#.",
         "#.#.#.#.#",
         ".#.###.#.",
         "....#....",
         "...#.#...",
         "...#.#...",
         "..#...#..",
         "..#...#.."},
    [IconRocket] =
        {"....#....",
         "...###...",
         "...#.#...",
         "...###...",
         "..#####..",
         "..#####..",
         ".##.#.##.",
         ".#..#..#.",
         "....#...."},
    [IconBolt] =
        {".....###.",
         "....###..",
         "...###...",
         "..######.",
         "....###..",
         "...###...",
         "..##.....",
         ".#.......",
         "........."},
    [IconChain] =
        {"####.####",
         "#..#.####",
         "#..#.####",
         "####.####",
         ".........",
         "####.####",
         "####.#..#",
         "####.#..#",
         "####.####"},
};

void ui_icon(Canvas* canvas, int16_t x, int16_t y, uint8_t icon) {
    if(icon >= IconCount) return;
    draw_rows(canvas, x, y, icon_rows[icon], 9);
}

/* Speaker with sound waves. With 'active' the waves pulse outwards. */
void ui_waves(Canvas* canvas, int16_t cx, int16_t cy, uint32_t anim, bool active) {
    /* speaker body */
    canvas_draw_box(canvas, cx - 9, cy - 2, 3, 5);
    for(int16_t k = 0; k < 4; k++) {
        canvas_draw_line(canvas, cx - 6 + k, cy - 2 - k, cx - 6 + k, cy + 2 + k);
    }
    /* three arcs - while active they appear one after another */
    uint32_t phase = (anim / 4) % 4;
    for(uint8_t a = 0; a < 3; a++) {
        if(active && a >= phase) continue;
        int16_t r = 4 + a * 4;
        for(int16_t dy = -r + 2; dy <= r - 2; dy++) {
            int16_t dx = 0;
            while((dx + 1) * (dx + 1) + dy * dy <= r * r)
                dx++;
            canvas_draw_dot(canvas, cx + dx - 2, cy + dy);
        }
    }
}

void ui_button(Canvas* canvas, int16_t x, int16_t y, int16_t w, int16_t h, const char* label, bool filled) {
    canvas_set_font(canvas, FontSecondary);
    canvas_set_color(canvas, ColorBlack);
    if(filled) {
        canvas_draw_rbox(canvas, x, y, w, h, 3);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, x, y, w, h, 3);
    }
    canvas_draw_str_aligned(canvas, x + w / 2, y + h / 2 + 1, AlignCenter, AlignCenter, label);
    canvas_set_color(canvas, ColorBlack);
}

void ui_progress(Canvas* canvas, int16_t x, int16_t y, int16_t w, int16_t h, uint32_t val, uint32_t max) {
    canvas_draw_rframe(canvas, x, y, w, h, 1);
    if(max == 0) return;
    if(val > max) val = max;
    int16_t fill = (int16_t)(((uint32_t)(w - 2) * val) / max);
    if(fill > 0) canvas_draw_box(canvas, x + 1, y + 1, fill, h - 2);
}

/* ---------- Answer boxes arranged like the D-pad ---------- */

typedef struct {
    int16_t x, y, w, h;
} Rect;

/* Wide enough for 6-letter words (35 px) and callsigns in FontKeyboard. */
static const Rect choice_rects[4] = {
    {34, 5, 60, 13}, /* up */
    {84, 26, 44, 13}, /* right */
    {34, 49, 60, 13}, /* down */
    {0, 26, 44, 13}, /* left */
};

void ui_choice_box(Canvas* canvas, uint8_t slot, const char* label, bool filled, bool cross, Font font) {
    const Rect* r = &choice_rects[slot & 3];
    canvas_set_color(canvas, ColorBlack);
    if(filled) {
        canvas_draw_rbox(canvas, r->x, r->y, r->w, r->h, 3);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, r->x, r->y, r->w, r->h, 3);
    }
    canvas_set_font(canvas, font);
    ui_str_fit(canvas, r->x + r->w / 2, r->y + 10, r->w - 6, AlignCenter, label);
    if(cross) {
        canvas_draw_line(canvas, r->x + 3, r->y + 2, r->x + r->w - 4, r->y + r->h - 3);
        canvas_draw_line(canvas, r->x + r->w - 4, r->y + 2, r->x + 3, r->y + r->h - 3);
    }
    canvas_set_color(canvas, ColorBlack);
}

/* ---------- Overlays ---------- */

#define TOAST_MS 2400
#define TOAST_SLIDE 160

void ui_toast(Canvas* canvas, App* app) {
    if(app->toast_count == 0) return;
    Toast* t = &app->toasts[0];
    uint32_t age = app->now - app->toast_start;
    int16_t y = 1;
    if(age < TOAST_SLIDE) {
        y = -22 + (int16_t)(23 * age / TOAST_SLIDE);
    } else if(age > TOAST_MS - TOAST_SLIDE) {
        uint32_t out = age - (TOAST_MS - TOAST_SLIDE);
        if(out > TOAST_SLIDE) out = TOAST_SLIDE;
        y = 1 - (int16_t)(23 * out / TOAST_SLIDE);
    }
    /* full width so nothing of the screen below peeks out at the sides */
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, y - 1, 128, 23);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 0, y, 128, 21, 4);
    canvas_draw_rframe(canvas, 1, y + 1, 126, 19, 3);

    /* icon in an inverted circle */
    canvas_draw_disc(canvas, 13, y + 10, 7);
    canvas_set_color(canvas, ColorWhite);
    if(t->icon == 2) {
        ui_flame(canvas, 10, y + 6, true);
    } else if(t->icon == 1) {
        ui_arrow(canvas, 13, y + 9, 0, 5);
        canvas_draw_box(canvas, 12, y + 9, 3, 4);
    } else {
        ui_star(canvas, 10, y + 7, true);
    }
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    ui_str_fit(canvas, 25, y + 10, 99, AlignLeft, t->title);
    canvas_set_font(canvas, FontSecondary);
    ui_str_fit(canvas, 25, y + 18, 99, AlignLeft, t->text);
}

void ui_yes_no(Canvas* canvas, const char* question, bool yes) {
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_rbox(canvas, 10, 16, 108, 34, 4);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 10, 16, 108, 34, 4);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignCenter, question);
    ui_button(canvas, 20, 34, 38, 12, "Yes", yes);
    ui_button(canvas, 70, 34, 38, 12, "No", !yes);
}

/* Same star as ui_star, drawn at double size (14x14). */
void ui_star_big(Canvas* canvas, int16_t x, int16_t y, bool filled) {
    static const char* const full[] = {
        "...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", ".#...#."};
    static const char* const empty[] = {
        "...#...", "..#.#..", "##...##", ".#...#.", "..#.#..", ".##.##.", ".#...#."};
    const char* const* rows = filled ? full : empty;
    for(uint8_t r = 0; r < 7; r++) {
        for(uint8_t c = 0; c < 7; c++) {
            if(rows[r][c] == '#') canvas_draw_box(canvas, x + c * 2, y + r * 2, 2, 2);
        }
    }
}

/* Thin progress bar along the very top edge: solid part = done, dotted =
 * still to go. Leaves y >= 4 free for the screen below it. */
void ui_top_progress(Canvas* canvas, uint32_t val, uint32_t max) {
    int16_t fill = max ? (int16_t)(128U * (val > max ? max : val) / max) : 0;
    if(fill > 0) canvas_draw_box(canvas, 0, 0, fill, 3);
    for(int16_t x = fill + 1; x < 128; x += 2) {
        canvas_draw_dot(canvas, x, 1);
    }
}

/* ".-." -> "di-dah-dit": how the sign sounds, the way it is taught. */
void ui_phonetic(const char* code, char* out, size_t out_size) {
    out[0] = '\0';
    size_t len = strlen(code);
    for(size_t i = 0; i < len; i++) {
        const char* part;
        if(code[i] == '-') {
            part = "dah";
        } else {
            part = (i + 1 == len) ? "dit" : "di";
        }
        size_t used = strlen(out);
        snprintf(out + used, out_size - used, "%s%s", i ? "-" : "", part);
    }
}
