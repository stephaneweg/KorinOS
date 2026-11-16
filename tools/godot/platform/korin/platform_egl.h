/**************************************************************************/
/*  platform_egl.h                                                        */
/**************************************************************************/
/* Korin OS : pas d'EGL. Avec EGL_STATIC, le chargeur de drivers/gles3 appelle
 * eglGetProcAddress, que korin_ui.c fait passer a armgl_proc. Les en-tetes EGL
 * de Mesa (prebuilt/mesa/include) servent seulement a compiler drivers/egl,
 * jamais lie (rien ne l'appelle sur cette plateforme). */

#pragma once

#include <EGL/egl.h> // IWYU pragma: export.
#include <EGL/eglext.h> // IWYU pragma: export.
