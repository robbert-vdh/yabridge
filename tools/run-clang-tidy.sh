#!/usr/bin/env bash
#
# Run clang-tidy on our code, with the correct compiler options set (since it
# doesn't seem to extract those from the compilation database)

set -euo pipefail

# This is the repository's root
cd "$(dirname "$0")/.."

shopt -s globstar nocaseglob
parallel clang-tidy --use-color --extra-arg='-m64' --extra-arg='-std=c++2a' \
    ::: src/plugin/**/*.cpp src/common/**/*.cpp
parallel clang-tidy --use-color --extra-arg='-m64' --extra-arg='-std=c++2a' \
    --extra-arg='-DWIN32' --extra-arg='-D_WIN32' --extra-arg='-D__WIN32__' --extra-arg='-D_WIN64' \
    --extra-arg='-isystem/usr/include/wine/windows' \
    ::: src/wine-host/**/*.cpp src/common/**/*.cpp
