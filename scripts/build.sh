#!/usr/bin/env bash
# Cross-builds build/FreelancerRandomizer.dll and build/FreelancerRandomizer.pdb in the
# fl-randomizer-build Docker image. FR_DWARF=OFF leaves the DWARF debug info (for GDB under
# Proton) out of the DLL, as CI does for releases; it defaults to ON.
set -euo pipefail
cd "$(dirname "$0")/.."

# shellcheck source=scripts/lib/docker.sh
. scripts/lib/docker.sh

# A cache from before FR_DWARF has -gdwarf baked into the RelWithDebInfo flags, which would
# override FR_DWARF; start that build dir's configuration over.
if [ -f build/CMakeCache.txt ] &&
   grep -E '^CMAKE_(C|CXX)_FLAGS_RELWITHDEBINFO[:=].*gdwarf' build/CMakeCache.txt >/dev/null; then
    echo "Removing build/CMakeCache.txt (it predates FR_DWARF)"
    rm -f build/CMakeCache.txt
fi

# shellcheck disable=SC2016 # $1 expands in the container
docker_run bash -c 'cmake -S /src -B /src/build -G Ninja \
                         -DCMAKE_TOOLCHAIN_FILE=/src/cmake/clang-cl.cmake \
                         -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                         -DFR_DWARF="$1" \
                     && cmake --build /src/build' build "${FR_DWARF:-ON}"

dll=build/FreelancerRandomizer.dll
if [ ! -f "${dll}" ]; then
    echo "Build finished but ${dll} is missing" >&2
    exit 1
fi
echo "Built $(pwd)/${dll}"
