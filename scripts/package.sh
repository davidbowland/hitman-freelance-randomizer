#!/usr/bin/env bash
# Packs build/FreelancerRandomizer.dll into build/FreelancerRandomizer-<version>.zip for players:
# a Retail/mods folder to copy into the game's folder, and install steps (packaging/INSTALL.txt).
# The version is the one CMakeLists.txt sets (scripts/version.sh). Run after scripts/build.sh;
# needs zip.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ $# -gt 0 ]; then
    echo "usage: $0 (takes no arguments; packs build/FreelancerRandomizer.dll)" >&2
    exit 1
fi
if [ ! -f build/FreelancerRandomizer.dll ]; then
    echo "build/FreelancerRandomizer.dll not found -- run scripts/build.sh first" >&2
    exit 1
fi
# The DLL carries only recipes (src/mod/LandslideRecipes.inc); game data generated locally
# (src/mod/generated/) must never be compiled into a shared build. grep: 0 found, 1 none, 2 failed.
status=0
included="$(grep -rlE --exclude-dir=generated '#[[:space:]]*include[[:space:]]*["<][^">]*generated/' src)" ||
    status=$?
if [ "${status}" -eq 0 ]; then
    echo "these sources include game data from src/mod/generated/, which must never be shared:" \
         "${included//$'\n'/ }" >&2
    exit 1
fi
if [ "${status}" -ne 1 ]; then
    echo "checking src/ for included game data failed (grep exit ${status})" >&2
    exit 1
fi
# The DLL must be built from the sources as they are now.
newer="$(find src CMakeLists.txt -newer build/FreelancerRandomizer.dll -type f -print -quit)"
if [ -n "${newer}" ]; then
    echo "${newer} is newer than build/FreelancerRandomizer.dll -- run scripts/build.sh first" >&2
    exit 1
fi

version="$(scripts/version.sh)"

stage="$(mktemp -d)"
trap 'rm -rf "${stage}"' EXIT
mkdir -p "${stage}/Retail/mods"
cp build/FreelancerRandomizer.dll "${stage}/Retail/mods/"
# CRLF, so Notepad on older Windows shows the lines.
sed -e "s/@VERSION@/${version}/" -e 's/$/\r/' packaging/INSTALL.txt \
    > "${stage}/How to install FreelancerRandomizer.txt"

zip_path="build/FreelancerRandomizer-${version}.zip"
rm -f "${zip_path}"
(cd "${stage}" && zip -q -X -r - "How to install FreelancerRandomizer.txt" Retail) > "${zip_path}"
echo "Packed ${zip_path}"
