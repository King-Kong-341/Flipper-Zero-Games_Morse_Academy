#include "morse.h"

/* The learning engine. A session is a list of steps, generated fresh (and
 * shuffled) every time, so no two runs are the same:
 *
 *   Tip       - short explanation of the lesson
 *   Intro     - meet a new sign: see it, hear it, feel it
 *   Guided    - key the new sign yourself, pattern shown
 *   Listen    - hear a sign, pick it with the arrow keys
 *   Send      - key the shown sign from memory
 *   WordListen / WordSend / CallListen - the same with words and callsigns
 *
 * Wrong answers come back at the end of the session. Weak signs (low
 * mastery) are picked more often. */

#define RIGHT_MS 700
#define XP_LESSON 10
#define XP_PRACTICE 5
#define PRACTICE_ITEMS 20
#define CALL_ITEMS 12

/* ---------- Pools & picking ---------- */

static uint8_t lesson_pool(uint8_t lesson, char* out) {
    uint8_t n = 0;
    if(lesson >= FINAL_LESSON) {
        for(uint8_t i = 0; i < CHAR_COUNT; i++)
            out[n++] = morse_table[i].ch;
    } else {
        for(uint8_t l = 0; l <= lesson; l++) {
            for(const char* c = lessons[l].chars; *c; c++)
                out[n++] = *c;
        }
    }
    out[n] = '\0';
    return n;
}

/* Random sign from 'pool', weaker signs are more likely. */
char pick_weighted(App* app, const char* pool, char avoid) {
    uint8_t n = (uint8_t)strlen(pool);
    if(n == 0) return 'E';
    uint32_t total = 0;
    uint16_t weights[CHAR_COUNT];
    for(uint8_t i = 0; i < n && i < CHAR_COUNT; i++) {
        int idx = morse_index(pool[i]);
        uint16_t w = (uint16_t)(125 - (idx >= 0 ? app->prog.mastery[idx] : 0));
        if(pool[i] == avoid && n > 1) w = 0;
        weights[i] = w;
        total += w;
    }
    if(total == 0) return pool[rnd(n)];
    uint32_t r = rnd(total);
    for(uint8_t i = 0; i < n && i < CHAR_COUNT; i++) {
        if(r < weights[i]) return pool[i];
        r -= weights[i];
    }
    return pool[n - 1];
}

/* How easily two signs are confused: same length and same start count most. */
static uint8_t similarity(const char* a, const char* b) {
    uint8_t s = 0;
    size_t la = strlen(a), lb = strlen(b);
    if(la == lb) s += 3;
    for(size_t i = 0; i < la && i < lb; i++) {
        if(a[i] == b[i]) s++;
    }
    return s;
}

/* Picks up to 'want' wrong answers for 'target' out of 'pool'. Mostly
 * look-alikes (that is where the learning happens), sometimes random. */
uint8_t pick_distractors(char target, const char* pool, char* out, uint8_t want) {
    char cand[CHAR_COUNT];
    uint8_t score[CHAR_COUNT];
    uint8_t n = 0;
    const char* tcode = morse_code(target);
    for(const char* c = pool; *c && n < CHAR_COUNT; c++) {
        if(*c == target) continue;
        cand[n] = *c;
        score[n] = similarity(tcode, morse_code(*c));
        n++;
    }
    /* sort by similarity, highest first (tiny list, bubble sort is fine) */
    for(uint8_t i = 0; i + 1 < n; i++) {
        for(uint8_t j = 0; j + 1 < n - i; j++) {
            if(score[j] < score[j + 1]) {
                uint8_t ts = score[j];
                score[j] = score[j + 1];
                score[j + 1] = ts;
                char tc = cand[j];
                cand[j] = cand[j + 1];
                cand[j + 1] = tc;
            }
        }
    }
    uint8_t got = 0;
    bool used[CHAR_COUNT] = {false};
    while(got < want && got < n) {
        uint8_t pick;
        if(rnd(100) < 65) {
            uint8_t top = n < 4 ? n : 4;
            pick = (uint8_t)rnd(top);
        } else {
            pick = (uint8_t)rnd(n);
        }
        if(used[pick]) {
            /* take the next free one instead */
            for(uint8_t k = 0; k < n; k++) {
                if(!used[k]) {
                    pick = k;
                    break;
                }
            }
        }
        used[pick] = true;
        out[got++] = cand[pick];
    }
    return got;
}

static bool word_fits(const char* w, const char* pool, uint8_t max_len) {
    if(strlen(w) > max_len) return false;
    for(const char* c = w; *c; c++) {
        if(strchr(pool, *c) == NULL) return false;
    }
    return true;
}

static uint16_t collect_words(const char* pool, uint8_t max_len, uint16_t* out, uint16_t max_out) {
    uint16_t n = 0;
    for(uint16_t i = 0; i < word_count && n < max_out; i++) {
        if(word_fits(word_list[i], pool, max_len)) out[n++] = i;
    }
    return n;
}

uint16_t words_available(App* app, uint8_t max_len) {
    char pool[CHAR_COUNT + 1];
    pool_chars(app, pool, false);
    uint16_t n = 0;
    for(uint16_t i = 0; i < word_count; i++) {
        if(word_fits(word_list[i], pool, max_len)) n++;
    }
    return n;
}

bool practice_mode_available(App* app, PracticeMode mode) {
    switch(mode) {
    case PracWordListen:
        return words_available(app, 6) >= 4;
    case PracWordSend:
        return words_available(app, 5) >= 4;
    case PracCalls:
        return app->prog.stars[15] > 0; /* numbers learned */
    default:
        return true;
    }
}

static void make_callsign(char* out) {
    static const char* const formats[] = {"LDLLL", "LLDLL", "LDLL", "LLDLLL", "LDL"};
    const char* f = formats[rnd(5)];
    uint8_t i = 0;
    for(; f[i]; i++) {
        out[i] = f[i] == 'L' ? (char)('A' + rnd(26)) : (char)('0' + rnd(10));
    }
    out[i] = '\0';
}

/* ---------- Session building ---------- */

static void add_step(Quiz* q, uint8_t type, const char* text) {
    if(q->count >= MAX_STEPS) return;
    Step* s = &q->steps[q->count++];
    s->type = type;
    s->retry = 0;
    strncpy(s->text, text, STEP_TEXT - 1);
    s->text[STEP_TEXT - 1] = '\0';
}

static void add_char_step(Quiz* q, uint8_t type, char c) {
    char t[2] = {c, '\0'};
    add_step(q, type, t);
}

static void shuffle_types(uint8_t* arr, uint8_t n) {
    for(uint8_t i = n; i > 1; i--) {
        uint8_t j = (uint8_t)rnd(i);
        uint8_t t = arr[i - 1];
        arr[i - 1] = arr[j];
        arr[j] = t;
    }
}

/* Adds 'n' mixed quiz items. 'favor' signs (the new ones) show up more. */
static void add_mixed_items(
    App* app,
    Quiz* q,
    const char* pool,
    const char* favor,
    uint8_t listen,
    uint8_t send,
    uint8_t word_listen,
    uint8_t word_send) {
    uint8_t types[40];
    uint8_t n = 0;
    for(uint8_t i = 0; i < listen && n < 40; i++)
        types[n++] = StepListen;
    for(uint8_t i = 0; i < send && n < 40; i++)
        types[n++] = StepSend;
    for(uint8_t i = 0; i < word_listen && n < 40; i++)
        types[n++] = StepWordListen;
    for(uint8_t i = 0; i < word_send && n < 40; i++)
        types[n++] = StepWordSend;
    shuffle_types(types, n);

    uint16_t words6[260];
    uint16_t words5[260];
    uint16_t w6 = collect_words(pool, 6, words6, 260);
    uint16_t w5 = collect_words(pool, 5, words5, 260);

    char last = 0;
    uint16_t last_word = 0xFFFF;
    for(uint8_t i = 0; i < n; i++) {
        if(types[i] == StepWordListen || types[i] == StepWordSend) {
            uint16_t* list = types[i] == StepWordListen ? words6 : words5;
            uint16_t cnt = types[i] == StepWordListen ? w6 : w5;
            if(cnt == 0) continue;
            uint16_t w = list[rnd(cnt)];
            if(w == last_word && cnt > 1) w = list[rnd(cnt)];
            last_word = w;
            add_step(q, types[i], word_list[w]);
        } else {
            char c;
            if(favor && favor[0] && rnd(100) < 55) {
                c = favor[rnd(strlen(favor))];
                if(c == last && strlen(favor) > 1) c = favor[rnd(strlen(favor))];
            } else {
                c = pick_weighted(app, pool, last);
            }
            last = c;
            add_char_step(q, types[i], c);
        }
    }
}

static void step_enter(App* app);

static void quiz_reset(App* app, QuizKind kind) {
    Quiz* q = &app->quiz;
    memset(q, 0, sizeof(Quiz));
    q->kind = kind;
    all_signals_off(app);
}

static void quiz_begin(App* app) {
    go_screen(app, ScrQuiz);
    app->quiz.cur = 0;
    step_enter(app);
}

void quiz_start_lesson(App* app, uint8_t lesson) {
    quiz_reset(app, QuizLesson);
    Quiz* q = &app->quiz;
    q->lesson = lesson;
    char pool[CHAR_COUNT + 1];
    lesson_pool(lesson, pool);
    const char* news = lessons[lesson].chars;

    add_step(q, StepTip, "");
    for(const char* c = news; *c; c++) {
        add_char_step(q, StepIntro, *c);
        add_char_step(q, StepGuided, *c);
    }

    uint16_t tmp[260];
    bool words = collect_words(pool, 6, tmp, 260) >= 4;
    bool final = lesson == FINAL_LESSON;
    add_mixed_items(
        app,
        q,
        pool,
        final ? NULL : news,
        final ? 10 : 7,
        final ? 6 : 5,
        words ? (final ? 5 : 2) : 0,
        words ? (final ? 3 : 1) : 0);
    quiz_begin(app);
}

void quiz_start_practice(App* app, PracticeMode mode) {
    quiz_reset(app, QuizPractice);
    Quiz* q = &app->quiz;
    q->mode = mode;
    char pool[CHAR_COUNT + 1];
    uint8_t n = pool_chars(app, pool, false);

    switch(mode) {
    case PracListen:
        add_mixed_items(app, q, pool, NULL, PRACTICE_ITEMS, 0, 0, 0);
        break;
    case PracSend:
        add_mixed_items(app, q, pool, NULL, 0, PRACTICE_ITEMS - 6, 0, 0);
        break;
    case PracWordListen:
        add_mixed_items(app, q, pool, NULL, 0, 0, PRACTICE_ITEMS - 6, 0);
        break;
    case PracWordSend:
        add_mixed_items(app, q, pool, NULL, 0, 0, 0, 8);
        break;
    case PracWeak: {
        /* the 6 learned signs with the lowest mastery */
        char weak[7];
        uint8_t wn = 0;
        bool taken[CHAR_COUNT + 1] = {false};
        while(wn < 6 && wn < n) {
            int best = -1;
            uint8_t best_m = 255;
            for(uint8_t i = 0; i < n; i++) {
                if(taken[i]) continue;
                int idx = morse_index(pool[i]);
                uint8_t m = idx >= 0 ? app->prog.mastery[idx] : 0;
                if(m < best_m) {
                    best_m = m;
                    best = i;
                }
            }
            if(best < 0) break;
            taken[best] = true;
            weak[wn++] = pool[best];
        }
        weak[wn] = '\0';
        /* distractors still come from everything learned */
        add_mixed_items(app, q, pool, weak, 10, 6, 0, 0);
        /* make sure the favored signs really dominate */
        for(uint8_t i = 0; i < q->count; i++) {
            if(rnd(100) < 70) q->steps[i].text[0] = weak[rnd(wn)];
        }
        break;
    }
    case PracCalls:
        for(uint8_t i = 0; i < CALL_ITEMS; i++) {
            char call[STEP_TEXT];
            make_callsign(call);
            add_step(q, StepCallListen, call);
        }
        break;
    default:
        break;
    }
    quiz_begin(app);
}

void quiz_start_placement(App* app) {
    quiz_reset(app, QuizPlacement);
    Quiz* q = &app->quiz;
    for(uint8_t l = 0; l < FINAL_LESSON; l++) {
        const char* chars = lessons[l].chars;
        uint8_t len = (uint8_t)strlen(chars);
        uint8_t a = (uint8_t)rnd(len);
        uint8_t b = (uint8_t)rnd(len);
        if(b == a) b = (uint8_t)((a + 1) % len);
        add_char_step(q, StepListen, chars[a]);
        add_char_step(q, StepListen, chars[b]);
    }
    quiz_begin(app);
}

/* ---------- Step flow ---------- */

static Step* cur_step(App* app) {
    return &app->quiz.steps[app->quiz.cur];
}

static bool step_uses_keyer(uint8_t type) {
    return type == StepGuided || type == StepSend || type == StepWordSend;
}

static void set_choices(Quiz* q, char opts[4][STEP_TEXT], uint8_t n) {
    static const uint8_t slots2[2] = {3, 1};
    static const uint8_t slots3[3] = {3, 0, 1};
    static const uint8_t slots4[4] = {0, 1, 2, 3};
    const uint8_t* slots = n <= 2 ? slots2 : (n == 3 ? slots3 : slots4);
    memset(q->options, 0, sizeof(q->options));
    memset(q->slot_used, 0, sizeof(q->slot_used));
    uint8_t perm[4] = {0, 1, 2, 3};
    shuffle_types(perm, n);
    for(uint8_t i = 0; i < n; i++) {
        uint8_t slot = slots[i];
        memcpy(q->options[slot], opts[perm[i]], STEP_TEXT);
        q->slot_used[slot] = true;
        if(perm[i] == 0) q->answer_slot = slot;
    }
}

static void build_choices(App* app) {
    Quiz* q = &app->quiz;
    Step* s = cur_step(app);
    char opts[4][STEP_TEXT];
    memset(opts, 0, sizeof(opts));
    strncpy(opts[0], s->text, STEP_TEXT - 1);
    uint8_t n = 1;

    if(s->type == StepListen) {
        char pool[CHAR_COUNT + 1];
        if(q->kind == QuizPractice) {
            pool_chars(app, pool, false);
        } else {
            uint8_t lesson = q->kind == QuizPlacement ? q->cur / 2 : q->lesson;
            lesson_pool(lesson, pool);
        }
        char d[3];
        uint8_t got = pick_distractors(s->text[0], pool, d, 3);
        for(uint8_t i = 0; i < got; i++) {
            opts[n][0] = d[i];
            opts[n][1] = '\0';
            n++;
        }
    } else if(s->type == StepWordListen) {
        char pool[CHAR_COUNT + 1];
        if(q->kind == QuizPractice) {
            pool_chars(app, pool, false);
        } else {
            lesson_pool(q->lesson, pool);
        }
        uint16_t words[260];
        uint16_t cnt = collect_words(pool, 6, words, 260);
        size_t tlen = strlen(s->text);
        /* first try same-length words, then anything */
        for(uint8_t pass = 0; pass < 2 && n < 4; pass++) {
            for(uint8_t tries = 0; tries < 60 && n < 4 && cnt > 1; tries++) {
                const char* w = word_list[words[rnd(cnt)]];
                if(pass == 0 && strlen(w) != tlen) continue;
                bool dup = false;
                for(uint8_t k = 0; k < n; k++) {
                    if(strcmp(opts[k], w) == 0) dup = true;
                }
                if(dup) continue;
                strncpy(opts[n], w, STEP_TEXT - 1);
                n++;
            }
        }
    } else if(s->type == StepCallListen) {
        /* look-alike callsigns: one or two characters changed */
        for(uint8_t tries = 0; tries < 40 && n < 4; tries++) {
            char c[STEP_TEXT];
            strncpy(c, s->text, STEP_TEXT - 1);
            c[STEP_TEXT - 1] = '\0';
            size_t len = strlen(c);
            uint8_t changes = 1 + (uint8_t)rnd(2);
            for(uint8_t k = 0; k < changes; k++) {
                size_t pos = rnd((uint32_t)len);
                if(c[pos] >= '0' && c[pos] <= '9') {
                    c[pos] = (char)('0' + rnd(10));
                } else {
                    char d[1];
                    char pool[CHAR_COUNT + 1];
                    lesson_pool(12, pool); /* all letters */
                    if(pick_distractors(c[pos], pool, d, 1) == 1) c[pos] = d[0];
                }
            }
            bool dup = false;
            for(uint8_t k = 0; k < n; k++) {
                if(strcmp(opts[k], c) == 0) dup = true;
            }
            if(dup) continue;
            memcpy(opts[n], c, STEP_TEXT);
            n++;
        }
    }
    set_choices(q, opts, n);
}

static void schedule_audio(App* app, uint32_t delay) {
    app->quiz.autoplay = true;
    app->quiz.autoplay_at = furi_get_tick() + delay;
}

static void play_step_audio(App* app) {
    Step* s = cur_step(app);
    app->quiz.autoplay = false;
    player_start(app, s->text);
}

static void step_enter(App* app) {
    Quiz* q = &app->quiz;
    Step* s = cur_step(app);
    q->state = QAsk;
    q->chosen = -1;
    q->tries = 0;
    q->hinted = false;
    q->typed_len = 0;
    q->typed[0] = '\0';
    q->wrong_char = 0;
    q->wrong_pattern[0] = '\0';
    q->autoplay = false;
    q->step_scored = false;
    q->step_requeued = false;
    keyer_enable(app, step_uses_keyer(s->type));

    switch(s->type) {
    case StepIntro:
        schedule_audio(app, 450);
        break;
    case StepGuided:
        schedule_audio(app, 350);
        break;
    case StepListen:
    case StepWordListen:
    case StepCallListen:
        build_choices(app);
        schedule_audio(app, 350);
        break;
    default:
        break;
    }
}

static void requeue_once(App* app) {
    Quiz* q = &app->quiz;
    if(q->step_requeued || q->kind == QuizPlacement) return;
    q->step_requeued = true;
    if(q->requeued >= 6 || q->count >= MAX_STEPS) return;
    Step copy = *cur_step(app);
    copy.retry = 1;
    q->steps[q->count++] = copy;
    q->requeued++;
}

static void score_once(App* app, bool ok) {
    Quiz* q = &app->quiz;
    if(q->step_scored) return;
    q->step_scored = true;
    Step* s = cur_step(app);
    if(s->retry) return; /* repeated mistakes don't count twice */
    q->scored++;
    if(ok) {
        q->first_ok++;
        q->run++;
        if(q->run > q->max_run) q->max_run = q->run;
        if(q->kind == QuizLesson) q->xp_gained += XP_LESSON;
        if(q->kind == QuizPractice) q->xp_gained += XP_PRACTICE;
    } else {
        q->run = 0;
    }
}

static void quiz_finish(App* app);

static void next_step(App* app) {
    Quiz* q = &app->quiz;
    player_stop(app);
    if(q->kind == QuizPlacement && q->placement_failed) {
        q->placement_lesson = q->cur / 2;
        quiz_finish(app);
        return;
    }
    q->cur++;
    if(q->cur >= q->count) {
        if(q->kind == QuizPlacement) q->placement_lesson = FINAL_LESSON;
        quiz_finish(app);
        return;
    }
    step_enter(app);
}

static void answer_choice(App* app, uint8_t slot) {
    Quiz* q = &app->quiz;
    Step* s = cur_step(app);
    if(!q->slot_used[slot]) return;
    bool ok = slot == q->answer_slot;
    q->chosen = (int8_t)slot;
    q->autoplay = false;
    player_stop(app);
    score_once(app, ok);
    if(s->type == StepListen) record_answer(app, s->text[0], ok);
    if(s->type == StepWordListen && ok && app->prog.words_correct < 60000) {
        app->prog.words_correct++;
    }
    if(ok) {
        fx_correct(app);
        q->state = QRight;
        q->state_until = furi_get_tick() + RIGHT_MS;
    } else {
        fx_wrong(app);
        q->state = QWrong;
        requeue_once(app);
        if(q->kind == QuizPlacement) q->placement_failed = true;
        /* play the right answer again so the ear connects the sound */
        schedule_audio(app, 700);
    }
}

static void handle_commit(App* app, char c, const char* pattern) {
    Quiz* q = &app->quiz;
    Step* s = cur_step(app);
    char target = s->type == StepWordSend ? s->text[q->typed_len] : s->text[0];
    bool ok = c == target;

    if(ok) {
        if(s->type == StepWordSend) {
            q->typed[q->typed_len++] = c;
            q->typed[q->typed_len] = '\0';
            q->wrong_char = 0;
            q->wrong_pattern[0] = '\0';
            if(q->typed_len < strlen(s->text)) {
                fx_click(app);
                q->state = QAsk;
                return;
            }
            if(app->prog.words_correct < 60000) app->prog.words_correct++;
        }
        if(s->type != StepGuided) {
            score_once(app, !q->hinted);
            if(s->type == StepSend && !q->step_requeued) record_answer(app, target, !q->hinted);
        }
        fx_correct(app);
        q->state = QRight;
        q->state_until = furi_get_tick() + RIGHT_MS;
        return;
    }

    q->tries++;
    q->wrong_char = c;
    memcpy(q->wrong_pattern, pattern, PATTERN_MAX);
    fx_wrong(app);
    if(s->type == StepGuided) {
        q->state = QTryAgain;
        return;
    }
    if(!q->step_scored) {
        score_once(app, false);
        if(s->type == StepSend) record_answer(app, target, false);
    }
    requeue_once(app);
    uint8_t max_tries = s->type == StepWordSend ? 3 : 2;
    if(q->tries >= max_tries) {
        q->state = QWrong; /* reveal the answer */
        keyer_clear(app);
        schedule_audio(app, 600);
    } else {
        q->state = QTryAgain;
    }
}

void quiz_tick(App* app) {
    Quiz* q = &app->quiz;
    if(q->paused) return;
    uint32_t now = app->now;
    Step* s = cur_step(app);

    if(q->autoplay && TIME_REACHED(now, q->autoplay_at) && !keyer_busy(app)) {
        q->autoplay = false;
        play_step_audio(app);
    }

    if(q->state == QRight && TIME_REACHED(now, q->state_until) && !app->fx.active) {
        next_step(app);
        return;
    }

    if(step_uses_keyer(s->type) && (q->state == QAsk || q->state == QTryAgain)) {
        if(q->state == QTryAgain && keyer_busy(app)) q->state = QAsk;
        char c;
        char pat[PATTERN_MAX];
        if(keyer_take_commit(app, &c, pat)) handle_commit(app, c, pat);
    }
}

/* ---------- Finish & results ---------- */

static void check_lesson_badges(App* app, uint8_t stars) {
    Progress* p = &app->prog;
    if(p->stars[0]) award_badge(app, BadgeFirstSteps);
    if(p->stars[3]) award_badge(app, BadgeSos);
    if(p->stars[12]) award_badge(app, BadgeAlphabet);
    if(p->stars[15]) award_badge(app, BadgeNumbers);
    if(p->stars[FINAL_LESSON]) award_badge(app, BadgeGraduate);
    if(stars >= 3) award_badge(app, BadgePerfect);
}

static void quiz_finish(App* app) {
    Quiz* q = &app->quiz;
    Progress* p = &app->prog;
    q->accuracy = q->scored ? (uint8_t)(q->first_ok * 100U / q->scored) : 0;

    go_screen(app, ScrResults);
    q->result_anim_start = furi_get_tick();

    if(q->kind == QuizLesson) {
        uint8_t acc = q->accuracy;
        q->stars = acc >= 95 ? 3 : (acc >= 85 ? 2 : (acc >= 70 ? 1 : 0));
        q->passed = q->stars > 0;
        if(q->passed) {
            q->xp_gained += 20 + 10 * q->stars;
            if(q->stars > p->stars[q->lesson]) {
                q->new_best_stars = p->stars[q->lesson] > 0;
                p->stars[q->lesson] = q->stars;
            }
            if(q->lesson == p->unlocked && p->unlocked < FINAL_LESSON) p->unlocked++;
            check_lesson_badges(app, q->stars);
            fx_success(app);
        } else {
            fx_fail(app);
        }
    } else if(q->kind == QuizPractice) {
        q->passed = true;
        q->xp_gained += 10;
        fx_success(app);
    } else {
        /* placement: every lesson before the first miss counts as done */
        q->passed = true;
        q->placement_done = true;
        uint8_t start = q->placement_lesson;
        for(uint8_t l = 0; l < start && l < FINAL_LESSON; l++) {
            if(p->stars[l] == 0) p->stars[l] = 1;
        }
        if(start > p->unlocked) p->unlocked = start;
        if(p->unlocked > FINAL_LESSON) p->unlocked = FINAL_LESSON;
        app->course_sel = p->unlocked;
        check_lesson_badges(app, 0);
        fx_success(app);
    }

    if(q->max_run > p->best_run) p->best_run = q->max_run;
    if(q->max_run >= 20) award_badge(app, BadgeSharpEar);
    if(p->words_correct >= 50) award_badge(app, BadgeWordsmith);
    if(app->set.wpm >= 20 && q->scored >= 10 && q->passed) award_badge(app, BadgeSpeed);
    if(p->sessions < 60000) p->sessions++;
    add_xp(app, q->xp_gained);
    progress_save(app);
}

void results_input(App* app, InputEvent* ev) {
    if(ev->type != InputTypeShort) return;
    Quiz* q = &app->quiz;
    if(ev->key == InputKeyOk) {
        if(q->kind == QuizLesson) {
            if(q->passed) {
                app->course_sel = app->prog.unlocked;
                go_screen(app, ScrCourse);
            } else {
                quiz_start_lesson(app, q->lesson);
            }
        } else if(q->kind == QuizPractice) {
            quiz_start_practice(app, (PracticeMode)q->mode);
        } else {
            go_screen(app, ScrCourse);
        }
    } else if(ev->key == InputKeyLeft) {
        if(q->kind == QuizLesson) {
            quiz_start_lesson(app, q->lesson);
        } else if(q->kind == QuizPractice) {
            quiz_start_practice(app, (PracticeMode)q->mode);
        }
    } else if(ev->key == InputKeyBack) {
        if(q->kind == QuizPractice) {
            go_screen(app, ScrPracticeMenu);
        } else if(q->kind == QuizLesson) {
            go_screen(app, ScrCourse);
        } else {
            go_screen(app, ScrMenu);
        }
    }
}

void results_draw(Canvas* canvas, App* app) {
    Quiz* q = &app->quiz;
    uint32_t age = app->now - q->result_anim_start;
    char buf[32];

    if(q->kind == QuizPlacement) {
        ui_frame(canvas, "PLACEMENT DONE");
        canvas_set_font(canvas, FontSecondary);
        if(q->placement_lesson >= FINAL_LESSON) {
            canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Wow - you know");
            canvas_draw_str_aligned(canvas, 64, 33, AlignCenter, AlignCenter, "every sign already!");
            canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "Try the final exam.");
        } else {
            canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignCenter, "Your start:");
            snprintf(buf, sizeof(buf), "Lesson %u", q->placement_lesson + 1);
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, buf);
            const char* chars = lessons[q->placement_lesson].chars;
            uint8_t n = (uint8_t)strlen(chars);
            int16_t w = n * 10 + (n - 1) * 4;
            int16_t x = 64 - w / 2;
            for(uint8_t i = 0; i < n; i++) {
                ui_big_char(canvas, x + i * 14, 38, chars[i], 2);
            }
        }
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 124, 60, AlignRight, AlignBottom, "OK >");
        return;
    }

    if(q->kind == QuizLesson) {
        ui_frame(canvas, q->passed ? "LESSON DONE!" : "ALMOST THERE");
        /* stars pop in one after another */
        for(uint8_t i = 0; i < 3; i++) {
            bool shown = age > 250U + i * 300U;
            bool filled = shown && i < q->stars;
            int16_t x = 39 + i * 18;
            int16_t y = 17;
            if(shown && filled && age < 250U + i * 300U + 120U) y -= 2; /* little hop */
            ui_star_big(canvas, x, y, filled);
        }
    } else {
        ui_frame(canvas, "PRACTICE DONE");
        snprintf(buf, sizeof(buf), "%u", q->accuracy);
        canvas_set_font(canvas, FontBigNumbers);
        uint16_t w = canvas_string_width(canvas, buf);
        canvas_draw_str(canvas, 64 - (int16_t)(w + 8) / 2, 34, buf);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 64 - (int16_t)(w + 8) / 2 + (int16_t)w + 1, 34, "%");
    }

    canvas_set_font(canvas, FontSecondary);
    /* counting-up XP */
    uint32_t shown_xp = q->xp_gained;
    if(age < 900) shown_xp = q->xp_gained * age / 900;
    if(q->kind == QuizLesson) {
        snprintf(buf, sizeof(buf), "Accuracy %u%%", q->accuracy);
        canvas_draw_str(canvas, 8, 42, buf);
        snprintf(buf, sizeof(buf), "+%lu XP", (unsigned long)shown_xp);
        canvas_draw_str_aligned(canvas, 120, 42, AlignRight, AlignBottom, buf);
    } else {
        snprintf(buf, sizeof(buf), "%u/%u right", q->first_ok, q->scored);
        canvas_draw_str(canvas, 8, 42, buf);
        snprintf(buf, sizeof(buf), "+%lu XP", (unsigned long)shown_xp);
        canvas_draw_str_aligned(canvas, 120, 42, AlignRight, AlignBottom, buf);
    }

    /* streak line */
    ui_flame(canvas, 8, 44, app->prog.streak > 0);
    if(q->kind == QuizLesson && !q->passed) {
        canvas_draw_str(canvas, 18, 51, "Need 70% - try again!");
    } else {
        snprintf(
            buf, sizeof(buf), "%u day streak", app->prog.streak ? app->prog.streak : 1);
        canvas_draw_str(canvas, 18, 51, buf);
    }

    const char* ok_label = "OK Next";
    if(q->kind == QuizLesson && !q->passed) ok_label = "OK Retry";
    if(q->kind == QuizPractice) ok_label = "OK Again";
    canvas_draw_str_aligned(canvas, 120, 60, AlignRight, AlignBottom, ok_label);
    if(q->kind == QuizLesson && q->passed) {
        canvas_draw_str(canvas, 8, 60, "< Retry");
    } else {
        canvas_draw_str(canvas, 8, 60, "Back: Menu");
    }
}

/* ---------- Input ---------- */

void quiz_input(App* app, InputEvent* ev) {
    Quiz* q = &app->quiz;
    Step* s = cur_step(app);

    if(q->paused) {
        if(ev->type != InputTypeShort) return;
        if(ev->key == InputKeyLeft || ev->key == InputKeyRight) {
            q->pause_yes = !q->pause_yes;
        } else if(ev->key == InputKeyOk) {
            q->paused = false;
            if(q->pause_yes) {
                if(q->kind == QuizPlacement) {
                    q->placement_failed = true;
                    next_step(app);
                } else {
                    go_screen(app, q->kind == QuizLesson ? ScrCourse : ScrPracticeMenu);
                }
            }
        } else if(ev->key == InputKeyBack) {
            q->paused = false;
        }
        return;
    }

    if(ev->key == InputKeyBack) {
        if(ev->type == InputTypeShort || ev->type == InputTypeLong) {
            player_stop(app);
            keyer_clear(app);
            q->paused = true;
            q->pause_yes = false;
        }
        return;
    }

    /* keying steps: paddles (and straight key) go to the keyer first */
    if(step_uses_keyer(s->type) && q->state != QRight && q->state != QWrong) {
        if(keyer_input(app, ev)) return;
    }

    if(ev->type != InputTypeShort) return;

    if(q->state == QRight) {
        if(ev->key == InputKeyOk) next_step(app);
        return;
    }
    if(q->state == QWrong) {
        if(ev->key == InputKeyOk) {
            next_step(app);
        } else if(ev->key == InputKeyDown || ev->key == InputKeyUp) {
            if(!player_busy(app)) play_step_audio(app);
        }
        return;
    }

    switch(s->type) {
    case StepTip:
        if(ev->key == InputKeyOk || ev->key == InputKeyRight) next_step(app);
        break;
    case StepIntro:
        if(ev->key == InputKeyOk) {
            if(!player_busy(app)) play_step_audio(app);
        } else if(ev->key == InputKeyRight) {
            next_step(app);
        }
        break;
    case StepGuided:
    case StepSend:
    case StepWordSend:
        if(ev->key == InputKeyOk) {
            keyer_force_commit(app); /* paddle mode: finish the letter now */
        } else if(ev->key == InputKeyUp) {
            keyer_clear(app);
            q->state = QAsk;
            q->wrong_char = 0;
            q->wrong_pattern[0] = '\0';
        } else if(ev->key == InputKeyDown && s->type != StepGuided && app->set.hints) {
            if(!q->hinted) {
                q->hinted = true;
                if(s->type == StepSend) {
                    score_once(app, false);
                    requeue_once(app);
                    record_answer(app, s->text[0], false);
                }
            }
        }
        break;
    default:
        if(ev->key == InputKeyOk) {
            if(!player_busy(app)) play_step_audio(app);
        } else if(ev->key == InputKeyUp) {
            answer_choice(app, 0);
        } else if(ev->key == InputKeyRight) {
            answer_choice(app, 1);
        } else if(ev->key == InputKeyDown) {
            answer_choice(app, 2);
        } else if(ev->key == InputKeyLeft) {
            answer_choice(app, 3);
        }
        break;
    }
}

/* ---------- Drawing ---------- */

static const char* sign_kind(char c) {
    if(c >= 'A' && c <= 'Z') return "LETTER";
    if(c >= '0' && c <= '9') return "NUMBER";
    return "SIGN";
}

static void draw_tip(Canvas* canvas, App* app) {
    Quiz* q = &app->quiz;
    const char* chars = lessons[q->lesson].chars;
    char buf[24];
    canvas_set_font(canvas, FontSecondary);
    if(q->lesson == FINAL_LESSON) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 13, AlignCenter, AlignCenter, "FINAL EXAM");
    } else {
        snprintf(buf, sizeof(buf), "L%u", q->lesson + 1);
        canvas_draw_str(canvas, 3, 13, buf);
        canvas_draw_str_aligned(canvas, 125, 13, AlignRight, AlignBottom, "NEW");
        uint8_t n = (uint8_t)strlen(chars);
        int16_t w = n * 10 + (n - 1) * 5;
        int16_t x = 64 - w / 2;
        for(uint8_t i = 0; i < n; i++) {
            ui_big_char(canvas, x + i * 15, 5, chars[i], 2);
        }
    }
    canvas_draw_line(canvas, 4, 21, 123, 21);
    canvas_set_font(canvas, FontSecondary);
    ui_wrap(canvas, 4, 30, 120, 9, lessons[q->lesson].tip, 0, 3);
    /* pulsing "OK" hint in the bottom right corner */
    if((app->anim / 12) % 2 == 0) {
        canvas_draw_rbox(canvas, 96, 52, 28, 11, 3);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, 96, 52, 28, 11, 3);
    }
    canvas_draw_str_aligned(canvas, 110, 58, AlignCenter, AlignCenter, "OK >");
    canvas_set_color(canvas, ColorBlack);
}

static void draw_intro(Canvas* canvas, App* app) {
    Step* s = cur_step(app);
    char c = s->text[0];
    const char* code = morse_code(c);
    char buf[32];

    /* big letter in a box on the left */
    canvas_draw_rframe(canvas, 3, 6, 30, 36, 3);
    ui_big_char(canvas, 8, 10, c, 4);

    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "NEW %s", sign_kind(c));
    canvas_draw_str_aligned(canvas, 81, 12, AlignCenter, AlignBottom, buf);

    int8_t lit = -1;
    bool playing = player_busy(app);
    if(playing) lit = player_current_element(app);
    ui_pattern(canvas, 81, 24, code, true, playing ? lit : 127, playing);

    /* how it sounds, full width under the letter box */
    ui_phonetic(code, buf, sizeof(buf));
    canvas_set_font(canvas, FontSecondary);
    ui_str_fit(canvas, 64, 51, 122, AlignCenter, buf);

    ui_dotted_row(canvas, 61, "OK: hear again", "Next >");
}

static void draw_key_hints(Canvas* canvas, App* app, bool allow_hint) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 63, "<dit");
    canvas_draw_str_aligned(canvas, 126, 63, AlignRight, AlignBottom, "dah>");
    const char* mid;
    if(app->set.key_mode == KeyModeStraight) {
        mid = "hold OK = key";
    } else {
        mid = allow_hint ? "^clear  v hint" : "^clear";
    }
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, mid);
}

/* The pattern the player is keying right now, with a blinking cursor. */
static void draw_live_pattern(Canvas* canvas, App* app, int16_t cy) {
    Keyer* k = &app->keyer;
    char pat[PATTERN_MAX + 1];
    memcpy(pat, k->pattern, PATTERN_MAX);
    pat[k->plen] = '\0';
    if(k->phase == KeyOn && k->plen < PATTERN_MAX - 1) {
        /* show the element that is sounding right now */
        pat[k->plen] = k->cur ? '-' : '.';
        pat[k->plen + 1] = '\0';
    }
    int16_t w = ui_pattern_width(pat, true);
    ui_pattern(canvas, 64, cy, pat, true, 127, false);
    if((app->anim / 8) % 2 == 0 && !k->straight_down) {
        int16_t cx = w > 0 ? 64 + w / 2 + 4 : 64;
        canvas_draw_line(canvas, cx, cy - 4, cx, cy + 4);
    }
    if(k->straight_down) {
        /* growing bar while the straight key is held */
        uint32_t held = app->now - k->down_at;
        int16_t len = (int16_t)(held / 12);
        if(len > 40) len = 40;
        canvas_draw_box(canvas, 64 + w / 2 + 3, cy - 2, len, 5);
    }
}

static void draw_send(Canvas* canvas, App* app) {
    Quiz* q = &app->quiz;
    Step* s = cur_step(app);
    char c = s->text[0];
    const char* code = morse_code(c);
    char buf[32];
    bool guided = s->type == StepGuided;

    canvas_draw_rframe(canvas, 3, 6, 25, 29, 3);
    ui_big_char(canvas, 8, 10, c, 3);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 33, 14, guided ? "Your turn!" : "Send it:");

    if(guided || q->hinted || q->state == QWrong) {
        /* target pattern: hollow, fills up as you key the right elements */
        int8_t lit = -1;
        Keyer* k = &app->keyer;
        if(strncmp(code, k->pattern, k->plen) == 0) lit = (int8_t)k->plen - 1;
        ui_pattern(canvas, 77, 27, code, true, q->state == QWrong ? 127 : lit, true);
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 33, 29, "from memory");
    }

    canvas_set_font(canvas, FontSecondary);
    if(q->state == QRight) {
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "Correct!");
        ui_pattern(canvas, 64, 51, code, false, 127, false);
    } else if(q->state == QWrong) {
        snprintf(buf, sizeof(buf), "It's %c - OK to go on", c);
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, buf);
    } else if(q->state == QTryAgain) {
        if(q->wrong_char) {
            snprintf(buf, sizeof(buf), "That was %c - try again", q->wrong_char);
        } else {
            snprintf(buf, sizeof(buf), "Unknown sign - try again");
        }
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, buf);
        ui_pattern(canvas, 64, 51, q->wrong_pattern, false, 127, false);
    } else {
        draw_live_pattern(canvas, app, 45);
    }
    draw_key_hints(canvas, app, !guided && app->set.hints);
}

static void draw_word_send(Canvas* canvas, App* app) {
    Quiz* q = &app->quiz;
    Step* s = cur_step(app);
    uint8_t n = (uint8_t)strlen(s->text);
    char buf[32];

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 3, 12, "Send the word:");

    int16_t w = n * 10 + (n - 1) * 4;
    int16_t x = 64 - w / 2;
    for(uint8_t i = 0; i < n; i++) {
        int16_t lx = x + i * 14;
        ui_big_char(canvas, lx, 16, s->text[i], 2);
        if(i < q->typed_len) {
            canvas_draw_box(canvas, lx, 32, 10, 2);
        } else if(i == q->typed_len && (app->anim / 8) % 2 == 0) {
            canvas_draw_line(canvas, lx, 33, lx + 9, 33);
        }
    }

    char target = q->typed_len < n ? s->text[q->typed_len] : 0;
    if(q->state == QRight) {
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "Great word!");
    } else if(q->state == QWrong) {
        canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignCenter, "Listen to it - OK to go on");
        if(target) ui_pattern(canvas, 64, 50, morse_code(target), false, 127, false);
    } else if(q->state == QTryAgain) {
        if(q->wrong_char) {
            snprintf(buf, sizeof(buf), "That was %c - try again", q->wrong_char);
        } else {
            snprintf(buf, sizeof(buf), "Unknown sign - try again");
        }
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, buf);
    } else if(q->hinted && target) {
        ui_pattern(canvas, 64, 40, morse_code(target), false, 127, false);
        draw_live_pattern(canvas, app, 50);
    } else {
        draw_live_pattern(canvas, app, 46);
    }
    draw_key_hints(canvas, app, app->set.hints);
}

static void draw_choice(Canvas* canvas, App* app) {
    Quiz* q = &app->quiz;
    Step* s = cur_step(app);
    bool single = s->type == StepListen;
    char buf[24];

    for(uint8_t slot = 0; slot < 4; slot++) {
        if(!q->slot_used[slot]) continue;
        bool filled = false;
        bool cross = false;
        if(q->state == QRight && slot == q->answer_slot) filled = true;
        if(q->state == QWrong) {
            if(slot == q->answer_slot) filled = (app->anim / 6) % 2 == 0;
            if(slot == (uint8_t)q->chosen) cross = true;
        }
        Font font = single ? FontPrimary : (s->type == StepCallListen ? FontKeyboard : FontSecondary);
        ui_choice_box(canvas, slot, q->options[slot], filled, cross, font);
    }

    /* corners */
    canvas_set_font(canvas, FontSecondary);
    if(q->kind == QuizPlacement) {
        snprintf(buf, sizeof(buf), "Test %u", q->cur / 2 + 1);
    } else {
        snprintf(buf, sizeof(buf), "%u/%u", q->cur + 1, q->count);
    }
    canvas_draw_str(canvas, 2, 12, buf);
    if(q->run >= 3) {
        ui_flame(canvas, 2, 14, true);
        snprintf(buf, sizeof(buf), "x%u", q->run);
        canvas_draw_str(canvas, 11, 22, buf);
    }
    canvas_draw_str_aligned(canvas, 126, 12, AlignRight, AlignBottom, "OK:");
    canvas_draw_str_aligned(canvas, 126, 21, AlignRight, AlignBottom, "replay");

    /* center: speaker while listening, the answer's pattern afterwards */
    if(q->state == QAsk) {
        ui_waves(canvas, 66, 33, app->anim, player_busy(app));
    } else {
        const char* answer = q->options[q->answer_slot];
        canvas_set_font(canvas, FontPrimary);
        if(single) {
            canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, answer);
            int8_t lit = player_busy(app) ? player_current_element(app) : 127;
            ui_pattern(canvas, 64, 42, morse_code(answer[0]), false, lit, player_busy(app));
        } else {
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str_aligned(
                canvas, 64, 33, AlignCenter, AlignCenter, q->state == QRight ? "Yes!" : "Hmm");
        }
    }
    canvas_set_font(canvas, FontSecondary);
    if(q->state == QRight) {
        canvas_draw_str(canvas, 2, 60, "Nice!");
    } else if(q->state == QWrong) {
        canvas_draw_str(canvas, 2, 60, "Oops!");
        canvas_draw_str_aligned(canvas, 126, 60, AlignRight, AlignBottom, "OK >");
    }
}

void quiz_draw(Canvas* canvas, App* app) {
    Quiz* q = &app->quiz;
    Step* s = cur_step(app);
    ui_top_progress(canvas, q->cur, q->count);

    switch(s->type) {
    case StepTip:
        draw_tip(canvas, app);
        break;
    case StepIntro:
        draw_intro(canvas, app);
        break;
    case StepGuided:
    case StepSend:
        draw_send(canvas, app);
        break;
    case StepWordSend:
        draw_word_send(canvas, app);
        break;
    default:
        draw_choice(canvas, app);
        break;
    }

    if(q->paused) {
        const char* question = "Quit lesson?";
        if(q->kind == QuizPractice) question = "Quit practice?";
        if(q->kind == QuizPlacement) question = "End the test?";
        ui_yes_no(canvas, question, q->pause_yes);
    }
}
