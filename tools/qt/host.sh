#!/bin/sh
# host.sh : Qt pour la machine de construction, dans $HOSTQT. La compilation croisée (build.sh) en prend les outils
# (QT_HOST_PATH) : moc, rcc, uic, qsb, qmlcachegen, qmltyperegistrar… Rien ne part dans Korin.
# Le compilateur et les bibliothèques de la distribution ; pas de X11, pas d'OpenGL : seulement les outils.
set -e
. "$(dirname "$0")/env.sh"
sh "$QTT/fetch.sh"
for m in $QT_MODULES; do
	[ -f "$HOSTQT/.done-$m" ] && continue
	o=$K/host-obj/$m
	rm -rf "$o"
	if [ $m = qtbase ]; then
		cmake -S "$(qt_src $m)" -B "$o" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOSTQT" \
			-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=ON \
			-DFEATURE_xcb=OFF -DFEATURE_xlib=OFF -DFEATURE_wayland=OFF -DINPUT_opengl=no -DFEATURE_vulkan=OFF \
			-DFEATURE_dbus=OFF -DFEATURE_glib=OFF -DFEATURE_icu=OFF -DFEATURE_fontconfig=OFF \
			-DFEATURE_sql=OFF -DFEATURE_printsupport=OFF -DFEATURE_testlib=ON \
			-DFEATURE_openssl=OFF -DFEATURE_libudev=OFF -DFEATURE_evdev=OFF -DFEATURE_libinput=OFF \
			-DFEATURE_system_zlib=OFF -DFEATURE_system_pcre2=OFF -DFEATURE_system_harfbuzz=OFF \
			-DFEATURE_system_freetype=OFF -DFEATURE_system_png=OFF -DFEATURE_system_jpeg=OFF
	else
		cmake -S "$(qt_src $m)" -B "$o" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$HOSTQT" \
			-DCMAKE_INSTALL_PREFIX="$HOSTQT" -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
	fi
	ninja -C "$o" -j "$JOBS"
	ninja -C "$o" install
	touch "$HOSTQT/.done-$m"
done
echo "Qt hôte : $HOSTQT"
