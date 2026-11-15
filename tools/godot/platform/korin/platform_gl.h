/**************************************************************************/
/*  platform_gl.h                                                         */
/**************************************************************************/
/* Korin OS : OpenGL ES 3.0 seulement (armgl, Mesa), charge par GLAD. */

#pragma once

#ifndef GLES_API_ENABLED
#define GLES_API_ENABLED // OpenGL ES seulement.
#endif

#ifndef GLAD_GLES2
#define GLAD_GLES2
#endif

#include <thirdparty/glad/glad/gl.h> // IWYU pragma: export.
