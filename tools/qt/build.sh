#!/bin/sh
# build.sh : Qt 6 pour Korin PC (docs/etude/QT.md), en statique, dans $PREFIX (~/korin-qt/x86_64/qt).
#
#   sh tools/qt/build.sh                    (tous les modules de QT_MODULES)
#   sh tools/qt/build.sh qtbase             (un seul ; « configure » seul : STEP=configure)
#
# Il faut : la chaîne x86_64-onyx-elf-14.2, libposix (ARCH=x86_64 sh tools/posix/build.sh), libuikit.a
# (sh tools/pc/apps.sh) pour la QPA korin, et Qt pour l'hôte (sh tools/qt/host.sh).
# Retouches : tools/qt/patches/<module>.patch (mkpatch.sh), appliquées sur la source tenue sous git ; la QPA
# korin (tools/qt/qpa) est recopiée dans qtbase/src/plugins/platforms/korin, le style korin (tools/qt/style) dans
# qtbase/src/plugins/styles/korin.
set -e
. "$(dirname "$0")/env.sh"
MODS=${1:-$QT_MODULES}
[ -x "$HOSTQT/bin/qtpaths" ] || [ -x "$HOSTQT/libexec/moc" ] || { echo "build.sh : d'abord sh tools/qt/host.sh" >&2; exit 1; }
[ -f "$POSIX/lib/posix.specs" ] || { echo "build.sh : pas de libposix ($POSIX)" >&2; exit 1; }
sh "$QTT/fetch.sh"

# l'environnement de CMake : libposix, les paquets -dev (find_package, pkg-config), libuikit.a
KENV="env KORIN_POSIX=$POSIX KORIN_QT_DEPS=$DEVSYS/devtools/gcc KORIN_UIKIT_LIBDIR=$UIKIT_LIBDIR
	PKG_CONFIG_SYSROOT_DIR=$DEVSYS PKG_CONFIG_LIBDIR=$DEVSYS/devtools/gcc/lib/pkgconfig PKG_CONFIG_PATH="
for m in $MODS; do
	s=$(qt_src $m)
	sh "$QTT/mkpatch.sh" apply $m
	o=$OBJ/$m
	if [ $m = qtbase ]; then
		# la QPA korin : sources du dépôt
		mkdir -p "$s/src/plugins/platforms/korin"
		for f in "$QTT"/qpa/*; do
			cmp -s "$f" "$s/src/plugins/platforms/korin/${f##*/}" || cp "$f" "$s/src/plugins/platforms/korin/"
		done
		# l'impression sur PrinterKit (tools/qt/printsupport) : qtbase/src/plugins/printsupport/korin
		mkdir -p "$s/src/plugins/printsupport/korin"
		for f in "$QTT"/printsupport/*; do
			cmp -s "$f" "$s/src/plugins/printsupport/korin/${f##*/}" || cp "$f" "$s/src/plugins/printsupport/korin/"
		done
		# le style korin de QWidgets (tools/qt/style) : qtbase/src/plugins/styles/korin
		mkdir -p "$s/src/plugins/styles/korin"
		for f in "$QTT"/style/*; do
			cmp -s "$f" "$s/src/plugins/styles/korin/${f##*/}" || cp "$f" "$s/src/plugins/styles/korin/"
		done
		UIKIT_INC="$ROOT/build/pc/gcc/include/kits;$ROOT/src/kits/uikit/include"
		[ -f "$UIKIT_LIBDIR/libuikit.a" ] || { echo "build.sh : pas de libuikit.a (sh tools/pc/apps.sh)" >&2; exit 1; }
		[ -f "$DEVSYS/devtools/gcc/lib/libfreetype.a" ] || sh "$QTT/deps.sh"
		$KENV cmake -S "$s" -B "$o" -G Ninja \
			-DCMAKE_TOOLCHAIN_FILE="$QTT/korin.cmake" -DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_INSTALL_PREFIX="$QT_KORIN_PREFIX" -DCMAKE_STAGING_PREFIX="$PREFIX" \
			-DQT_HOST_PATH="$HOSTQT" -DQT_FORCE_BUILD_TOOLS=ON \
			-DQT_QMAKE_TARGET_MKSPEC=korin-g++ -DBUILD_SHARED_LIBS=OFF \
			-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DQT_GENERATE_SBOM=OFF \
			-DKORIN_UIKIT_INCLUDE_DIRS="$UIKIT_INC" -DKORIN_UIKIT_LIBRARY=uikit \
			-DKORIN_PRINTERKIT_INCLUDE_DIR="$ROOT/src/kits/printerkit/include" \
			-DQT_QPA_DEFAULT_PLATFORM=${QPA:-korin} \
			-DINPUT_opengl=no -DFEATURE_vulkan=OFF -DFEATURE_egl=OFF \
			-DFEATURE_xcb=OFF -DFEATURE_xlib=OFF -DFEATURE_wayland=OFF -DFEATURE_linuxfb=OFF \
			-DFEATURE_vnc=OFF -DFEATURE_eglfs=OFF -DFEATURE_directfb=OFF -DFEATURE_vkkhrdisplay=OFF \
			-DFEATURE_dbus=OFF -DFEATURE_glib=OFF -DFEATURE_icu=OFF -DFEATURE_fontconfig=OFF \
			-DFEATURE_sql=OFF -DFEATURE_printsupport=ON -DFEATURE_cups=OFF -DFEATURE_testlib=ON \
			-DFEATURE_openssl_linked=ON -DFEATURE_libudev=OFF -DFEATURE_evdev=OFF -DFEATURE_libinput=OFF \
			-DFEATURE_tslib=OFF -DFEATURE_mtdev=OFF -DFEATURE_xkbcommon=OFF -DFEATURE_inotify=OFF \
			-DFEATURE_systemsemaphore=OFF -DFEATURE_sharedmemory=OFF -DFEATURE_sysv_shm=OFF \
			-DFEATURE_sysv_sem=OFF -DFEATURE_posix_shm=OFF -DFEATURE_posix_sem=OFF \
			-DFEATURE_reduce_relocations=OFF -DFEATURE_elf_private_full_version=OFF \
			-DFEATURE_system_zlib=ON -DFEATURE_system_png=ON -DFEATURE_system_freetype=ON \
			-DFEATURE_system_harfbuzz=ON -DFEATURE_system_pcre2=OFF -DFEATURE_system_jpeg=OFF \
			-DOPENSSL_ROOT_DIR="$DEVSYS/devtools/gcc" -DOPENSSL_USE_STATIC_LIBS=ON \
			-DFEATURE_system_doubleconversion=OFF -DFEATURE_system_md4c=OFF \
			-DFEATURE_backtrace=OFF -DFEATURE_journald=OFF -DFEATURE_syslog=OFF -DFEATURE_slog2=OFF \
			-DFEATURE_libresolv=OFF -DFEATURE_gssapi=OFF -DFEATURE_networklistmanager=OFF \
			${QTBASE_EXTRA:-}
	else
		$KENV cmake -S "$s" -B "$o" -G Ninja \
			-DCMAKE_TOOLCHAIN_FILE="$QTT/korin.cmake" -DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_INSTALL_PREFIX="$QT_KORIN_PREFIX" \
			-DCMAKE_STAGING_PREFIX="$PREFIX" -DQT_HOST_PATH="$HOSTQT" -DQT_FORCE_BUILD_TOOLS=ON \
			-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DQT_GENERATE_SBOM=OFF \
			-DQt6_DIR="$PREFIX/lib/cmake/Qt6" \
			-DFEATURE_qml_jit=OFF ${QTMOD_EXTRA:-}	# (QML en interpréteur : l'assembleur de JSC ne connaît pas Korin)
	fi
	[ "$STEP" = configure ] && continue
	ninja -C "$o" -j "$JOBS" ${NINJA_FLAGS:-}
	ninja -C "$o" install
done
echo "Qt pour Korin : $PREFIX"
