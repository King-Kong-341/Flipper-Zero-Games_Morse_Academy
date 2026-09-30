/*
 * Morse Academy for the Flipper Zero
 * ----------------------------------
 * Learn Morse code from zero: a lesson path with 18 lessons + final exam,
 * practice modes, three games, a free keyer with live decoding, an alphabet
 * reference, stats, badges, daily goal and streak.
 *
 * Keying: Left = dit, Right = dah (hold to repeat, both = alternate).
 *         Optional straight key on OK (Settings -> Keyer).
 * Back:   in lessons/games -> pause; elsewhere one screen back.
 *         Long Back -> main menu (in the main menu: exit).
 */

#include "morse.h"

#define TAG "MorseAcademy"
#define FRAME_MS 33
#define TOAST_MS 2400

/* ---------- Small helpers used everywhere ---------- */

uint32_t rnd(uint32_t max) {
    return max ? furi_hal_random_get() % max : 0;
}

uint32_t today_number(void) {
    return furi_hal_rtc_get_timestamp() / 86400U;
}

/* XP needed to reach 'level': 0, 60, 240, 540, 960, 1500, ... */
uint32_t level_floor_xp(uint8_t level) {
    if(level <= 1) return 0;
    uint32_t l = level - 1U;
    return 60U * l * l;
}

uint8_t current_level(App* app) {
    uint8_t l = 1;
    while(l < 99 && app->prog.xp >= level_floor_xp(l + 1))
        l++;
    return l;
}

void push_toast(App* app, const char* title, const char* text, uint8_t icon) {
    if(app->toast_count >= TOAST_QUEUE) return;
    Toast* t = &app->toasts[app->toast_count++];
    strncpy(t->title, title, sizeof(t->title) - 1);
    t->title[sizeof(t->title) - 1] = '\0';
    strncpy(t->text, text, sizeof(t->text) - 1);
    t->text[sizeof(t->text) - 1] = '\0';
    t->icon = icon;
}

void award_badge(App* app, BadgeId id) {
    uint32_t bit = 1UL << id;
    if(app->prog.badges & bit) return;
    app->prog.badges |= bit;
    app->prog_dirty = true;
    push_toast(app, "Badge unlocked!", badges[id].name, 0);
}

void touch_streak(App* app) {
    Progress* p = &app->prog;
    uint32_t today = today_number();
    if(p->last_day == today && p->streak > 0) return;
    if(p->streak > 0 && p->last_day + 1 == today) {
        p->streak++;
    } else {
        p->streak = 1;
    }
    p->last_day = today;
    if(p->streak > p->best_streak) p->best_streak = p->streak;
    if(p->streak >= 3) award_badge(app, BadgeStreak3);
    if(p->streak >= 7) award_badge(app, BadgeStreak7);
    app->prog_dirty = true;
}

void add_xp(App* app, uint16_t amount) {
    if(amount == 0) return;
    Progress* p = &app->prog;
    uint8_t level_before = current_level(app);
    uint32_t today = today_number();
    if(p->xp_day != today) {
        p->xp_day = today;
        p->xp_today = 0;
    }
    uint16_t goal = daily_goals[app->set.daily_goal & 3];
    bool goal_before = p->xp_today >= goal;
    p->xp += amount;
    p->xp_today = (uint16_t)(p->xp_today + amount > 60000 ? 60000 : p->xp_today + amount);
    touch_streak(app);

    if(!goal_before && p->xp_today >= goal) {
        char text[24];
        snprintf(text, sizeof(text), "%u XP today!", p->xp_today);
        push_toast(app, "Daily goal!", text, 2);
    }
    uint8_t level_after = current_level(app);
    if(level_after > level_before) {
        char text[24];
        snprintf(text, sizeof(text), "Lv %u - %s", level_after, level_title(level_after));
        push_toast(app, "Level up!", text, 1);
    }
    app->prog_dirty = true;
}

/* Updates the per-sign statistics. Mastery moves quickly towards 100 on
 * correct answers and drops harder on mistakes, so weak signs show up. */
void record_answer(App* app, char c, bool correct) {
    int i = morse_index(c);
    if(i < 0) return;
    Progress* p = &app->prog;
    if(p->seen[i] < 60000) p->seen[i]++;
    p->total_answers++;
    if(correct) {
        if(p->hits[i] < 60000) p->hits[i]++;
        p->total_correct++;
        p->mastery[i] = (uint8_t)(p->mastery[i] + (100 - p->mastery[i] + 3) / 4);
    } else {
        p->mastery[i] = (uint8_t)(p->mastery[i] * 3 / 5);
    }
    if(p->mastery[i] > 100) p->mastery[i] = 100;
    app->prog_dirty = true;
}

static int8_t lesson_of_char(char c) {
    for(uint8_t l = 0; l < LESSON_COUNT; l++) {
        if(strchr(lessons[l].chars, c) != NULL && c != '\0') return (int8_t)l;
    }
    return -1;
}

bool char_learned(App* app, char c) {
    int8_t l = lesson_of_char(c);
    if(l < 0) return false;
    return l < app->prog.unlocked || app->prog.stars[l] > 0;
}

/* Signs the player has learned so far (for practice and games). With
 * 'include_current' the signs of the lesson in progress are added too. */
uint8_t pool_chars(App* app, char* out, bool include_current) {
    uint8_t n = 0;
    for(uint8_t l = 0; l < FINAL_LESSON; l++) {
        bool take = char_learned(app, lessons[l].chars[0]) ||
                    (include_current && l == app->prog.unlocked);
        if(!take) continue;
        for(const char* c = lessons[l].chars; *c; c++)
            out[n++] = *c;
    }
    if(n < 2) {
        n = 0;
        out[n++] = 'E';
        out[n++] = 'T';
    }
    out[n] = '\0';
    return n;
}

void apply_backlight(App* app) {
    if(app->set.backlight && !app->backlight_forced) {
        notification_message(app->notif, &sequence_display_backlight_enforce_on);
        app->backlight_forced = true;
    } else if(!app->set.backlight && app->backlight_forced) {
        notification_message(app->notif, &sequence_display_backlight_enforce_auto);
        app->backlight_forced = false;
    }
}

void go_screen(App* app, Screen s) {
    bool active_before = app->screen == ScrQuiz || app->screen == ScrGame ||
                         app->screen == ScrFreeKey || app->screen == ScrTutorial;
    if(active_before && s != app->screen) {
        all_signals_off(app);
        keyer_enable(app, false);
    }
    if(s == ScrMenu) {
        player_stop(app);
        if(app->prog_dirty) progress_save(app);
    }
    app->screen = s;
    app->screen_since = furi_get_tick();
}

/* ---------- Toasts ---------- */

static void toast_update(App* app) {
    if(app->toast_count == 0) return;
    if(!app->toast_started) {
        /* wait for a quiet moment so the jingle never cuts into Morse */
        if(player_busy(app) || keyer_busy(app) || app->fx.active) return;
        app->toast_started = true;
        app->toast_start = app->now;
        if(app->toasts[0].icon == 2) {
            fx_goal(app);
        } else {
            fx_badge(app);
        }
    } else if(app->now - app->toast_start >= TOAST_MS) {
        for(uint8_t i = 1; i < app->toast_count; i++)
            app->toasts[i - 1] = app->toasts[i];
        app->toast_count--;
        app->toast_started = false;
    }
}

/* ---------- Dispatch ---------- */

static void handle_input(App* app, InputEvent* ev) {
    if(ev->key == InputKeyBack && ev->type == InputTypeLong) {
        switch(app->screen) {
        case ScrMenu:
        case ScrWelcome:
        case ScrExperience:
            app->running = false;
            return;
        case ScrQuiz:
        case ScrGame:
        case ScrFreeKey:
        case ScrTutorial:
            break; /* these handle it themselves (pause / leave) */
        default:
            go_screen(app, ScrMenu);
            return;
        }
    }

    switch(app->screen) {
    case ScrWelcome:
        welcome_input(app, ev);
        break;
    case ScrExperience:
        experience_input(app, ev);
        break;
    case ScrTutorial:
        tutorial_input(app, ev);
        break;
    case ScrMenu:
        menu_input(app, ev);
        break;
    case ScrCourse:
        course_input(app, ev);
        break;
    case ScrQuiz:
        quiz_input(app, ev);
        break;
    case ScrResults:
        results_input(app, ev);
        break;
    case ScrPracticeMenu:
        practice_menu_input(app, ev);
        break;
    case ScrGamesMenu:
        games_menu_input(app, ev);
        break;
    case ScrGame:
        game_input(app, ev);
        break;
    case ScrGameOver:
        game_over_input(app, ev);
        break;
    case ScrFreeKey:
        freekey_input(app, ev);
        break;
    case ScrAlphabet:
        alphabet_input(app, ev);
        break;
    case ScrStats:
        stats_input(app, ev);
        break;
    case ScrSettings:
        settings_input(app, ev);
        break;
    case ScrConfirm:
        confirm_input(app, ev);
        break;
    case ScrHelp:
        help_input(app, ev);
        break;
    }
}

static void handle_tick(App* app) {
    switch(app->screen) {
    case ScrQuiz:
        quiz_tick(app);
        break;
    case ScrGame:
        game_tick(app);
        break;
    case ScrFreeKey:
        freekey_tick(app);
        break;
    case ScrTutorial:
        tutorial_tick(app);
        break;
    default:
        break;
    }
}

static void render_callback(Canvas* canvas, void* ctx) {
    App* app = ctx;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    switch(app->screen) {
    case ScrWelcome:
        welcome_draw(canvas, app);
        break;
    case ScrExperience:
        experience_draw(canvas, app);
        break;
    case ScrTutorial:
        tutorial_draw(canvas, app);
        break;
    case ScrMenu:
        menu_draw(canvas, app);
        break;
    case ScrCourse:
        course_draw(canvas, app);
        break;
    case ScrQuiz:
        quiz_draw(canvas, app);
        break;
    case ScrResults:
        results_draw(canvas, app);
        break;
    case ScrPracticeMenu:
        practice_menu_draw(canvas, app);
        break;
    case ScrGamesMenu:
        games_menu_draw(canvas, app);
        break;
    case ScrGame:
        game_draw(canvas, app);
        break;
    case ScrGameOver:
        game_over_draw(canvas, app);
        break;
    case ScrFreeKey:
        freekey_draw(canvas, app);
        break;
    case ScrAlphabet:
        alphabet_draw(canvas, app);
        break;
    case ScrStats:
        stats_draw(canvas, app);
        break;
    case ScrSettings:
        settings_draw(canvas, app);
        break;
    case ScrConfirm:
        confirm_draw(canvas, app);
        break;
    case ScrHelp:
        help_draw(canvas, app);
        break;
    }

    if(app->toast_started) ui_toast(canvas, app);
}

static void input_callback(InputEvent* event, void* ctx) {
    App* app = ctx;
    furi_message_queue_put(app->queue, event, FuriWaitForever);
}

/* ---------- Lifecycle ---------- */

int32_t morse_academy_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->running = true;
    app->queue = furi_message_queue_alloc(16, sizeof(InputEvent));
    app->notif = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);

    settings_load(app);
    progress_load(app);
    if(app->prog.xp_day != today_number()) app->prog.xp_today = 0;

    app->screen = app->prog.onboarded ? ScrMenu : ScrWelcome;
    app->now = furi_get_tick();
    app->screen_since = app->now;
    app->course_sel = app->prog.unlocked;

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, render_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    apply_backlight(app);

    uint32_t last_draw = 0;
    InputEvent ev;
    while(app->running) {
        bool got = furi_message_queue_get(app->queue, &ev, next_deadline(app)) == FuriStatusOk;
        app->now = furi_get_tick();
        if(got) handle_input(app, &ev);

        player_update(app);
        keyer_update(app);
        fx_update(app);
        app->now = furi_get_tick();
        handle_tick(app);
        toast_update(app);

        if(got || app->now - last_draw >= FRAME_MS) {
            app->anim = app->now / FRAME_MS;
            last_draw = app->now;
            view_port_update(app->view_port);
        }
    }

    all_signals_off(app);
    audio_release(app);
    if(app->prog_dirty) progress_save(app);
    notification_message(app->notif, &sequence_reset_rgb);
    if(app->backlight_forced) {
        notification_message(app->notif, &sequence_display_backlight_enforce_auto);
    }

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(app->queue);
    free(app);
    return 0;
}
