# Cross-compiles the mod for Windows x64 (clang-cl + xwin).
FROM ubuntu:24.04

ARG LLVM_VERSION=19
ARG XWIN_VERSION=0.10.0

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl cmake ninja-build python3 g++ \
        clang-${LLVM_VERSION} lld-${LLVM_VERSION} llvm-${LLVM_VERSION} clang-format-${LLVM_VERSION} \
    && rm -rf /var/lib/apt/lists/*

# Unversioned names (clang-cl, lld-link, llvm-lib, ...) that the toolchain file expects.
# No LLVM package ships clang-cl; clang picks its cl driver mode from that name.
ENV PATH=/usr/lib/llvm-${LLVM_VERSION}/bin:${PATH}
RUN ln -sf clang /usr/lib/llvm-${LLVM_VERSION}/bin/clang-cl

# xwin's release tarball, checked against its SHA256 (the one xwin publishes with the
# release) before use. Changing XWIN_VERSION means updating both checksums.
ARG XWIN_SHA256_X86_64=d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88
ARG XWIN_SHA256_AARCH64=6d56d28537a86f37aa3d041318898f25ee3100c6b6ec332ad873c28faf37be23
RUN arch="$(uname -m)" \
    && case "${arch}" in \
         x86_64) sha="${XWIN_SHA256_X86_64}" ;; \
         aarch64) sha="${XWIN_SHA256_AARCH64}" ;; \
         *) echo "no xwin checksum for ${arch}" >&2; exit 1 ;; \
       esac \
    && tarball="/tmp/xwin-${XWIN_VERSION}-${arch}.tar.gz" \
    && curl -fsSL -o "${tarball}" "https://github.com/Jake-Shadle/xwin/releases/download/${XWIN_VERSION}/xwin-${XWIN_VERSION}-${arch}-unknown-linux-musl.tar.gz" \
    && echo "${sha}  ${tarball}" | sha256sum -c - \
    && tar xzf "${tarball}" -C /tmp \
    && mv /tmp/xwin-${XWIN_VERSION}-${arch}-unknown-linux-musl/xwin /usr/local/bin/xwin \
    && rm -rf "${tarball}" /tmp/xwin-${XWIN_VERSION}-${arch}-unknown-linux-musl

# MSVC CRT + Windows SDK (fetch-xwin.sh pins the SDK version and adds case-variant symlinks).
# Running xwin accepts Microsoft's license for them.
COPY fetch-xwin.sh /opt/fetch-xwin.sh
RUN mkdir -p /opt/xwin && bash /opt/fetch-xwin.sh /opt/xwin && rm -rf /opt/xwin/.xwin/cache
ENV XWIN_SPLAT_DIR=/opt/xwin/.xwin/splat

WORKDIR /src
