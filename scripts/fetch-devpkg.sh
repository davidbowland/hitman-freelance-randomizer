#!/usr/bin/env bash
# Downloads and verifies the pinned ZHMModSDK DevPkg into third_party/devpkg, and fetches it
# again when the version recorded in third_party/devpkg/.version differs from VERSION.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="v4.2.1"
SHA256="d13a986b588588d6456743ba0f9746dea00b321a964b0269b98711597400e885"
URL="https://github.com/OrfeasZ/ZHMModSDK/releases/download/${VERSION}/DevPkg-ZHMModSDK.zip"
DEST="third_party/devpkg"
ZIP="third_party/DevPkg-${VERSION}.zip"

if [ -f "${DEST}/include/IPluginInterface.h" ] && [ "$(cat "${DEST}/.version" 2>/dev/null)" = "${VERSION}" ]; then
    echo "DevPkg ${VERSION} is current in ${DEST}"
    exit 0
fi

mkdir -p third_party
curl -fsSL -o "${ZIP}" "${URL}"
actual="$(shasum -a 256 "${ZIP}" 2>/dev/null || sha256sum "${ZIP}")"
actual="${actual%% *}"
if [ "${actual}" != "${SHA256}" ]; then
    echo "DevPkg checksum mismatch: expected ${SHA256}, got ${actual}" >&2
    rm -f "${ZIP}"
    exit 1
fi

rm -rf "${DEST}"
unzip -q "${ZIP}" -d "${DEST}"
printf '%s\n' "${VERSION}" > "${DEST}/.version"
echo "DevPkg ${VERSION} extracted to ${DEST}"
