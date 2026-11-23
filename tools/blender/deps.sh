#!/bin/sh
# deps.sh : construit les dépendances de classe A de Blender pour Korin PC (docs/etude/BLENDER.md § 3), en
# bibliothèques statiques installées dans $DEPS (~/korin-blender/x86_64/deps). Les options suivent les recettes de
# Blender (build_files/build_environment/cmake/<nom>.cmake), en statique et sans Python ni outils.
# zlib, libpng, libjpeg, libwebp, freetype viennent du sysroot de Jet ($JET).
#
#   sh tools/blender/deps.sh            (dans WSL, depuis la racine du dépôt : toutes, dans l'ordre)
#   sh tools/blender/deps.sh tbb oiio   (seulement celles-là)
# Une dépendance déjà construite (marque $OBJ/<nom>/.fait) est sautée ; FORCE=1 la refait.
set -e
. "$(dirname "$0")/env.sh"
[ -f "$JET/lib/libz.a" ] || { echo "deps.sh : pas de $JET/lib/libz.a (ARCH=x86_64 sh tools/webkit/build-deps.sh)" >&2; exit 1; }
[ -f "$POSIX/lib/libposix.a" ] || { echo "deps.sh : pas de $POSIX/lib/libposix.a (tools/posix/build.sh)" >&2; exit 1; }

# Les bibliothèques de Jet sont recopiées dans $DEPS (jetlibs) : jamais -I $JET/include, qui a aussi des copies des
# en-têtes de libposix (fcntl.h…) : leur #include_next y retomberait au lieu de newlib.
ZLIB="-DZLIB_LIBRARY=$DEPS/lib/libz.a -DZLIB_INCLUDE_DIR=$DEPS/include"
PNG="-DPNG_LIBRARY=$DEPS/lib/libpng16.a -DPNG_PNG_INCLUDE_DIR=$DEPS/include"
JPEG="-DJPEG_LIBRARY=$DEPS/lib/libjpeg.a -DJPEG_INCLUDE_DIR=$DEPS/include"

jetlibs() {
	mkdir -p "$DEPS/lib/pkgconfig" "$DEPS/include"
	for l in z png16 png jpeg webp webpdemux webpmux sharpyuv freetype brotlidec brotlicommon; do
		[ -f "$JET/lib/lib$l.a" ] && cp "$JET/lib/lib$l.a" "$DEPS/lib/"
	done
	for h in zlib.h zconf.h png.h pngconf.h pnglibconf.h libpng16 jpeglib.h jconfig.h jerror.h jmorecfg.h webp \
		freetype2 brotli; do
		cp -R "$JET/include/$h" "$DEPS/include/"
	done
	for pc in zlib libpng libpng16 libjpeg libwebp libwebpdemux libwebpmux libsharpyuv freetype2 libbrotlidec \
		libbrotlicommon; do
		[ -f "$JET/lib/pkgconfig/$pc.pc" ] && sed "s|$JET|$DEPS|g" "$JET/lib/pkgconfig/$pc.pc" > "$DEPS/lib/pkgconfig/$pc.pc"
	done
	return 0
}

imath() { cmake_korin "$SRC/imath" "$OBJ/imath" -DBUILD_TESTING=OFF -DPYTHON=OFF; }
deflate() { cmake_korin "$SRC/deflate" "$OBJ/deflate" -DLIBDEFLATE_BUILD_STATIC_LIB=ON -DLIBDEFLATE_BUILD_SHARED_LIB=OFF \
	-DLIBDEFLATE_BUILD_GZIP=OFF; }
openexr() { cmake_korin "$SRC/openexr" "$OBJ/openexr" -DBUILD_TESTING=OFF -DOPENEXR_INSTALL_TOOLS=OFF \
	-DOPENEXR_BUILD_TOOLS=OFF -DOPENEXR_INSTALL_EXAMPLES=OFF -DOPENEXR_BUILD_EXAMPLES=OFF \
	-DOPENEXR_FORCE_INTERNAL_DEFLATE=OFF -DOPENEXR_FORCE_INTERNAL_OPENJPH=ON -DBUILD_WEBSITE=OFF \
	-DIEX_HAVE_CONTROL_REGISTER_SUPPORT=0 -DIEX_HAVE_SIGCONTEXT_H=0; }
	# (le ucontext de libposix a la forme de Linux, mais pas sa sigaction : pas de pièges des flottants par signal)
tiff() { cmake_korin "$SRC/tiff" "$OBJ/tiff" $ZLIB $JPEG -Dlzma=OFF -Djbig=OFF -Dzstd=OFF -Dwebp=OFF -Dlerc=OFF \
	-Dlibdeflate=OFF -Dtiff-tests=OFF -Dtiff-tools=OFF -Dtiff-contrib=OFF -Dtiff-docs=OFF -Dsphinx=OFF -Dcxx=OFF; }
fmt() { cmake_korin "$SRC/fmt" "$OBJ/fmt" -DFMT_TEST=OFF -DFMT_DOC=OFF; }
robinmap() { cmake_korin "$SRC/robinmap" "$OBJ/robinmap"; }
expat() { cmake_korin "$SRC/expat/expat" "$OBJ/expat" -DEXPAT_BUILD_DOCS=OFF -DEXPAT_BUILD_EXAMPLES=OFF \
	-DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_SHARED_LIBS=OFF; }
yamlcpp() { cmake_korin "$SRC/yamlcpp" "$OBJ/yamlcpp" -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF \
	-DYAML_CPP_BUILD_CONTRIB=OFF; }
pystring() {
	# pystring n'a pas de CMakeLists qui installe : deux fichiers, compilés à la main.
	mkdir -p "$OBJ/pystring" "$DEPS/include/pystring" "$DEPS/lib"
	${TC}g++ -march=tigerlake -mno-avx512f -specs="$POSIX/lib/posix.specs" -O2 -g0 -c "$SRC/pystring/pystring.cpp" \
		-o "$OBJ/pystring/pystring.o"
	rm -f "$DEPS/lib/libpystring.a"
	${TC}ar rcs "$DEPS/lib/libpystring.a" "$OBJ/pystring/pystring.o"
	cp "$SRC/pystring/pystring.h" "$DEPS/include/pystring/"
}
minizipng() { cmake_korin "$SRC/minizipng" "$OBJ/minizipng" $ZLIB -DMZ_FETCH_LIBS=OFF -DMZ_LIBCOMP=OFF \
	-DMZ_PKCRYPT=OFF -DMZ_WZAES=OFF -DMZ_OPENSSL=OFF -DMZ_SIGNING=OFF -DMZ_LZMA=OFF -DMZ_ZSTD=OFF -DMZ_BZIP2=OFF \
	-DMZ_ICONV=OFF -DMZ_ZLIB=ON -DMZ_COMPAT=ON -DCMAKE_INSTALL_INCLUDEDIR=include/minizip-ng; }
# Les dépendances d'OpenColorIO, aussi pour qui le lie en statique (sa config CMake les recherche : OIIO, Blender).
OCIODEPS="-Dexpat_ROOT=$DEPS -Dpystring_ROOT=$DEPS -DImath_ROOT=$DEPS -Dminizip-ng_ROOT=$DEPS
-Dminizip-ng_INCLUDE_DIR=$DEPS/include/minizip-ng/minizip -Dminizip-ng_LIBRARY=$DEPS/lib/libminizip.a
-Dyaml-cpp_DIR=$DEPS/lib/cmake/yaml-cpp"
opencolorio() { cmake_korin "$SRC/opencolorio" "$OBJ/opencolorio" $ZLIB -DOCIO_BUILD_APPS=OFF -DOCIO_BUILD_PYTHON=OFF \
	-DOCIO_BUILD_NUKE=OFF -DOCIO_BUILD_JAVA=OFF -DOCIO_BUILD_DOCS=OFF -DOCIO_BUILD_TESTS=OFF \
	-DOCIO_BUILD_GPU_TESTS=OFF -DOCIO_USE_SIMD=ON -DOCIO_INSTALL_EXT_PACKAGES=NONE \
	$OCIODEPS; }
tbb() { cmake_korin "$SRC/tbb" "$OBJ/tbb" -DTBB_TEST=OFF -DTBBMALLOC_BUILD=ON -DTBBMALLOC_PROXY_BUILD=OFF \
	-DTBB_STRICT=OFF -DTBB_DISABLE_HWLOC_AUTOMATIC_SEARCH=ON -DTBB_EXAMPLES=OFF; }
openimageio() { cmake_korin "$SRC/openimageio" "$OBJ/openimageio" $ZLIB $PNG $JPEG \
	-DUSE_PYTHON=OFF -DOIIO_BUILD_TOOLS=OFF -DOIIO_BUILD_TESTS=OFF -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF \
	-DINSTALL_DOCS=OFF -DSTOP_ON_WARNING=OFF -DOpenImageIO_BUILD_MISSING_DEPS="" -DLINKSTATIC=ON \
	-DUSE_TBB=ON -DUSE_OPENCOLORIO=ON -DUSE_WEBP=ON -DUSE_FREETYPE=OFF -DUSE_OPENJPEG=OFF -DUSE_OPENJPH=OFF \
	-DUSE_LIBHEIF=OFF -DUSE_FFMPEG=OFF -DUSE_GIF=OFF -DUSE_LIBRAW=OFF -DUSE_JXL=OFF -DUSE_PTEX=OFF \
	-DUSE_OPENVDB=OFF -DUSE_OPENCV=OFF -DUSE_QT=OFF -DUSE_DCMTK=OFF -DUSE_NUKE=OFF -DUSE_EXTERNAL_PUGIXML=OFF \
	-DOpenColorIO_DIR="$DEPS/lib/cmake/OpenColorIO" $OCIODEPS -DTBB_ROOT="$DEPS" -DOpenEXR_ROOT="$DEPS" -DImath_ROOT="$DEPS" \
	-Dlibdeflate_ROOT="$DEPS" -Dfmt_ROOT="$DEPS" -DRobinmap_ROOT="$DEPS" -DWebP_ROOT="$DEPS" \
	-DTIFF_ROOT="$DEPS" -Dlibjpeg-turbo_ROOT="$DEPS" -DJPEG_ROOT="$DEPS"; }
vulkan_headers() { cmake_korin "$SRC/vulkan_headers" "$OBJ/vulkan_headers"; }
spirv_headers() { cmake_korin "$SRC/spirv_headers" "$OBJ/spirv_headers" -DSPIRV_HEADERS_ENABLE_TESTS=OFF; }
spirv_tools() { cmake_korin "$SRC/spirv_tools" "$OBJ/spirv_tools" -DSPIRV-Headers_SOURCE_DIR="$SRC/spirv_headers" \
	-DSPIRV_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON -DSPIRV_WERROR=OFF -DSPIRV_TOOLS_BUILD_STATIC=ON; }
# shaderc : seulement libshaderc_combined.a (shaderc + glslang + SPIRV-Tools en une archive) : sa cible
# libshaderc_shared.so ne se lie pas avec la libstdc++ statique de Korin.
shaderc() {
	TARGETS="shaderc_combined" cmake_korin "$SRC/shaderc" "$OBJ/shaderc" -DSHADERC_SKIP_TESTS=ON \
		-DSHADERC_SKIP_EXAMPLES=ON -DSHADERC_SKIP_COPYRIGHT_CHECK=ON \
		-DSHADERC_SPIRV_TOOLS_DIR="$SRC/spirv_tools" -DSHADERC_SPIRV_HEADERS_DIR="$SRC/spirv_headers" \
		-DSHADERC_GLSLANG_DIR="$SRC/shaderc_glslang" -DSPIRV_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON \
		-DSPIRV_WERROR=OFF -DENABLE_GLSLANG_BINARIES=OFF -DGLSLANG_TESTS=OFF -DSHADERC_ENABLE_SHARED_CRT=ON || return 1
	cp "$OBJ/shaderc/libshaderc/libshaderc_combined.a" "$DEPS/lib/"
	mkdir -p "$DEPS/include"
	cp -R "$SRC/shaderc/libshaderc/include/shaderc" "$DEPS/include/"
}
spirv_reflect() { cmake_korin "$SRC/spirv_reflect" "$OBJ/spirv_reflect" -DSPIRV_REFLECT_EXECUTABLE=OFF \
	-DSPIRV_REFLECT_STATIC_LIB=ON -DSPIRV_REFLECT_EXAMPLES=OFF; }
vulkan_memory_allocator() { cmake_korin "$SRC/vulkan_memory_allocator" "$OBJ/vulkan_memory_allocator"; }

ALL="jetlibs imath deflate openexr tiff fmt robinmap expat yamlcpp pystring minizipng opencolorio tbb openimageio
vulkan_headers spirv_headers shaderc spirv_reflect vulkan_memory_allocator"

# Les retouches de Korin (tools/blender/patches/<nom>.patch), posées une fois dans la source (marque .korin-patch).
patch_src() {
	p=$BL/patches/$1.patch
	[ -f "$p" ] || return 0
	[ -f "$SRC/$1/.korin-patch" ] && cmp -s "$p" "$SRC/$1/.korin-patch" && return 0
	[ -f "$SRC/$1/.korin-patch" ] && { echo "deps.sh : $1 a une ancienne retouche : refaire la source (rm -rf $SRC/$1, fetch.sh)" >&2; return 1; }
	(cd "$SRC/$1" && patch -p1 < "$p") && cp "$p" "$SRC/$1/.korin-patch"
}

for d in ${*:-$ALL}; do
	if [ -f "$OBJ/$d/.fait" ] && [ -z "$FORCE" ]; then
		echo "== $d : déjà construite"
		continue
	fi
	echo "== $d"
	patch_src "$d" || exit 1
	t0=$(date +%s)
	$d || { echo "== $d : ÉCHEC" >&2; exit 1; }
	mkdir -p "$OBJ/$d"
	touch "$OBJ/$d/.fait"
	echo "== $d : faite en $(( $(date +%s) - t0 )) s"
done
echo "deps.sh : tout est dans $DEPS"
