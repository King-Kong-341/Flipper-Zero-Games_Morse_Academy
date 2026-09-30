#include "morse.h"

/* Everything that makes a Morse signal: tone, LED and vibration are switched
 * directly through the HAL so dits and dahs keep exact timing (notification
 * sequences are queued and would smear short elements).
 *
 * LED colors:  blue = Flipper is sending, cyan = you are keying,
 *              green = correct, red = wrong, magenta = badge, yellow = goal */

#define LIGHT_ALL (LightRed | LightGreen | LightBlue)

uint32_t unit_ms(App* app, uint8_t wpm) {
    if(wpm == 0) wpm = app->set.wpm;
    if(wpm < 5) wpm = 5;
    return 1200U / wpm;
}

/* Gap after a sign and after a word. With Farnsworth spacing the signs keep
 * their full speed but the pauses between them get longer (ARRL formula). */
static void gaps(App* app, uint8_t wpm, bool farnsworth, uint32_t* char_gap, uint32_t* word_gap) {
    uint32_t u = unit_ms(app, wpm);
    uint32_t eff = app->set.eff_wpm;
    if(!farnsworth || eff >= wpm || eff == 0) {
        *char_gap = 3 * u;
        *word_gap = 7 * u;
        return;
    }
    uint32_t ta = (60000U * wpm - 37200U * eff) / (eff * wpm);
    *char_gap = 3 * ta / 19;
    *word_gap = 7 * ta / 19;
}

static float speaker_gain(App* app) {
    return (float)app->set.volume / 100.0f;
}

void signal_set(App* app, bool on, bool from_user) {
    if(on) {
        if(app->set.sound && app->set.volume > 0) {
            if(!app->speaker_owned) app->speaker_owned = furi_hal_speaker_acquire(5);
            if(app->speaker_owned) {
                furi_hal_speaker_start((float)app->set.tone_hz, speaker_gain(app));
            }
        }
        if(app->set.led) {
            furi_hal_light_set(LIGHT_ALL, 0);
            furi_hal_light_set(from_user ? (LightGreen | LightBlue) : LightBlue, 255);
        }
        if(app->set.vibro && !from_user) furi_hal_vibro_on(true);
    } else {
        if(app->speaker_owned) furi_hal_speaker_stop();
        furi_hal_light_set(LIGHT_ALL, 0);
        furi_hal_vibro_on(false);
    }
}

void audio_release(App* app) {
    if(app->speaker_owned) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
        app->speaker_owned = false;
    }
}

/* ---------- Playback ---------- */

static void player_begin(App* app, const char* text, uint8_t wpm, bool farnsworth) {
    fx_stop(app);
    Player* p = &app->player;
    if(p->on) signal_set(app, false, false);
    strncpy(p->text, text, PLAY_MAX);
    p->text[PLAY_MAX] = '\0';
    p->len = (uint8_t)strlen(p->text);
    p->pos = 0;
    while(p->pos < p->len && morse_index(p->text[p->pos]) < 0)
        p->pos++; /* skip spaces and unknown signs at the start */
    p->el = 0;
    p->on = false;
    p->lead_in = true;
    p->wpm_override = wpm;
    p->no_farnsworth = !farnsworth;
    p->active = p->pos < p->len;
    /* short lead-in so the first dit is not swallowed by the key click */
    p->next = furi_get_tick() + 120;
}

void player_start(App* app, const char* text) {
    player_begin(app, text, 0, true);
}

void player_start_fast(App* app, const char* text, uint8_t wpm) {
    player_begin(app, text, wpm, false);
}

void player_stop(App* app) {
    Player* p = &app->player;
    if(p->on) signal_set(app, false, false);
    p->on = false;
    p->active = false;
}

bool player_busy(App* app) {
    return app->player.active;
}

int8_t player_current_char(App* app) {
    return app->player.active ? (int8_t)app->player.pos : -1;
}

int8_t player_current_element(App* app) {
    Player* p = &app->player;
    if(!p->active) return -1;
    return p->on ? (int8_t)p->el : (int8_t)p->el - 1;
}

void player_update(App* app) {
    Player* p = &app->player;
    if(!p->active) return;
    uint32_t now = furi_get_tick();
    if(!TIME_REACHED(now, p->next)) return;

    uint8_t wpm = p->wpm_override ? p->wpm_override : app->set.wpm;
    uint32_t u = unit_ms(app, wpm);
    p->lead_in = false;

    if(p->on) {
        signal_set(app, false, false);
        p->on = false;
        const char* code = morse_code(p->text[p->pos]);
        p->el++;
        if(code[p->el] != '\0') {
            p->next = now + u;
            return;
        }
        /* sign finished - on to the next one */
        p->pos++;
        p->el = 0;
        bool word = false;
        while(p->pos < p->len && (p->text[p->pos] == ' ' || morse_index(p->text[p->pos]) < 0)) {
            if(p->text[p->pos] == ' ') word = true;
            p->pos++;
        }
        if(p->pos >= p->len) {
            p->active = false;
            return;
        }
        uint32_t cg, wg;
        gaps(app, wpm, !p->no_farnsworth, &cg, &wg);
        p->next = now + (word ? wg : cg);
    } else {
        const char* code = morse_code(p->text[p->pos]);
        if(code[0] == '\0') {
            p->active = false;
            return;
        }
        signal_set(app, true, false);
        p->on = true;
        p->next = now + (code[p->el] == '-' ? 3 * u : u);
    }
}

/* ---------- Keyer (your input) ---------- */

static uint32_t commit_delay(App* app) {
    static const uint8_t mult[3] = {3, 5, 8};
    static const uint16_t min_ms[3] = {250, 400, 650};
    uint32_t d = unit_ms(app, 0) * mult[app->set.letter_gap % 3];
    if(d < min_ms[app->set.letter_gap % 3]) d = min_ms[app->set.letter_gap % 3];
    return d;
}

static void keyer_commit(App* app) {
    Keyer* k = &app->keyer;
    k->pattern[k->plen] = '\0';
    memcpy(k->commit_pattern, k->pattern, PATTERN_MAX);
    k->commit_char = morse_decode(k->pattern);
    k->has_commit = true;
    k->plen = 0;
    k->pattern[0] = '\0';
    k->overflow = false;
}

static void keyer_append(App* app, uint8_t element) {
    Keyer* k = &app->keyer;
    if(k->plen < PATTERN_MAX - 1) {
        k->pattern[k->plen++] = element ? '-' : '.';
        k->pattern[k->plen] = '\0';
    }
    /* nothing has more than 6 elements - treat a 7th as "done, unknown" */
    if(k->plen >= PATTERN_MAX - 1) k->overflow = true;
}

void keyer_clear(App* app) {
    Keyer* k = &app->keyer;
    if(k->phase == KeyOn || k->straight_down) signal_set(app, false, true);
    k->plen = 0;
    k->pattern[0] = '\0';
    k->qlen = 0;
    k->held[0] = k->held[1] = false;
    k->phase = KeyIdle;
    k->straight_down = false;
    k->has_commit = false;
    k->overflow = false;
    if(k->dit_avg == 0) k->dit_avg = unit_ms(app, 0);
}

void keyer_enable(App* app, bool enabled) {
    keyer_clear(app);
    app->keyer.enabled = enabled;
    app->keyer.dit_avg = unit_ms(app, 0);
}

static void keyer_start_element(App* app, uint32_t now) {
    Keyer* k = &app->keyer;
    if(k->qlen == 0) return;
    k->cur = k->queue[0];
    for(uint8_t i = 1; i < k->qlen; i++)
        k->queue[i - 1] = k->queue[i];
    k->qlen--;
    fx_stop(app);
    uint32_t u = unit_ms(app, 0);
    k->phase = KeyOn;
    k->phase_end = now + (k->cur ? 3 * u : u);
    signal_set(app, true, true);
}

bool keyer_input(App* app, InputEvent* ev) {
    Keyer* k = &app->keyer;
    if(!k->enabled) return false;
    uint32_t now = furi_get_tick();

    /* Paddles work in both modes: Left = dit, Right = dah */
    if(ev->key == InputKeyLeft || ev->key == InputKeyRight) {
        uint8_t which = ev->key == InputKeyRight ? 1 : 0;
        if(ev->type == InputTypePress) {
            k->held[which] = true;
            if(k->qlen < sizeof(k->queue)) k->queue[k->qlen++] = which;
            if(k->phase == KeyIdle) keyer_start_element(app, now);
        } else if(ev->type == InputTypeRelease) {
            k->held[which] = false;
        }
        return true;
    }

    /* Straight key on OK */
    if(app->set.key_mode == KeyModeStraight && ev->key == InputKeyOk) {
        if(ev->type == InputTypePress && k->phase == KeyIdle) {
            fx_stop(app);
            k->straight_down = true;
            k->down_at = now;
            signal_set(app, true, true);
        } else if(ev->type == InputTypeRelease && k->straight_down) {
            k->straight_down = false;
            signal_set(app, false, true);
            uint32_t dur = now - k->down_at;
            uint32_t threshold = k->dit_avg * 2;
            if(dur < threshold) {
                keyer_append(app, 0);
                k->dit_avg = (k->dit_avg * 3 + dur) / 4;
            } else {
                keyer_append(app, 1);
                k->dit_avg = (k->dit_avg * 3 + dur / 3) / 4;
            }
            if(k->dit_avg < 30) k->dit_avg = 30;
            if(k->dit_avg > 400) k->dit_avg = 400;
            k->last_end = now;
        }
        return true;
    }
    return false;
}

void keyer_update(App* app) {
    Keyer* k = &app->keyer;
    if(!k->enabled) return;
    uint32_t now = furi_get_tick();

    if(k->phase == KeyOn && TIME_REACHED(now, k->phase_end)) {
        signal_set(app, false, true);
        keyer_append(app, k->cur);
        k->phase = KeyGap;
        k->phase_end = now + unit_ms(app, 0);
    }
    if(k->phase == KeyGap && TIME_REACHED(now, k->phase_end)) {
        if(k->qlen == 0 && (k->held[0] || k->held[1])) {
            /* paddle held down: repeat it (both held = alternate, iambic) */
            uint8_t next = k->held[0] && k->held[1] ? (uint8_t)!k->cur : (k->held[1] ? 1 : 0);
            k->queue[k->qlen++] = next;
        }
        if(k->qlen > 0 && !k->overflow) {
            keyer_start_element(app, now);
        } else {
            k->qlen = 0;
            k->phase = KeyIdle;
            k->last_end = now;
        }
    }
    if(k->phase == KeyIdle && !k->straight_down && k->plen > 0 && !k->has_commit) {
        if(k->overflow || TIME_REACHED(now, k->last_end + commit_delay(app))) {
            keyer_commit(app);
        }
    }
}

bool keyer_take_commit(App* app, char* out_char, char* out_pattern) {
    Keyer* k = &app->keyer;
    if(!k->has_commit) return false;
    if(out_char) *out_char = k->commit_char;
    if(out_pattern) memcpy(out_pattern, k->commit_pattern, PATTERN_MAX);
    k->has_commit = false;
    return true;
}

/* OK in paddle mode: finish the letter right now instead of waiting. */
bool keyer_force_commit(App* app) {
    Keyer* k = &app->keyer;
    if(k->plen == 0 || k->straight_down) return false;
    if(k->phase == KeyOn) {
        signal_set(app, false, true);
        keyer_append(app, k->cur);
    }
    k->phase = KeyIdle;
    k->qlen = 0;
    keyer_commit(app);
    return true;
}

bool keyer_busy(App* app) {
    Keyer* k = &app->keyer;
    return k->phase != KeyIdle || k->straight_down || k->plen > 0;
}

/* ---------- Jingles ---------- */

void fx_play(App* app, const Note* notes, uint8_t count, uint8_t led, uint16_t vib_ms) {
    Fx* f = &app->fx;
    if(app->player.active) player_stop(app);
    f->notes = notes;
    f->count = count;
    f->idx = 0;
    f->active = true;
    f->led = app->set.led ? led : 0;
    uint32_t now = furi_get_tick();
    if(f->led) {
        furi_hal_light_set(LIGHT_ALL, 0);
        furi_hal_light_set(f->led, 255);
    }
    if(app->set.vibro && vib_ms > 0) {
        furi_hal_vibro_on(true);
        f->vib_on = true;
        f->vib_until = now + vib_ms;
    }
    if(app->set.sound && app->set.volume > 0 && notes[0].freq > 0) {
        if(!app->speaker_owned) app->speaker_owned = furi_hal_speaker_acquire(5);
        if(app->speaker_owned) furi_hal_speaker_start((float)notes[0].freq, speaker_gain(app));
    }
    f->next = now + notes[0].ms;
}

void fx_stop(App* app) {
    Fx* f = &app->fx;
    if(f->active) {
        if(app->speaker_owned) furi_hal_speaker_stop();
        if(f->led) furi_hal_light_set(LIGHT_ALL, 0);
        f->active = false;
    }
    if(f->vib_on) {
        furi_hal_vibro_on(false);
        f->vib_on = false;
    }
}

void fx_update(App* app) {
    Fx* f = &app->fx;
    uint32_t now = furi_get_tick();
    if(f->vib_on && TIME_REACHED(now, f->vib_until)) {
        furi_hal_vibro_on(false);
        f->vib_on = false;
    }
    if(!f->active || !TIME_REACHED(now, f->next)) return;
    f->idx++;
    if(f->idx >= f->count) {
        if(app->speaker_owned) furi_hal_speaker_stop();
        if(f->led) furi_hal_light_set(LIGHT_ALL, 0);
        f->active = false;
        return;
    }
    const Note* n = &f->notes[f->idx];
    if(app->speaker_owned) {
        if(n->freq > 0 && app->set.sound && app->set.volume > 0) {
            furi_hal_speaker_start((float)n->freq, speaker_gain(app));
        } else {
            furi_hal_speaker_stop();
        }
    }
    f->next = now + n->ms;
}

#define NOTES(arr) arr, (uint8_t)(sizeof(arr) / sizeof(arr[0]))

static const Note n_correct[] = {{1047, 70}, {1319, 110}};
static const Note n_wrong[] = {{330, 130}, {0, 30}, {220, 240}};
static const Note n_success[] = {{523, 90}, {659, 90}, {784, 90}, {1047, 260}};
static const Note n_fail[] = {{523, 110}, {440, 110}, {349, 110}, {262, 300}};
static const Note n_badge[] = {{784, 80}, {988, 80}, {1175, 80}, {1568, 280}};
static const Note n_goal[] = {{659, 90}, {784, 90}, {1047, 90}, {0, 40}, {1047, 200}};
static const Note n_click[] = {{1400, 12}};
static const Note n_zap[] = {{1760, 35}, {2349, 55}};
static const Note n_boom[] = {{196, 90}, {147, 180}};
static const Note n_miss[] = {{262, 90}};

void fx_correct(App* app) {
    fx_play(app, NOTES(n_correct), LightGreen, 0);
}
void fx_wrong(App* app) {
    fx_play(app, NOTES(n_wrong), LightRed, 180);
}
void fx_success(App* app) {
    fx_play(app, NOTES(n_success), LightGreen, 150);
}
void fx_fail(App* app) {
    fx_play(app, NOTES(n_fail), LightRed, 250);
}
void fx_badge(App* app) {
    fx_play(app, NOTES(n_badge), LightRed | LightBlue, 120);
}
void fx_goal(App* app) {
    fx_play(app, NOTES(n_goal), LightRed | LightGreen, 120);
}
void fx_click(App* app) {
    fx_play(app, NOTES(n_click), 0, 0);
}
void fx_zap(App* app) {
    fx_play(app, NOTES(n_zap), LightGreen, 0);
}
void fx_boom(App* app) {
    fx_play(app, NOTES(n_boom), LightRed, 220);
}
void fx_miss(App* app) {
    fx_play(app, NOTES(n_miss), LightRed, 0);
}
void fx_preview(App* app) {
    player_start_fast(app, "A", app->set.wpm);
}

void all_signals_off(App* app) {
    player_stop(app);
    fx_stop(app);
    Keyer* k = &app->keyer;
    k->phase = KeyIdle;
    k->straight_down = false;
    k->qlen = 0;
    if(app->speaker_owned) furi_hal_speaker_stop();
    furi_hal_light_set(LIGHT_ALL, 0);
    furi_hal_vibro_on(false);
}

/* How long the main loop may sleep before the next timing-critical event. */
uint32_t next_deadline(App* app) {
    uint32_t now = furi_get_tick();
    int32_t best = 33; /* ~30 fps animation */
    int32_t d;
    if(app->player.active) {
        d = (int32_t)(app->player.next - now);
        if(d < best) best = d;
    }
    if(app->keyer.enabled && app->keyer.phase != KeyIdle) {
        d = (int32_t)(app->keyer.phase_end - now);
        if(d < best) best = d;
    }
    if(app->fx.active) {
        d = (int32_t)(app->fx.next - now);
        if(d < best) best = d;
    }
    if(app->fx.vib_on) {
        d = (int32_t)(app->fx.vib_until - now);
        if(d < best) best = d;
    }
    if(best < 1) best = 1;
    return (uint32_t)best;
}
