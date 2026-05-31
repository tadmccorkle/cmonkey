# AGENTS.md

## Project

`cmonkey` is a C implementation of an interpreter for the Monkey programming language (from *Writing an Interpreter in Go*). It is a single-translation-unit program: all source lives in [src/main.c](src/main.c) and the built binary is written to `bin/cmonkey`. Source will be split to multiple files later during development and compiled as a unity build.

## Layout

- [src/main.c](src/main.c) - the entire program. It is organized into sections delimited by `////` banner comments:
  - `std` - base typedefs, common macros (`internal`, `KiB`, `min`, ...).
  - `ll` - singly-linked-list helper macros.
  - `arena` - arena allocator and scratch arenas (`Arena`, `TmpArena`, `scratch_begin`/`scratch_end`).
  - `strings` - `Str8` length-based string type and helpers.
  - `lex` - tokenizer.
  - `parse` - Pratt parser producing AST + diagnostic `Message`s.
  - `eval` - tree-walking evaluator, `Object` model, and `Env` environment.
  - `main` - entry point and CLI dispatch.
  - `rlpl` / `rppl` / `repl` - read-lex/parse/eval-print loops.
  - `test` - in-binary test suite.
- [run.sh](run.sh) - build/lint/run driver (see below). Invoke instead of calling `clang` directly so the right flags are used.
- [bin/](bin/) - build output (gitignored).
- [.scratch/](.scratch/) - local scratch dir (gitignored); fine to drop throwaway files here.
- [.clang-format](.clang-format) - formatting config; run `clang-format` before finishing a change.

## Build, run, lint

All workflows go through `./run.sh`:

- `./run.sh build` - build `bin/cmonkey` with `-Wall -Wextra -Wpedantic`.
- `./run.sh run [args...]` - build then run; same as `./run.sh` with no subcommand. Args are forwarded to the binary.
- `./run.sh lint` - `clang -fsyntax-only -Weverything` with the project's relaxed warning set. Prefer this over `slint` when changing code.
- `./run.sh slint` - strict lint (fewer warnings disabled). Not typically used during development. Only used to establish baseline to track warnings over time.
- `./run.sh clean` - remove `bin/`.

Extra flags after the subcommand are forwarded to `clang` (e.g. `./run.sh build -O2 -g`).

## Running the interpreter

The binary dispatches on `argv[1]`:

- `./bin/cmonkey` - full REPL (lex -> parse -> eval).
- `./bin/cmonkey lex` - RLPL, prints token stream.
- `./bin/cmonkey parse` - RPPL, prints AST / parser diagnostics.
- `./bin/cmonkey test` - runs the in-binary test suite.
  - **Always run this after changes to `lex`, `parse`, or `eval`.**
  - A non-zero exit code means a test failed; details are printed to stdout.

## Verification checklist for any change

1. `./run.sh lint` - must be clean.
2. `./run.sh build` - must build without warnings.
3. `./run.sh test` - must exit 0 if you touched lexer/parser/evaluator code.
4. `clang-format -i src/main.c` - keep formatting consistent.

## Conventions

- Single translation unit. New code goes into the matching `////` section in [src/main.c](src/main.c); do not split into new files.
- Use the project typedefs (`u32`, `s64`, `b32`, `usize`, `Str8`, ...) rather than raw stdint / `char *` / `size_t` where an alias exists.
- Function-internal storage: `internal` for file-scope helpers, `global` for module globals, `local_persist` for function-local statics, `per_thread` for thread-locals.
- Memory: allocate from an `Arena`. For transient work inside a function use `scratch_begin(conflict)` / `scratch_end` and free everything at the end of the scope. Do not introduce `malloc`/`free` outside of arena internals.
- Strings: prefer `Str8` and the `str8_*` helpers; use `str8_va(s)` with the `"%.*s"` format specifier when printing.
- Annotate work-in-progress thoughts with `// TODO(ai): ...` and design notes with `// NOTE(ai): ...` to match the existing style.
- Formatting is enforced by [.clang-format](.clang-format) - 2-space indent, 100-column limit, Allman-style braces, pointer-right (`T *x`).
