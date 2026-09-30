/*
 * Morse Academy for the Flipper Zero
 * ----------------------------------
 * Shared types and declarations for all modules:
 *
 *   morse_main.c    app lifecycle, event loop, screen dispatch, XP/badges
 *   morse_data.c    Morse table, lessons, word list, badges, pixel font
 *   morse_signal.c  tone / LED / vibration output, playback, keyer, jingles
 *   morse_store.c   settings + progress on the SD card
 *   morse_draw.c    shared drawing helpers (frames, patterns, big letters)
 *   morse_menus.c   menus, course map, settings, stats, help, onboarding
 *   morse_quiz.c    lesson / practice / placement engine + results
 *   morse_games.c   Morse Rush, Sound Sprint, Echo Chain, Free Key
 */
#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------- Sizes ---------- */

#define CHAR_COUNT 41 /* A-Z, 0-9, . , ? / = */
#define LESSON_COUNT 19 /* 18 lessons + final exam */
#define FINAL_LESSON (LESSON_COUNT - 1)
#define BADGE_COUNT 12
#define MAX_STEPS 44
#define STEP_TEXT 8 /* longest word / callsign incl. terminator */
#define PLAY_MAX 40
#define PATTERN_MAX 8
#define FREE_TEXT_MAX 60
#define RUSH_FALLERS 5
#define ECHO_MAX 24

/* Time helper: true once 'now' has reached the tick 't' (wrap-safe). */
#define TIME_REACHED(now, t) ((int32_t)((now) - (t)) >= 0)

/* ---------- Static data (morse_data.c) ---------- */

typedef struct {
    char ch;
    const char* code;
} MorseChar;

typedef struct {
    const char* chars; /* new signs taught in this lesson */
    const char* tip;
} LessonDef;

typedef struct {
    const char* name;
    const char* desc;
} BadgeDef;

typedef enum {
    BadgeFirstSteps,
    BadgeSos,
    BadgeAlphabet,
    BadgeNumbers,
    BadgeGraduate,
    BadgePerfect,
    BadgeStreak3,
    BadgeStreak7,
    BadgeSharpEar,
    BadgeWordsmith,
    BadgeRush,
    BadgeSpeed,
} BadgeId;

extern const MorseChar morse_table[CHAR_COUNT];
extern const LessonDef lessons[LESSON_COUNT];
extern const BadgeDef badges[BADGE_COUNT];
extern const char* const word_list[];
extern const uint16_t word_count;
extern const uint8_t glyph_5x7[CHAR_COUNT][7];

int morse_index(char c);
const char* morse_code(char c);
char morse_decode(const char* pattern);
const char* level_title(uint8_t level);

/* ---------- Settings & progress (morse_store.c) ---------- */

typedef enum {
    KeyModePaddle, /* Left = dit, Right = dah */
    KeyModeStraight, /* hold OK: short = dit, long = dah */
} KeyMode;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t sound;
    uint8_t volume; /* 0-100 */
    uint16_t tone_hz;
    uint8_t vibro;
    uint8_t led;
    uint8_t wpm; /* character speed */
    uint8_t eff_wpm; /* Farnsworth effective speed (<= wpm) */
    uint8_t key_mode;
    uint8_t letter_gap; /* 0 short, 1 normal, 2 long */
    uint8_t hints;
    uint8_t daily_goal; /* index into daily goal table */
    uint8_t backlight;
} Settings;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t onboarded;
    uint8_t unlocked; /* highest unlocked lesson index */
    uint8_t stars[LESSON_COUNT];
    uint32_t xp;
    uint32_t last_day; /* day number of the last activity (for streaks) */
    uint16_t streak;
    uint16_t best_streak;
    uint32_t xp_day; /* day number xp_today belongs to */
    uint16_t xp_today;
    uint8_t mastery[CHAR_COUNT]; /* 0-100 */
    uint16_t seen[CHAR_COUNT];
    uint16_t hits[CHAR_COUNT];
    uint16_t best_rush;
    uint16_t best_sprint;
    uint16_t best_echo;
    uint32_t badges; /* bit per BadgeId */
    uint32_t total_answers;
    uint32_t total_correct;
    uint16_t words_correct;
    uint16_t best_run;
    uint16_t sessions;
} Progress;

extern const uint8_t daily_goals[4];

/* ---------- Runtime state ---------- */

typedef struct {
    char text[PLAY_MAX + 1];
    uint8_t len;
    uint8_t pos; /* index into text */
    uint8_t el; /* element index inside the current sign */
    bool active;
    bool on;
    bool lead_in;
    uint32_t next;
    uint8_t wpm_override; /* 0 = use settings */
    bool no_farnsworth;
} Player;

typedef enum {
    KeyIdle,
    KeyOn,
    KeyGap,
} KeyPhase;

typedef struct {
    bool enabled;
    char pattern[PATTERN_MAX];
    uint8_t plen;
    uint8_t queue[8];
    uint8_t qlen;
    bool held[2]; /* [0] dit paddle, [1] dah paddle */
    KeyPhase phase;
    uint8_t cur;
    uint32_t phase_end;
    uint32_t last_end; /* tick when the last element ended */
    bool straight_down;
    uint32_t down_at;
    uint32_t dit_avg; /* adaptive straight-key dit length (ms) */
    bool has_commit;
    char commit_char; /* 0 = unknown pattern */
    char commit_pattern[PATTERN_MAX];
    bool overflow;
} Keyer;

typedef struct {
    uint16_t freq; /* 0 = rest */
    uint16_t ms;
} Note;

typedef struct {
    const Note* notes;
    uint8_t count;
    uint8_t idx;
    bool active;
    uint32_t next;
    uint8_t led; /* Light mask shown while the jingle plays */
    uint32_t vib_until;
    bool vib_on;
} Fx;

typedef enum {
    StepTip,
    StepIntro,
    StepGuided,
    StepListen,
    StepSend,
    StepWordListen,
    StepWordSend,
    StepCallListen,
} StepType;

typedef struct {
    uint8_t type;
    uint8_t retry;
    char text[STEP_TEXT];
} Step;

typedef enum {
    QuizLesson,
    QuizPractice,
    QuizPlacement,
} QuizKind;

typedef enum {
    PracListen,
    PracSend,
    PracWordListen,
    PracWordSend,
    PracWeak,
    PracCalls,
    PracCount,
} PracticeMode;

typedef enum {
    QAsk,
    QRight, /* correct answer, auto-advance */
    QWrong, /* wrong answer shown, wait for OK */
    QTryAgain, /* send step: wrong, let them retry */
} QuizState;

typedef struct {
    QuizKind kind;
    uint8_t lesson;
    uint8_t mode;
    Step steps[MAX_STEPS];
    uint8_t count;
    uint8_t cur;
    uint8_t scored;
    uint8_t first_ok;
    uint8_t requeued;

    /* current step */
    QuizState state;
    uint32_t state_until;
    char options[4][STEP_TEXT]; /* indexed by slot: 0 up, 1 right, 2 down, 3 left */
    bool slot_used[4];
    uint8_t answer_slot;
    int8_t chosen;
    uint8_t tries;
    bool hinted;
    char typed[STEP_TEXT];
    uint8_t typed_len;
    char wrong_char;
    char wrong_pattern[PATTERN_MAX];
    bool autoplay;
    uint32_t autoplay_at;

    /* session results */
    uint16_t xp_gained;
    uint8_t run;
    uint8_t stars;
    bool passed;
    bool new_best_stars;
    uint8_t placement_lesson;
    bool placement_done;
    bool pause_yes;
    bool paused;
    bool step_scored;
    bool step_requeued;
    bool placement_failed;
    uint8_t max_run;
    uint8_t accuracy;
    uint32_t result_anim_start;
} Quiz;

typedef enum {
    GameRush,
    GameSprint,
    GameEcho,
    GameCount,
} GameKind;

typedef struct {
    char ch;
    int32_t y; /* fixed point, 1/256 px */
    uint8_t lane;
    bool alive;
    uint32_t born;
} Faller;

typedef struct {
    int16_t x, y;
    uint32_t start;
    bool active;
} Burst;

typedef struct {
    GameKind kind;
    uint16_t score;
    uint8_t lives;
    uint16_t combo;
    uint8_t level;
    uint32_t start;
    uint32_t last_update;
    bool over;
    bool new_record;
    uint16_t xp_gained;
    bool paused;
    bool pause_yes;
    uint32_t pause_started;

    /* Morse Rush */
    Faller fallers[RUSH_FALLERS];
    uint32_t next_spawn;
    int32_t speed; /* 1/256 px per ms */
    uint32_t spawn_ms;
    Burst burst;
    int16_t zap_x, zap_y;
    uint32_t zap_until;
    char missed_char;
    uint32_t missed_until;
    uint32_t hurt_until;

    /* Sound Sprint */
    uint32_t time_left_ms;
    char target;
    char options[4];
    bool slot_used[4];
    uint8_t answer_slot;
    int8_t chosen;
    uint32_t feedback_until;
    uint8_t sprint_wpm;
    uint32_t bonus_until;

    /* Echo Chain */
    char seq[ECHO_MAX + 1];
    uint8_t seq_len;
    uint8_t echo_pos;
    uint8_t echo_phase; /* 0 intro, 1 listening, 2 your turn, 3 round ok, 4 fail */
    uint32_t phase_until;
    char echo_wrong;
} Game;

typedef enum {
    ScrWelcome,
    ScrExperience,
    ScrTutorial,
    ScrMenu,
    ScrCourse,
    ScrQuiz,
    ScrResults,
    ScrPracticeMenu,
    ScrGamesMenu,
    ScrGame,
    ScrGameOver,
    ScrFreeKey,
    ScrAlphabet,
    ScrStats,
    ScrSettings,
    ScrConfirm,
    ScrHelp,
} Screen;

typedef enum {
    ConfirmReset,
    ConfirmPlacement,
} ConfirmAction;

#define TOAST_QUEUE 4

typedef struct {
    char title[20];
    char text[24];
    uint8_t icon; /* 0 badge, 1 level, 2 goal */
} Toast;

typedef struct {
    Screen screen;
    bool running;
    uint32_t now;
    uint32_t anim; /* frame counter for animations */

    Settings set;
    Progress prog;
    bool prog_dirty;

    Player player;
    Keyer keyer;
    Fx fx;
    Quiz quiz;
    Game game;

    /* UI state */
    uint8_t menu_sel;
    uint8_t menu_scroll;
    uint8_t prac_sel;
    uint8_t prac_scroll;
    uint8_t games_sel;
    uint8_t course_sel;
    uint8_t set_sel;
    uint8_t set_scroll;
    uint8_t alpha_sel;
    uint8_t stats_page;
    uint8_t badge_sel;
    uint8_t help_page;
    uint8_t help_scroll;
    uint8_t help_lines; /* total lines of the current help page (set while drawing) */
    uint8_t exp_sel;
    uint8_t tut_step;
    uint32_t tut_until;
    bool confirm_yes;
    ConfirmAction confirm_action;
    Screen confirm_return;
    uint32_t screen_since;

    /* Free key */
    char free_text[FREE_TEXT_MAX + 1];
    uint8_t free_len;
    uint32_t free_last_commit;

    /* Toasts (badge unlocked, level up, daily goal) */
    Toast toasts[TOAST_QUEUE];
    uint8_t toast_count;
    uint32_t toast_start;
    bool toast_started;

    /* System */
    FuriMessageQueue* queue;
    NotificationApp* notif;
    Storage* storage;
    ViewPort* view_port;
    Gui* gui;
    bool speaker_owned;
    bool backlight_forced;
} App;

/* ---------- morse_store.c ---------- */

void settings_defaults(Settings* s);
void settings_load(App* app);
void settings_save(App* app);
void progress_defaults(Progress* p);
void progress_load(App* app);
void progress_save(App* app);

/* ---------- morse_signal.c ---------- */

uint32_t unit_ms(App* app, uint8_t wpm);
void signal_set(App* app, bool on, bool from_user);
void audio_release(App* app);
void player_start(App* app, const char* text);
void player_start_fast(App* app, const char* text, uint8_t wpm);
void player_stop(App* app);
void player_update(App* app);
bool player_busy(App* app);
int8_t player_current_char(App* app);
int8_t player_current_element(App* app);

void keyer_enable(App* app, bool enabled);
void keyer_clear(App* app);
bool keyer_input(App* app, InputEvent* ev);
void keyer_update(App* app);
bool keyer_take_commit(App* app, char* out_char, char* out_pattern);
bool keyer_force_commit(App* app);
bool keyer_busy(App* app);

void fx_play(App* app, const Note* notes, uint8_t count, uint8_t led, uint16_t vib_ms);
void fx_stop(App* app);
void fx_update(App* app);
void fx_correct(App* app);
void fx_wrong(App* app);
void fx_success(App* app);
void fx_fail(App* app);
void fx_badge(App* app);
void fx_goal(App* app);
void fx_click(App* app);
void fx_zap(App* app);
void fx_boom(App* app);
void fx_miss(App* app);
void fx_preview(App* app);
void all_signals_off(App* app);
uint32_t next_deadline(App* app);

/* ---------- morse_main.c (shared game logic) ---------- */

void go_screen(App* app, Screen s);
void add_xp(App* app, uint16_t amount);
void award_badge(App* app, BadgeId id);
void push_toast(App* app, const char* title, const char* text, uint8_t icon);
void record_answer(App* app, char c, bool correct);
uint8_t current_level(App* app);
uint32_t level_floor_xp(uint8_t level);
uint32_t today_number(void);
void touch_streak(App* app);
uint8_t pool_chars(App* app, char* out, bool include_current);
bool char_learned(App* app, char c);
void apply_backlight(App* app);
uint32_t rnd(uint32_t max);

/* ---------- morse_draw.c ---------- */

void ui_frame(Canvas* canvas, const char* title);
void ui_dotted_row(Canvas* canvas, int16_t y, const char* label, const char* key);
void ui_scroll_arrow(Canvas* canvas, int16_t cx, int16_t cy, bool up);
void ui_arrow(Canvas* canvas, int16_t cx, int16_t cy, uint8_t dir, uint8_t size);
int16_t ui_pattern_width(const char* code, bool big);
void ui_pattern(
    Canvas* canvas,
    int16_t cx,
    int16_t cy,
    const char* code,
    bool big,
    int8_t lit_upto,
    bool outline_rest);
void ui_big_char(Canvas* canvas, int16_t x, int16_t y, char c, uint8_t scale);
uint8_t ui_wrap(
    Canvas* canvas,
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t line_h,
    const char* text,
    uint8_t skip,
    uint8_t max_lines);
void ui_str_fit(Canvas* canvas, int16_t x, int16_t y, int16_t max_w, Align h, const char* text);
void ui_star(Canvas* canvas, int16_t x, int16_t y, bool filled);
void ui_star_big(Canvas* canvas, int16_t x, int16_t y, bool filled);
void ui_top_progress(Canvas* canvas, uint32_t val, uint32_t max);
void ui_phonetic(const char* code, char* out, size_t out_size);
void ui_icon(Canvas* canvas, int16_t x, int16_t y, uint8_t icon);
void ui_heart(Canvas* canvas, int16_t x, int16_t y, bool filled);
void ui_flame(Canvas* canvas, int16_t x, int16_t y, bool filled);
void ui_lock(Canvas* canvas, int16_t x, int16_t y);
void ui_waves(Canvas* canvas, int16_t cx, int16_t cy, uint32_t anim, bool active);
void ui_button(Canvas* canvas, int16_t x, int16_t y, int16_t w, int16_t h, const char* label, bool filled);
void ui_progress(Canvas* canvas, int16_t x, int16_t y, int16_t w, int16_t h, uint32_t val, uint32_t max);
void ui_choice_box(
    Canvas* canvas,
    uint8_t slot,
    const char* label,
    bool filled,
    bool cross,
    Font font);
void ui_toast(Canvas* canvas, App* app);
void ui_yes_no(Canvas* canvas, const char* question, bool yes);

typedef enum {
    IconLearn,
    IconPractice,
    IconGames,
    IconKey,
    IconAlphabet,
    IconStats,
    IconSettings,
    IconHelp,
    IconEar,
    IconSend,
    IconWords,
    IconTarget,
    IconRadio,
    IconRocket,
    IconBolt,
    IconChain,
    IconCount,
} IconId;

/* ---------- morse_menus.c ---------- */

void menu_input(App* app, InputEvent* ev);
void menu_draw(Canvas* canvas, App* app);
void practice_menu_input(App* app, InputEvent* ev);
void practice_menu_draw(Canvas* canvas, App* app);
void games_menu_input(App* app, InputEvent* ev);
void games_menu_draw(Canvas* canvas, App* app);
void course_input(App* app, InputEvent* ev);
void course_draw(Canvas* canvas, App* app);
void settings_input(App* app, InputEvent* ev);
void settings_draw(Canvas* canvas, App* app);
void confirm_input(App* app, InputEvent* ev);
void confirm_draw(Canvas* canvas, App* app);
void alphabet_input(App* app, InputEvent* ev);
void alphabet_draw(Canvas* canvas, App* app);
void stats_input(App* app, InputEvent* ev);
void stats_draw(Canvas* canvas, App* app);
void help_input(App* app, InputEvent* ev);
void help_draw(Canvas* canvas, App* app);
void welcome_input(App* app, InputEvent* ev);
void welcome_draw(Canvas* canvas, App* app);
void experience_input(App* app, InputEvent* ev);
void experience_draw(Canvas* canvas, App* app);
void tutorial_input(App* app, InputEvent* ev);
void tutorial_tick(App* app);
void tutorial_draw(Canvas* canvas, App* app);

/* ---------- morse_quiz.c ---------- */

void quiz_start_lesson(App* app, uint8_t lesson);
void quiz_start_practice(App* app, PracticeMode mode);
void quiz_start_placement(App* app);
void quiz_input(App* app, InputEvent* ev);
void quiz_tick(App* app);
void quiz_draw(Canvas* canvas, App* app);
void results_input(App* app, InputEvent* ev);
void results_draw(Canvas* canvas, App* app);
bool practice_mode_available(App* app, PracticeMode mode);
char pick_weighted(App* app, const char* pool, char avoid);
uint8_t pick_distractors(char target, const char* pool, char* out, uint8_t want);
uint16_t words_available(App* app, uint8_t max_len);

/* ---------- morse_games.c ---------- */

void game_start(App* app, GameKind kind);
void game_input(App* app, InputEvent* ev);
void game_tick(App* app);
void game_draw(Canvas* canvas, App* app);
void game_over_input(App* app, InputEvent* ev);
void game_over_draw(Canvas* canvas, App* app);
void freekey_start(App* app);
void freekey_input(App* app, InputEvent* ev);
void freekey_tick(App* app);
void freekey_draw(Canvas* canvas, App* app);
uint16_t game_best(App* app, GameKind kind);
