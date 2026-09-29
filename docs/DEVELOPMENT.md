# 🛠 Morse Academy — Developer Guide

How the app is built, how the Morse timing works, and how to change
lessons, words or the look.

- [Build & install](#build--install)
- [Architecture](#architecture)
- [Main loop & timing](#main-loop--timing)
- [Morse timing (WPM & Farnsworth)](#morse-timing-wpm--farnsworth)
- [The keyer](#the-keyer)
- [The learning engine](#the-learning-engine)
- [Saved data](#saved-data)
- [Customizing](#customizing)
- [PC screen preview](#pc-screen-preview)
- [Compiler gotchas](#compiler-gotchas)
- [Continuous integration](#continuous-integration)

---

## Build & install

```bash
pip install ufbt
python -m ufbt              # -> dist/morse_academy.fap
python -m ufbt launch       # build + install + start on a USB-connected Flipper
```

Helper scripts in [`scripts/`](../scripts/):

| Script | Does |
|---|---|
| `build.ps1` / `build.sh` | builds and copies the `.fap` to the repo root |
| `install.ps1` / `install.sh` | builds, installs and launches on a connected Flipper |
| `preview.ps1` / `preview.sh` | renders all screens to PNG (see [PC screen preview](#pc-screen-preview)) |

The first `ufbt` run downloads the SDK and ARM toolchain (a few hundred MB) to `~/.ufbt`.
The app targets the **official release SDK** (built with 1.4.3).

## Architecture

The app is a single `ViewPort` with a hand-written screen state machine —
no ViewDispatcher/scenes. Everything lives in one `App` struct (`morse.h`).

```
                 ┌──────────────┐
  input events ─▶│ morse_main.c │── dispatches by app->screen ──┐
                 │  event loop  │                               │
                 └──────┬───────┘                               ▼
                        │ every wake-up             ┌───────────────────────┐
                        ▼                           │ morse_menus.c         │ menus, path, settings,
              ┌──────────────────┐                  │ morse_quiz.c          │ lessons & practice
              │ morse_signal.c   │◀── play/key ─────│ morse_games.c         │ games & free key
              │ player · keyer · │                  └──────────┬────────────┘
              │ jingles (fx)     │                             │ draw
              └────────┬─────────┘                             ▼
                       │ HAL: speaker, LED, vibro     ┌──────────────┐
                       ▼                              │ morse_draw.c │ shared drawing helpers
                   hardware                           └──────────────┘
   morse_data.c: tables & text      morse_store.c: SD card
```

| Module | Responsibility |
|---|---|
| `morse.h` | all types (`Settings`, `Progress`, `Player`, `Keyer`, `Quiz`, `Game`, `App`), constants, prototypes |
| `morse_main.c` | lifecycle, event loop, input/draw dispatch, XP, levels, streak, badges, toasts, sign pools |
| `morse_data.c` | Morse table, 19 lesson definitions, 233-word list, badge texts, level titles, 5×7 pixel font |
| `morse_signal.c` | exact-timing output (tone/LED/vibration), playback `player_*`, keyer `keyer_*`, jingles `fx_*` |
| `morse_quiz.c` | step generation, choice building, scoring, re-queueing mistakes, results screen |
| `morse_games.c` | Morse Rush, Sound Sprint, Echo Chain, game-over screen, Free Key |
| `morse_menus.c` | main/practice/games menus, lesson path, settings, confirm dialog, alphabet, stats, help, onboarding |
| `morse_draw.c` | frames, dotted rows, arrows, dit/dah patterns, scaled letters, word wrap, icons, answer boxes, toast |
| `morse_store.c` | binary settings/progress records with magic + version |

### Screens

`Screen` enum in `morse.h`. Each screen has `*_input()` and `*_draw()`; the
screens with running logic also have `*_tick()`. `go_screen()` switches
screens and silences all signals when leaving an active screen (quiz, game,
free key, tutorial).

## Main loop & timing

```c
while(running) {
    got = furi_message_queue_get(queue, &ev, next_deadline(app));
    if(got) handle_input(app, &ev);
    player_update(app); keyer_update(app); fx_update(app);
    handle_tick(app);   toast_update(app);
    redraw at ~30 fps (or immediately after input)
}
```

There is **no fixed timer**. `next_deadline()` returns the time until the
next signal edge (end of a dit, start of the next element, end of a jingle
note), capped at 33 ms for animations. The loop sleeps exactly that long, so
elements are accurate to about a millisecond instead of a timer's tick size.

Tone, LED and vibration are switched **directly through the HAL**
(`furi_hal_speaker_*`, `furi_hal_light_set`, `furi_hal_vibro_on`) — the
notification service queues messages, which would smear 50 ms dits. On exit
`sequence_reset_rgb` hands the LED back to the system.

## Morse timing (WPM & Farnsworth)

Standard PARIS timing: one **unit** = `1200 / WPM` ms.

| Element | Length |
|---|---|
| dit | 1 unit |
| dah | 3 units |
| gap inside a sign | 1 unit |
| gap between signs | 3 units |
| gap between words | 7 units |

With **Farnsworth spacing** (`Settings → Spacing` < `Speed`) the signs keep
their full speed but the gaps are stretched (ARRL formula, `gaps()` in
`morse_signal.c`):

```
ta        = (60·c − 37.2·s) / (s·c)   seconds   (c = speed, s = spacing)
sign gap  = 3·ta / 19
word gap  = 7·ta / 19
```

Games use `player_start_fast()` (no Farnsworth) because they play single signs.

## The keyer

`Keyer` in `morse_signal.c` turns key presses into patterns:

- **Paddles** (◀ dit, ▶ dah) use `InputTypePress` / `InputTypeRelease`.
  Presses go into a small queue, so fast tapping is never lost. While a paddle
  is held, the element repeats after each 1-unit gap; with both held it
  alternates (iambic).
- **Straight key** (OK): press/release times are measured. The dit/dah
  border is `2 × dit_avg`, and `dit_avg` adapts to the player's own rhythm.
- A sign is **committed** after a pause of `unit × {3,5,8}` (min 250 / 400 / 650 ms,
  `Settings → Letter pause`), when OK is pressed in paddle mode, or after a
  7th element (nothing has more than 6). Screens read it with `keyer_take_commit()`.

## The learning engine

A session (`Quiz`) is a list of up to 44 `Step`s generated fresh every time:

| Step | Used for |
|---|---|
| `StepTip` | lesson explanation |
| `StepIntro` | meet a new sign (plays automatically) |
| `StepGuided` | key the new sign with the pattern visible (not scored) |
| `StepListen` | hear → pick among up to 4 (D-pad layout) |
| `StepSend` | see → key |
| `StepWordListen` / `StepWordSend` | the same with words |
| `StepCallListen` | callsigns (generated: `LDLLL`, `LLDLL`, `LDLL`, `LLDLLL`, `LDL`) |

- **Sign choice:** `pick_weighted()` weights every sign by `125 − mastery`,
  so weak signs come up more. New signs of a lesson are favored 55 %.
- **Wrong answers:** `pick_distractors()` prefers look-alikes (same length,
  same beginning) 65 % of the time — that's where learning happens.
- **Mistakes** are appended once more at the end (`requeue_once`, max 6 per
  session). Repeated steps aren't scored twice.
- **Mastery** per sign (0–100): right → `m += (100 − m) / 4`, wrong → `m = m × 3/5`.
- **Stars:** first-try accuracy ≥ 70 / 85 / 95 %.
- **Placement:** 2 listening steps per lesson; the first miss ends the test.

XP is collected during a session and booked at the end (`add_xp`), so
level-up/goal/badge toasts appear on the results screen, not mid-quiz.
Toasts wait for silence before they play their jingle.

## Saved data

Two small binary files in `/ext/apps_data/morse_academy/`:

| File | Struct | Content |
|---|---|---|
| `settings.bin` | `Settings` | all settings |
| `progress.bin` | `Progress` | unlocked lesson, stars, XP, streak, per-sign mastery/seen/hits, best scores, badges, counters |

Both start with a magic number and a version. A file with an unknown version
is ignored and defaults are used — **if you change a struct, bump
`SETTINGS_VERSION` / `PROGRESS_VERSION`** in `morse_store.c`.

## Customizing

### Words
`word_list[]` in `morse_data.c`. Uppercase, **max. 6 letters** (they must fit
the answer boxes). A word is only used once all its letters are learned, so
you can add anything — it shows up automatically at the right time.

### Lessons
`lessons[]` in `morse_data.c`: the new signs and a tip (keep the tip short
enough for 3 lines — check with the preview tool). `LESSON_COUNT` in
`morse.h` must match, the last entry (empty signs) is the final exam.
Badge conditions referring to lesson indexes are in `check_lesson_badges()`
(`morse_quiz.c`).

### Sounds
Jingles are `Note` arrays (`{frequency_hz, duration_ms}`, 0 Hz = rest) at the
end of `morse_signal.c`, each with an LED color and vibration length.

### Icons & pictures
Menu icons, stars, hearts, flame and lock are `'#'` / `'.'` strings in
`morse_draw.c` — edit them like ASCII art. The app icon is drawn by
`make_icon.py` (black pixels are drawn on the Flipper).

## PC screen preview

[`tools/flipper_preview`](../tools/flipper_preview/) renders every screen to
PNG with the Flipper's **real fonts** — useful to catch text that touches a
line or runs out of a box before you copy the app to the device.

```bash
python tools/flipper_preview/sim_screens.py            # -> tools/flipper_preview/shots/
python tools/flipper_preview/sim_screens.py docs/screenshots_new
```

- `flipsim.py` — canvas emulation: decodes the u8g2 font blobs and
  implements text, rounded frames/boxes, discs, circles and lines with the
  same algorithms as u8g2. On first use it **extracts the fonts from your
  ufbt SDK** (`~/.ufbt/current/lib/libu8g2.a`) — they are not stored in this repo.
- `sim_ui.py` — Python version of `morse_draw.c` (reads tables, icons and the
  pixel font straight from the C sources).
- `sim_screens.py` — Python version of every screen's draw function with
  example states. **It is a port** — when you move things in C, move them here
  too.

Font facts: `FontPrimary` = helvB08 (ascent 8), `FontSecondary` = haxrcorp4089
(ascent 7), `FontKeyboard` = profont11 (monospace, 6 px), `FontBigNumbers` =
profont22 (digits only). `canvas_draw_str_aligned(…, AlignCenter)` puts the
baseline at `y + ascent / 2`.

## Compiler gotchas

ufbt compiles with `-Wall -Wextra -Werror -Wdouble-promotion -Wundef`:

- unused functions/variables break the build
- `snprintf` into a buffer that *might* be too small breaks the build
  (`-Wformat-truncation`) — size buffers generously
- mixing `size_t` and `int` in `?:` breaks the build (`-Wsign-compare`)
- use float literals (`1.0f`); double promotion is an error
- `strtok` is not exported by the firmware — parse with `strchr`

## Continuous integration

[`.github/workflows/build.yml`](../.github/workflows/build.yml) builds the app
with the official [ufbt GitHub Action](https://github.com/flipperdevices/flipperzero-ufbt-action)
on every push and pull request and uploads the `.fap` as a build artifact.
