#include "morse.h"

/* Three games plus the free keyer:
 *
 *   Morse Rush   - letters fall down, key them before they hit the ground
 *   Sound Sprint - 60 seconds: hear a sign, pick it, it gets faster
 *   Echo Chain   - hear a growing chain of signs, key it back (Simon-style)
 *   Free Key     - key anything, it is decoded live */

#define RUSH_TOP 16 /* spawn height (center of the letter box) */
#define RUSH_GROUND 44 /* a letter box whose center reaches this line lands */
#define SPRINT_MS 60000U

static const int16_t lane_x[RUSH_FALLERS] = {14, 39, 64, 89, 114};

uint16_t game_best(App* app, GameKind kind) {
    switch(kind) {
    case GameRush:
        return app->prog.best_rush;
    case GameSprint:
        return app->prog.best_sprint;
    default:
        return app->prog.best_echo;
    }
}

static const char* game_name(GameKind kind) {
    switch(kind) {
    case GameRush:
        return "MORSE RUSH";
    case GameSprint:
        return "SOUND SPRINT";
    default:
        return "ECHO CHAIN";
    }
}

/* ---------- Sound Sprint helpers ---------- */

static void sprint_new_round(App* app) {
    Game* g = &app->game;
    char pool[CHAR_COUNT + 1];
    pool_chars(app, pool, false);
    g->target = pick_weighted(app, pool, g->target);
    char d[3];
    uint8_t n = pick_distractors(g->target, pool, d, 3);
    char opts[4] = {g->target, 0, 0, 0};
    for(uint8_t i = 0; i < n; i++)
        opts[i + 1] = d[i];
    uint8_t count = n + 1;

    static const uint8_t slots2[2] = {3, 1};
    static const uint8_t slots3[3] = {3, 0, 1};
    static const uint8_t slots4[4] = {0, 1, 2, 3};
    const uint8_t* slots = count <= 2 ? slots2 : (count == 3 ? slots3 : slots4);
    uint8_t perm[4] = {0, 1, 2, 3};
    for(uint8_t i = count; i > 1; i--) {
        uint8_t j = (uint8_t)rnd(i);
        uint8_t t = perm[i - 1];
        perm[i - 1] = perm[j];
        perm[j] = t;
    }
    memset(g->options, 0, sizeof(g->options));
    memset(g->slot_used, 0, sizeof(g->slot_used));
    for(uint8_t i = 0; i < count; i++) {
        g->options[slots[i]] = opts[perm[i]];
        g->slot_used[slots[i]] = true;
        if(perm[i] == 0) g->answer_slot = slots[i];
    }
    g->chosen = -1;
    g->feedback_until = 0;
    char t[2] = {g->target, '\0'};
    player_start_fast(app, t, g->sprint_wpm);
}

/* ---------- Echo Chain helpers ---------- */

static void echo_add(App* app) {
    Game* g = &app->game;
    if(g->seq_len >= ECHO_MAX) return;
    char pool[CHAR_COUNT + 1];
    pool_chars(app, pool, false);
    char last = g->seq_len ? g->seq[g->seq_len - 1] : 0;
    g->seq[g->seq_len++] = pick_weighted(app, pool, last);
    g->seq[g->seq_len] = '\0';
}

static void echo_round(App* app, uint32_t now) {
    Game* g = &app->game;
    g->echo_phase = 0;
    g->echo_pos = 0;
    g->phase_until = now + 900;
    keyer_enable(app, false);
}

/* ---------- Start / end ---------- */

void game_start(App* app, GameKind kind) {
    Game* g = &app->game;
    memset(g, 0, sizeof(Game));
    g->kind = kind;
    g->lives = 3;
    g->level = 1;
    g->start = furi_get_tick();
    g->last_update = g->start;
    go_screen(app, ScrGame);

    switch(kind) {
    case GameRush:
        keyer_enable(app, true);
        g->speed = 4;
        g->spawn_ms = 2600;
        g->next_spawn = 700;
        break;
    case GameSprint:
        g->time_left_ms = SPRINT_MS;
        g->sprint_wpm = app->set.wpm;
        sprint_new_round(app);
        break;
    default:
        echo_add(app);
        echo_round(app, g->start);
        break;
    }
}

static void game_end(App* app) {
    Game* g = &app->game;
    Progress* p = &app->prog;
    all_signals_off(app);
    keyer_enable(app, false);
    g->over = true;
    uint16_t* best = g->kind == GameRush ? &p->best_rush :
                     (g->kind == GameSprint ? &p->best_sprint : &p->best_echo);
    g->new_record = g->score > *best;
    if(g->new_record) *best = g->score;

    if(g->kind == GameRush) {
        g->xp_gained = g->score / 10;
        if(g->score >= 100) award_badge(app, BadgeRush);
    } else if(g->kind == GameSprint) {
        g->xp_gained = g->score * 2;
    } else {
        g->xp_gained = g->score * 5;
    }
    go_screen(app, ScrGameOver);
    if(g->new_record && g->score > 0) {
        fx_success(app);
    } else {
        fx_fail(app);
    }
    add_xp(app, g->xp_gained);
    progress_save(app);
}

/* ---------- Morse Rush ---------- */

static void rush_spawn(App* app) {
    Game* g = &app->game;
    uint8_t alive = 0;
    for(uint8_t i = 0; i < RUSH_FALLERS; i++)
        if(g->fallers[i].alive) alive++;
    uint8_t max_alive = (uint8_t)(1 + g->level / 2 + 1);
    if(max_alive > RUSH_FALLERS) max_alive = RUSH_FALLERS;
    if(alive >= max_alive) return;

    /* free lane without a letter near the top */
    uint8_t lanes[RUSH_FALLERS];
    uint8_t nl = 0;
    for(uint8_t l = 0; l < RUSH_FALLERS; l++) {
        bool busy = false;
        for(uint8_t i = 0; i < RUSH_FALLERS; i++) {
            if(g->fallers[i].alive && g->fallers[i].lane == l) busy = true;
        }
        if(!busy) lanes[nl++] = l;
    }
    if(nl == 0) return;

    char pool[CHAR_COUNT + 1];
    pool_chars(app, pool, false);
    char last = 0;
    for(uint8_t i = 0; i < RUSH_FALLERS; i++) {
        if(g->fallers[i].alive) last = g->fallers[i].ch;
    }
    for(uint8_t i = 0; i < RUSH_FALLERS; i++) {
        if(g->fallers[i].alive) continue;
        Faller* f = &g->fallers[i];
        f->alive = true;
        f->ch = pick_weighted(app, pool, last);
        f->lane = lanes[rnd(nl)];
        f->y = RUSH_TOP * 1000;
        f->born = app->now;
        return;
    }
}

static Faller* rush_lowest(App* app) {
    Game* g = &app->game;
    Faller* best = NULL;
    for(uint8_t i = 0; i < RUSH_FALLERS; i++) {
        Faller* f = &g->fallers[i];
        if(f->alive && (!best || f->y > best->y)) best = f;
    }
    return best;
}

static void rush_commit(App* app, char c) {
    Game* g = &app->game;
    Faller* hit = NULL;
    for(uint8_t i = 0; i < RUSH_FALLERS; i++) {
        Faller* f = &g->fallers[i];
        if(f->alive && f->ch == c && (!hit || f->y > hit->y)) hit = f;
    }
    if(hit) {
        hit->alive = false;
        uint16_t bonus = g->combo > 10 ? 10 : g->combo;
        g->score = (uint16_t)(g->score + 10 + bonus * 2);
        g->combo++;
        g->zap_x = lane_x[hit->lane];
        g->zap_y = (int16_t)(hit->y / 1000);
        g->zap_until = app->now + 160;
        g->burst.x = g->zap_x;
        g->burst.y = g->zap_y;
        g->burst.start = app->now;
        g->burst.active = true;
        record_answer(app, c, true);
        fx_zap(app);
        uint8_t new_level = (uint8_t)(1 + g->score / 100);
        if(new_level > g->level) {
            g->level = new_level;
            g->speed += 1;
            if(g->speed > 14) g->speed = 14;
            if(g->spawn_ms > 1100) g->spawn_ms -= 200;
        }
    } else {
        g->combo = 0;
        fx_miss(app);
    }
}

static void rush_tick(App* app, uint32_t dt) {
    Game* g = &app->game;
    char c;
    if(keyer_take_commit(app, &c, NULL)) {
        if(c) {
            rush_commit(app, c);
        } else {
            g->combo = 0;
            fx_miss(app);
        }
    }

    if(g->next_spawn <= dt) {
        rush_spawn(app);
        g->next_spawn = g->spawn_ms;
    } else {
        g->next_spawn -= dt;
    }

    for(uint8_t i = 0; i < RUSH_FALLERS; i++) {
        Faller* f = &g->fallers[i];
        if(!f->alive) continue;
        f->y += g->speed * (int32_t)dt;
        if(f->y >= RUSH_GROUND * 1000) {
            f->alive = false;
            g->combo = 0;
            if(g->lives > 0) g->lives--;
            g->missed_char = f->ch;
            g->missed_until = app->now + 1800;
            g->hurt_until = app->now + 350;
            record_answer(app, f->ch, false);
            fx_boom(app);
            if(g->lives == 0) {
                game_end(app);
                return;
            }
        }
    }
}

static void draw_hud(Canvas* canvas, App* app) {
    Game* g = &app->game;
    char buf[16];
    for(uint8_t i = 0; i < 3; i++) {
        bool blink = app->now < g->hurt_until && i == g->lives && (app->anim % 2) == 0;
        ui_heart(canvas, 2 + i * 9, 1, i < g->lives || blink);
    }
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%u", g->score);
    canvas_draw_str_aligned(canvas, 126, 8, AlignRight, AlignBottom, buf);
    snprintf(buf, sizeof(buf), "Lv%u", g->level);
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignBottom, buf);
    if(g->combo >= 3) {
        snprintf(buf, sizeof(buf), "x%u", g->combo);
        canvas_draw_str(canvas, 80, 8, buf);
    }
}

static void draw_rush(Canvas* canvas, App* app) {
    Game* g = &app->game;
    draw_hud(canvas, app);
    canvas_draw_line(canvas, 0, 10, 127, 10);

    /* ground: dotted grass line */
    for(int16_t x = 0; x < 128; x += 2)
        canvas_draw_dot(canvas, x, 50);
    if(app->now < g->hurt_until) canvas_draw_line(canvas, 0, 51, 127, 51);

    Faller* low = rush_lowest(app);
    for(uint8_t i = 0; i < RUSH_FALLERS; i++) {
        Faller* f = &g->fallers[i];
        if(!f->alive) continue;
        int16_t cx = lane_x[f->lane];
        int16_t cy = (int16_t)(f->y / 1000);
        char t[2] = {f->ch, '\0'};
        canvas_set_color(canvas, ColorBlack);
        if(f == low) {
            canvas_draw_rbox(canvas, cx - 6, cy - 6, 13, 13, 3);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, cx - 6, cy - 6, 13, 13, 3);
        }
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, cx + 1, cy + 1, AlignCenter, AlignCenter, t);
        canvas_set_color(canvas, ColorBlack);
    }

    /* zap beam from the antenna + a little burst */
    if(app->now < g->zap_until) {
        canvas_draw_line(canvas, 64, 53, g->zap_x, g->zap_y + 6);
    }
    if(g->burst.active) {
        uint32_t age = app->now - g->burst.start;
        if(age > 300) {
            g->burst.active = false;
        } else {
            int16_t r = (int16_t)(3 + age / 30);
            static const int8_t dx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
            static const int8_t dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
            for(uint8_t k = 0; k < 8; k++) {
                canvas_draw_dot(canvas, g->burst.x + dx[k] * r, g->burst.y + dy[k] * r);
            }
        }
    }

    /* bottom strip: what you key, or a hint / the letter you just missed */
    Keyer* k = &app->keyer;
    char buf[16];
    if(k->plen > 0 || k->phase == KeyOn) {
        char pat[PATTERN_MAX + 1];
        memcpy(pat, k->pattern, PATTERN_MAX);
        pat[k->plen] = '\0';
        if(k->phase == KeyOn && k->plen < PATTERN_MAX - 1) {
            pat[k->plen] = k->cur ? '-' : '.';
            pat[k->plen + 1] = '\0';
        }
        ui_pattern(canvas, 64, 58, pat, true, 127, false);
    } else if(app->now < g->missed_until && g->missed_char) {
        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "Missed %c:", g->missed_char);
        canvas_draw_str(canvas, 2, 61, buf);
        char t[2] = {g->missed_char, '\0'};
        ui_pattern(canvas, 90, 58, morse_code(t[0]), false, 127, false);
    } else if(low && app->set.hints) {
        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "Hint %c:", low->ch);
        canvas_draw_str(canvas, 2, 61, buf);
        ui_pattern(canvas, 80, 58, morse_code(low->ch), false, 127, false);
    } else {
        ui_icon(canvas, 60, 54, IconRadio);
    }
}

/* ---------- Sound Sprint ---------- */

static void sprint_answer(App* app, uint8_t slot) {
    Game* g = &app->game;
    if(!g->slot_used[slot] || g->chosen >= 0) return;
    g->chosen = (int8_t)slot;
    player_stop(app);
    bool ok = slot == g->answer_slot;
    record_answer(app, g->target, ok);
    if(ok) {
        g->score = (uint16_t)(g->score + 1 + g->combo / 5);
        g->combo++;
        if(g->combo % 5 == 0 && g->sprint_wpm < 35) g->sprint_wpm++;
        if(g->combo % 10 == 0) {
            g->time_left_ms += 3000;
            g->bonus_until = app->now + 900;
        }
        fx_correct(app);
        g->feedback_until = app->now + 260;
    } else {
        g->combo = 0;
        g->time_left_ms = g->time_left_ms > 3000 ? g->time_left_ms - 3000 : 0;
        fx_wrong(app);
        g->feedback_until = app->now + 800;
    }
}

static void sprint_tick(App* app, uint32_t dt) {
    Game* g = &app->game;
    if(g->time_left_ms <= dt) {
        g->time_left_ms = 0;
        game_end(app);
        return;
    }
    g->time_left_ms -= dt;
    if(g->chosen >= 0 && TIME_REACHED(app->now, g->feedback_until)) sprint_new_round(app);
}

static void draw_sprint(Canvas* canvas, App* app) {
    Game* g = &app->game;
    char buf[16];
    ui_top_progress(canvas, g->time_left_ms, SPRINT_MS);
    for(uint8_t slot = 0; slot < 4; slot++) {
        if(!g->slot_used[slot]) continue;
        char t[2] = {g->options[slot], '\0'};
        bool filled = g->chosen >= 0 && slot == g->answer_slot;
        bool cross = g->chosen >= 0 && slot == (uint8_t)g->chosen && slot != g->answer_slot;
        ui_choice_box(canvas, slot, t, filled, cross, FontPrimary);
    }
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%u", g->score);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 14, buf);
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%lus", (unsigned long)((g->time_left_ms + 999) / 1000));
    canvas_draw_str_aligned(canvas, 126, 13, AlignRight, AlignBottom, buf);
    snprintf(buf, sizeof(buf), "%uwpm", g->sprint_wpm);
    canvas_draw_str_aligned(canvas, 126, 22, AlignRight, AlignBottom, buf);
    if(g->combo >= 3) {
        ui_flame(canvas, 2, 16, true);
        snprintf(buf, sizeof(buf), "x%u", g->combo);
        canvas_draw_str(canvas, 11, 24, buf);
    }
    if(app->now < g->bonus_until) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 4, 58, "+3s!");
    }
    if(g->chosen < 0) {
        ui_waves(canvas, 66, 33, app->anim, player_busy(app));
    } else {
        char t[2] = {g->target, '\0'};
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, t);
        ui_pattern(canvas, 64, 42, morse_code(g->target), false, 127, false);
    }
}

/* ---------- Echo Chain ---------- */

static void echo_tick(App* app) {
    Game* g = &app->game;
    uint32_t now = app->now;
    switch(g->echo_phase) {
    case 0:
        if(TIME_REACHED(now, g->phase_until)) {
            g->echo_phase = 1;
            player_start(app, g->seq);
        }
        break;
    case 1:
        if(!player_busy(app)) {
            g->echo_phase = 2;
            keyer_enable(app, true);
        }
        break;
    case 2: {
        char c;
        if(keyer_take_commit(app, &c, NULL)) {
            char want = g->seq[g->echo_pos];
            record_answer(app, want, c == want);
            if(c == want) {
                g->echo_pos++;
                if(g->echo_pos >= g->seq_len) {
                    g->score = g->seq_len;
                    g->echo_phase = 3;
                    g->phase_until = now + 900;
                    keyer_enable(app, false);
                    fx_correct(app);
                } else {
                    fx_click(app);
                }
            } else {
                g->echo_wrong = c ? c : '*';
                g->echo_phase = 4;
                g->phase_until = now + 1800;
                keyer_enable(app, false);
                fx_wrong(app);
            }
        }
        break;
    }
    case 3:
        if(TIME_REACHED(now, g->phase_until)) {
            echo_add(app);
            echo_round(app, now);
        }
        break;
    default:
        if(TIME_REACHED(now, g->phase_until)) game_end(app);
        break;
    }
}

static void draw_echo(Canvas* canvas, App* app) {
    Game* g = &app->game;
    char buf[24];
    canvas_set_font(canvas, FontPrimary);
    snprintf(buf, sizeof(buf), "ROUND %u", g->seq_len);
    canvas_draw_str_aligned(canvas, 64, 7, AlignCenter, AlignCenter, buf);
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "Best %u", app->prog.best_echo);
    canvas_draw_str_aligned(canvas, 126, 8, AlignRight, AlignBottom, buf);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    /* slot row: a window of up to 9 slots around the active one */
    int8_t playing = g->echo_phase == 1 ? player_current_char(app) : -1;
    uint8_t active = g->echo_phase == 1 ? (uint8_t)(playing < 0 ? 0 : playing) : g->echo_pos;
    uint8_t first = active > 4 ? active - 4 : 0;
    if(g->seq_len > 9 && first + 9 > g->seq_len) first = g->seq_len - 9;
    uint8_t shown = g->seq_len - first < 9 ? g->seq_len - first : 9;
    int16_t x0 = 64 - (shown * 13 - 2) / 2;
    for(uint8_t i = 0; i < shown; i++) {
        uint8_t idx = first + i;
        int16_t x = x0 + i * 13;
        bool done = g->echo_phase >= 2 && idx < g->echo_pos;
        bool reveal = g->echo_phase >= 3;
        bool is_active = idx == active && g->echo_phase <= 2;
        canvas_set_color(canvas, ColorBlack);
        if(is_active && (g->echo_phase == 1 || (app->anim / 6) % 2 == 0)) {
            canvas_draw_rbox(canvas, x, 17, 11, 13, 2);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, x, 17, 11, 13, 2);
        }
        char t[2] = {'?', '\0'};
        if(done || reveal) t[0] = g->seq[idx];
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, x + 6, 24, AlignCenter, AlignCenter, t);
        canvas_set_color(canvas, ColorBlack);
    }

    canvas_set_font(canvas, FontSecondary);
    switch(g->echo_phase) {
    case 0:
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, "Get ready...");
        break;
    case 1:
        ui_waves(canvas, 66, 42, app->anim, true);
        break;
    case 2: {
        Keyer* k = &app->keyer;
        char pat[PATTERN_MAX + 1];
        memcpy(pat, k->pattern, PATTERN_MAX);
        pat[k->plen] = '\0';
        if(k->phase == KeyOn && k->plen < PATTERN_MAX - 1) {
            pat[k->plen] = k->cur ? '-' : '.';
            pat[k->plen + 1] = '\0';
        }
        if(pat[0]) {
            ui_pattern(canvas, 64, 42, pat, true, 127, false);
        } else {
            canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, "Your turn - key it back!");
        }
        break;
    }
    case 3:
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, "Chain complete!");
        break;
    default: {
        char want = g->seq[g->echo_pos];
        snprintf(buf, sizeof(buf), "It was %c, not %c", want, g->echo_wrong);
        canvas_draw_str_aligned(canvas, 64, 37, AlignCenter, AlignCenter, buf);
        ui_pattern(canvas, 64, 46, morse_code(want), false, 127, false);
        break;
    }
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 63, "<dit");
    canvas_draw_str_aligned(canvas, 126, 63, AlignRight, AlignBottom, "dah>");
}

/* ---------- Shared game input / tick / draw ---------- */

void game_input(App* app, InputEvent* ev) {
    Game* g = &app->game;
    if(g->paused) {
        if(ev->type != InputTypeShort) return;
        if(ev->key == InputKeyLeft || ev->key == InputKeyRight) {
            g->pause_yes = !g->pause_yes;
        } else if(ev->key == InputKeyOk) {
            g->paused = false;
            g->last_update = app->now;
            if(g->pause_yes) {
                all_signals_off(app);
                go_screen(app, ScrGamesMenu);
            } else if(g->kind == GameEcho && g->echo_phase == 1) {
                player_start(app, g->seq); /* replay the chain after a pause */
            }
        } else if(ev->key == InputKeyBack) {
            g->paused = false;
            g->last_update = app->now;
            if(g->kind == GameEcho && g->echo_phase == 1) player_start(app, g->seq);
        }
        return;
    }
    if(ev->key == InputKeyBack) {
        if(ev->type == InputTypeShort || ev->type == InputTypeLong) {
            player_stop(app);
            keyer_clear(app);
            g->paused = true;
            g->pause_yes = false;
        }
        return;
    }

    if(g->kind == GameSprint) {
        if(ev->type != InputTypeShort) return;
        if(ev->key == InputKeyOk) {
            if(g->chosen < 0 && !player_busy(app)) {
                char t[2] = {g->target, '\0'};
                player_start_fast(app, t, g->sprint_wpm);
            }
        } else if(ev->key == InputKeyUp) {
            sprint_answer(app, 0);
        } else if(ev->key == InputKeyRight) {
            sprint_answer(app, 1);
        } else if(ev->key == InputKeyDown) {
            sprint_answer(app, 2);
        } else if(ev->key == InputKeyLeft) {
            sprint_answer(app, 3);
        }
        return;
    }

    /* Rush and Echo are played with the keyer */
    if(keyer_input(app, ev)) return;
    if(ev->type == InputTypeShort) {
        if(ev->key == InputKeyOk) keyer_force_commit(app);
        if(ev->key == InputKeyUp) keyer_clear(app);
    }
}

void game_tick(App* app) {
    Game* g = &app->game;
    if(g->paused || g->over) {
        g->last_update = app->now;
        return;
    }
    uint32_t dt = app->now - g->last_update;
    if(dt > 100) dt = 100;
    g->last_update = app->now;
    switch(g->kind) {
    case GameRush:
        rush_tick(app, dt);
        break;
    case GameSprint:
        sprint_tick(app, dt);
        break;
    default:
        echo_tick(app);
        break;
    }
}

void game_draw(Canvas* canvas, App* app) {
    Game* g = &app->game;
    switch(g->kind) {
    case GameRush:
        draw_rush(canvas, app);
        break;
    case GameSprint:
        draw_sprint(canvas, app);
        break;
    default:
        draw_echo(canvas, app);
        break;
    }
    if(g->paused) ui_yes_no(canvas, "Quit game?", g->pause_yes);
}

/* ---------- Game over ---------- */

void game_over_input(App* app, InputEvent* ev) {
    if(ev->type != InputTypeShort) return;
    if(ev->key == InputKeyOk) {
        game_start(app, app->game.kind);
    } else if(ev->key == InputKeyLeft || ev->key == InputKeyBack) {
        go_screen(app, ScrGamesMenu);
    }
}

void game_over_draw(Canvas* canvas, App* app) {
    Game* g = &app->game;
    char buf[24];
    ui_frame(canvas, game_name(g->kind));

    /* confetti on a new record, calm twinkle otherwise */
    for(uint8_t k = 0; k < 6; k++) {
        int16_t lx = 6 + (k * 5) % 26;
        int16_t rx = 96 + (k * 5 + 3) % 26;
        if(g->new_record) {
            int16_t fy = 18 + (int16_t)((app->anim / 2 + k * 4) % 20);
            canvas_draw_dot(canvas, lx, fy);
            canvas_draw_dot(canvas, rx, 38 - (fy - 18));
        } else if(((app->anim / 10) + k) % 3 == 0) {
            canvas_draw_dot(canvas, lx, 20 + (k * 3) % 15);
            canvas_draw_dot(canvas, rx, 22 + (k * 4) % 13);
        }
    }

    canvas_set_font(canvas, FontSecondary);
    if(g->new_record && g->score > 0) {
        if((app->anim / 8) % 2 == 0) {
            canvas_draw_str_aligned(canvas, 64, 21, AlignCenter, AlignCenter, "* NEW RECORD *");
        }
    } else {
        snprintf(buf, sizeof(buf), "Best: %u", game_best(app, g->kind));
        canvas_draw_str_aligned(canvas, 64, 21, AlignCenter, AlignCenter, buf);
    }

    snprintf(buf, sizeof(buf), "%u", g->score);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, buf);

    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "+%u XP", g->xp_gained);
    canvas_draw_str_aligned(canvas, 64, 47, AlignCenter, AlignCenter, buf);

    canvas_draw_str(canvas, 6, 60, "< Games");
    canvas_draw_str_aligned(canvas, 122, 60, AlignRight, AlignBottom, "OK Again");
}

/* ---------- Free Key ---------- */

void freekey_start(App* app) {
    app->free_len = 0;
    app->free_text[0] = '\0';
    app->free_last_commit = 0;
    go_screen(app, ScrFreeKey);
    keyer_enable(app, true);
}

static void free_append(App* app, char c) {
    if(app->free_len >= FREE_TEXT_MAX) {
        /* drop the oldest 20 characters */
        memmove(app->free_text, app->free_text + 20, FREE_TEXT_MAX - 20);
        app->free_len = FREE_TEXT_MAX - 20;
    }
    app->free_text[app->free_len++] = c;
    app->free_text[app->free_len] = '\0';
}

void freekey_input(App* app, InputEvent* ev) {
    if(ev->key == InputKeyBack) {
        if(ev->type == InputTypeShort || ev->type == InputTypeLong) go_screen(app, ScrMenu);
        return;
    }
    if(player_busy(app) && ev->type == InputTypePress) player_stop(app);
    if(keyer_input(app, ev)) return;

    if(ev->key == InputKeyDown && ev->type == InputTypeLong) {
        if(app->free_len) player_start(app, app->free_text);
        return;
    }
    if(ev->key == InputKeyUp && ev->type == InputTypeLong) {
        keyer_clear(app);
        app->free_len = 0;
        app->free_text[0] = '\0';
        return;
    }
    if(ev->type != InputTypeShort) return;
    if(ev->key == InputKeyOk) {
        keyer_force_commit(app);
    } else if(ev->key == InputKeyUp) {
        if(keyer_busy(app)) {
            keyer_clear(app);
        } else if(app->free_len) {
            app->free_text[--app->free_len] = '\0';
        }
    } else if(ev->key == InputKeyDown) {
        if(app->free_len && app->free_text[app->free_len - 1] != ' ') free_append(app, ' ');
    }
}

void freekey_tick(App* app) {
    char c;
    if(keyer_take_commit(app, &c, NULL)) {
        free_append(app, c ? c : '*');
        app->free_last_commit = app->now;
    }
    /* long silence after a letter = word gap -> automatic space */
    uint32_t word_gap = unit_ms(app, 0) * 7;
    if(word_gap < 900) word_gap = 900;
    if(app->free_len && app->free_text[app->free_len - 1] != ' ' && !keyer_busy(app) &&
       app->free_last_commit && TIME_REACHED(app->now, app->free_last_commit + word_gap)) {
        free_append(app, ' ');
    }
}

void freekey_draw(Canvas* canvas, App* app) {
    char buf[24];
    Keyer* k = &app->keyer;
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "FREE KEY");
    canvas_set_font(canvas, FontSecondary);
    uint32_t wpm = app->set.key_mode == KeyModeStraight && k->dit_avg ? 1200 / k->dit_avg :
                                                                          app->set.wpm;
    snprintf(buf, sizeof(buf), "%lu WPM", (unsigned long)wpm);
    canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, buf);

    /* text box: the last two lines of what you keyed (monospace font) */
    canvas_draw_rframe(canvas, 0, 12, 128, 27, 3);
    canvas_set_font(canvas, FontKeyboard);
    const uint8_t per_line = 20;
    uint8_t total_lines = (uint8_t)(app->free_len / per_line + 1);
    uint8_t first_line = total_lines > 2 ? total_lines - 2 : 0;
    for(uint8_t l = 0; l < 2; l++) {
        uint8_t start = (uint8_t)((first_line + l) * per_line);
        if(start > app->free_len) break;
        char line[24];
        uint8_t len = app->free_len - start < per_line ? app->free_len - start : per_line;
        memcpy(line, app->free_text + start, len);
        line[len] = '\0';
        canvas_draw_str(canvas, 4, 23 + l * 11, line);
        if(first_line + l == total_lines - 1 && (app->anim / 8) % 2 == 0) {
            int16_t cx = 4 + len * 6;
            canvas_draw_box(canvas, cx, 16 + l * 11, 2, 9);
        }
    }
    if(app->free_len == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignCenter, "Start keying...");
    }

    /* live pattern + what it decodes to so far */
    char pat[PATTERN_MAX + 1];
    memcpy(pat, k->pattern, PATTERN_MAX);
    pat[k->plen] = '\0';
    if(k->phase == KeyOn && k->plen < PATTERN_MAX - 1) {
        pat[k->plen] = k->cur ? '-' : '.';
        pat[k->plen + 1] = '\0';
    }
    if(pat[0]) {
        ui_pattern(canvas, 56, 46, pat, true, 127, false);
        char guess = morse_decode(pat);
        canvas_set_font(canvas, FontPrimary);
        snprintf(buf, sizeof(buf), "=%c", guess ? guess : '?');
        canvas_draw_str_aligned(canvas, 124, 46, AlignRight, AlignCenter, buf);
    } else if(player_busy(app)) {
        ui_waves(canvas, 66, 46, app->anim, true);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 63, "<dit");
    canvas_draw_str_aligned(canvas, 126, 63, AlignRight, AlignBottom, "dah>");
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, "^del vspace");
}
