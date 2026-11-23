#!/usr/bin/env bash
# Prints the version CMakeLists.txt sets (e.g. 1.0.0), or with --next-minor the next minor
# version (1.1.0), as the release pipeline bumps it.
set -euo pipefail
cd "$(dirname "$0")/.."

version="$(sed -n 's/^project(FreelancerRandomizer VERSION \([0-9]*\.[0-9]*\.[0-9]*\) .*/\1/p' CMakeLists.txt)"
if [ -z "${version}" ]; then
    echo "could not read the version from CMakeLists.txt" >&2
    exit 1
fi

case "${1-}" in
    "") echo "${version}" ;;
    --next-minor)
        IFS=. read -r major minor _ <<< "${version}"
        echo "${major}.$((minor + 1)).0"
        ;;
    *)
        echo "usage: $0 [--next-minor]" >&2
        exit 1
        ;;
esac
