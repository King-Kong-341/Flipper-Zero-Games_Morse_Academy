# Flipper PC preview

Renders the app's screens on your PC with the Flipper Zero's **real fonts**,
so you can spot text that touches a line or runs out of a box before
copying the app to the device. It's a layout check — it doesn't replace
testing on a real Flipper.

```bash
pip install pillow
python sim_screens.py              # -> shots/  (one PNG per screen + contact sheets)
python sim_screens.py some/folder  # -> some/folder/
```

| File | What it is |
|---|---|
| `flipsim.py` | Canvas emulation: u8g2 font decoder + text, frames, rounded boxes, discs, circles, lines using u8g2's algorithms |
| `sim_ui.py` | Python version of `morse_draw.c`; reads the Morse table, lessons, badges, icons and pixel font directly from the C sources |
| `sim_screens.py` | Python version of every screen's draw code with example states |

**Fonts:** on first run `flipsim.py` extracts the four Flipper fonts
(helvB08, haxrcorp4089, profont11, profont22) from your ufbt SDK
(`~/.ufbt/current/lib/libu8g2.a`) into `fonts/` using the ARM toolchain that
ufbt installs. Run `python -m ufbt` once before using the preview.

**Keep it in sync:** the screens are a hand-made port of the C draw
functions. When you move something in the C code, move it in
`sim_screens.py` too.
