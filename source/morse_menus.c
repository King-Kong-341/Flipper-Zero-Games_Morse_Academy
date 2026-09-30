#include "morse.h"

/* Menus, lesson path, settings, alphabet, stats, help and the first-start
 * onboarding (welcome -> experience -> interactive key tutorial). */

static bool is_nav(InputEvent* ev) {
    return ev->type == InputTypeShort || ev->type == InputTypeRepeat;
}

/* Moves a list selection and keeps it inside the visible window. */
static void list_move(uint8_t* sel, uint8_t* scroll, uint8_t count, uint8_t visible, int8_t dir) {
    if(dir < 0 && *sel > 0) (*sel)--;
    if(dir > 0 && *sel + 1 < count) (*sel)++;
    if(*sel < *scroll) *scroll = *sel;
    if(*sel >= *scroll + visible) *scroll = (uint8_t)(*sel - visible + 1);
}

static void draw_scrollbar(Canvas* canvas, int16_t x, int16_t y, int16_t h, uint8_t pos, uint8_t count) {
    if(count <= 1) return;
    for(int16_t yy = y; yy < y + h; yy += 2)
        canvas_draw_dot(canvas, x + 1, yy);
    int16_t thumb = h / count;
    if(thumb < 4) thumb = 4;
    int16_t ty = y + (int16_t)((h - thumb) * pos / (count - 1));
    canvas_draw_box(canvas, x, ty, 3, thumb);
}

/* A menu row: icon, bold label, optional right-hand info. */
static void draw_row(
    Canvas* canvas,
    int16_t y,
    int16_t w,
    uint8_t icon,
    const char* label,
    const char* info,
    bool selected,
    bool locked) {
    canvas_set_color(canvas, ColorBlack);
    if(selected) {
        canvas_draw_rbox(canvas, 1, y, w, 12, 3);
        canvas_set_color(canvas, ColorWhite);
    }
    ui_icon(canvas, 4, y + 2, icon);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 17, y + 10, label);
    canvas_set_font(canvas, FontSecondary);
    if(locked) {
        ui_lock(canvas, w - 9, y + 3);
    } else if(info && info[0]) {
        canvas_draw_str_aligned(canvas, w - 3, y + 10, AlignRight, AlignBottom, info);
    }
    canvas_set_color(canvas, ColorBlack);
}

/* ---------- Main menu ---------- */

#define MENU_COUNT 8
#define MENU_VISIBLE 4

static const char* const menu_labels[MENU_COUNT] =
    {"Learn", "Practice", "Games", "Free Key", "Alphabet", "Stats", "Settings", "Help"};
static const uint8_t menu_icons[MENU_COUNT] = {
    IconLearn,
    IconPractice,
    IconGames,
    IconKey,
    IconAlphabet,
    IconStats,
    IconSettings,
    IconHelp};

void menu_input(App* app, InputEvent* ev) {
    if(!is_nav(ev)) return;
    if(ev->key == InputKeyUp) {
        list_move(&app->menu_sel, &app->menu_scroll, MENU_COUNT, MENU_VISIBLE, -1);
    } else if(ev->key == InputKeyDown) {
        list_move(&app->menu_sel, &app->menu_scroll, MENU_COUNT, MENU_VISIBLE, 1);
    } else if(ev->key == InputKeyBack && ev->type == InputTypeShort) {
        app->running = false;
    } else if(ev->key == InputKeyOk && ev->type == InputTypeShort) {
        switch(app->menu_sel) {
        case 0:
            app->course_sel = app->prog.unlocked;
            go_screen(app, ScrCourse);
            break;
        case 1:
            go_screen(app, ScrPracticeMenu);
            break;
        case 2:
            go_screen(app, ScrGamesMenu);
            break;
        case 3:
            freekey_start(app);
            break;
        case 4:
            go_screen(app, ScrAlphabet);
            break;
        case 5:
            app->stats_page = 0;
            go_screen(app, ScrStats);
            break;
        case 6:
            app->set_sel = 0;
            app->set_scroll = 0;
            go_screen(app, ScrSettings);
            break;
        default:
            app->help_page = 0;
            app->help_scroll = 0;
            go_screen(app, ScrHelp);
            break;
        }
    }
}

void menu_draw(Canvas* canvas, App* app) {
    char buf[16];
    /* header: title (96 px wide in FontPrimary) and the streak flame */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 3, 10, "MORSE ACADEMY");

    uint16_t goal = daily_goals[app->set.daily_goal & 3];
    bool goal_met = app->prog.xp_today >= goal;
    snprintf(buf, sizeof(buf), "%u", app->prog.streak);
    canvas_set_font(canvas, FontSecondary);
    uint16_t nw = canvas_string_width(canvas, buf);
    canvas_draw_str_aligned(canvas, 127, 10, AlignRight, AlignBottom, buf);
    ui_flame(canvas, 127 - (int16_t)nw - 9, 2, goal_met);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    for(uint8_t i = 0; i < MENU_VISIBLE; i++) {
        uint8_t idx = app->menu_scroll + i;
        if(idx >= MENU_COUNT) break;
        const char* info = "";
        if(idx == 0) {
            if(app->prog.stars[FINAL_LESSON]) {
                info = "done";
            } else {
                snprintf(buf, sizeof(buf), "L%u", app->prog.unlocked + 1);
                info = buf;
            }
        } else if(idx == 5) {
            snprintf(buf, sizeof(buf), "Lv%u", current_level(app));
            info = buf;
        }
        draw_row(canvas, 15 + i * 12, 120, menu_icons[idx], menu_labels[idx], info, app->menu_sel == idx, false);
    }
    draw_scrollbar(canvas, 124, 15, 48, app->menu_sel, MENU_COUNT);
}

/* ---------- Practice menu ---------- */

static const char* const prac_labels[PracCount] =
    {"Listen", "Send", "Hear words", "Send words", "Weak spots", "Callsigns"};
static const char* const prac_desc[PracCount] = {
    "Hear a sign, pick it",
    "See a sign, key it",
    "Hear a word, pick it",
    "Key whole words",
    "Train your weakest signs",
    "Copy radio callsigns",
};
static const uint8_t prac_icons[PracCount] =
    {IconEar, IconKey, IconWords, IconSend, IconTarget, IconRadio};

static void draw_sub_header(Canvas* canvas, const char* title) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 7, AlignCenter, AlignCenter, title);
    canvas_draw_line(canvas, 0, 13, 127, 13);
}

void practice_menu_input(App* app, InputEvent* ev) {
    if(!is_nav(ev)) return;
    if(ev->key == InputKeyUp) {
        list_move(&app->prac_sel, &app->prac_scroll, PracCount, 3, -1);
    } else if(ev->key == InputKeyDown) {
        list_move(&app->prac_sel, &app->prac_scroll, PracCount, 3, 1);
    } else if(ev->type != InputTypeShort) {
        return;
    } else if(ev->key == InputKeyBack || ev->key == InputKeyLeft) {
        go_screen(app, ScrMenu);
    } else if(ev->key == InputKeyOk) {
        if(practice_mode_available(app, (PracticeMode)app->prac_sel)) {
            quiz_start_practice(app, (PracticeMode)app->prac_sel);
        } else {
            fx_wrong(app);
        }
    }
}

void practice_menu_draw(Canvas* canvas, App* app) {
    draw_sub_header(canvas, "PRACTICE");
    for(uint8_t i = 0; i < 3; i++) {
        uint8_t idx = app->prac_scroll + i;
        if(idx >= PracCount) break;
        bool locked = !practice_mode_available(app, (PracticeMode)idx);
        draw_row(canvas, 15 + i * 12, 120, prac_icons[idx], prac_labels[idx], "", app->prac_sel == idx, locked);
    }
    draw_scrollbar(canvas, 124, 15, 36, app->prac_sel, PracCount);
    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_set_font(canvas, FontSecondary);
    const char* desc = prac_desc[app->prac_sel];
    if(!practice_mode_available(app, (PracticeMode)app->prac_sel)) {
        desc = app->prac_sel == PracCalls ? "Unlocks after lesson 16" : "Unlocks after lesson 3";
    }
    canvas_draw_str_aligned(canvas, 64, 61, AlignCenter, AlignBottom, desc);
}

/* ---------- Games menu ---------- */

static const char* const game_labels[GameCount] = {"Morse Rush", "Sound Sprint", "Echo Chain"};
static const char* const game_desc[GameCount] = {
    "Key falling letters!",
    "60 s of fast listening",
    "Repeat longer chains",
};
static const uint8_t game_icons[GameCount] = {IconRocket, IconBolt, IconChain};

void games_menu_input(App* app, InputEvent* ev) {
    if(!is_nav(ev)) return;
    if(ev->key == InputKeyUp) {
        if(app->games_sel > 0) app->games_sel--;
    } else if(ev->key == InputKeyDown) {
        if(app->games_sel + 1 < GameCount) app->games_sel++;
    } else if(ev->type != InputTypeShort) {
        return;
    } else if(ev->key == InputKeyBack || ev->key == InputKeyLeft) {
        go_screen(app, ScrMenu);
    } else if(ev->key == InputKeyOk) {
        game_start(app, (GameKind)app->games_sel);
    }
}

void games_menu_draw(Canvas* canvas, App* app) {
    char buf[16];
    draw_sub_header(canvas, "GAMES");
    for(uint8_t i = 0; i < GameCount; i++) {
        uint16_t best = game_best(app, (GameKind)i);
        buf[0] = '\0';
        if(best) snprintf(buf, sizeof(buf), "%u", best);
        draw_row(canvas, 15 + i * 12, 126, game_icons[i], game_labels[i], buf, app->games_sel == i, false);
    }
    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 61, AlignCenter, AlignBottom, game_desc[app->games_sel]);
}

/* ---------- Lesson path ---------- */

static int16_t node_x(uint8_t i) {
    static const int16_t xs[4] = {14, 28, 42, 28};
    return xs[i % 4];
}

void course_input(App* app, InputEvent* ev) {
    if(!is_nav(ev)) return;
    if(ev->key == InputKeyUp) {
        if(app->course_sel > 0) app->course_sel--;
    } else if(ev->key == InputKeyDown) {
        if(app->course_sel + 1 < LESSON_COUNT) app->course_sel++;
    } else if(ev->type != InputTypeShort) {
        return;
    } else if(ev->key == InputKeyBack || ev->key == InputKeyLeft) {
        go_screen(app, ScrMenu);
    } else if(ev->key == InputKeyOk) {
        if(app->course_sel <= app->prog.unlocked) {
            quiz_start_lesson(app, app->course_sel);
        } else {
            fx_wrong(app);
        }
    }
}

void course_draw(Canvas* canvas, App* app) {
    uint8_t sel = app->course_sel;
    char buf[16];

    /* the path: selected node in the middle, neighbours above and below */
    for(int8_t d = -3; d <= 3; d++) {
        int16_t i = (int16_t)sel + d;
        if(i < 0 || i >= LESSON_COUNT) continue;
        int16_t cx = node_x((uint8_t)i);
        int16_t cy = 32 + d * 19;
        /* connector to the next node */
        if(i + 1 < LESSON_COUNT) {
            int16_t nx = node_x((uint8_t)(i + 1));
            int16_t ny = cy + 19;
            bool open = i + 1 <= app->prog.unlocked;
            for(int16_t k = 1; k < 10; k++) {
                if(!open && k % 2) continue;
                int16_t px = cx + (nx - cx) * k / 10;
                int16_t py = cy + 7 + (ny - cy - 14) * k / 10;
                canvas_draw_dot(canvas, px, py);
            }
        }
        bool done = app->prog.stars[i] > 0;
        bool open = i <= app->prog.unlocked;
        canvas_set_color(canvas, ColorBlack);
        if(done) {
            canvas_draw_disc(canvas, cx, cy, 7);
            canvas_set_color(canvas, ColorWhite);
        } else if(open) {
            canvas_draw_circle(canvas, cx, cy, 7);
            canvas_draw_circle(canvas, cx, cy, 6);
        } else {
            for(uint8_t a = 0; a < 16; a++) {
                static const int8_t dx[16] = {7, 6, 5, 3, 0, -3, -5, -6, -7, -6, -5, -3, 0, 3, 5, 6};
                static const int8_t dy[16] = {0, 3, 5, 6, 7, 6, 5, 3, 0, -3, -5, -6, -7, -6, -5, -3};
                canvas_draw_dot(canvas, cx + dx[a], cy + dy[a]);
            }
        }
        if(i == FINAL_LESSON) {
            ui_star(canvas, cx - 3, cy - 3, true);
        } else if(!open) {
            ui_lock(canvas, cx - 3, cy - 3);
        } else {
            snprintf(buf, sizeof(buf), "%d", i + 1);
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str_aligned(canvas, cx, cy + 1, AlignCenter, AlignCenter, buf);
        }
        canvas_set_color(canvas, ColorBlack);
        if(d == 0 && (app->anim / 8) % 2 == 0) {
            canvas_draw_circle(canvas, cx, cy, 9);
        }
    }

    /* info panel on the right */
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 56, 0, 72, 64);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 58, 1, 69, 62, 4);

    canvas_set_font(canvas, FontSecondary);
    if(sel == FINAL_LESSON) {
        canvas_draw_str_aligned(canvas, 92, 10, AlignCenter, AlignBottom, "FINAL EXAM");
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 92, 23, AlignCenter, AlignCenter, "All signs");
    } else {
        snprintf(buf, sizeof(buf), "LESSON %u", sel + 1);
        canvas_draw_str_aligned(canvas, 92, 10, AlignCenter, AlignBottom, buf);
        const char* chars = lessons[sel].chars;
        uint8_t n = (uint8_t)strlen(chars);
        int16_t w = n * 10 + (n - 1) * 4;
        int16_t x = 92 - w / 2;
        for(uint8_t k = 0; k < n; k++) {
            ui_big_char(canvas, x + k * 14, 14, chars[k], 2);
        }
    }

    for(uint8_t s = 0; s < 3; s++) {
        ui_star(canvas, 79 + s * 9, 33, s < app->prog.stars[sel]);
    }

    if(sel <= app->prog.unlocked) {
        const char* label = app->prog.stars[sel] ? "OK Again" : "OK Start";
        ui_button(canvas, 64, 45, 57, 13, label, (app->anim / 12) % 2 == 0);
    } else {
        ui_lock(canvas, 67, 48);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 78, 55, "Locked");
    }
}

/* ---------- Settings ---------- */

typedef enum {
    SetSound,
    SetVolume,
    SetTone,
    SetVibro,
    SetLed,
    SetSpeed,
    SetSpacing,
    SetKeyer,
    SetGap,
    SetHints,
    SetGoal,
    SetScreen,
    SetTestSpeed,
    SetPlacement,
    SetReset,
    SetCount,
} SettingItem;

#define SET_VISIBLE 3

static const char* const set_labels[SetCount] = {
    "Sound",
    "Volume",
    "Tone",
    "Vibration",
    "LED",
    "Speed",
    "Spacing",
    "Keyer",
    "Letter pause",
    "Hints",
    "Daily goal",
    "Screen on",
    "Test speed",
    "Placement test",
    "Reset progress",
};
static const char* const set_desc[SetCount] = {
    "Beeps for dits and dahs",
    "How loud the tone is",
    "Pitch of the beep",
    "Buzz when Flipper sends",
    "Colors show what happens",
    "Speed of each sign (WPM)",
    "Extra pause between signs",
    "Paddle: < > / Straight: OK",
    "Pause that ends a letter",
    "Down key shows the pattern",
    "XP to earn every day",
    "Keep the backlight on",
    "OK plays a test word",
    "OK: find your level again",
    "OK: delete all progress",
};

static void change_setting(App* app, int8_t dir) {
    Settings* s = &app->set;
    switch(app->set_sel) {
    case SetSound:
        s->sound = !s->sound;
        break;
    case SetVolume: {
        int v = s->volume + dir * 10;
        s->volume = (uint8_t)(v < 0 ? 0 : (v > 100 ? 100 : v));
        fx_click(app);
        break;
    }
    case SetTone: {
        int t = s->tone_hz + dir * 50;
        s->tone_hz = (uint16_t)(t < 400 ? 400 : (t > 1000 ? 1000 : t));
        player_start_fast(app, "E", s->wpm);
        break;
    }
    case SetVibro:
        s->vibro = !s->vibro;
        break;
    case SetLed:
        s->led = !s->led;
        break;
    case SetSpeed: {
        int w = s->wpm + dir;
        s->wpm = (uint8_t)(w < 5 ? 5 : (w > 40 ? 40 : w));
        if(s->eff_wpm > s->wpm) s->eff_wpm = s->wpm;
        break;
    }
    case SetSpacing: {
        int e = s->eff_wpm + dir;
        s->eff_wpm = (uint8_t)(e < 3 ? 3 : (e > s->wpm ? s->wpm : e));
        break;
    }
    case SetKeyer:
        s->key_mode = s->key_mode == KeyModePaddle ? KeyModeStraight : KeyModePaddle;
        break;
    case SetGap: {
        int g = s->letter_gap + dir;
        s->letter_gap = (uint8_t)(g < 0 ? 0 : (g > 2 ? 2 : g));
        break;
    }
    case SetHints:
        s->hints = !s->hints;
        break;
    case SetGoal: {
        int g = s->daily_goal + dir;
        s->daily_goal = (uint8_t)(g < 0 ? 0 : (g > 3 ? 3 : g));
        break;
    }
    case SetScreen:
        s->backlight = !s->backlight;
        apply_backlight(app);
        break;
    default:
        return;
    }
    settings_save(app);
}

void settings_input(App* app, InputEvent* ev) {
    if(!is_nav(ev)) return;
    if(ev->key == InputKeyUp) {
        list_move(&app->set_sel, &app->set_scroll, SetCount, SET_VISIBLE, -1);
    } else if(ev->key == InputKeyDown) {
        list_move(&app->set_sel, &app->set_scroll, SetCount, SET_VISIBLE, 1);
    } else if(ev->key == InputKeyLeft) {
        change_setting(app, -1);
    } else if(ev->key == InputKeyRight) {
        change_setting(app, 1);
    } else if(ev->type != InputTypeShort) {
        return;
    } else if(ev->key == InputKeyBack) {
        settings_save(app);
        go_screen(app, ScrMenu);
    } else if(ev->key == InputKeyOk) {
        switch(app->set_sel) {
        case SetTestSpeed:
            player_start(app, "PARIS");
            break;
        case SetPlacement:
            app->confirm_action = ConfirmPlacement;
            app->confirm_yes = false;
            app->confirm_return = ScrSettings;
            go_screen(app, ScrConfirm);
            break;
        case SetReset:
            app->confirm_action = ConfirmReset;
            app->confirm_yes = false;
            app->confirm_return = ScrSettings;
            go_screen(app, ScrConfirm);
            break;
        case SetVolume:
        case SetTone:
        case SetSpeed:
        case SetSpacing:
        case SetGap:
        case SetGoal:
            change_setting(app, 1);
            break;
        default:
            change_setting(app, 1);
            break;
        }
    }
}

static void setting_value(App* app, uint8_t item, char* buf, size_t size) {
    static const char* const gaps[3] = {"Short", "Normal", "Long"};
    Settings* s = &app->set;
    buf[0] = '\0';
    switch(item) {
    case SetSound:
        snprintf(buf, size, "%s", s->sound ? "ON" : "OFF");
        break;
    case SetTone:
        snprintf(buf, size, "%u Hz", s->tone_hz);
        break;
    case SetVibro:
        snprintf(buf, size, "%s", s->vibro ? "ON" : "OFF");
        break;
    case SetLed:
        snprintf(buf, size, "%s", s->led ? "ON" : "OFF");
        break;
    case SetSpeed:
        snprintf(buf, size, "%u WPM", s->wpm);
        break;
    case SetSpacing:
        snprintf(buf, size, "%u WPM", s->eff_wpm);
        break;
    case SetKeyer:
        snprintf(buf, size, "%s", s->key_mode == KeyModePaddle ? "Paddle" : "Straight");
        break;
    case SetGap:
        snprintf(buf, size, "%s", gaps[s->letter_gap % 3]);
        break;
    case SetHints:
        snprintf(buf, size, "%s", s->hints ? "ON" : "OFF");
        break;
    case SetGoal:
        snprintf(buf, size, "%u XP", daily_goals[s->daily_goal & 3]);
        break;
    case SetScreen:
        snprintf(buf, size, "%s", s->backlight ? "ON" : "OFF");
        break;
    case SetTestSpeed:
    case SetPlacement:
    case SetReset:
        snprintf(buf, size, "OK");
        break;
    default:
        break;
    }
}

void settings_draw(Canvas* canvas, App* app) {
    ui_frame(canvas, "SETTINGS");
    char buf[16];
    for(uint8_t i = 0; i < SET_VISIBLE; i++) {
        uint8_t idx = app->set_scroll + i;
        if(idx >= SetCount) break;
        int16_t y = 18 + i * 11;
        bool selected = app->set_sel == idx;
        canvas_set_color(canvas, ColorBlack);
        if(selected) {
            canvas_draw_rbox(canvas, 4, y, 114, 11, 3);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 8, y + 8, set_labels[idx]);

        if(idx == SetVolume) {
            /* 10 small bars, growing towards the loud end */
            for(uint8_t b = 0; b < 10; b++) {
                int16_t bh = 3 + b / 2;
                int16_t bx = 70 + b * 4;
                int16_t by = y + 9 - bh;
                if(b < app->set.volume / 10) {
                    canvas_draw_box(canvas, bx, by, 3, bh);
                } else {
                    canvas_draw_dot(canvas, bx + 1, y + 8);
                }
            }
        } else {
            setting_value(app, idx, buf, sizeof(buf));
            bool adjustable = idx != SetTestSpeed && idx != SetPlacement && idx != SetReset;
            if(selected && adjustable) {
                canvas_draw_str_aligned(canvas, 114, y + 8, AlignRight, AlignBottom, ">");
                uint16_t w = canvas_string_width(canvas, buf);
                canvas_draw_str_aligned(canvas, 108, y + 8, AlignRight, AlignBottom, buf);
                canvas_draw_str_aligned(canvas, 104 - (int16_t)w, y + 8, AlignRight, AlignBottom, "<");
            } else {
                canvas_draw_str_aligned(canvas, 114, y + 8, AlignRight, AlignBottom, buf);
            }
        }
        canvas_set_color(canvas, ColorBlack);
    }
    draw_scrollbar(canvas, 121, 18, 33, app->set_sel, SetCount);
    canvas_draw_line(canvas, 4, 52, 123, 52);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 61, AlignCenter, AlignBottom, set_desc[app->set_sel]);
}

/* ---------- Confirm dialog ---------- */

void confirm_input(App* app, InputEvent* ev) {
    if(ev->type != InputTypeShort) return;
    if(ev->key == InputKeyLeft || ev->key == InputKeyRight) {
        app->confirm_yes = !app->confirm_yes;
    } else if(ev->key == InputKeyBack) {
        go_screen(app, app->confirm_return);
    } else if(ev->key == InputKeyOk) {
        if(!app->confirm_yes) {
            go_screen(app, app->confirm_return);
            return;
        }
        if(app->confirm_action == ConfirmReset) {
            progress_defaults(&app->prog);
            app->prog.onboarded = 1;
            progress_save(app);
            app->course_sel = 0;
            fx_fail(app);
            go_screen(app, ScrMenu);
        } else {
            quiz_start_placement(app);
        }
    }
}

void confirm_draw(Canvas* canvas, App* app) {
    bool reset = app->confirm_action == ConfirmReset;
    ui_frame(canvas, reset ? "RESET PROGRESS?" : "PLACEMENT TEST?");
    canvas_set_font(canvas, FontSecondary);
    ui_wrap(
        canvas,
        6,
        25,
        116,
        9,
        reset ? "Deletes lessons, XP, stars, badges and stats. Settings stay." :
                "A short listening test. Lessons you already know get unlocked.",
        0,
        3);
    ui_button(canvas, 18, 49, 40, 12, "Yes", app->confirm_yes);
    ui_button(canvas, 70, 49, 40, 12, "No", !app->confirm_yes);
}

/* ---------- Alphabet reference ---------- */

static char similar_sign(char c) {
    const char* code = morse_code(c);
    uint8_t best = 0;
    char best_c = 0;
    for(uint8_t i = 0; i < CHAR_COUNT; i++) {
        char o = morse_table[i].ch;
        if(o == c) continue;
        const char* oc = morse_table[i].code;
        uint8_t s = strlen(oc) == strlen(code) ? 3 : 0;
        for(size_t k = 0; code[k] && oc[k]; k++) {
            if(code[k] == oc[k]) s++;
        }
        if(s > best) {
            best = s;
            best_c = o;
        }
    }
    return best_c;
}

void alphabet_input(App* app, InputEvent* ev) {
    if(!is_nav(ev)) return;
    if(ev->key == InputKeyLeft) {
        app->alpha_sel = app->alpha_sel == 0 ? CHAR_COUNT - 1 : app->alpha_sel - 1;
    } else if(ev->key == InputKeyRight) {
        app->alpha_sel = (uint8_t)((app->alpha_sel + 1) % CHAR_COUNT);
    } else if(ev->key == InputKeyDown) {
        /* jump to the next group: letters -> numbers -> signs */
        app->alpha_sel = app->alpha_sel < 26 ? 26 : (app->alpha_sel < 36 ? 36 : 0);
    } else if(ev->key == InputKeyUp) {
        app->alpha_sel = app->alpha_sel >= 36 ? 26 : (app->alpha_sel >= 26 ? 0 : 36);
    } else if(ev->type != InputTypeShort) {
        return;
    } else if(ev->key == InputKeyOk) {
        char t[2] = {morse_table[app->alpha_sel].ch, '\0'};
        player_start(app, t);
    } else if(ev->key == InputKeyBack) {
        player_stop(app);
        go_screen(app, ScrMenu);
    }
}

void alphabet_draw(Canvas* canvas, App* app) {
    char c = morse_table[app->alpha_sel].ch;
    const char* code = morse_table[app->alpha_sel].code;
    char buf[32];

    canvas_draw_rframe(canvas, 1, 1, 37, 49, 4);
    ui_big_char(canvas, 7, 8, c, 5);

    canvas_set_font(canvas, FontSecondary);
    const char* kind = (c >= 'A' && c <= 'Z') ? "LETTER" : ((c >= '0' && c <= '9') ? "NUMBER" : "SIGN");
    canvas_draw_str(canvas, 43, 9, kind);
    snprintf(buf, sizeof(buf), "%u/%u", app->alpha_sel + 1, CHAR_COUNT);
    canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, buf);

    bool playing = player_busy(app);
    int8_t lit = playing ? player_current_element(app) : 127;
    ui_pattern(canvas, 84, 20, code, true, lit, playing);

    /* how it sounds - split into two lines if it is too wide */
    ui_phonetic(code, buf, sizeof(buf));
    if(canvas_string_width(canvas, buf) <= 82) {
        canvas_draw_str_aligned(canvas, 84, 33, AlignCenter, AlignBottom, buf);
    } else {
        size_t len = strlen(buf);
        size_t cut = len / 2;
        while(cut < len && buf[cut] != '-')
            cut++;
        buf[cut] = '\0';
        canvas_draw_str_aligned(canvas, 84, 31, AlignCenter, AlignBottom, buf);
        canvas_draw_str_aligned(canvas, 84, 40, AlignCenter, AlignBottom, buf + cut + 1);
    }

    int idx = app->alpha_sel;
    char sim = similar_sign(c);
    if(sim) {
        snprintf(buf, sizeof(buf), "Like: %c", sim);
        canvas_draw_str(canvas, 43, 49, buf);
    }
    if(char_learned(app, c)) {
        snprintf(buf, sizeof(buf), "%u%%", app->prog.mastery[idx]);
        canvas_draw_str_aligned(canvas, 126, 49, AlignRight, AlignBottom, buf);
        ui_progress(canvas, 74, 44, 26, 5, app->prog.mastery[idx], 100);
    } else {
        canvas_draw_str_aligned(canvas, 126, 49, AlignRight, AlignBottom, "new");
    }

    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str(canvas, 2, 61, "OK: play");
    canvas_draw_str_aligned(canvas, 126, 61, AlignRight, AlignBottom, "< browse >");
}

/* ---------- Stats ---------- */

#define STATS_PAGES 4

void stats_input(App* app, InputEvent* ev) {
    if(!is_nav(ev)) return;
    if(ev->key == InputKeyLeft) {
        app->stats_page = app->stats_page == 0 ? STATS_PAGES - 1 : app->stats_page - 1;
    } else if(ev->key == InputKeyRight) {
        app->stats_page = (uint8_t)((app->stats_page + 1) % STATS_PAGES);
    } else if(ev->key == InputKeyDown && app->stats_page == 2) {
        app->badge_sel = (uint8_t)((app->badge_sel + 1) % BADGE_COUNT);
    } else if(ev->key == InputKeyUp && app->stats_page == 2) {
        app->badge_sel = app->badge_sel == 0 ? BADGE_COUNT - 1 : app->badge_sel - 1;
    } else if(ev->key == InputKeyBack && ev->type == InputTypeShort) {
        go_screen(app, ScrMenu);
    }
}

static void stats_header(Canvas* canvas, const char* title) {
    ui_frame(canvas, title);
    ui_arrow(canvas, 6, 8, 3, 3);
    ui_arrow(canvas, 121, 8, 1, 3);
}

static void draw_stats_overview(Canvas* canvas, App* app) {
    Progress* p = &app->prog;
    char buf[32];
    stats_header(canvas, "OVERVIEW");
    uint8_t lvl = current_level(app);
    canvas_set_font(canvas, FontPrimary);
    snprintf(buf, sizeof(buf), "Lv %u %s", lvl, level_title(lvl));
    canvas_draw_str(canvas, 6, 26, buf);
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%lu XP", (unsigned long)p->xp);
    canvas_draw_str_aligned(canvas, 122, 26, AlignRight, AlignBottom, buf);
    uint32_t lo = level_floor_xp(lvl);
    uint32_t hi = level_floor_xp(lvl + 1);
    ui_progress(canvas, 6, 29, 116, 5, p->xp - lo, hi - lo);

    ui_flame(canvas, 6, 37, p->streak > 0);
    snprintf(buf, sizeof(buf), "Streak %u  (best %u)", p->streak, p->best_streak);
    canvas_draw_str(canvas, 16, 45, buf);

    uint16_t goal = daily_goals[app->set.daily_goal & 3];
    uint16_t today = p->xp_day == today_number() ? p->xp_today : 0;
    snprintf(buf, sizeof(buf), "Today %u/%u XP", today, goal);
    canvas_draw_str(canvas, 6, 53, buf);
    ui_progress(canvas, 84, 48, 38, 5, today, goal);

    uint8_t done = 0;
    for(uint8_t l = 0; l < LESSON_COUNT; l++)
        if(p->stars[l]) done++;
    uint32_t acc = p->total_answers ? p->total_correct * 100 / p->total_answers : 0;
    snprintf(buf, sizeof(buf), "Lessons %u/%u   %lu%% right", done, LESSON_COUNT, (unsigned long)acc);
    canvas_draw_str(canvas, 6, 61, buf);
}

static void draw_stats_signs(Canvas* canvas, App* app) {
    stats_header(canvas, "SIGNS");
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < CHAR_COUNT; i++) {
        int16_t x = 4 + (i % 11) * 11;
        int16_t y = 18 + (i / 11) * 11;
        char c = morse_table[i].ch;
        char t[2] = {c, '\0'};
        uint8_t m = app->prog.mastery[i];
        bool learned = char_learned(app, c);
        canvas_set_color(canvas, ColorBlack);
        if(learned && m >= 80) {
            canvas_draw_rbox(canvas, x, y, 10, 10, 2);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str_aligned(canvas, x + 5, y + 7, AlignCenter, AlignBottom, t);
        } else {
            canvas_draw_str_aligned(canvas, x + 5, y + 7, AlignCenter, AlignBottom, t);
            if(learned) {
                int16_t bw = (int16_t)(m * 9 / 100);
                if(bw < 1) bw = 1;
                canvas_draw_line(canvas, x + 1, y + 9, x + bw, y + 9);
            } else {
                canvas_draw_dot(canvas, x + 5, y + 9);
            }
        }
        canvas_set_color(canvas, ColorBlack);
    }
}

/* One row with all 12 medals, the selected one explained below it. */
static void draw_medal(Canvas* canvas, int16_t cx, int16_t cy, uint8_t r, bool got) {
    if(got) {
        canvas_draw_disc(canvas, cx, cy, r);
    } else {
        for(uint8_t a = 0; a < 16; a++) {
            static const int8_t dx[16] = {8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3, 0, 3, 6, 7};
            static const int8_t dy[16] = {0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3};
            canvas_draw_dot(canvas, cx + dx[a] * r / 8, cy + dy[a] * r / 8);
        }
    }
}

static void draw_stats_badges(Canvas* canvas, App* app) {
    char buf[24];
    stats_header(canvas, "BADGES");
    uint8_t count = 0;
    for(uint8_t i = 0; i < BADGE_COUNT; i++) {
        int16_t cx = 9 + i * 10;
        bool got = app->prog.badges & (1UL << i);
        if(got) count++;
        draw_medal(canvas, cx, 22, 3, got);
        if(i == app->badge_sel) ui_arrow(canvas, cx, 30, 0, 3);
    }
    canvas_draw_line(canvas, 4, 33, 123, 33);

    bool got = app->prog.badges & (1UL << app->badge_sel);
    draw_medal(canvas, 15, 46, 9, got);
    if(got) {
        canvas_set_color(canvas, ColorWhite);
        ui_star(canvas, 12, 43, true);
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 15, 47, AlignCenter, AlignCenter, "?");
    }
    canvas_set_font(canvas, FontPrimary);
    ui_str_fit(canvas, 29, 44, 96, AlignLeft, badges[app->badge_sel].name);
    canvas_set_font(canvas, FontSecondary);
    ui_str_fit(canvas, 29, 53, 96, AlignLeft, badges[app->badge_sel].desc);
    snprintf(buf, sizeof(buf), "%s  %u/%u", got ? "Earned!" : "Not yet", count, BADGE_COUNT);
    canvas_draw_str(canvas, 29, 61, buf);
}

static void draw_stats_records(Canvas* canvas, App* app) {
    char buf[16];
    Progress* p = &app->prog;
    stats_header(canvas, "RECORDS");
    snprintf(buf, sizeof(buf), "%u", p->best_rush);
    ui_dotted_row(canvas, 25, "Morse Rush", buf);
    snprintf(buf, sizeof(buf), "%u", p->best_sprint);
    ui_dotted_row(canvas, 33, "Sound Sprint", buf);
    snprintf(buf, sizeof(buf), "%u", p->best_echo);
    ui_dotted_row(canvas, 41, "Echo Chain", buf);
    snprintf(buf, sizeof(buf), "%u", p->best_run);
    ui_dotted_row(canvas, 49, "Best run", buf);
    snprintf(buf, sizeof(buf), "%u", p->words_correct);
    ui_dotted_row(canvas, 57, "Words right", buf);
}

void stats_draw(Canvas* canvas, App* app) {
    switch(app->stats_page) {
    case 0:
        draw_stats_overview(canvas, app);
        break;
    case 1:
        draw_stats_signs(canvas, app);
        break;
    case 2:
        draw_stats_badges(canvas, app);
        break;
    default:
        draw_stats_records(canvas, app);
        break;
    }
}

/* ---------- Help ---------- */

typedef struct {
    const char* title;
    const char* text;
} HelpPage;

static const HelpPage help_pages[] = {
    {"WHAT IS MORSE?",
     "Morse code turns letters into short and long beeps. Short = dit (.), long = dah (-). "
     "Radio hams, pilots and sailors use it."},
    {"YOUR MORSE KEY",
     "Left = dit, Right = dah. Hold a key to repeat it. Pause a moment (or press OK) and "
     "the letter is done. Up clears."},
    {"STRAIGHT KEY",
     "Like one button? Settings > Keyer > Straight. Then hold OK: a short press is a dit, a "
     "long press is a dah."},
    {"LESSONS",
     "Every lesson: meet the new signs, key them, then a quiz. Pick answers with the arrow "
     "keys. OK plays the sound again."},
    {"STARS & XP",
     "70% right = 1 star, 85% = 2, 95% = 3. A star unlocks the next lesson. Mistakes come "
     "back at the end - that's how you learn."},
    {"LEARN BY EAR",
     "Don't count dots - learn the rhythm! Say it out loud: di-dah is A. A little practice "
     "every day keeps your streak alive."},
    {"SPEED",
     "Speed = how fast each sign plays (WPM, words per minute). Spacing adds pauses between "
     "signs. Raise spacing as you get better."},
    {"LED COLORS",
     "Blue: Flipper sends. Cyan: you key. Green: right. Red: wrong. Magenta: badge. Yellow: "
     "daily goal."},
    {"GAMES",
     "Morse Rush: key falling letters before they land. Sound Sprint: 60 s of fast "
     "listening. Echo Chain: repeat ever longer chains."},
    {"CONTROLS",
     "Back: pause or go back. Hold Back: main menu. Hold Back in the main menu to exit. "
     "Your progress is saved automatically."},
};
#define HELP_COUNT (sizeof(help_pages) / sizeof(help_pages[0]))
#define HELP_VISIBLE 4

void help_input(App* app, InputEvent* ev) {
    if(!is_nav(ev)) return;
    if(ev->key == InputKeyLeft) {
        app->help_page = app->help_page == 0 ? (uint8_t)(HELP_COUNT - 1) : app->help_page - 1;
        app->help_scroll = 0;
    } else if(ev->key == InputKeyRight) {
        app->help_page = (uint8_t)((app->help_page + 1) % HELP_COUNT);
        app->help_scroll = 0;
    } else if(ev->key == InputKeyDown) {
        if(app->help_scroll + HELP_VISIBLE < app->help_lines) app->help_scroll++;
    } else if(ev->key == InputKeyUp) {
        if(app->help_scroll > 0) app->help_scroll--;
    } else if(ev->key == InputKeyBack && ev->type == InputTypeShort) {
        go_screen(app, ScrMenu);
    }
}

void help_draw(Canvas* canvas, App* app) {
    const HelpPage* page = &help_pages[app->help_page % HELP_COUNT];
    ui_frame(canvas, page->title);
    canvas_set_font(canvas, FontSecondary);
    app->help_lines = ui_wrap(canvas, 6, 25, 112, 9, page->text, app->help_scroll, HELP_VISIBLE);
    if(app->help_scroll > 0) ui_scroll_arrow(canvas, 123, 21, true);
    if(app->help_scroll + HELP_VISIBLE < app->help_lines) ui_scroll_arrow(canvas, 123, 50, false);
    char buf[12];
    snprintf(buf, sizeof(buf), "< %u/%u >", app->help_page + 1, (unsigned)HELP_COUNT);
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, buf);
}

/* ---------- Onboarding ---------- */

void welcome_input(App* app, InputEvent* ev) {
    if(ev->type != InputTypeShort) return;
    if(ev->key == InputKeyOk) {
        app->exp_sel = 0;
        fx_click(app);
        go_screen(app, ScrExperience);
    } else if(ev->key == InputKeyBack) {
        app->running = false;
    }
}

static void draw_tower(Canvas* canvas, int16_t x, int16_t base_y, uint32_t anim) {
    /* lattice radio mast */
    int16_t top = base_y - 34;
    canvas_draw_line(canvas, x, top, x - 9, base_y);
    canvas_draw_line(canvas, x, top, x + 9, base_y);
    for(int16_t k = 1; k < 5; k++) {
        int16_t y = top + k * 7;
        int16_t half = 9 * k * 7 / 34;
        canvas_draw_line(canvas, x - half, y, x + half, y);
        if(k < 4) {
            int16_t half2 = 9 * (k + 1) * 7 / 34;
            canvas_draw_line(canvas, x - half, y, x + half2, y + 7);
        }
    }
    canvas_draw_line(canvas, x - 12, base_y, x + 12, base_y);
    canvas_draw_disc(canvas, x, top - 1, 2);
    /* waves travelling outwards from the top */
    uint32_t phase = (anim / 5) % 4;
    for(uint8_t a = 0; a < 3; a++) {
        if(a >= phase) continue;
        int16_t r = 6 + a * 5;
        for(int16_t dy = -r / 2; dy <= r / 2; dy++) {
            int16_t dx = 0;
            while((dx + 1) * (dx + 1) + dy * dy <= r * r)
                dx++;
            canvas_draw_dot(canvas, x + dx, top - 1 + dy);
            canvas_draw_dot(canvas, x - dx, top - 1 + dy);
        }
    }
}

void welcome_draw(Canvas* canvas, App* app) {
    draw_tower(canvas, 22, 60, app->anim);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 46, 12, "MORSE");
    canvas_draw_str(canvas, 46, 23, "ACADEMY");
    /* "HI" in Morse as a little decoration under the title */
    ui_pattern(canvas, 58, 30, "....", false, 127, false);
    ui_pattern(canvas, 80, 30, "..", false, 127, false);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 46, 41, "Learn Morse code");
    canvas_draw_str(canvas, 46, 49, "from zero to hero!");
    ui_button(canvas, 46, 52, 78, 11, "OK - Let's go", (app->anim / 12) % 2 == 0);
}

static void finish_onboarding(App* app) {
    app->prog.onboarded = 1;
    progress_save(app);
    settings_save(app);
}

void experience_input(App* app, InputEvent* ev) {
    if(!is_nav(ev)) return;
    if(ev->key == InputKeyUp) {
        if(app->exp_sel > 0) app->exp_sel--;
    } else if(ev->key == InputKeyDown) {
        if(app->exp_sel < 2) app->exp_sel++;
    } else if(ev->type != InputTypeShort) {
        return;
    } else if(ev->key == InputKeyBack) {
        go_screen(app, ScrWelcome);
    } else if(ev->key == InputKeyOk) {
        Settings* s = &app->set;
        if(app->exp_sel == 0) {
            s->wpm = 15;
            s->eff_wpm = 8;
        } else if(app->exp_sel == 1) {
            s->wpm = 16;
            s->eff_wpm = 10;
        } else {
            s->wpm = 20;
            s->eff_wpm = 15;
            for(uint8_t l = 0; l < FINAL_LESSON; l++) {
                if(app->prog.stars[l] == 0) app->prog.stars[l] = 1;
            }
            app->prog.unlocked = FINAL_LESSON;
        }
        app->tut_step = 0;
        app->tut_until = 0;
        go_screen(app, ScrTutorial);
        keyer_enable(app, true);
    }
}

void experience_draw(Canvas* canvas, App* app) {
    static const char* const opts[3] = {"Not at all", "A little", "Very well"};
    ui_frame(canvas, "HOW MUCH MORSE?");
    for(uint8_t i = 0; i < 3; i++) {
        ui_button(canvas, 20, 18 + i * 12, 88, 11, opts[i], app->exp_sel == i);
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Redo it later in Settings");
}

/* Interactive tutorial: key E, T and A once before the real start. */
static const char* const tut_targets[3] = {".", "-", ".-"};
static const char* const tut_text[4] = {
    "Press LEFT once. A short beep is a dit.",
    "Now press RIGHT. A long beep is a dah.",
    "Key an A: dit then dah (Left, Right).",
    "Perfect! A letter ends when you pause. Ready!",
};

void tutorial_input(App* app, InputEvent* ev) {
    if(ev->key == InputKeyBack && (ev->type == InputTypeShort || ev->type == InputTypeLong)) {
        go_screen(app, ScrExperience);
        return;
    }
    if(app->tut_step < 3) {
        if(keyer_input(app, ev)) return;
        if(ev->type == InputTypeShort && ev->key == InputKeyOk) keyer_force_commit(app);
        if(ev->type == InputTypeShort && ev->key == InputKeyUp) keyer_clear(app);
        return;
    }
    if(ev->type == InputTypeShort && ev->key == InputKeyOk) {
        uint8_t choice = app->exp_sel;
        finish_onboarding(app);
        if(choice == 1) {
            quiz_start_placement(app);
        } else if(choice == 0) {
            app->course_sel = 0;
            go_screen(app, ScrCourse);
        } else {
            go_screen(app, ScrMenu);
        }
    }
}

void tutorial_tick(App* app) {
    if(app->tut_step >= 3) return;
    if(app->tut_until && TIME_REACHED(app->now, app->tut_until)) {
        app->tut_until = 0;
        app->tut_step++;
        keyer_clear(app);
        if(app->tut_step == 3) fx_success(app);
        return;
    }
    char c;
    char pat[PATTERN_MAX];
    if(!app->tut_until && keyer_take_commit(app, &c, pat)) {
        if(strcmp(pat, tut_targets[app->tut_step]) == 0) {
            fx_correct(app);
            app->tut_until = app->now + 600;
        } else {
            fx_wrong(app);
        }
    }
}

void tutorial_draw(Canvas* canvas, App* app) {
    ui_frame(canvas, app->tut_step < 3 ? "TRY YOUR KEY" : "YOU'RE READY");
    canvas_set_font(canvas, FontSecondary);
    ui_wrap(canvas, 6, 25, 116, 9, tut_text[app->tut_step], 0, 2);

    if(app->tut_step < 3) {
        /* target, filling up as it is keyed */
        const char* target = tut_targets[app->tut_step];
        Keyer* k = &app->keyer;
        int8_t lit = -1;
        if(app->tut_until) {
            lit = 127;
        } else if(strncmp(target, k->pattern, k->plen) == 0) {
            lit = (int8_t)k->plen - 1;
        }
        ui_pattern(canvas, 64, 43, target, true, lit, true);
        canvas_draw_str(canvas, 4, 61, "<dit");
        canvas_draw_str_aligned(canvas, 124, 61, AlignRight, AlignBottom, "dah>");
        /* the pressed side lights up */
        if(k->phase == KeyOn) {
            if(k->cur) {
                canvas_draw_rframe(canvas, 100, 52, 26, 11, 3);
            } else {
                canvas_draw_rframe(canvas, 2, 52, 22, 11, 3);
            }
        }
    } else {
        ui_pattern(canvas, 64, 44, ".-", true, 127, false);
        ui_button(canvas, 34, 50, 60, 12, "OK - Start", (app->anim / 12) % 2 == 0);
    }
}
