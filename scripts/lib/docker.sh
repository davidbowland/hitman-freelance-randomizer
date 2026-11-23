# shellcheck shell=bash
# Sourced by build.sh, test.sh and format.sh, from the repository root.
# Fetches the DevPkg (unless the caller set FR_SKIP_DEVPKG=1), builds the
# fl-randomizer-build image if needed, and defines docker_run.

IMAGE="fl-randomizer-build"

if [ "${FR_SKIP_DEVPKG:-}" != 1 ]; then
    scripts/fetch-devpkg.sh
fi
# Cached after the first run; -q keeps the layer log out of the build output.
echo "Building build image if needed (first run downloads the Windows SDK and takes several minutes)..."
docker build -q -t "${IMAGE}" docker >/dev/null

# The container runs as the calling user, so build outputs aren't owned by root; HOME=/tmp
# gives CMake a writable home.
docker_args=(--rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$(pwd)":/src -w /src)

# A non-empty DOCKER_CPUS wins. In CI (CI set, no DOCKER_CPUS) no limit is passed. Otherwise
# two thirds of the Docker VM's cores, at least 1, so the machine stays usable during a build.
if [ -n "${DOCKER_CPUS:-}" ]; then
    docker_args+=(--cpus "${DOCKER_CPUS}")
elif [ -z "${CI:-}" ]; then
    ncpu="$(docker info --format '{{.NCPU}}' 2>/dev/null || true)"
    case "${ncpu}" in
        ''|*[!0-9]*) docker_cpus=3 ;; # docker info failed or printed something unexpected
        *) docker_cpus=$(( ncpu * 2 / 3 )) ;;
    esac
    if [ "${docker_cpus}" -lt 1 ]; then
        docker_cpus=1
    fi
    docker_args+=(--cpus "${docker_cpus}")
fi

# Runs a command in the build image with the repository mounted at /src. Callers add their
# own docker run options (e.g. -e) to docker_args first.
docker_run() {
    docker run "${docker_args[@]}" "${IMAGE}" "$@"
}
