#!/usr/bin/env bash
#
# Run clang-tidy on our code, with the correct compiler options set (since it
# doesn't seem to extract those from the compilation database)
#
# TODO: There are a few use-after-move warning for the error codes in Boost's
#       library code. Is there any way to silence these?

set -euo pipefail

# This is the repository's root
cd "$(dirname "$0")/.."

exec run-clang-tidy -p build \
  -extra-arg='-m64' \
  -extra-arg='-std=c++2a' \
  -extra-arg='-I/usr/include/wine' \
  src/{common,plugin,wine-host}
