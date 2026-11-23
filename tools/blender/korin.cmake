# korin.cmake : fichier de chaîne CMake du port de Blender (Korin PC), repris de tools/cao/korin.cmake.
# Variables d'environnement : KORIN_POSIX (build/pc/posix), KORIN_TOOLCHAINS (défaut /opt/toolchains),
# KORIN_BLENDER_DEPS (le préfixe des dépendances déjà construites, cherché aussi par find_package).
# CMake voit un « Linux » (UNIX), mais __linux__ n'est pas défini : le code tiers prend ses chemins POSIX génériques.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
if(DEFINED ENV{KORIN_TOOLCHAINS})
	set(_tc "$ENV{KORIN_TOOLCHAINS}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-")
else()
	set(_tc "/opt/toolchains/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-")
endif()
set(KORIN_POSIX "$ENV{KORIN_POSIX}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES KORIN_POSIX)

set(CMAKE_C_COMPILER ${_tc}gcc)
set(CMAKE_CXX_COMPILER ${_tc}g++)
set(CMAKE_AR ${_tc}ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${_tc}ranlib CACHE FILEPATH "")
set(CMAKE_STRIP ${_tc}strip CACHE FILEPATH "")
# -g0 : Blender et ses dépendances font des Go d'objets avec les informations de débogage (WSL n'a que 9 Go).
# Processeur : celui de Jet (tools/webkit/env.sh), dont on lie les bibliothèques (KORIN_CPU pour changer).
if(DEFINED ENV{KORIN_CPU})
	set(_cpu "$ENV{KORIN_CPU}")
else()
	set(_cpu "-march=tigerlake -mno-avx512f -mtune=tigerlake")
endif()
set(_flags "${_cpu} -specs=${KORIN_POSIX}/lib/posix.specs -D_GNU_SOURCE -DFD_SETSIZE=1024 -ffunction-sections -fdata-sections -g0")
set(CMAKE_C_FLAGS_INIT "${_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_flags}")
# (-specs vient déjà des CMAKE_<LANG>_FLAGS, repris sur la ligne de liaison : deux fois, GCC refuse)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

set(CMAKE_FIND_ROOT_PATH "${KORIN_POSIX}")
if(DEFINED ENV{KORIN_BLENDER_DEPS})
	list(APPEND CMAKE_FIND_ROOT_PATH "$ENV{KORIN_BLENDER_DEPS}")
endif()
# Le sysroot de Jet (tools/webkit/build-deps.sh) : zlib, libpng, libjpeg, libwebp, freetype, brotli, OpenSSL…
if(DEFINED ENV{KORIN_JET_SYSROOT})
	list(APPEND CMAKE_FIND_ROOT_PATH "$ENV{KORIN_JET_SYSROOT}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
