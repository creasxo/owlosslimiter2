# OW Loss Limiter

[![build](https://github.com/creasxo/owlosslimiter2/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/creasxo/owlosslimiter2/actions/workflows/build.yml)

A small Windows app that closes Overwatch after a set number of losses in a row.

I wrote it because "one more game" after a losing streak almost never goes well.
It watches the screen for the end-of-match DEFEAT / VICTORY banner, keeps count,
and ends the session for you when the streak hits your limit.

It only looks at the screen and the process list. It never reads or writes game
memory, injects anything, or hooks the game, so there is nothing for anti-cheat
to object to.

## How it works

Every couple of seconds a worker thread:

1. Checks whether `Overwatch.exe` is running (Toolhelp process snapshot).
2. Captures the middle of the screen, where the result banner appears, and
   thresholds it to black and white (GDI `BitBlt`).
3. Pipes the crop as a PNG into `tesseract` and looks for `DEFEAT` or `VICTORY`
   in the output.
4. Feeds the result into `LossTracker`, a small state machine that:
   - only accepts a result after it shows up on two polls in a row, so one bad
     OCR read doesn't count,
   - then ignores the screen until the banner and rank screen are gone, so one
     match is never counted twice,
   - resets the streak on a win.
5. When the streak reaches the limit, terminates the game process and stops.

`LossTracker` takes the current time as an argument instead of reading a clock,
which is what makes it unit-testable.

## Requirements

- Windows 10 or 11
- [MinGW-w64](https://www.mingw-w64.org/) (`g++` and `windres` on `PATH`) to build
- [Tesseract OCR](https://github.com/UB-Mannheim/tesseract/wiki). The default
  install location, `C:\Program Files\Tesseract-OCR\tesseract.exe`, is picked up
  automatically; anywhere else can be set in the app.
- Overwatch in **Borderless Windowed** mode. Exclusive fullscreen can't be
  captured with GDI.

## Build

```bat
build.bat
```

This produces `LossLimiter.exe` in the repository root. It is statically
linked, so it can be copied anywhere on its own.

## Usage

1. Run `LossLimiter.exe`.
2. Set the loss limit (default 3) and, if needed, the Tesseract path.
3. Click **Start**. The log panel shows what it sees (process found, results
   detected, current streak).
4. Play. After the Nth consecutive loss, Overwatch is closed.

Settings are saved to `config.txt` next to the exe:

```ini
losses=3
poll=2.000
process=Overwatch.exe
tesseract_path=
debug=0
```

| Key              | Meaning                                                          |
| ---------------- | ---------------------------------------------------------------- |
| `losses`         | Consecutive losses before the game is closed                     |
| `poll`           | Seconds between screen checks                                    |
| `process`        | Executable name to watch and close                               |
| `tesseract_path` | Full path to `tesseract.exe`; empty means use the default location |
| `debug`          | `1` saves every screen crop to `debug_crops/`, named by what OCR read |

If results aren't being detected, turn on debug mode and look at the crops.
That usually shows straight away whether the banner is outside the capture area
or whether the game is in exclusive fullscreen.

A log is written to `LossLimiter.log` (rotated at 1 MB).

## Tests

```bat
tests\run_tests.bat [path\to\tesseract.exe]
```

The test harness covers:

- the `LossTracker` state machine (debouncing, no double counting, streak reset,
  hitting the limit),
- config load/save, including malformed files,
- string and process-lookup helpers,
- an end-to-end OCR check that renders "DEFEAT" / "VICTORY" to a bitmap and runs
  it through the same Tesseract pipeline the app uses.

CI runs the build and the full test suite on `windows-latest` for every push
(see `.github/workflows/build.yml`).

## Project layout

```
src/
  main.cpp            entry point
  gui.cpp             Win32 window, settings form, log view
  limiter.cpp         worker thread: poll, capture, OCR, act
  loss_tracker.cpp    streak state machine (pure logic, no Win32)
  screen_capture.cpp  screen grab, crop, threshold, PNG encode
  ocr.cpp             runs tesseract over stdin/stdout pipes
  process_utils.cpp   find and terminate the game process
  config.cpp          config.txt read/write
  logging.cpp         file + UI log sink
tests/                test harness and runner
resources/            app manifest (DPI awareness, common controls)
third_party/          stb_image_write (PNG encoding)
archive/              earlier Python and OpenCV prototypes, kept for reference
```

## Known limitations

- Only English game text is recognised.
- The capture region is a fixed fraction of the primary monitor, so it assumes
  the game is on that monitor.
- If you relaunch the game after it's been closed, the limiter is already
  stopped. Press **Start** again to reset the streak and keep watching.
