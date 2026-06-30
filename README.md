# cmonkey

A C implementation of an interpreter for the **Monkey** programming language from [*Writing an Interpreter in Go*](https://interpreterbook.com/).

`cmonkey` is a tree-walking interpreter built as a unity build with a small base layer.

## Build & run

All workflows go through `./run.sh`:

```sh
./run.sh            # build and start the REPL (lex -> parse -> eval)
./run.sh lex        # REPL printing the token stream (RLPL)
./run.sh parse      # REPL printing the AST / parser diagnostics (RPPL)
./run.sh build      # build bin/cmonkey
./run.sh test       # build and run the test suite (bin/cmonkey.test)
./run.sh clean      # remove bin/
```
