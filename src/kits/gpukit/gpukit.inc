; gpukit.inc - constantes publiques de GpuKit (gpukit.abi, docs/V3D.md)

; --- gk_info : tampon de sortie ------------------------------------------------
GKI_GPU		= 0		; 1 le GPU sert, 0 le CPU
GKI_STATE	= 8		; service : 1 pret, 0 absent, -1 eteint apres un blocage
GKI_VERSION	= 16		; 42 : V3D 4.2
GKI_REV		= 24		; revision
GKI_JOBS	= 32		; jobs faits par le GPU depuis le demarrage (tout le systeme)
GKI_BUSY_US	= 40		; temps GPU cumule (us)
GKI_LAST_US	= 48		; duree du dernier job de ce process (us)
GKI_SIZE	= 64

GK_MAX_W	= 2048		; un job : 2048 x 2048 au plus (au-dela : le CPU)
GK_MAX_H	= 2048

; --- textures et rendu (gk_texture, gk_texture_rect, gk_render) ----------------
; Memes structures et codes que kapi gpu_texture / gpu_render d'Onyx (v53-v72) :
; gpucomp s'y porte tel quel. Entiers C (int) : 32 bits, etendus par le kit.
GK_E_NOGPU	= -1		; pas de GPU (QEMU, no_gpu, eteint) : a l'appelant de faire au CPU
GK_E_INVAL	= -2		; mauvais arguments
GK_E_LOST	= -3		; le GPU s'est arrete (le CPU desormais ; textures perdues)
GK_E_NOMEM	= -4		; plus de memoire ou de texture libre

GK_MAX_TEX	= 2048		; cote d'une texture
GK_MAX_TEXTURES	= 512		; textures d'un process
GK_MAX_BATCHES	= 1024		; lots d'un gk_render
GK_MAX_VERTS	= 3*65536	; sommets d'un gk_render

; un sommet (32 octets) : position (espace de decoupe, transformee par la
; matrice du lot), coordonnees de texture (0..1), couleur (multipliee par le
; texel) et couleur ajoutee (0 : aucune)
GKV_X		= 0		; flottants x y z w
GKV_Y		= 4
GKV_Z		= 8
GKV_W		= 12
GKV_S		= 16		; flottants s t
GKV_T		= 20
GKV_RGBA	= 24		; octets r g b a
GKV_RGBA2	= 28		; octets r2 g2 b2 a2
GKV_SIZE	= 32

; un lot (80 octets) : sommets [first, first + count) (triangles), son etat
GKB_FIRST	= 0		; u32
GKB_COUNT	= 4		; u32
GKB_TEX		= 8		; s32 : texture (gk_texture) ou -1
GKB_FLAGS	= 12		; u32 GK_B_*
GKB_MATRIX	= 16		; 16 flottants, ligne par ligne : decoupe = M * (x y z w)
GKB_SIZE	= 80

GK_B_ZFUNC	= 7		; test de profondeur (0 : LESS)
GK_Z_LESS	= 0
GK_Z_EQUAL	= 2
GK_Z_LEQUAL	= 3
GK_Z_GREATER	= 4
GK_Z_NOTEQUAL	= 5
GK_Z_GEQUAL	= 6
GK_Z_ALWAYS	= 7		; pas de test
GK_B_NOZWRITE	= 1 shl 3
GK_B_CULL_BACK	= 1 shl 4
GK_B_CULL_FRONT	= 1 shl 5
GK_B_BLEND_SHIFT = 8		; mode de melange (4 bits) :
GK_BLEND_NONE	= 0		;   opaque
GK_BLEND_ALPHA	= 1		;   s a + d (1 - a)
GK_BLEND_ADD	= 2		;   s a + d
GK_BLEND_MUL	= 3		;   s d
GK_BLEND_PREMUL	= 4		;   s + d (1 - sa)
GK_BLEND_MULCOL	= 5		;   (premultiplies) couleur s d + d (1 - sa), alpha garde
GK_BLEND_UNDER	= 6		;   couleur s (1 - da) + d, alpha par-dessus
GK_BLEND_SCREEN	= 7		;   s + d (1 - s)
GK_BLEND_PLUS	= 8		;   s + d (borne)
GK_BLEND_RSUB	= 9		;   d - s (borne a 0), alpha garde
GK_BLEND_LIGHTEN = 10		;   max (s, d)
GK_BLEND_DSTIN	= 11		;   d sa (masque)
GK_BLEND_DSTOUT	= 12		;   d (1 - sa) (decoupe)
GK_BLEND_LAST	= 12
GK_B_LINEAR	= 1 shl 12	; filtrage bilineaire (sinon le plus proche)
GK_B_WRAP_S_SHIFT = 13		; 2 bits : 0 repete, 1 bord, 2 miroir
GK_B_WRAP_T_SHIFT = 15
GK_WRAP_REPEAT	= 0
GK_WRAP_CLAMP	= 1
GK_WRAP_MIRROR	= 2
GK_B_NOMATRIX	= 1 shl 17	; matrice identite (sommets deja en espace de decoupe)
GK_B_ALPHATEST	= 1 shl 18	; + seuil (0..255) shl 19 : alpha < seuil / 255 pas dessine
GK_B_AT_SHIFT	= 19

; la cible (32 octets) : w x h pixels 0x00RRGGBB, pas en PIXELS
GKF_PIXELS	= 0		; adresse
GKF_W		= 8		; s32
GKF_H		= 12		; s32
GKF_STRIDE	= 16		; s32 : pixels par ligne
GKF_CLEAR	= 20		; u32 0xRRGGBB (GK_F_ALPHA : 0xAARRGGBB)
GKF_FLAGS	= 24		; u32 GK_F_*
GKF_SIZE	= 32
GK_F_KEEP	= 1		; dessine par-dessus les pixels (sinon effaces a clear)
GK_F_ALPHA	= 2		; cible 0xAARRGGBB premultipliee : alpha charge, melange, ecrit

; gk_draw : une fiche par texture (24 octets), dans une table que le lot designe
GKT_PIXELS	= 0		; adresse : 0xAARRGGBB, largeur pixels par ligne (le CPU les lit)
GKT_W		= 8		; s32
GKT_H		= 12		; s32
GKT_ID		= 16		; s32 : gk_texture, < 0 : pas sur le GPU (l'image va au CPU)
GKT_SIZE	= 24

; gk_debug (essais : gputest lists, tools/v3d_ref/gk_ref.cpp)
GK_DBG_DRY	= 1		; x1 = 1 : listes construites, rien envoye au GPU (marche sans GPU)
GK_DBG_LISTS	= 2		; x1 = sortie GKD_* : les listes du dernier job
GK_DBG_TEX	= 3		; x1 = texture, x2 = sortie GKD_TEX_* : ses texels
GKD_BCL		= 0
GKD_BCL_LEN	= 8
GKD_RCL		= 16
GKD_RCL_LEN	= 24
GKD_IND		= 32
GKD_IND_LEN	= 40
GKD_WK_PHYS	= 48		; adresse physique du tampon de travail
GKD_TGT		= 56		; adresse physique de la cible
GKD_SIZE	= 64
GKD_TEX_VA	= 0
GKD_TEX_BYTES	= 8
GKD_TEX_PHYS	= 16
