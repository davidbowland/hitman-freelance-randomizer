#!/usr/bin/env bash
# Formats the C++ sources (src/**/*.{h,cpp} and tests/**/*.{h,cpp}, not tests/doctest.h) with
# clang-format-19 and .clang-format, inside the fl-randomizer-build Docker image.
# --check changes nothing and exits non-zero if any file is not formatted (CI runs this).
set -euo pipefail
cd "$(dirname "$0")/.."

check=0
case "${1:-}" in
    "") ;;
    --check) check=1 ;;
    *)
        echo "usage: scripts/format.sh [--check]" >&2
        exit 1
        ;;
esac

# Tracked files only, so untracked or ignored files (e.g. third_party, src/mod/generated)
# are never touched.
files=()
while IFS= read -r file; do
    files+=("${file}")
done < <(git ls-files -- ':(glob)src/**/*.h' ':(glob)src/**/*.cpp' ':(glob)tests/**/*.h' ':(glob)tests/**/*.cpp' \
    ':(exclude)tests/doctest.h' | LC_ALL=C sort)

if [ "${#files[@]}" -eq 0 ]; then
    echo "No source files found" >&2
    exit 1
fi

# clang-format needs only the image, not the DevPkg.
FR_SKIP_DEVPKG=1
# shellcheck source=scripts/lib/docker.sh
. scripts/lib/docker.sh

if [ "${check}" -eq 1 ]; then
    if ! docker_run clang-format-19 --dry-run --Werror "${files[@]}"; then
        echo "Some files are not formatted; run scripts/format.sh and commit the result." >&2
        exit 1
    fi
    echo "Formatting is clean (${#files[@]} files)."
else
    docker_run clang-format-19 -i "${files[@]}"
    echo "Formatted ${#files[@]} files."
fi
