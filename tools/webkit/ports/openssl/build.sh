#!/bin/sh
# build.sh -- OpenSSL dans le sysroot de WebKit (docs/OPENSSL.md) : tools/openssl/build.sh sur la
# libposix du sysroot, puis en-tetes, archives et .pc dans $ONYX_SYSROOT. Le TLS de curl et la
# cryptographie de WebKit (WebCrypto, condensats de PAL).
. "$(dirname "$0")/../common.sh"

O=$WK_OUT/openssl
env -u CFLAGS -u CXXFLAGS -u LDFLAGS -u CC -u CXX -u AR -u RANLIB NO_TESTS=1 SPECS=$WK_OUT/posix/lib/posix.specs OUT=$O WORK=$WK_OUT/openssl-work \
	sh "$ARMPI/tools/openssl/build.sh"
rm -rf "$ONYX_SYSROOT/include/openssl"
cp -r "$O/include/openssl" "$ONYX_SYSROOT/include/"
cp "$O/lib/libssl.a" "$O/lib/libcrypto.a" "$ONYX_SYSROOT/lib/"
for p in libcrypto libssl openssl; do
	sed "s|^prefix=.*|prefix=$ONYX_SYSROOT|" "$O/lib/pkgconfig/$p.pc" > "$ONYX_SYSROOT/lib/pkgconfig/$p.pc"
done
ls -la "$ONYX_SYSROOT/lib/"libssl.a "$ONYX_SYSROOT/lib/"libcrypto.a
