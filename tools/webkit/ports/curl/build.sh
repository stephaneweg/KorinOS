#!/bin/sh
# build.sh -- curl for Onyx (third_party/curl-8.16.0, the curl licence): libcurl.a + its headers
# into the POSIX sysroot, and the curl tool (out/ports/bin/curl.elf). CMake with
# tools/onyx-toolchain.cmake.
#
# TLS: OpenSSL (tools/webkit/ports/openssl/build.sh, built first). HTTP/2: nghttp2; compression: zlib and
# brotli (all in third_party). The threaded resolver (getaddrinfo on a worker thread: pthreads);
# no IPv6, no Unix sockets, no LDAP / libpsl / libssh2 / libidn2; the multi interface's wake-up
# (curl_multi_wakeup) is a pipe: HAVE_SOCKETPAIR=0 with CURL_DISABLE_SOCKETPAIR off -- that option
# on removes the wake-up altogether, and WebKit's request scheduler, which polls without a time
# limit and is woken when a request is added, then never starts a transfer. The CA bundle: the card's Mozilla bundle, /etc/ca-bundle.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
. "$(dirname "$0")/../common.sh"

SRC=$TP/curl-8.16.0
B=$PORTS_OUT/build/curl
mkdir -p "$B"
onyx_install_deps
[ -f "$ONYX_SYSROOT/lib/libssl.a" ] || sh "$PORTS/openssl/build.sh"

S=$ONYX_SYSROOT
# Korin PC: the wake-up is an AF_UNIX socketpair, not a pipe. libposix's poll() waits for local channels in the
# kernel (HANDLE_WAIT) but looks at pipes and NetKit sockets in a user-space loop, 1000 times a second: the
# scheduler's thread (curlThread), waiting on its pipe without a time limit, made 36,000 calls a second and kept
# netd busy on a page at rest (20/11/2026). (The Pi: unchanged, not tried there.)
WAKE="-DHAVE_SOCKETPAIR=0 -DENABLE_UNIX_SOCKETS=OFF"
[ "${ONYX_ARCH:-}" = x86_64 ] && WAKE="-DHAVE_SOCKETPAIR=1 -DHAVE_PIPE=0 -DENABLE_UNIX_SOCKETS=ON"
echo "curl: configure (CMake, the Onyx toolchain file)"
onyx_cmake -S "$SRC" -B "$B" -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_STATIC_CURL=ON \
	-DBUILD_CURL_EXE=ON -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DBUILD_LIBCURL_DOCS=OFF \
	-DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF -DPICKY_COMPILER=OFF -DCURL_USE_PKGCONFIG=OFF \
	-DCURL_ENABLE_EXPORT_TARGET=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Perl=ON \
	-DCURL_USE_MBEDTLS=OFF -DCURL_USE_OPENSSL=ON -DOPENSSL_INCLUDE_DIR="$S/include" \
	-DOPENSSL_SSL_LIBRARY="$S/lib/libssl.a" -DOPENSSL_CRYPTO_LIBRARY="$S/lib/libcrypto.a" \
	-DUSE_NGHTTP2=ON -DNGHTTP2_INCLUDE_DIR="$S/include" -DNGHTTP2_LIBRARY="$S/lib/libnghttp2.a" \
	-DCURL_ZLIB=ON -DZLIB_INCLUDE_DIR="$S/include" -DZLIB_LIBRARY="$S/lib/libz.a" \
	-DCURL_BROTLI=ON -DBROTLI_INCLUDE_DIR="$S/include" -DBROTLICOMMON_LIBRARY="$S/lib/libbrotlidec.a" \
	-DBROTLIDEC_LIBRARY="$S/lib/libbrotlidec.a" -DCURL_ZSTD=OFF \
	-DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBSSH=OFF -DUSE_LIBIDN2=OFF \
	-DCURL_USE_GSSAPI=OFF -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON -DENABLE_IPV6=OFF \
	-DENABLE_THREADED_RESOLVER=ON -DCURL_DISABLE_SOCKETPAIR=OFF $WAKE \
	-DCURL_CA_BUNDLE="/etc/ca-bundle" -DCURL_CA_PATH=none \
	-DHAVE_POLL_FINE=1 -DHAVE_EVENTFD=0 \
	-DCMAKE_INSTALL_PREFIX="$S" >"$B/configure.log" || { tail -40 "$B/configure.log"; exit 1; }
grep -E "Protocols:|Features:|Enabled SSL backends" "$B/configure.log" || true
echo "curl: build"
cmake --build "$B" -j "$JOBS" >"$B/build.log" || { grep -B2 -A12 "error" "$B/build.log" | head -80; exit 1; }
cmake --install "$B" >"$B/install.log" 2>&1 || { tail -20 "$B/install.log"; exit 1; }
# (with CURL_ENABLE_EXPORT_TARGET off, curl's install leaves the library out: WebKit links it)
cp "$B/lib/libcurl.a" "$S/lib/libcurl.a"

onyx_tool_done "$B/src/curl" curl
