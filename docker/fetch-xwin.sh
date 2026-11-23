#!/usr/bin/env bash
# Downloads the MSVC CRT and Windows SDK for x64 into <root>/.xwin/splat (cache in
# <root>/.xwin/cache). Usage: fetch-xwin.sh <root>
set -euo pipefail
cd "${1:?usage: fetch-xwin.sh <root directory>}"

XWIN_SDK_VERSION="${XWIN_SDK_VERSION:-10.0.26100}"

xwin --accept-license --arch x86_64 --cache-dir .xwin/cache --sdk-version "$XWIN_SDK_VERSION" \
    splat --include-debug-libs --output .xwin/splat

# Case variants of header and library names that some code includes or links.
um=.xwin/splat/sdk/include/um
if [ -e "$um/WS2tcpip.h" ]; then
    ln -sf WS2tcpip.h "$um/Ws2tcpip.h"
fi
if [ -e "$um/shlobj.h" ]; then
    ln -sf shlobj.h "$um/Shlobj.h"
fi

umlib=.xwin/splat/sdk/lib/um/x86_64
if [ -e "$umlib/winmm.lib" ]; then
    ln -sf winmm.lib "$umlib/Winmm.lib"
fi
