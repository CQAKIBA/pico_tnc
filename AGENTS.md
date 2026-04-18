# Repository instructions

This repository contains firmware for the `pico_tnc` project.
Keep changes small, reviewable, and easy to revert.

## Priorities
1. Preserve existing firmware behavior unless the task explicitly changes it.
2. Prefer minimal diffs over large refactors.
3. Keep memory usage and queue sizes visible when changing text output paths.
4. When changing user-visible commands, update both `README.md` and `README_JP.md` if needed.

## Working rules
- Prefer one feature per commit.
- Do not rename unrelated files.
- Avoid broad formatting-only edits.
- Keep comments concise and technical.
- When adding generated data, keep the generator script in the repo.

## Build and validation
- Build the firmware after any non-trivial change.
- If a build cannot be run, record that fact in `WORKLOG.md`.
- When changing USB/TTY/output code, note possible RAM impact and queue behavior in `WORKLOG.md`.

## Logging after each task
After making code changes, update `WORKLOG.md` with:
- date
- summary of requested change
- files changed
- behavior changes
- validation status
- remaining risks or TODOs

If the task is part of a multi-step effort, also update `PLAN.md`.

## Generated files
- Put generator scripts under `tools/`.
- Mark generated files clearly at the top of the file.
- If a generated table is updated, update the generator script in the same change.

## Current project-specific plan
The current planned work around help output is tracked in `PLAN.md`.

## Structural separation

The contents of libmona_pico should be separated into layers and not mixed together.

* Core layer:

  * `mona_compat.c`
  * `mona_compat.h`
  * Contains Monacoin-specific pure logic only.
  * Must not depend on PICO-TNC command parsing, settings storage, UI, USB/TTY output, or platform-specific code.
  * Must use an abstract crypto interface only.

* Adapter layer:

  * `mona_pico_api.c`
  * `mona_pico_api.h`
  * Bridges PICO-TNC data structures and command behavior to the core layer.
  * May translate active address type, stored key format, and command-facing behavior.
  * Must not contain direct hardware access or UI-specific printing logic.

* Backend layer:

  * Crypto backend files for SHA256, HMAC-SHA256, RIPEMD160, and secp256k1.
  * This layer provides the implementation required by `mona_crypto_vtable_t`.
  * It may depend on vendored third-party crypto code.
  * Keep this layer separate from the Monacoin protocol logic.

* Application layer:

  * PICO-TNC command parser, settings structure, persistence, and user-visible output.
  * This layer may call the adapter layer.
  * It must not reimplement Monacoin signing/address logic internally.

Dependency direction must remain:
Application -> Adapter -> Core -> Backend

Do not move code in the opposite direction.
Do not mix command parsing, settings persistence, or UI formatting into the core layer.
Do not add OpenSSL-dependent files to the firmware build.
PC-side verification tools must remain outside the firmware path.

