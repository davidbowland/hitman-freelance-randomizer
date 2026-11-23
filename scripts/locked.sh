#!/usr/bin/env bash
# Runs a command while holding a machine-wide lock, so parallel worktrees take turns
# with Docker builds (several at once can run the Docker VM out of memory).
# Usage: scripts/locked.sh scripts/build.sh   |   scripts/locked.sh scripts/test.sh
# The lock is a symlink whose target is the owner's PID, so taking it and recording the
# owner are one atomic step; a lock whose owner has exited is broken.
# Other HITMAN mod repos use the same lock name, so their builds take turns too.
set -euo pipefail
lock="${TMPDIR:-/tmp}/hitman-mod-docker.lock"
waiting=0
while :; do
    # Older versions of this script used a directory here.
    if [ -d "${lock}" ] && [ ! -L "${lock}" ]; then
        echo "${lock} is a lock directory left by an older locked.sh;" \
             "if no build is running, remove it (rm -r \"${lock}\") and retry" >&2
        exit 1
    fi
    if ln -s "$$" "${lock}" 2>/dev/null; then
        break
    fi
    holder="$(readlink "${lock}" 2>/dev/null || true)"
    if [ -z "${holder}" ]; then
        continue # released between ln and readlink
    fi
    if ! ps -p "${holder}" >/dev/null 2>&1; then
        # Re-check just before removing, so a lock another waiter already broke and
        # re-took isn't removed too.
        if [ "$(readlink "${lock}" 2>/dev/null || true)" = "${holder}" ]; then
            echo "Breaking stale lock held by PID ${holder}" >&2
            rm -f "${lock}"
        fi
        continue
    fi
    if [ "${waiting}" -eq 0 ]; then
        echo "waiting for lock held by PID ${holder}" >&2
        waiting=1
    fi
    sleep 5
done
trap 'rm -f "${lock}"' EXIT
"$@"
