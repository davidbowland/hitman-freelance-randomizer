#!/usr/bin/env bash
# Builds and runs FreelancerRandomizer's host unit tests (fr_tests) inside the fl-randomizer-build
# Docker image (native Linux compiler, not clang-cl), building the image if needed.
# FR_SANITIZE=address,undefined or FR_SANITIZE=thread builds them with those sanitizers, in
# build-host-asan or build-host-tsan. FR_RECIPE_E2E=<folder in the repository, relative to it> runs the
# end-to-end recipe check on real game files (tests/test_landslide_recipes.cpp says what the folder holds).
# Extra arguments are passed to the doctest runner.
set -euo pipefail
cd "$(dirname "$0")/.."

sanitize="${FR_SANITIZE:-}"
case "${sanitize}" in
    "") build_dir=build-host ;;
    address,undefined) build_dir=build-host-asan ;;
    thread) build_dir=build-host-tsan ;;
    *)
        echo "FR_SANITIZE must be empty, address,undefined or thread (got '${sanitize}')" >&2
        exit 1
        ;;
esac

# Compile and link flags for the sanitizer build; each build dir keeps its own cache.
sanitize_flags=""
if [ -n "${sanitize}" ]; then
    sanitize_flags="-fsanitize=${sanitize} -fno-omit-frame-pointer"
fi

# shellcheck source=scripts/lib/docker.sh
. scripts/lib/docker.sh

if [ -n "${FR_RECIPE_E2E:-}" ]; then
    docker_args+=(-e "FR_RECIPE_E2E=${FR_RECIPE_E2E}")
fi
# Undefined behavior fails the run instead of only printing a report.
docker_args+=(-e "UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}")

# shellcheck disable=SC2016 # $1 and $2 expand in the container
docker_run bash -c 'build="/src/$1" flags="$2"; shift 2
                    cmake -S /src/tests -B "${build}" -G Ninja -DCMAKE_BUILD_TYPE=Release \
                          -DCMAKE_CXX_FLAGS="${flags}" \
                          -DCMAKE_EXE_LINKER_FLAGS="${flags}" \
                    && cmake --build "${build}" \
                    && "${build}/fr_tests" "$@"' fr_tests "${build_dir}" "${sanitize_flags}" "$@"
