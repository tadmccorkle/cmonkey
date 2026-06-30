#!/bin/sh

SRC_DIR="$(dirname $0)"
BIN_NAME="cmonkey"
TEST_NAME="cmonkey.test"

BUILD_FLAGS="-std=c11 -Wno-gnu-zero-variadic-macro-arguments -Wno-undefined-internal"
LINT_STRICT_FLAGS="$BUILD_FLAGS -Wno-declaration-after-statement -Wno-pre-c11-compat -Wno-padded"
LINT_LAX_FLAGS="$LINT_STRICT_FLAGS -Wno-switch-enum -Wno-covered-switch-default -Wno-poison-system-directories"

build() {
	mkdir -p $SRC_DIR/bin
	clang $SRC_DIR/src/main.c -Wall -Wextra -Wpedantic $BUILD_FLAGS -o $SRC_DIR/bin/$BIN_NAME "$@"
}

slint() {
	clang $SRC_DIR/src/main.c -fsyntax-only -Weverything $LINT_STRICT_FLAGS "$@"
}

lint() {
	clang $SRC_DIR/src/main.c -fsyntax-only -Weverything $LINT_LAX_FLAGS "$@"
}

test() {
	mkdir -p $SRC_DIR/bin
	clang $SRC_DIR/src/main.c -fsyntax-only -Wall -Wextra -Wpedantic $BUILD_FLAGS
	clang $SRC_DIR/src/test.c -Wall -Wextra -Wpedantic $BUILD_FLAGS -o $SRC_DIR/bin/$TEST_NAME "$@"
	$SRC_DIR/bin/$TEST_NAME
}

clean() {
	rm -rf $SRC_DIR/bin
}

run() {
	build && $SRC_DIR/bin/$BIN_NAME "$@"
}

case "${1:-}" in
	build|lint|slint|test|clean|run)
		"$@"
		;;
	--info)
		echo "cmonkey run.sh"
		;;
	*)
		run "$@"
		;;
esac
