#!/usr/bin/env bash
# Sets keys in a section of a mod's ini on the Steam Deck, adding any key the section lacks,
# after a timestamped backup. Keeps the file's line endings (CRLF if it uses CRLF). Refuses
# while HITMAN runs, and fails if the section doesn't exist.
# Usage: scripts/ini-set.sh <ini name> <section> key=value [key=value ...]
# e.g. scripts/ini-set.sh freelancerrandomizer.ini settings log_loadout=true
# Host and game folder come from DECK_HOST and GAME_DIR, set in the environment or in
# scripts/local.env (see scripts/local.env.example); the environment wins.
set -euo pipefail

if [ $# -lt 3 ]; then
    echo "usage: $0 <ini name> <section> key=value [key=value ...]" >&2
    exit 1
fi
ini="$1"
section="$2"
shift 2
for pair in "$@"; do
    case "${pair}" in
        *$'\n'*) echo "a pair can't contain a newline: '${pair}'" >&2; exit 1 ;;
        =*) echo "empty key in '${pair}'" >&2; exit 1 ;;
        *=*)
            key="${pair%%=*}"
            key="${key//[$' \t']/}"
            if [ -z "${key}" ]; then
                echo "empty key in '${pair}' (only whitespace before =)" >&2
                exit 1
            fi
            ;;
        *) echo "not a key=value pair: '${pair}'" >&2; exit 1 ;;
    esac
done

cd "$(dirname "$0")/.."

# shellcheck source=scripts/lib/deck.sh
. scripts/lib/deck.sh

if hitman_running; then
    echo "HITMAN is running on ${DECK_HOST}; not editing ${ini}" >&2
    exit 1
fi

remote="${GAME_DIR}/Retail/mods/${ini}"
remote_args="$(printf '%q ' "${remote}" "${section}" "$@")"

# Each pair is split on its first '='. Existing keys match with optional whitespace around
# '='; keys the section lacks are added at its end. Exit 3 means the section is missing.
status=0
ssh -- "${DECK_HOST}" "bash -s -- ${remote_args}" <<'EOF' || status=$?
set -euo pipefail
f="$1"; want="$2"; shift 2
if [ ! -f "${f}" ]; then
    echo "${f} not found" >&2
    exit 3
fi
crlf=0
if grep -q $'\r$' "${f}"; then
    crlf=1
fi
PAIRS="$(printf '%s\n' "$@")" WANT="${want}" awk -v crlf="${crlf}" '
function trim(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); return s }
function header(s) { s = trim(s); return substr(s, 2, length(s) - 2) }
function flush(   i, k) {
    for (i = 1; i <= n; i++) { k = order[i]; if (!(k in done)) { print k " = " set[k]; done[k] = 1 } }
}
BEGIN {
    want = ENVIRON["WANT"]
    ORS = crlf ? "\r\n" : "\n"
    n = split(ENVIRON["PAIRS"], list, "\n")
    for (i = 1; i <= n; i++) {
        eq = index(list[i], "=")
        k = trim(substr(list[i], 1, eq - 1))
        set[k] = substr(list[i], eq + 1)
        order[i] = k
    }
}
{ sub(/\r$/, "") }
/^[ \t]*\[.*\][ \t]*$/ {
    if (sec == want) flush()
    sec = header($0)
    if (sec == want) found = 1
    print
    next
}
{
    eq = index($0, "=")
    if (sec == want && eq > 0) {
        k = trim(substr($0, 1, eq - 1))
        if (k in set) { print k " = " set[k]; done[k] = 1; next }
    }
    print
}
END {
    if (sec == want) flush()
    if (!found) exit 3
}' "${f}" > "${f}.new" || {
    status=$?
    rm -f "${f}.new"
    if [ "${status}" -eq 3 ]; then
        echo "no [${want}] section in ${f}" >&2
    fi
    exit "${status}"
}
cp "${f}" "${f}.bak-$(date +%Y%m%d-%H%M%S)"
mv "${f}.new" "${f}"
# Show the section as it is now.
WANT="${want}" awk '
BEGIN { want = ENVIRON["WANT"] }
{ sub(/\r$/, "") }
/^[ \t]*\[.*\][ \t]*$/ { s = $0; gsub(/^[ \t]*\[|\][ \t]*$/, "", s); in_sec = (s == want) }
in_sec { print NR ": " $0 }' "${f}"
EOF

if [ "${status}" -eq 255 ]; then
    echo "Could not edit ${ini} on ${DECK_HOST} over SSH (exit 255); is SSH enabled on the target?" >&2
    exit 1
elif [ "${status}" -ne 0 ]; then
    exit "${status}"
fi
