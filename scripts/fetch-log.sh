#!/usr/bin/env bash
# Copies FreelancerRandomizer.log and, if present, FreelancerRandomizer.prev.log from the
# Steam Deck (or another Linux machine running the game over SSH) into logs/, named with the
# current timestamp. Fails if FreelancerRandomizer.log doesn't exist.
# Host and game folder come from DECK_HOST and GAME_DIR, set in the environment or in
# scripts/local.env (see scripts/local.env.example); the environment wins.
set -euo pipefail

cd "$(dirname "$0")/.."

# shellcheck source=scripts/lib/deck.sh
. scripts/lib/deck.sh
REMOTE_DIR="${GAME_DIR}/Retail/mods"

out_dir=logs
mkdir -p "${out_dir}"
stamp="$(date +%Y%m%d-%H%M%S)"

# Streams one file from REMOTE_DIR to a local file; removes the partial copy on failure.
fetch() {
    local remote="${REMOTE_DIR}/$1" out="$2"
    if ! ssh -- "${DECK_HOST}" "cat $(printf '%q' "${remote}")" > "${out}"; then
        rm -f "${out}"
        echo "Could not read ${DECK_HOST}:${remote}" >&2
        exit 1
    fi
    echo "Saved ${out} ($(wc -l < "${out}" | tr -d ' ') lines)"
}

# True when the file exists in REMOTE_DIR on the Deck; exits the script if the Deck is
# unreachable.
remote_exists() {
    local status=0
    ssh -- "${DECK_HOST}" "test -f $(printf '%q' "${REMOTE_DIR}/$1")" || status=$?
    if [ "${status}" -eq 255 ]; then
        echo "Could not reach ${DECK_HOST} over SSH; is SSH enabled on the target?" >&2
        exit 1
    fi
    return "${status}"
}

found=0
if remote_exists FreelancerRandomizer.log; then
    found=1
    fetch FreelancerRandomizer.log "${out_dir}/FreelancerRandomizer-${stamp}.log"
fi
if remote_exists FreelancerRandomizer.prev.log; then
    fetch FreelancerRandomizer.prev.log "${out_dir}/FreelancerRandomizer-${stamp}.prev.log"
fi

if [ "${found}" -eq 0 ]; then
    echo "FreelancerRandomizer.log not found in ${DECK_HOST}:${REMOTE_DIR}" >&2
    exit 1
fi
