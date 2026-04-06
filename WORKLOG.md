# Work log

This file tracks implementation work, validation, and remaining risks.

## 2026-04-05

### Summary
Removed the unused test-packet replay path and related files from normal firmware builds:
- removed `TEST_PACKET`-guarded test hooks from `main.c` (`test.h` include, `test_init()`, `test()`)
- removed `test.c` and `packet_table.c` from `pico_tnc/CMakeLists.txt` sources
- deleted `pico_tnc/test.c`, `pico_tnc/test.h`, `pico_tnc/packet_table.c`, and `pico_tnc/packet_table.h`

### Files changed
- `pico_tnc/main.c`, `pico_tnc/CMakeLists.txt`, `pico_tnc/test.c`, `pico_tnc/test.h`, `pico_tnc/packet_table.c`, `pico_tnc/packet_table.h`, `WORKLOG.md`

### Behavior changes
- Normal firmware runtime path no longer contains dormant test replay hooks.
- Large `packet_table` blob and replay implementation are removed from the repository tree and default build, so ROM usage is expected to decrease.
- No intended changes to AX.25, send/receive, command handling, or help behavior.

### Validation status
- `rg -n "packet_table|packet_table\.h|test_init\(|\btest\(\)|TEST_PACKET|#include "test\.h"" pico_tnc` confirms no remaining matches.
- Build attempted with `cmake -S . -B build && cmake --build build -j4`, but this environment lacks `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`), so full build verification could not complete.

### Remaining risks / TODO
- If test replay is needed again later, restore deleted files and re-add source entries in `pico_tnc/CMakeLists.txt` and call sites in `main.c` intentionally.

## 2026-04-05

### Summary
Removed unused TRACE-related code as one grouped change:
- deleted `cmd_trace()` implementation, TRACE enum, command table registration, and DISP trace output
- removed TRACE lines from English/Japanese help text
- removed `param.trace` member from `param_t` and its initializer entry

### Files changed
- `pico_tnc/cmd.c`, `pico_tnc/help.c`, `pico_tnc/tnc.h`, `pico_tnc/tnc.c`, `WORKLOG.md`

### Validation status
- `rg -n "param\\.trace|cmd_trace|TRace|TRACE|TR_OFF|TR_XMIT|TR_RCV|\\{ \\\"TRACE\\\"" pico_tnc` : no remaining references in source.
- Build attempted with `cmake -S . -B build && cmake --build build -j4`, but this environment lacks `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`), so full build verification could not complete.

### Remaining risks / TODO
- None specific beyond required environment setup for full firmware build.

## 2026-04-04

### Request
In `help` / `help ja` output, check whether `MYCALL` and `UNPROTO` are set, and if not, display a warning message in the selected language.

### Files changed
- `pico_tnc/help.c`
- `README.md`
- `README_JP.md`
- `WORKLOG.md`

### Behavior changes
- `help` now checks `param.mycall` and `param.unproto[0]` when the command starts.
- If either value is unset, help output first prints a 3-line warning in the active help language:
  - English (`help`)
  - Japanese (`help ja` / `help ja sjis` / `help ja utf8`)
- Existing non-blocking one-line-at-a-time help output flow is preserved.
- A blank separator line is inserted after the warning block before normal help text.

### Validation
- Build attempted with `cmake -S . -B build && cmake --build build -j4`.
- Build could not run in this environment because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured.

### Risks / TODO
- Warning lines add a few extra help output lines, so queue usage duration (not per-line maximum) increases slightly while help is printing.
- Per-line output size and line buffer behavior remain unchanged (`utf8_to_sjis_line()` temporary buffer is still 256 bytes).

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

## 2026-04-04

### Request
Refactor Japanese help Shift_JIS output so UTF-8→Shift_JIS conversion is a single path integrated in `help.c`, while keeping generated level-1 kanji table files untouched.

### Files changed
- `pico_tnc/help.c`
- `pico_tnc/CMakeLists.txt`
- `pico_tnc/sjis_level1_tablehelp.c` (deleted)
- `pico_tnc/sjis_level1_tablehelp.h` (deleted)
- `PLAN.md`
- `WORKLOG.md`

### Behavior changes
- Consolidated Japanese help UTF-8→Shift_JIS conversion logic into `help.c` so `help ja sjis` always passes through one conversion path.
- Japanese UTF-8 source lines are now defined in `help.c` and converted per line only when SJIS mode is selected.
- Added Katakana range mapping (`U+30A1`..`U+30F6`) to the same conversion function used for hiragana/symbols/level-1 kanji.
- Unmapped code points still fall back to `?`.
- Non-blocking `help_poll()` line-by-line send behavior remains unchanged.

### Validation
- Build attempted with `cmake -S . -B build && cmake --build build -j4`.
- Build could not run in this environment because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured.

### Risks / TODO
- Conversion still uses linear search over the level-1 table per character; acceptable for help text volume but not optimized.
- Per-line temporary SJIS buffer remains 256 bytes; very long lines are truncated to fit, as before.
- Queue behavior remains one-line-at-a-time; SJIS lines can consume more bytes per line than UTF-8 ASCII-heavy lines.

## 2026-04-04

### Request
Centralize UTF-8→Shift_JIS conversion behavior in the PHP generator, regenerate the table outputs, and simplify `help.c` to UTF-8 decode + generated table lookup + `?` fallback while preserving non-blocking line-by-line help output.

### Files changed
- `tools/gen_sjis_level1_table.php`
- `pico_tnc/sjis_level1_table.h`
- `pico_tnc/sjis_level1_table.c`
- `pico_tnc/help.c`
- `PLAN.md`
- `WORKLOG.md`

### Behavior changes
- Expanded generator output ordering to: hiragana, katakana, first-level kanji, then symbols.
- Added symbol section at table tail (ASCII and required Japanese punctuation) to reduce C-side handwritten exceptions.
- Removed handwritten hiragana/katakana/symbol exception conversion logic from `help.c`; conversion now uses generated-table lookup plus `?` fallback.
- Preserved existing non-blocking `help_poll()` behavior and per-line send flow.

### Validation
- Ran `php tools/gen_sjis_level1_table.php` successfully to regenerate `pico_tnc/sjis_level1_table.h` and `.c`.
- Verified generated table contains requested words/marks coverage (`コマンド`, `キー`, `シリアル`, `デジピータ`, `キャリブレーション`, `・`, `ー`, `「」`) via script check.
- Build attempted with `cmake -S . -B build && cmake --build build -j4`.
- Build could not run in this environment because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured.

### Risks / TODO
- `lookup_sjis_from_level1()` remains linear search per decoded character; acceptable for help text volume but not optimized for large streams.
- Queue behavior and `QUEUE_SIZE` were intentionally left unchanged.

## 2026-04-04

### Request
Adjust UTF-8→Shift_JIS centralization so pure ASCII is not listed in the generated table and is passed through directly at runtime.

### Files changed
- `tools/gen_sjis_level1_table.php`
- `pico_tnc/sjis_level1_table.c`
- `pico_tnc/help.c`
- `WORKLOG.md`

### Behavior changes
- Removed ASCII code points from the generated symbol section in `sjis_level1_table.c`.
- `help.c` now passes ASCII (`U+0000`..`U+007F`) through directly without table lookup.
- Non-ASCII conversion path remains generated-table lookup with `?` fallback.
- Non-blocking `help_poll()` line-by-line send behavior remains unchanged.

### Validation
- Ran `php tools/gen_sjis_level1_table.php` successfully and regenerated outputs.
- Ran script validation:
  - confirmed ASCII space (`0x0020`) is absent from table
  - confirmed requested Japanese words/symbols coverage still present (`missing none`).
- Build attempted with `cmake -S . -B build && cmake --build build -j4`.
- Build could not run in this environment because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured.

### Risks / TODO
- Table lookup remains linear search per non-ASCII character; acceptable for help text volume.
- Queue behavior and `QUEUE_SIZE` remain unchanged by this adjustment.

## 2026-04-06

### Summary
Implemented AXHANG end-to-end and then refined it to modern ms-based UX:
- added AXHANG parameter, command, DISP output, and TX state-machine hang handling
- transitioned AXHANG internal unit to milliseconds with parser support for decimals and unit suffixes (`ms`, `s`)
- kept backward compatibility where unitless numeric input is treated as legacy 10ms units
- updated AXHANG storage type to `uint16_t`
- rounded sub-millisecond inputs to nearest millisecond and set valid range to `0..1000ms`

### Files changed
- `pico_tnc/tnc.h`
- `pico_tnc/tnc.c`
- `pico_tnc/cmd.c`
- `pico_tnc/send.c`
- `pico_tnc/help.c`
- `README.md`
- `README_JP.md`
- `WORKLOG.md`

### Behavior changes
- `AXHANG` now stores and displays milliseconds (e.g. `AXHANG 150ms`), stored as `uint16_t`.
- Input examples:
  - `axhang 12` -> 120ms (legacy-compatible)
  - `axhang 12.5` -> 125ms (legacy-compatible)
  - `axhang 250ms` -> 250ms
  - `axhang 0.35s` -> 350ms
- Fractional inputs are rounded to nearest millisecond.
- Send hang timeout compare uses elapsed tick time converted to milliseconds.

### Validation status
- `cmake -S . -B build && cmake --build build -j4` attempted; build did not run because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured in this environment.

### Remaining risks / TODOs
- `param_t` layout changed (`axhang` unit/width/type), so persisted flash data from older firmware may decode unexpectedly; startup clamp enforces `0..1000ms`.
- AXHANG command/help text became slightly longer; no queue-size constants were changed and output remains line-based.

## 2026-04-06

### Request
Add `cmd_axdelay()` and register `AXDELAY`, while modernizing delay UX for TX delay handling with millisecond-based `uint16_t` storage and unit parsing.

### Files changed
- `pico_tnc/cmd.c`
- `pico_tnc/tnc.h`
- `pico_tnc/tnc.c`
- `pico_tnc/help.c`
- `README.md`
- `README_JP.md`
- `WORKLOG.md`

### Behavior changes
- Added `AXDELAY` command and command table registration.
- `TXDELAY`/`AXDELAY` now parse `n`, `nms`, and `ns` with decimal input support.
  - unitless `n` keeps backward compatibility as legacy `10ms` units.
  - `ms` uses milliseconds directly.
  - `s` scales by `1000`.
  - sub-millisecond values are rounded to nearest millisecond.
  - accepted range is `0..1000ms`.
- Added separate internal delay parameters: `txdelay` and `axdelay` (both `uint16_t`, milliseconds).
- `DISP` now prints both `TXDELAY` and `AXDELAY`.
- Reworked `DISP` output into sectioned blocks (`Station`, `Network`, `Auto Operation`, `GPS / Sensor`, `Hardware`, `Diagnostics`) for easier reading while preserving all relevant fields.
- TX delay initialization keeps original defaults (`txdelay=100`, `axdelay=60`) while runtime conversion uses prior formula relationships (`2/3` and inverse).

### Validation status
- Build attempted with `cmake -S . -B build && cmake --build build -j4`; build is not possible in this environment because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured.

### Remaining risks / TODOs
- `param_t` layout changed by adding `axdelay` and widening `txdelay`; persisted flash settings from older firmware may map unexpectedly until rewritten.
- Command/help output lines became slightly longer; queue constants and buffering sizes were not changed.
