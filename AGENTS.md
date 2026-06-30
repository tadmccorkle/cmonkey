# AGENTS.md

## Project

`cmonkey` is a C implementation of an interpreter for the Monkey programming language (from *Writing an Interpreter in Go*). It is built as a **unity build**: each executable has a single translation unit (a `.c` file) that `#include`s the headers and then the implementation `.c` files of every subsystem it needs. The interpreter binary is written to `bin/cmonkey` and the test binary to `bin/cmonkey.test`.

## Layout

The program is split into a base layer (`src/base`) and one file per major interpreter component, plus separate entry points for the interpreter and the test suite.

- [src/main.c](src/main.c) - interpreter entry point. Includes the base layer plus `lex`, `parse`, and `eval`, dispatches on `argv[1]`, and contains the `rlpl` / `rppl` / `repl` loops. Built to `bin/cmonkey`.
- [src/test.c](src/test.c) - test-suite entry point and the in-binary tests. A separate unity build that includes the same subsystems plus [src/test.h](src/test.h) (test assertion macros). Built to `bin/cmonkey.test`.
- `src/base/` - the base layer. [base.h](src/base/base.h) / [base.c](src/base/base.c) are aggregate headers/units that pull in the sublayers:
  - [base_core.h](src/base/base_core.h) / [base_core.c](src/base/base_core.c) - base typedefs (`u32`, `s64`, `b32`, `usize`, ...), common macros (`internal`, `global`, `KiB`, `min`, `arr_count`, ...), and singly-linked-list helper macros.
  - [base_arena.h](src/base/base_arena.h) / [base_arena.c](src/base/base_arena.c) - arena allocator and scratch arenas (`Arena`, `TmpArena`, `scratch_begin`/`scratch_end`).
  - [base_string.h](src/base/base_string.h) / [base_string.c](src/base/base_string.c) - `Str8` length-based string type and helpers.
- [src/lex.h](src/lex.h) / [src/lex.c](src/lex.c) - tokenizer.
- [src/parse.h](src/parse.h) / [src/parse.c](src/parse.c) - Pratt parser producing AST + diagnostic `Message`s.
- [src/eval.h](src/eval.h) / [src/eval.c](src/eval.c) - tree-walking evaluator, `Object` model, and `Env` environment.
- [run.sh](run.sh) - build/lint/run/test driver (see below). Invoke instead of calling `clang` directly so the right flags are used.
- [bin/](bin/) - build output (gitignored).
- [.scratch/](.scratch/) - local scratch dir (gitignored); fine to drop throwaway files here.
- [.clang-format](.clang-format) - formatting config; run `clang-format` before finishing a change.

## Build, run, lint, test

All workflows go through `./run.sh`:

- `./run.sh build` - build `bin/cmonkey` from [src/main.c](src/main.c) with `-Wall -Wextra -Wpedantic`.
- `./run.sh run [args...]` - build then run; same as `./run.sh` with no subcommand. Args are forwarded to the binary.
- `./run.sh test` - syntax-check [src/main.c](src/main.c), then build `bin/cmonkey.test` from [src/test.c](src/test.c) and run it. A non-zero exit code means a test failed; details are printed to stdout.
- `./run.sh lint` - `clang -fsyntax-only -Weverything` on [src/main.c](src/main.c) with the project's relaxed warning set. Prefer this over `slint` when changing code.
- `./run.sh slint` - strict lint (fewer warnings disabled). Not typically used during development. Only used to establish a baseline to track warnings over time.
- `./run.sh clean` - remove `bin/`.

Extra flags after the subcommand are forwarded to `clang` (e.g. `./run.sh build -O2 -g`).

## Running the interpreter

The binary dispatches on `argv[1]`:

- `./bin/cmonkey` - full REPL (lex -> parse -> eval).
- `./bin/cmonkey lex` - RLPL, prints token stream.
- `./bin/cmonkey parse` - RPPL, prints AST / parser diagnostics.

The test suite is a **separate binary**: run it with `./run.sh test` (or directly via `./bin/cmonkey.test`), not through the interpreter binary. **Always run `./run.sh test` after changes to `lex`, `parse`, or `eval`.**

## Verification checklist for any change

1. `./run.sh lint` - must be clean.
2. `./run.sh build` - must build without warnings.
3. `./run.sh test` - must exit 0 if you touched lexer/parser/evaluator code.
4. `clang-format -i <files you changed>` - keep formatting consistent.

## Conventions

- Unity build: each subsystem is a `.c`/`.h` pair under `src/` (base sublayers under `src/base/`). New code goes into the matching subsystem file; declare public items in the `.h` and define them in the `.c`. If you add a new subsystem `.c`, remember to `#include` it (and its header) in both [src/main.c](src/main.c) and [src/test.c](src/test.c) as appropriate. Do not introduce a separate per-file compilation/link step - keep the single-translation-unit-per-executable model.
- Use the project typedefs (`u32`, `s64`, `b32`, `usize`, `Str8`, ...) rather than raw stdint / `char *` / `size_t` where an alias exists.
- Function/storage qualifiers: `internal` for file-scope helpers, `global` for module globals, `local_persist` for function-local statics, `per_thread` for thread-locals.
- Memory: allocate from an `Arena`. For transient work inside a function use `scratch_begin(conflict)` / `scratch_end` and free everything at the end of the scope. Do not introduce `malloc`/`free` outside of arena internals.
- Strings: prefer `Str8` and the `str8_*` helpers; use `str8_va(s)` with the `"%.*s"` format specifier when printing.
- Annotate work-in-progress thoughts with `// TODO(ai): ...` and design notes with `// NOTE(ai): ...` to match the existing style.
- Formatting is enforced by [.clang-format](.clang-format) - 2-space indent, 100-column limit, Allman-style braces, pointer-right (`T *x`).
