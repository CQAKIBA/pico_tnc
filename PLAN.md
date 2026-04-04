# Current plan (help output work)

## 2026-04-04
- [x] Split help implementation into `help.c`/`help.h`.
- [x] Convert help output to non-blocking line-by-line send via `help_poll()`.
- [x] Add Japanese help output modes (`help ja sjis` / `help ja utf8`) and UTF-8→SJIS conversion path.
- [ ] Validate firmware behavior on hardware (USB terminal responsiveness while help is printing).
- [x] Consolidate Japanese help UTF-8→SJIS conversion into a single path in `help.c` (including katakana fallback handling).
- [x] Move UTF-8→Shift_JIS mapping exceptions (hiragana/katakana/symbols) into PHP-generated table so `help.c` keeps decode+lookup+fallback only.
