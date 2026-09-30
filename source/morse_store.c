#include "morse.h"

/* Settings and progress are stored as small binary records with a magic
 * number and version. A file from an older/unknown version is ignored and
 * the defaults are used instead - no text parsing needed. */

#define SETTINGS_PATH APP_DATA_PATH("settings.bin")
#define PROGRESS_PATH APP_DATA_PATH("progress.bin")
#define SETTINGS_MAGIC 0x4D53524DU /* "MRSM" */
#define PROGRESS_MAGIC 0x4D535250U /* "MRSP" */
#define SETTINGS_VERSION 1
#define PROGRESS_VERSION 1

void settings_defaults(Settings* s) {
    memset(s, 0, sizeof(Settings));
    s->magic = SETTINGS_MAGIC;
    s->version = SETTINGS_VERSION;
    s->sound = 1;
    s->volume = 80;
    s->tone_hz = 700;
    s->vibro = 1;
    s->led = 1;
    s->wpm = 15;
    s->eff_wpm = 8;
    s->key_mode = KeyModePaddle;
    s->letter_gap = 1;
    s->hints = 1;
    s->daily_goal = 1;
    s->backlight = 1;
}

void progress_defaults(Progress* p) {
    memset(p, 0, sizeof(Progress));
    p->magic = PROGRESS_MAGIC;
    p->version = PROGRESS_VERSION;
}

static bool read_record(App* app, const char* path, void* data, size_t size) {
    bool ok = false;
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        ok = storage_file_read(file, data, size) == size;
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

static void write_record(App* app, const char* path, const void* data, size_t size) {
    storage_common_mkdir(app->storage, APP_DATA_PATH(""));
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, data, size);
    }
    storage_file_close(file);
    storage_file_free(file);
}

void settings_load(App* app) {
    Settings s;
    if(read_record(app, SETTINGS_PATH, &s, sizeof(s)) && s.magic == SETTINGS_MAGIC &&
       s.version == SETTINGS_VERSION) {
        app->set = s;
    } else {
        settings_defaults(&app->set);
    }
    /* clamp everything, in case the file was edited by hand */
    Settings* v = &app->set;
    if(v->volume > 100) v->volume = 100;
    if(v->tone_hz < 400 || v->tone_hz > 1000) v->tone_hz = 700;
    if(v->wpm < 5 || v->wpm > 40) v->wpm = 15;
    if(v->eff_wpm < 3 || v->eff_wpm > v->wpm) v->eff_wpm = v->wpm < 8 ? v->wpm : 8;
    if(v->key_mode > KeyModeStraight) v->key_mode = KeyModePaddle;
    if(v->letter_gap > 2) v->letter_gap = 1;
    if(v->daily_goal > 3) v->daily_goal = 1;
}

void settings_save(App* app) {
    write_record(app, SETTINGS_PATH, &app->set, sizeof(Settings));
}

void progress_load(App* app) {
    Progress p;
    if(read_record(app, PROGRESS_PATH, &p, sizeof(p)) && p.magic == PROGRESS_MAGIC &&
       p.version == PROGRESS_VERSION) {
        app->prog = p;
    } else {
        progress_defaults(&app->prog);
    }
    if(app->prog.unlocked >= LESSON_COUNT) app->prog.unlocked = LESSON_COUNT - 1;
    for(uint8_t i = 0; i < CHAR_COUNT; i++) {
        if(app->prog.mastery[i] > 100) app->prog.mastery[i] = 100;
    }
}

void progress_save(App* app) {
    write_record(app, PROGRESS_PATH, &app->prog, sizeof(Progress));
    app->prog_dirty = false;
}
