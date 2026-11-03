# armpi-sdl.cmake -- fichier de chaine CMake des ports SDL (docs/SDL.md) : celui de WebKit, sur le sysroot de
# ~/armpi-sdl, qui a libposix de ce depot, les dependances de WebKit, et SDL. Passer -DONYX_SYSROOT=...
include(${CMAKE_CURRENT_LIST_DIR}/../webkit/armpi-toolchain.cmake)
string(APPEND CMAKE_C_FLAGS_INIT " -O2 -g0 -D_GNU_SOURCE -D__STDC_NO_THREADS__=1")
string(APPEND CMAKE_CXX_FLAGS_INIT " -O2 -g0 -D_GNU_SOURCE -D__STDC_NO_THREADS__=1")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
