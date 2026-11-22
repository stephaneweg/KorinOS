/* png.h -- une image RGB (3 octets par pixel, lignes sans bourrage) en PNG : malloc, *out_len octets ; 0 : mémoire */
#ifndef KORIN_MCP_PNG_H
#define KORIN_MCP_PNG_H

#include <stddef.h>
#include <stdint.h>

uint8_t *png_encode(const uint8_t *rgb, int w, int h, size_t *out_len);

#endif
