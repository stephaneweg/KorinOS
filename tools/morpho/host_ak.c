/* host_ak.c -- le synthétiseur d'AudioKit (ak_synth.c, sa banque FM) compilé pour l'hôte : pour écouter les sons et la
 * musique de Morpho sans Korin (tools/morpho/run.sh sound). La SoundFont n'est pas là : la banque FM, comme une carte
 * sans SoundFont. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define KORIN_KIT_H
#define KIT_HIDDEN
#include "../../src/kits/audiokit/ak.h"
void *ak_alloc (unsigned long n) { return calloc (1, n); }
long ak_sf_default (void) { return 0; }
void ak_free (void *p) { free (p); }
long ak_fm_live (long fm, long on) { (void) fm; (void) on; return 0; }
