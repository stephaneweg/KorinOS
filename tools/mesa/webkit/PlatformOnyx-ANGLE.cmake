# ANGLE sur ARMPI (Source/ThirdParty/ANGLE/PlatformOnyx.cmake de la copie de tools/mesa/build-webkit.sh) : le backend
# GL sur armegl, comme PlatformGTK.cmake, sans fenetre. ARMPI se presente comme Linux, pour ANGLE seulement.
list(APPEND ANGLE_DEFINITIONS ANGLE_PLATFORM_LINUX EGL_NO_PLATFORM_SPECIFIC_TYPES USE_SYSTEM_EGL ANGLE_ENABLE_OPENGL)
include(linux.cmake)
add_compile_options(-D__linux__=1)

list(APPEND ANGLE_SOURCES
    ${gl_backend_sources}
    ${angle_system_utils_sources_linux}
    ${angle_system_utils_sources_posix}
    ${angle_dma_buf_sources}
    ${libangle_gl_egl_dl_sources}
    ${libangle_gl_egl_sources}
    ${libangle_gl_sources}
    ${libangle_gpu_info_util_sources}
    ${libangle_gpu_info_util_linux_sources}
)
