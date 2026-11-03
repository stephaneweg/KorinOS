; ============================================================================
;  inc/clip.inc - protocole du service « clipboard » (src/sys/clipd, comme
;  clipd d'Onyx). Les programmes passent par UIKit (ui_clip_*, uikit_clip.inc) ;
;  seul le widget (src/sys/clipboard) parle au service directement.
;
;  Message de 512 octets (WM_*). Le contenu reste dans la memoire de
;  l'appelant : clipd le lit ou l'y ecrit par SVC_MAP_CALLER.
;
;    CLIP_PUT     a adresse, b octets, c copie a completer (0 : nouvelle),
;                 d 8 octets a mettre devant (0 : aucun ; l'image : l | h << 32),
;                 donnees : format, 0 -> id de la copie, < 0 : refus
;    CLIP_GET     a adresse (0 : la taille seule), b place, c octets a sauter,
;                 donnees : formats « text\0rtf\0\0 », le prefere d'abord
;                 -> octets ecrits ; a octets de la representation (0 : rien
;                 ne convient), b rang du format, c id de la copie
;                 (l'element du curseur, sinon la plus recente qui en a un)
;    CLIP_INFO    a rang (0 : la plus recente) -> 1 / 0 : au-dela ; a id,
;                 b 1 si curseur, c heure * 256 + minute, d octets, donnees CI_*
;    CLIP_STATE   -> numero de serie de l'anneau ; a nombre de copies
;    CLIP_CURSOR  a id : le curseur (ce que Ctrl+V colle)
;    CLIP_DELETE  a id
;    CLIP_CLEAR
;    CLIP_DROPCUT l'element du curseur s'en va si c'est une coupe (fichiers
;                 deplaces par le collage)
; ============================================================================

CLIP_PUT	= 1
CLIP_GET	= 2
CLIP_INFO	= 3
CLIP_STATE	= 4
CLIP_CURSOR	= 5
CLIP_DELETE	= 6
CLIP_CLEAR	= 7
CLIP_DROPCUT	= 8

CLIP_RING	= 10			; copies gardees
CLIP_REPS	= 4			; representations d'une copie
CLIP_FMT	= 24			; format : nom (0 compris)
CLIP_MAXITEM	= 48 * 1024 * 1024	; une copie au plus
CLIP_MAXBYTES	= 64 * 1024 * 1024	; tout l'anneau : les plus anciennes partent

; donnees de CLIP_INFO
CI_KIND		= 0			; format principal (CLIP_FMT)
CI_W		= 24			; (32 bits) image : largeur, hauteur
CI_H		= 28
CI_NFILES	= 32			; (32 bits) fichiers : nombre
CI_PREVIEW	= 40			; une ligne de texte, jusqu'au 0
CI_PREVIEW_MAX	= 400
