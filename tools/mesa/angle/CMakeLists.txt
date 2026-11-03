# ANGLE pour ARMPI, seul : backend GL sur armegl (docs/MESA.md). Reprend les listes de sources d'ANGLE (les
# *.cmake de son dossier) et en fait une bibliotheque statique, sans le systeme de construction de WebKit.
cmake_minimum_required (VERSION 3.20)
project (armangle C CXX)
set (A ${ANGLE_SRC})
set (is_linux TRUE)
include (${A}/Compiler.cmake)
include (${A}/GLESv2.cmake)
include (${A}/GL.cmake)
include (${A}/linux.cmake)

set (SOURCES
	${libangle_common_sources} ${libangle_common_shader_state_sources} ${angle_preprocessor_sources}
	${angle_translator_glsl_base_sources} ${angle_translator_essl_sources}
	${angle_translator_essl_symbol_table_sources} ${angle_translator_glsl_and_vulkan_base_sources}
	${angle_translator_glsl_sources} ${angle_translator_sources}
	src/common/angle_version_info.cpp src/libANGLE/capture/FrameCapture_mock.cpp src/libANGLE/capture/serialize_mock.cpp
	${libangle_sources} ${libangle_image_util_sources} ${xxhash_sources}
	third_party/zlib/google/compression_utils_portable.cc
	${gl_backend_sources} ${angle_system_utils_sources_linux} ${angle_system_utils_sources_posix} ${angle_dma_buf_sources}
	${libangle_gl_egl_dl_sources} ${libangle_gl_egl_sources} ${libangle_gl_sources}
	${libangle_gpu_info_util_sources} ${libangle_gpu_info_util_linux_sources}
	${libglesv2_entry_point_sources} ${libglesv2_sources} ${libegl_sources})
list (FILTER SOURCES EXCLUDE REGEX "\\.(h|inc|inl|def)$")
list (REMOVE_DUPLICATES SOURCES)
set (ABS)
foreach (s ${SOURCES})
	if (IS_ABSOLUTE ${s})
		list (APPEND ABS ${s})
	else ()
		list (APPEND ABS ${A}/${s})
	endif ()
endforeach ()

add_library (armangle STATIC ${ABS})
target_include_directories (armangle PRIVATE ${A}/WebKit ${A}/include ${A}/include/KHR ${A}/src ${A}/src/common/base
	${A}/src/common/third_party/xxhash ${A}/third_party/zlib/google ${ZLIB_INC} ${GEN})
target_compile_definitions (armangle PRIVATE ANGLE_ENABLE_ESSL ANGLE_ENABLE_GLSL ANGLE_ENABLE_OPENGL ANGLE_PLATFORM_LINUX
	EGL_NO_PLATFORM_SPECIFIC_TYPES USE_SYSTEM_EGL LIBANGLE_IMPLEMENTATION LIBGLESV2_IMPLEMENTATION
	GL_GLES_PROTOTYPES=0 EGL_EGL_PROTOTYPES=0 EGLAPI=)
target_compile_options (armangle PRIVATE -include ${A}/ANGLEPrefix.h -D__linux__=1 $<$<COMPILE_LANGUAGE:CXX>:-std=c++20> -w -fno-rtti)
