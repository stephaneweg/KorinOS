/* clip.h : protocole du service « clipboard » (src/sys/clipd, comme clipd d'Onyx ; asm :
 * src/inc/clip.inc). Les programmes passent par UIKit (ui_clip_*) ; seul le widget
 * (src/sys/clipboard) parle au service directement.
 *
 * Message de 512 octets (WM_*). Le contenu reste dans la mémoire de l'appelant : clipd le lit ou
 * l'y écrit par SVC_MAP_CALLER.
 *
 *   CLIP_PUT     a adresse, b octets, c copie à compléter (0 : nouvelle), d 8 octets à mettre
 *                devant (0 : aucun ; l'image : l | h << 32), données : format, 0 -> id de la
 *                copie, < 0 : refus
 *   CLIP_GET     a adresse (0 : la taille seule), b place, c octets à sauter, données : formats
 *                « text\0rtf\0\0 », le préféré d'abord -> octets écrits ; a octets de la
 *                représentation (0 : rien ne convient), b rang du format, c id de la copie
 *                (l'élément du curseur, sinon la plus récente qui en a un)
 *   CLIP_INFO    a rang (0 : la plus récente) -> 1 / 0 : au-delà ; a id, b 1 si curseur,
 *                c heure * 256 + minute, d octets, données CI_*
 *   CLIP_STATE   -> numéro de série de l'anneau ; a nombre de copies
 *   CLIP_CURSOR  a id : le curseur (ce que Ctrl+V colle)
 *   CLIP_DELETE  a id
 *   CLIP_CLEAR
 *   CLIP_DROPCUT l'élément du curseur s'en va si c'est une coupe (fichiers déplacés par le
 *                collage)
 *
 * Les constantes (CLIP_PUT..CLIP_DROPCUT, CLIP_RING, CLIP_REPS, CLIP_FMT, CLIP_MAXITEM,
 * CLIP_MAXBYTES, CI_KIND, CI_W, CI_H, CI_NFILES, CI_PREVIEW, CI_PREVIEW_MAX) sont déjà dans
 * src/kits/uikit/uk_consts.h, tiré de clip.inc : on les prend de là (les redéfinir ici, écrites
 * autrement, ferait des redéfinitions de macros). */
#ifndef KORIN_CLIP_H
#define KORIN_CLIP_H

#include "sys_consts.h"

#endif
