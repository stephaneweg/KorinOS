//
// tr.h -- Morpho : les textes passent par le dictionnaire du système (lang/en.txt de l'app ; la clé est le français).
//
#ifndef MORPHO_TR_H
#define MORPHO_TR_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <uikit.h>
#define TR(s) ((const char *) ui_tr ((long) (s)))
#endif
