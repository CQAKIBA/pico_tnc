# Work log

This file tracks implementation work, validation, and remaining risks.

## 2026-04-20

### Summary
Implemented initial `sign` command support for plain text signing as `sign msg <text>` with interactive TX confirmation.

### Files changed
- `pico_tnc/cmd.c`
- `pico_tnc/help.c`
- `README.md`
- `README_JP.md`
- `PLAN.md`
- `WORKLOG.md`

### Behavior changes
- Added `SIGN` command parser entry with `sign msg <text>` subcommand.
- `sign msg` now:
  - validates key slot + `MYCALL`/`UNPROTO` presence.
  - builds JSON payload as `{"msg":"<text>"}` (with basic JSON escaping for `"` and `\`).
  - signs the JSON string via `mona_keyslot_sign_message()`.
  - concatenates JSON + signature and prints the packet text.
  - measures and prints signing time in microseconds.
  - computes AX.25 UI frame length and prints `< 256byte OK` / `>= 256byte NG`.
  - waits for `[Enter]` to TX or `[ESC]` to abort.
- Added pending-input state for sign TX confirmation:
  - Enter sends one UI frame using current `MYCALL`/`UNPROTO`.
  - ESC aborts with `Aborted by user.`.
- Updated HELP/README (EN/JP) command documentation to include `sign msg`.

### Validation status
- Build attempted with:
  - `cmake -S . -B build`
  - `cmake --build build -j4`
- In this environment, full firmware build cannot complete due missing Pico SDK configuration (`PICO_SDK_PATH` not set).

### Remaining risks / TODO
- Sign payload buffer is capped at 255 bytes (`CMD_BUF_LEN` interaction + signature length); longer messages are rejected before TX prompt.
- Added text output lines for signing status and packet preview increase transient USB/TTY queue usage; queue constants were not changed, so behavior depends on existing `tty_write` pacing and queue headroom.
- `sign qsl ...` and verify command paths are still pending by plan.

## 2026-04-18

### Summary
Added a new `about` command that prints version/edition and third-party component attribution text, and documented it in help/README outputs.

### Files changed
- `pico_tnc/cmd.c`
- `pico_tnc/help.c`
- `README.md`
- `README_JP.md`
- `WORKLOG.md`

### Behavior changes
- Added `ABOUT` command parsing in the command table (`about` case-insensitive) and implemented multiline output text in English only.
- Added help line entry:
  - English help: `ABOUT (about) - Version Information`
  - Japanese help: `ABOUT (about) - バージョン情報` (description only)
- Added README command documentation in both English and Japanese command lists.

### Validation status
- Build attempted with:
  - `cmake -S . -B build`
  - `cmake --build build -j4`
- In this environment, full firmware build cannot complete due missing Pico SDK configuration (`PICO_SDK_PATH` not set).

### Remaining risks / TODO
- `about` output is a relatively long multiline response; no queue constants were changed, so it relies on existing line-by-line `tty_write_str` behavior and available TX queue headroom.
- Confirm on real device terminal clients that long attribution output remains readable under current serial pacing.

## 2026-04-18

### Summary
Integrated vendored `secp256k1` into firmware build wiring and replaced minimal crypto stubs with real RIPEMD-160 + secp256k1 operations so `privkey show` can derive/print actual addresses.

### Files changed
- `pico_tnc/CMakeLists.txt`
- `pico_tnc/mona_backend_minimal.c`
- `WORKLOG.md`

### Behavior changes
- Firmware build now includes vendored `secp256k1` as a subdirectory target and links `secp256k1` into `pico_tnc`.
- `mona_backend_minimal` now provides:
  - RIPEMD-160 implementation (for HASH160 address generation).
  - secp256k1 compressed pubkey creation.
  - compact sign/recover/verify plumbing via `secp256k1` API.
- `mona_backend_minimal_has_full_crypto()` now reports `true`, so `privkey show` address derivation path can run with real backend primitives.

### Validation status
- Syntax-checked backend translation unit with secp headers:
  - `gcc -std=c11 -fsyntax-only -I./pico_tnc -I./pico_tnc/secp256k1/include pico_tnc/mona_backend_minimal.c`
- Firmware CMake configure attempted:
  - `cmake -S . -B build` fails in this environment because `PICO_SDK_PATH` is not configured.
  - `PICO_SDK_FETCH_FROM_GIT=ON cmake -S . -B build` fails due outbound network restriction (`CONNECT tunnel failed, response 403`) while cloning pico-sdk.

### Remaining risks / TODO
- Full firmware compile/link validation is still pending in an environment with local pico-sdk access.
- secp256k1 integration increases flash/RAM footprint; measure map/size deltas on target build environment before release.

## 2026-04-18

### Summary
Implemented Monacoin address derivation in `privkey show` so the address section now attempts real `p2pkh (M)`, `p2sh (P)`, and `p2wpkh (mona1)` calculation from the persisted private key instead of fixed placeholder text.

### Files changed
- `pico_tnc/cmd.c`
- `WORKLOG.md`

### Behavior changes
- `privkey show` now calls `mona_keypair_from_secret()` and prints derived addresses:
  - `addr_M` for p2pkh
  - `addr_P` for p2sh
  - `addr_mona1` for p2wpkh
- If derivation fails, each line prints `(calculation failed)` instead of `(unavailable in this stage)`.
- Queue behavior and output flow are unchanged; only line contents were updated.

### Validation status
- Static review confirms placeholder lines were replaced with derive-and-print logic in the `privkey show` path.
- Build attempted with `cmake -S . -B build && cmake --build build -j4`, but full build verification could not complete because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured in this environment.

### Remaining risks / TODO
- Current backend (`mona_backend_minimal`) still reports `mona_backend_minimal_has_full_crypto() == false`; if required crypto primitives remain unavailable at runtime, address lines will show `(calculation failed)`.

## 2026-04-18

### Summary
Implemented `privkey` type switching via `privkey set [m|p|mona1|p2pkh|p2sh|p2wpkh]` so users can normalize and persist only the active address type without re-importing key material.

### Files changed
- `pico_tnc/cmd.c`
- `pico_tnc/help.c`
- `README.md`
- `README_JP.md`
- `PLAN.md`
- `WORKLOG.md`

### Behavior changes
- `privkey set` now accepts address-type aliases (`m`, `p`, `mona1`) and canonical names (`p2pkh`, `p2sh`, `p2wpkh`) as a type-only operation.
- Type-only `privkey set` normalizes through `mona_parse_addr_type()` and stores the canonical active type in persistent parameters.
- Existing key import behavior (`privkey set <WIF or RAW>`) remains unchanged.
- Help and README command descriptions now document combined type/WIF/RAW `set` usage.

### Validation status
- Static command-path review confirms:
  - type-only `set` path is evaluated before WIF/RAW parsing.
  - type normalization uses the same parser as `privkey gen`.
- Build attempted with `cmake -S . -B build && cmake --build build -j4`, but full build verification could not complete because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured in this environment.

### Remaining risks / TODO
- `privkey type` is currently implemented through `privkey set <type>` UX; if a dedicated subcommand syntax is required later, parser/help text should be extended in a separate step.

## 2026-04-17

### Summary
Added `privkey show` with an interactive safety prompt so key persistence can be tested end-to-end (write/import then read/display) without introducing full signing backend integration.

### Files changed
- `pico_tnc/cmd.h`
- `pico_tnc/tty.c`
- `pico_tnc/cmd.c`
- `pico_tnc/mona_backend_minimal.h`
- `pico_tnc/mona_backend_minimal.c`
- `pico_tnc/CMakeLists.txt`
- `pico_tnc/help.c`
- `README.md`
- `README_JP.md`
- `PLAN.md`
- `WORKLOG.md`

### Behavior changes
- Added `privkey show` command:
  - prints SECURITY NOTICE and waits for one key input.
  - Enter (`CR`) proceeds and prints key data.
  - any other key aborts and prints `Aborted by user.`.
- Added pending-input handling hooks in `cmd`/`tty` so confirmation can be handled before normal command parsing resumes.
- Added minimal mona crypto bootstrap (`mona_backend_minimal_init()`):
  - provides SHA-256 only (sufficient for WIF Base58Check encode/decode used by `set`/`show`).
  - keeps RIPEMD160/HMAC/secp funcs as unsupported stubs (no sign/verify enablement in this step).
- `privkey show` output now includes:
  - typed WIFs for `p2pkh`, `p2sh` (`p2wpkh-p2sh:`), `p2wpkh` (`p2wpkh:`)
  - raw hex key
  - active type
  - address lines are explicitly marked unavailable in this stage.

### Validation status
- Static checks:
  - command table includes `PRIVKEY` and `show/gen/set` paths.
  - pending-input interception path is wired in `tty_input()`.
  - build list includes `mona_backend_minimal.c` and excludes `tools/*`.
- Build attempted with `cmake -S . -B build && cmake --build build -j4`, but full build verification could not complete because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured in this environment.

### Remaining risks / TODO
- Address derivation is intentionally deferred until secp+hash backend is implemented; `show` currently prints address lines as unavailable.
- Minimal backend currently supports only SHA-256 needed for WIF encode/decode; sign/verify path remains intentionally disabled.

## 2026-04-17

### Summary
Implemented the first staged integration step for `libmona_pico` keyslot persistence without adding signing/verification runtime dependencies:
- extended persistent settings with minimal Monacoin keyslot fields (raw 32-byte secret, valid flag, compressed flag, active address type)
- added `privkey gen [m|p|mona1|p2pkh|p2sh|p2wpkh]`
- added `privkey set <WIF or RAW>`
- integrated only required `libmona_pico` core/adapter sources into firmware build (`tools/*` remains excluded)

### Files changed
- `pico_tnc/tnc.h`
- `pico_tnc/tnc.c`
- `pico_tnc/cmd.c`
- `pico_tnc/help.c`
- `pico_tnc/CMakeLists.txt`
- `README.md`
- `README_JP.md`
- `PLAN.md`
- `WORKLOG.md`

### Behavior changes
- New command `privkey gen [type]`:
  - generates a random 32-byte secret and stores it in `param`.
  - stores `compressed=true` and normalized active type (`p2pkh`/`p2sh`/`p2wpkh`).
  - supports aliases `m`, `p`, `mona1`.
- New command `privkey set <WIF or RAW>`:
  - imports typed/untyped WIF or 64-hex raw key using `mona_pico_api`.
  - typed WIF updates active type from prefix.
  - untyped WIF and RAW keep the existing active type; if no prior key exists, default type is `p2pkh`.
- Persistence format remains minimal:
  - `mona_privkey[32]`
  - `mona_privkey_valid`
  - `mona_privkey_compressed`
  - `mona_active_type`
- No WIF/address strings are persisted.

### Validation status
- Command-level static validation by source review:
  - alias normalization is handled via `mona_parse_addr_type()`.
  - typed WIF policy (`keep_active_type_if_untyped=true`) is wired in `cmd_privkey`.
- Build attempted with `cmake -S . -B build && cmake --build build -j4`, but this environment still lacks `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`), so full firmware build verification could not complete.

### Remaining risks / TODO
- Random secret generation currently checks only for all-zero and does not enforce secp256k1 curve-order bounds; tighten after backend/sign path integration.
- Existing flash records created by older firmware may contain `0xff` in newly added fields; startup sanitization now clamps invalid values to safe defaults.
- `privkey show`/`privkey type` and sign/verify command surfaces are not implemented in this step.

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

## 2026-04-18

### Request
Implement `privkey gen [type]` entropy-collection mode using user key input bytes + timing, then generate/store a new private key with normalized active type.

### Files changed
- `pico_tnc/cmd.c`
- `WORKLOG.md`
- `PLAN.md`

### Behavior changes
- Replaced immediate RNG-based `privkey gen` with interactive entropy collection mode:
  - command prints English guidance and starts `Remaining entropy counter: 640`.
  - per-key event mixes input byte, event index, high-resolution time, delta from previous event, burst length, and counter values into 32-byte internal hash state.
  - pasted/line-buffered bursts are accepted but score lower, so the visible counter decreases more slowly.
- Added `privkey gen` pending-input state machine that runs in existing TTY pending-input path (no queue model changes).
- On counter completion, derives a 32-byte secret from the accumulated state and re-hashes until a valid secp256k1 private-key range value is obtained.
- Stores generated key into persistent settings as:
  - `mona_privkey[32]` raw secret
  - `mona_privkey_compressed = 1` (via keyslot init)
  - `mona_active_type` normalized to `p2pkh|p2sh|p2wpkh`
  - `mona_privkey_valid = 1`

### Validation status
- Build attempted with `cmake -S . -B build && cmake --build build -j4`; build is not possible in this environment because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured.

### Remaining risks / TODOs
- Entropy quality scoring uses timing/burst heuristics only; it is intentionally lightweight and not a certified TRNG assessment.
- Entropy collection now emits frequent short progress lines (`Remaining entropy counter: ...`); queue sizes were left unchanged, but heavy host-side buffering could still make visual updates appear bursty.

## 2026-04-18

### Request
Refine `privkey gen` UX with explicit operation/progress confirmation, ESC abort path, inline countdown overwrite, and save confirmation (`Enter` to save / `ESC` to abort).

### Files changed
- `pico_tnc/cmd.c`
- `WORKLOG.md`

### Behavior changes
- Generation start message updated to explicit operation notices:
  - `Initiating private key generation.`
  - keyboard-mash guidance
  - ESC abort warning block
- Progress counter is now shown inline and updated with carriage-return overwrite on the same line as much as possible.
- When entropy collection completes, key is prepared but not persisted immediately:
  - shows `Private key generation complete.`
  - waits for `[Enter]` to save or `[ESC]` to abort.
- On save, prints completion + critical key backup warning block.
- On abort (during collection or save confirmation), prints exactly `Aborted by user.` and returns to prompt without saving.

### Validation status
- Build attempted with `cmake -S . -B build && cmake --build build -j4`; build is not possible in this environment because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured.

### Remaining risks / TODOs
- Inline overwrite display behavior may vary by terminal emulation and host buffering; queue size and buffering constants were not changed.

## 2026-04-18

### Request
Clarify pending-input state naming so it is not tied to `privkey show` only.

### Files changed
- `pico_tnc/cmd.c`
- `WORKLOG.md`

### Behavior changes
- Renamed the pending-input state type/variables to command-generic names:
  - `privkey_show_state_t` -> `cmd_pending_state_t`
  - `privkey_show_state` -> `cmd_pending_state`
  - `privkey_show_ttyp` -> `cmd_pending_ttyp`
- Updated enum value names to clearly separate `privkey show` confirm and `privkey gen` collecting states.
- No functional behavior change; this is a readability/maintenance naming cleanup.

### Validation status
- Build attempted with `cmake -S . -B build && cmake --build build -j4`; build is not possible in this environment because `PICO_SDK_PATH` (or `PICO_SDK_FETCH_FROM_GIT`) is not configured.

### Remaining risks / TODOs
- None newly introduced by this rename-only cleanup.

## 2026-04-20

### Summary
Fixed build break in `cmd_consume_pending_input()` by restoring the missing converse port constant used by sign TX confirmation.

### Files changed
- `pico_tnc/cmd.c`
- `WORKLOG.md`

### Behavior changes
- No functional behavior change intended.
- `sign` TX confirmation path now compiles again because `CONVERSE_PORT` is defined in `cmd.c`.

### Validation status
- Build attempted with:
  - `cmake --build build -j4` (failed: `build` directory did not exist)
  - `cmake -S . -B build` (failed: `PICO_SDK_PATH` not set in this environment)
- Full firmware build could not be completed in this environment.

### Remaining risks / TODO
- `CONVERSE_PORT` is now defined in both `tty.c` and `cmd.c`; consider centralizing this constant in a shared header in a later cleanup change.
