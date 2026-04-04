# Work log

This file tracks implementation work, validation, and remaining risks.

## 2026-04-04

### Request
Initialize repository workflow files for ChatGPT/Codex-assisted development.

### Files changed
- `AGENTS.md`
- `WORKLOG.md`
- `PLAN.md`
- `.logs/2026-04-04-bootstrap.md`

### Behavior changes
- No firmware behavior changes.
- Added repository instructions, work log template, and current project plan.

### Validation
- Files added only. No firmware build required.

### Risks / TODO
- Keep this file updated after each meaningful code change.

## 2026-04-04

### Request
Split the help command related implementation into `help.c` and `help.h`.

### Files changed
- `pico_tnc/cmd.c`
- `pico_tnc/help.c`
- `pico_tnc/help.h`
- `pico_tnc/CMakeLists.txt`
- `WORKLOG.md`

### Behavior changes
- No intended command behavior changes.
- Moved the `HELP`/`?` command text output implementation out of `cmd.c` into a dedicated `help.c` module.

### Validation
- Build attempted with `cmake -S . -B build && cmake --build build -j4`.
- Build could not run in this environment because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured.

### Risks / TODO
- `help.c` adds one additional compilation unit, but no new queues or runtime buffers were introduced.
- Help text still uses the same static string; RAM/queue behavior should remain unchanged.

## 2026-04-04

### Request
Convert separated `help.c` handling to non-blocking one-line-at-a-time output.

### Files changed
- `pico_tnc/help.c`
- `pico_tnc/help.h`
- `pico_tnc/cmd.c`
- `pico_tnc/main.c`
- `PLAN.md`
- `WORKLOG.md`

### Behavior changes
- HELP/`?` now starts an internal help send state instead of writing the full text immediately.
- `help_poll()` sends at most one help line per call, appending CRLF per line.
- `OK` for HELP is returned only after all help lines are sent.
- Other commands keep existing immediate `OK` behavior.

### Validation
- Build attempted with `cmake -S . -B build && cmake --build build -j4`.
- Build could not run in this environment because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured.

### Risks / TODO
- Help output now spans multiple main-loop iterations; on very slow links, completion timing depends on loop cadence.
- Design assumes each help line is sufficiently shorter than `QUEUE_SIZE`.

## 2026-04-04

### Request
Generate UTF-8→Shift_JIS (JIS X 0208 level-1 kanji) C code table using PHP `mb_convert_encoding()`, keep output readable with 8 entries per line, add visible character comments, and place generated files under `./pico_tnc`.

### Files changed
- `tools/gen_sjis_level1_table.php`
- `pico_tnc/sjis_level1_table.h`
- `pico_tnc/sjis_level1_table.c`
- `WORKLOG.md`

### Behavior changes
- Added PHP generator that filters to Shift_JIS level-1 range (`0x889F` to `0x9872`) with UTF-8 round-trip validation.
- Generator now writes both generated files to `./pico_tnc` (`sjis_level1_table.h`, `sjis_level1_table.c`).
- Generated C table keeps 8 initializers per line and inserts one human-readable comment line (`// 文字 ...`) per 8-entry block.
- No runtime firmware behavior changes yet (data only; not wired into command/IO paths).

### Validation
- Ran `php tools/gen_sjis_level1_table.php` successfully and regenerated `pico_tnc/sjis_level1_table.h` / `pico_tnc/sjis_level1_table.c` (2,965 entries).
- Build attempted with `cmake -S . -B build && cmake --build build -j4`.
- Build could not run in this environment because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured.

### Risks / TODO
- Table is large; if linked into firmware later, flash/RAM impact should be reviewed at integration time.
- Generation currently targets SJIS byte-range criteria for first-level kanji; variant-specific requirements may need additional filtering rules.

## 2026-04-04

### Request
Add Japanese help text in UTF-8, add `help ja`/`help ja sjis`/`help ja utf8` behavior, and add UTF-8→SJIS line conversion using `sjis_level1_table`.

### Files changed
- `pico_tnc/help.c`
- `pico_tnc/sjis_level1_tablehelp.c`
- `pico_tnc/sjis_level1_tablehelp.h`
- `pico_tnc/CMakeLists.txt`
- `README.md`
- `README_JP.md`
- `PLAN.md`
- `WORKLOG.md`

### Behavior changes
- English help now starts with: `JAPANESE HELP: help ja sjis | help ja utf8`.
- Added parameterized help command handling:
  - `help` (unchanged English help)
  - `help ja` and `help ja sjis` (Japanese help output encoded as SJIS bytes)
  - `help ja utf8` (Japanese help output in UTF-8)
- Added UTF-8→SJIS conversion function with 1-line conversion and 256-byte line buffer usage.
- Japanese SJIS conversion path uses `sjis_level1_table` for level-1 kanji and a small supplemental map for ASCII/hiragana/common punctuation.

### Validation
- Build attempted with `cmake -S . -B build && cmake --build build -j4`.
- Build could not run in this environment because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured.

### Risks / TODO
- Help line conversion uses a fixed 256-byte temporary buffer per output line in `help_poll()`; lines longer than that are truncated to fit.
- Queue behavior remains line-at-a-time (non-blocking help poll), but SJIS lines may be up to 2 bytes/character and can increase per-line write size relative to ASCII-only help.
