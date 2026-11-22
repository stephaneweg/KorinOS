# cmake.sh : CMake 3.31.6 pour Korin PC (lu par tools/devtools/build.sh, pas lancé seul), construit par le
# CMake de l'hôte avec un fichier de chaîne pour Korin (système « Onyx », la libposix du PC). /devtools/gcc :
# bin/cmake, bin/ctest, bin/cpack, share/cmake-3.31 (les modules). Sans curl ni OpenSSL (pas de
# file(DOWNLOAD) ni de CDash), sans l'interface ccmake ni cmake-gui.
#  * libuv (Utilities/cmlibuv) n'a pas Korin : le système « Onyx » y prend la boucle par poll
#    (posix-poll.c), l'heure par clock_gettime (posix-hrtime.c) et tools/devtools/cmake/korin.c (mémoire,
#    processeurs, chemin du programme) ;
#  * dans Korin, uname() dit « ARMPI » : Modules/Platform/ARMPI.cmake (et Onyx.cmake, pour les fichiers de
#    chaîne de tools/webkit) décrit le système : Unix, programmes statiques, pas de bibliothèques partagées.
say cmake
fetch cmake-3.31.6.tar.gz https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6.tar.gz \
	653427f0f5014750aafff22727fb2aa60c6c732ca91808cfb78ce22ddd9e55f0
unpack cmake-3.31.6.tar.gz cmake-3.31.6
S=$WORK/src/cmake-3.31.6
cp "$ROOT/tools/devtools/cmake/korin.c" "$S/Utilities/cmlibuv/src/unix/korin.c"
python3 - "$S/Utilities/cmlibuv/CMakeLists.txt" <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
anchor = 'if(CMAKE_SYSTEM_NAME STREQUAL "QNX")'
add = '''if(CMAKE_SYSTEM_NAME STREQUAL "Onyx")
  list(APPEND uv_headers
    include/uv/posix.h
    )
  list(APPEND uv_defines
    _GNU_SOURCE
    )
  list(APPEND uv_sources
    src/unix/korin.c
    src/unix/posix-hrtime.c
    src/unix/posix-poll.c
    src/unix/no-fsevents.c
    src/unix/no-proctitle.c
    )
endif()

'''
assert anchor in s
s = s.replace(anchor, add + anchor, 1)
open(p, 'w').write(s)
EOF
# les sources de libuv : Korin (__armpi__, la libposix) fait comme QNX (uv/posix.h, pas de multicast par
# source, pas de sendmmsg...)
for f in "$S"/Utilities/cmlibuv/include/uv/unix.h "$S"/Utilities/cmlibuv/src/unix/*.c "$S"/Utilities/cmlibuv/src/unix/*.h; do
	grep -q '__QNX__' "$f" && sed -i 's/defined(__QNX__)/(defined(__QNX__) || defined(__armpi__))/g' "$f"
done
B=$WORK/build/cmake
rm -rf "$B"
mkdir -p "$B"
cat > "$B/korin.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Onyx)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
list(APPEND CMAKE_MODULE_PATH "$ROOT/tools/webkit/cmake")
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_AR ${TC}ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${TC}ranlib CACHE FILEPATH "")
set(CMAKE_STRIP ${TC}strip CACHE FILEPATH "")
set(CMAKE_C_FLAGS_INIT "-D_GNU_SOURCE")
set(CMAKE_CXX_FLAGS_INIT "-D_GNU_SOURCE")
set(CMAKE_FIND_ROOT_PATH "$ROOT/build/pc/posix")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
cd "$B"
cmake -G Ninja -S "$S" -B "$B" -DCMAKE_TOOLCHAIN_FILE="$B/korin.cmake" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" \
	-DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_USE_OPENSSL=OFF -DBUILD_TESTING=OFF -DBUILD_CursesDialog=OFF \
	-DBUILD_QtDialog=OFF -DCMAKE_USE_SYSTEM_LIBRARIES=OFF -DCMake_ENABLE_DEBUGGER=OFF \
	-DCMAKE_DATA_DIR=share/cmake-3.31 -DCMAKE_DOC_DIR=share/doc/cmake-3.31 -DCMAKE_MAN_DIR=share/man \
	-DKWSYS_LFS_WORKS=1 -DKWSYS_CHAR_IS_SIGNED=1 -DHAVE_POLL_FINE_EXITCODE=0 \
	-DCMAKE_HAVE_LIBC_PTHREAD=1 >configure.log 2>&1 || { tail -40 configure.log; die "cmake : configure"; }
ninja -j"$JOBS" >make.log 2>&1 || { grep -n 'error\|undefined reference' make.log | head -40; die "cmake : make"; }
tree cmake
DESTDIR="$T" ninja install >install.log 2>&1 || { tail -20 install.log; die "cmake : install"; }
for b in cmake ctest cpack; do ${TC}strip "$T$PREFIX/bin/$b"; done
rm -rf "$T$PREFIX/share/doc" "$T$PREFIX/share/man" "$T$PREFIX/share/bash-completion" "$T$PREFIX/share/emacs" \
	"$T$PREFIX/share/vim" "$T$PREFIX/share/aclocal"
M=$T$PREFIX/share/cmake-3.31/Modules/Platform
cp "$ROOT/tools/webkit/cmake/Platform/Onyx.cmake" "$M/Onyx.cmake"
cp "$ROOT/tools/webkit/cmake/Platform/Onyx.cmake" "$M/ARMPI.cmake"
