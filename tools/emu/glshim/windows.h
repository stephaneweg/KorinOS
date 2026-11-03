/* gxgl.cpp d'Onyx hors de Windows : EGL fait ce que WGL faisait (tools/emu/gxes_host.cpp) */
#ifndef GLSHIM_WINDOWS_H
#define GLSHIM_WINDOWS_H
#include <EGL/egl.h>
#define APIENTRY
#define wglGetProcAddress(n) ((void *) eglGetProcAddress (n))
typedef void *HMODULE;
static inline HMODULE GetModuleHandleA (const char *) { return 0; }
static inline void *GetProcAddress (HMODULE, const char *) { return 0; }
#endif
