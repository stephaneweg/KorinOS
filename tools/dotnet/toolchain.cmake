# toolchain.cmake -- CoreCLR compile par la chaine d'Onyx (GCC 14.2, newlib, libposix). ARMPI se fait
# passer pour Linux arm64 : les bibliotheques C# de linux-arm64 restent bonnes, ce qui differe est
# sous __armpi__ (comme WebKit garde le nom Onyx, tools/webkit/armpi-toolchain.cmake).
# ARMPI_ARCH=x86_64 (build.sh, ARCH=x86_64) : Korin PC, qui se fait passer pour Linux x64, chaine
# x86_64-onyx-elf et libposix de build/pc/posix.
set(CMAKE_SYSTEM_NAME Linux)

set(ARMPI_ROOT "$ENV{ARMPI_ROOT}")
set(ARMPI_COMPAT ${ARMPI_ROOT}/tools/dotnet/compat)
if("$ENV{ARMPI_ARCH}" STREQUAL "x86_64")
  set(CMAKE_SYSTEM_PROCESSOR x86_64)
  set(TC /opt/toolchains/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-)
  set(ARMPI_TARGET x86_64-onyx-elf)
  set(ARMPI_FLAGS "-specs=${ARMPI_ROOT}/build/pc/posix/lib/posix.specs -D_GNU_SOURCE -DFD_SETSIZE=1024 -D__linux__=1 -I${ARMPI_COMPAT}")
  set(ARMPI_LIBDIR $ENV{HOME}/armpi-dotnet/lib-x64)
else()
  set(CMAKE_SYSTEM_PROCESSOR aarch64)
  set(TC /opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-)
  set(ARMPI_TARGET aarch64-onyx-elf)
  set(ARMPI_FLAGS "-mcpu=cortex-a72 -specs=${ARMPI_ROOT}/build/posix/lib/posix.specs -D_GNU_SOURCE -DFD_SETSIZE=1024 -D__linux__=1 -I${ARMPI_COMPAT}")
  set(ARMPI_LIBDIR $ENV{HOME}/armpi-dotnet/lib)
endif()
set(CMAKE_C_COMPILER ${TC}gcc)
set(CMAKE_CXX_COMPILER ${TC}g++)
set(CMAKE_ASM_COMPILER ${TC}gcc)
set(CMAKE_C_COMPILER_TARGET ${ARMPI_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${ARMPI_TARGET})

set(ARMPI_CFLAGS "${ARMPI_FLAGS} -include ${ARMPI_COMPAT}/armpi_compat.h")
set(CMAKE_C_FLAGS_INIT "${ARMPI_CFLAGS} -Wno-error=incompatible-pointer-types")
set(CMAKE_CXX_FLAGS_INIT "${ARMPI_CFLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${ARMPI_FLAGS}")
# (pthread, dl, rt : dans libposix ; des bibliotheques vides pour -lpthread, -ldl, -lrt : build.sh)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L${ARMPI_LIBDIR}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
