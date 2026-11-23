#!/usr/bin/env bash
# Copies build/FreelancerRandomizer.dll to the Steam Deck (or another Linux machine running the game
# over SSH) and enables it in Retail/mods.ini. Requires SSH on the target. Host and game
# folder come from DECK_HOST and GAME_DIR, set in the environment or in scripts/local.env
# (see scripts/local.env.example); the environment wins.
set -euo pipefail

if [ $# -gt 0 ]; then
    echo "usage: $0 (takes no arguments; deploys build/FreelancerRandomizer.dll)" >&2
    exit 1
fi

cd "$(dirname "$0")/.."

# shellcheck source=scripts/lib/deck.sh
. scripts/lib/deck.sh
RETAIL="${GAME_DIR}/Retail"

if [ ! -f build/FreelancerRandomizer.dll ]; then
    echo "build/FreelancerRandomizer.dll not found -- run scripts/build.sh first" >&2
    exit 1
fi

if hitman_running; then
    echo "HITMAN is running on ${DECK_HOST} -- quit HITMAN first" >&2
    exit 1
fi

if ! ssh -- "${DECK_HOST}" "test -f $(printf '%q' "${RETAIL}/HITMAN3.exe")"; then
    echo "HITMAN3.exe not found in ${RETAIL} on ${DECK_HOST} -- set GAME_DIR" >&2
    exit 1
fi

# Stream to a temp file and rename, so a dropped connection never leaves a partial DLL.
dest="$(printf '%q' "${RETAIL}/mods/FreelancerRandomizer.dll")"
ssh -- "${DECK_HOST}" "mkdir -p $(printf '%q' "${RETAIL}/mods") && cat > ${dest}.tmp && mv -f ${dest}.tmp ${dest}" \
    < build/FreelancerRandomizer.dll
echo "Copied build/FreelancerRandomizer.dll to ${DECK_HOST}:${RETAIL}/mods/"

# Adds the [freelancerrandomizer] section (the SDK reads mods.ini sections lowercased) when it is
# missing, backing mods.ini up first to a timestamped mods.ini.bak-YYYYMMDD-HHMMSS (never
# overwriting an earlier backup). New lines use the file's line ending (CRLF if it already
# has one).
ssh -- "${DECK_HOST}" "bash -s -- $(printf '%q' "${RETAIL}")" <<'EOF'
set -euo pipefail
retail="$1"
section=freelancerrandomizer
ini="${retail}/mods.ini"
if [ ! -f "${ini}" ]; then
    echo "mods.ini not found in ${retail} -- start the game once with the SDK installed" >&2
    exit 1
fi
eol='\n'
if grep -q $'\r$' "${ini}"; then
    eol='\r\n'
fi
if grep -qix "\[${section}\][[:space:]]*" "${ini}"; then
    echo "[${section}] already present in mods.ini"
    exit 0
fi
backup="${ini}.bak-$(date +%Y%m%d-%H%M%S)"
if [ -e "${backup}" ]; then
    echo "$(basename "${backup}") already exists -- run again in a second" >&2
    exit 1
fi
cp "${ini}" "${backup}"
echo "Backed up mods.ini to $(basename "${backup}")"
# Make sure the new section starts on its own line.
if [ -s "${ini}" ] && [ -n "$(tail -c1 "${ini}")" ]; then
    printf '%b' "${eol}" >> "${ini}"
fi
printf '[%s]%b' "${section}" "${eol}" >> "${ini}"
echo "Added [${section}] to mods.ini"
EOF

cat <<EOF

Next steps:
  1. Start HITMAN (a mod must be enabled in mods.ini before the game starts).
  2. Play a Freelancer mission.
  3. Run scripts/fetch-log.sh to copy the logs back.
EOF
