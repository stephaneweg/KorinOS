//
// rvz.h -- les images de disque compressées de Dolphin, RVZ et WIA (GameCube seulement), lues à la demande par blocs
// (un jeu fait 1,4 Go : rien n'est décompressé en entier). Écrit d'après la description publique du format
// (docs/WiaAndRvz.md de Dolphin), sans son code. Voir rvz.cpp.
//
#ifndef GCEMU_RVZ_H
#define GCEMU_RVZ_H

#include <stdio.h>
#include <stdint.h>

struct Rvz;

// 1 si les 4 premiers octets sont ceux d'un RVZ ou d'un WIA
bool rvz_is (const unsigned char *head4);
// le fichier (gardé ouvert, fermé par rvz_close) -> l'image, 0 si refusée (*err : pourquoi, en anglais)
Rvz *rvz_open (FILE *f, const char **err);
uint64_t rvz_disc_size (const Rvz *r);		// taille de l'ISO d'origine
bool rvz_read (Rvz *r, uint64_t off, uint32_t len, unsigned char *dst);	// octets de l'ISO d'origine
void rvz_close (Rvz *r);

#endif
