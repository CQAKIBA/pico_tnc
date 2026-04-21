# Current plan (help output work)

## 2026-04-04
- [x] Split help implementation into `help.c`/`help.h`.
- [x] Convert help output to non-blocking line-by-line send via `help_poll()`.
- [x] Add Japanese help output modes (`help ja sjis` / `help ja utf8`) and UTF-8→SJIS conversion path.
- [ ] Validate firmware behavior on hardware (USB terminal responsiveness while help is printing).
- [x] Consolidate Japanese help UTF-8→SJIS conversion into a single path in `help.c` (including katakana fallback handling).
- [x] Move UTF-8→Shift_JIS mapping exceptions (hiragana/katakana/symbols) into PHP-generated table so `help.c` keeps decode+lookup+fallback only.

## 2026-04-17
- [x] Add minimal Monacoin keyslot persistence fields to `param_t` (raw32 + compressed + active type + valid).
- [x] Add `privkey gen [m|p|mona1|p2pkh|p2sh|p2wpkh]` to generate/store keyslot seed.
- [x] Add `privkey set <WIF or RAW>` to import/store keyslot with typed-WIF active type normalization.
- [x] Add `privkey show` with interactive confirmation and persisted-key display.
- [x] Rework `privkey gen [type]` to interactive entropy collection (input bytes + timing) and secp256k1-range-safe key derivation.
- [x] Add `privkey type` command set (`privkey set [m|p|mona1|p2pkh|p2sh|p2wpkh]` for active type update).
- [ ] Add Pico crypto backend and wire sign/verify commands.
  - [x] Add `sign msg <text>` flow: JSON payload build, message signing, TX confirm prompt, and AX.25 UI-frame enqueue.
  - [x] Add `sign qsl <...>` payload format and command flow.
  - [ ] Add verify command flow.
