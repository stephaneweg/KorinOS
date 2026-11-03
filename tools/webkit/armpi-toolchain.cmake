# armpi-toolchain.cmake -- fichier de chaine CMake : du code tiers pour ARMPI, sur le sysroot de
# libposix (tools/webkit/sysroot.sh). Repris de tools/onyx-toolchain.cmake d'Onyx : le systeme
# s'appelle toujours « Onyx » pour CMake (cmake/Platform/Onyx.cmake : UNIX, statique seulement), ce
# qui garde la serie de patchs de WebKit d'Onyx telle quelle (PORT=Onyx, WTF_OS_ONYX).
#
#   cmake -S <src> -B <build> -DCMAKE_TOOLCHAIN_FILE=<armpi>/tools/webkit/armpi-toolchain.cmake \
#         -DONYX_SYSROOT=<sysroot>
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
# hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
# do so, subject to the following conditions: The above copyright notice and this permission
# notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
# IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

set(CMAKE_SYSTEM_NAME Onyx)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSTEM_VERSION 1)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(ONYX_TOOLCHAIN_PREFIX "aarch64-onyx-elf-" CACHE STRING "prefixe de la chaine")
find_program(ONYX_CC "${ONYX_TOOLCHAIN_PREFIX}gcc" PATHS /opt/toolchains/aarch64-onyx-elf-14.2/bin NO_CMAKE_FIND_ROOT_PATH)
if(NOT ONYX_CC)
	message(FATAL_ERROR "${ONYX_TOOLCHAIN_PREFIX}gcc introuvable (/opt/toolchains/aarch64-onyx-elf-14.2)")
endif()

if(NOT ONYX_SYSROOT)
	if(DEFINED ENV{ONYX_SYSROOT} AND NOT "$ENV{ONYX_SYSROOT}" STREQUAL "")
		set(ONYX_SYSROOT "$ENV{ONYX_SYSROOT}")
	else()
		set(ONYX_SYSROOT "$ENV{HOME}/armpi-webkit-build/sysroot")
	endif()
endif()
set(ONYX_SYSROOT "${ONYX_SYSROOT}" CACHE PATH "sysroot de libposix (tools/webkit/sysroot.sh)")
if(NOT EXISTS "${ONYX_SYSROOT}/lib/onyx.specs")
	message(FATAL_ERROR "pas de sysroot dans ${ONYX_SYSROOT} : sh tools/webkit/sysroot.sh")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ONYX_SYSROOT ONYX_TOOLCHAIN_PREFIX)
get_filename_component(ONYX_TOOLCHAIN_BIN "${ONYX_CC}" DIRECTORY)
set(CMAKE_C_COMPILER "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_AR "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}strip" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}objcopy" CACHE FILEPATH "")

# onyx-cc.specs : __unix__, __onyx__, __armpi__, -isystem du sysroot
set(ONYX_CC_SPECS "-specs=${ONYX_SYSROOT}/lib/onyx-cc.specs")
set(ONYX_COMMON_FLAGS "-mcpu=cortex-a72 ${ONYX_CC_SPECS} -fno-pic -fno-pie -ffunction-sections -fdata-sections -isystem ${ONYX_SYSROOT}/include -DFD_SETSIZE=1024")
set(CMAKE_C_FLAGS_INIT "${ONYX_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ONYX_COMMON_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-a72 ${ONYX_CC_SPECS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-specs=${ONYX_SYSROOT}/lib/onyx.specs -L${ONYX_SYSROOT}/lib")

set(CMAKE_FIND_ROOT_PATH "${ONYX_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_PREFIX_PATH "${ONYX_SYSROOT}")
set(CMAKE_INSTALL_PREFIX "${ONYX_SYSROOT}" CACHE PATH "installation dans le sysroot")

set(ENV{PKG_CONFIG_LIBDIR} "${ONYX_SYSROOT}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "programmes statiques")
