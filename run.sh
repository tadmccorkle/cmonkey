#!/bin/sh

SRC_DIR="$(dirname $0)"
BIN_NAME="cmonkey"

BUILD_FLAGS="-Wno-gnu-zero-variadic-macro-arguments"
LINT_STRICT_FLAGS="$BUILD_FLAGS -Wno-declaration-after-statement -Wno-pre-c11-compat -Wno-padded"
LINT_LAX_FLAGS="$LINT_STRICT_FLAGS -Wno-unused-macros -Wno-switch-enum -Wno-covered-switch-default -Wno-poison-system-directories"

slint() {
	clang $SRC_DIR/src/main.c -fsyntax-only -Weverything $LINT_STRICT_FLAGS "$@"
}

lint() {
	clang $SRC_DIR/src/main.c -fsyntax-only -Weverything $LINT_LAX_FLAGS "$@"
}

build() {
	mkdir -p $SRC_DIR/bin
	clang $SRC_DIR/src/main.c -Wall -Wextra -Wpedantic $BUILD_FLAGS -o $SRC_DIR/bin/$BIN_NAME "$@"
}

clean() {
	rm -rf $SRC_DIR/bin
}

run() {
	build && $SRC_DIR/bin/$BIN_NAME "$@"
}

# _completions() {
# 	cat <<EOF
# # Commands
# command : build   : Build the program
# command : clean   : Clean build artifacts
# command : run     : Build and run the program
#
# # Global flags
# flag : --version : Show version
# flag : -h        : Show help
# flag : --help    : Show help
# EOF
# }

case "${1:-}" in
	build|lint|slint|clean|run)
		"$@"
		;;
	--version)
		echo "cmonkey run.sh version 1.0.0"
		;;
	*)
		run "$@"
		;;
esac
