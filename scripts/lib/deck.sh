# shellcheck shell=bash
# Shared by deploy.sh, fetch-log.sh and ini-set.sh. Loads scripts/local.env, checks
# DECK_HOST, defaults GAME_DIR, and defines hitman_running. Remote paths and arguments are
# shell-quoted with printf %q: ssh joins its arguments into one string that the Deck's
# shell splits again, which would break on the space in "HITMAN 3".

# Values already in the environment win over scripts/local.env. (An empty value counts as
# unset.)
deck_env_host="${DECK_HOST-}"; deck_host_set="${DECK_HOST:+1}"
deck_env_game_dir="${GAME_DIR-}"; deck_game_dir_set="${GAME_DIR:+1}"
if [ -f "$(dirname "${BASH_SOURCE[0]}")/../local.env" ]; then
    # shellcheck source=/dev/null
    . "$(dirname "${BASH_SOURCE[0]}")/../local.env"
fi
if [ -n "${deck_host_set}" ]; then DECK_HOST="${deck_env_host}"; fi
if [ -n "${deck_game_dir_set}" ]; then GAME_DIR="${deck_env_game_dir}"; fi

if [ -z "${DECK_HOST:-}" ]; then
    echo "DECK_HOST is not set -- copy scripts/local.env.example to scripts/local.env and set it there, or export DECK_HOST" >&2
    exit 1
fi
GAME_DIR="${GAME_DIR:-/home/deck/.local/share/Steam/steamapps/common/HITMAN 3}"

# True when HITMAN is running on the Deck, false when it isn't. Exits the script when the
# Deck can't be checked (pgrep exits 0 or 1; ssh itself exits 255 on connection failure).
hitman_running() {
    local status=0
    ssh -- "${DECK_HOST}" 'pgrep -f -i "[h]itman3\.exe"' >/dev/null || status=$?
    case "${status}" in
        0) return 0 ;;
        1) return 1 ;;
        *)
            echo "Could not check ${DECK_HOST} over SSH (exit ${status}); is SSH enabled on the target?" >&2
            exit 1
            ;;
    esac
}
