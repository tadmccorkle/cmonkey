#!/bin/sh

CPROG_DIR="$(dirname $0)"

build() {
	mkdir -p $CPROG_DIR/bin
	clang $CPROG_DIR/src/main.c -Wall -Wextra -o $CPROG_DIR/bin/prog "$@"
}

clean() {
	rm -rf $CPROG_DIR/bin
}

run() {
	build && $CPROG_DIR/bin/prog "$@"
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
	build|clean|run)
		"$@"
		;;
	--version)
		echo "cinterpreter run.sh version 1.0.0"
		;;
	*)
		run "$@"
		;;
esac
