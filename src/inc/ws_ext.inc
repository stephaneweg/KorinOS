; ============================================================================
;  inc/ws_ext.inc - extensions du protocole d'Elegant (docs/DESIGN-ELEGANT.md
;  16), ajoutees avec les widgets de UIKit. Meme message que les WS_OP_* de
;  armpi.inc (a y fusionner plus tard) ; inclus par elegant.asm et uikit.asm.
; ============================================================================

; Presse-papiers du systeme (texte UTF-8), tenu par Elegant (elegant/uikit_ops.inc)
WS_OP_CLIP_SET		= 18		; a decalage, b longueur totale, +40 morceau ;
					; donnees : le morceau -> 0 (decalage 0 : nouveau contenu)
WS_OP_CLIP_GET		= 19		; a decalage -> longueur du morceau ; b longueur
					; totale, c generation ; donnees : le morceau
WS_CLIP_MAX		= 65536		; octets au plus
WS_CLIP_PIECE		= 464		; morceau max (toutes les donnees du message)

WS_OP_WIN_POS		= 20		; a id -> a x, b y du contenu a l'ecran, c largeur, d hauteur

; Fenetre redimensionnable : role WF_RESIZABLE ; Elegant envoie WEV_RESIZE
; (EV_X largeur, EV_Y hauteur voulues : bord droit / bas glisse, bouton
; agrandir) ; le programme (UIKit : ui_resize) repond par WS_OP_RESIZE.
WF_RESIZABLE		= 512		; role : redimensionnable (bords, agrandir)
WF_MODAL		= 1024		; role : dialogue modal, il bloque les autres fenetres
					; de son programme (un clic dessus le ramene devant)
WF_GLYPH_LIGHT		= 4096		; role : boutons du cadre en clair (contenu fonce sous eux)
WF_BODY_DARK		= 8192		; role : corps du cadre noir (coins d'un contenu noir)
WS_OP_WIN_DRAG		= 34		; a id : le bouton tenu deplace la fenetre (zone vide du contenu)
WF_CURTAIN		= 2048		; role : rideau, les fenetres normales sont cachees tant
					; qu'elle existe (WF_TOP ; le lanceur du panel)
WEV_RESIZE		= 8		; +8 largeur, +12 hauteur demandees
WS_OP_RESIZE		= 21		; a id, b largeur, c hauteur -> 0 ; a tampon neuf,
					; b / c decalages des cadres (comme CREATE)

; Formes du curseur : WS_OP_CURSOR (a id, b forme) ; la forme vaut tant que
; le pointeur est sur le contenu de cette fenetre
WS_OP_CURSOR		= 22		; a id, b forme UC_* -> 0
UC_ARROW		= 0
UC_TEXT			= 1		; barre de texte
UC_HAND			= 2		; main (lien, bouton)
UC_HRESIZE		= 3		; redimensionner a gauche / a droite
UC_VRESIZE		= 4		; en haut / en bas
UC_WAIT			= 5		; sablier
UC_CROSS		= 6		; croix
UC_DRAG			= 7		; (glisser-deposer en cours : Elegant)
UC_MOVE			= 8		; quatre fleches : deplacer
UC_NWSE			= 9		; coin : en haut a gauche / en bas a droite
UC_NESW			= 10		; coin : en haut a droite / en bas a gauche
UC_NO			= 11		; interdit

; Souris relative (jeux) : WS_OP_POINTER_LOCK (a id, b 1 : prend, 0 : rend). Tant que la fenetre tient le
; pointeur et a le clavier, le curseur est cache et ne bouge plus ; elle recoit WEV_MOUSE, genre WEV_M_REL,
; +8 dx, +12 dy, +16 boutons. Elle le perd avec le clavier (WEV_FOCUS) ou a sa fermeture.
WS_OP_POINTER_LOCK	= 35		; a id, b 1 / 0 -> 0
WEV_M_REL		= 3

; Glisser-deposer entre fenetres : pendant un glisser (bouton tenu apres un
; appui dans son contenu), le programme donne les donnees (WS_OP_DRAG) ; au
; relache, Elegant envoie WEV_DROP a la fenetre dessous (le programme y
; lit les donnees : WS_OP_DROP_GET)
WS_OP_DRAG		= 23		; a genre (UDT_*), +40 octets ; donnees -> 0
WS_OP_DROP_GET		= 24		; -> octets ; a genre, b octets ; donnees
WS_DRAG_MAX		= 464
WEV_DROP		= 9		; +8 x, +12 y (contenu), +16 genre, +20 octets
UDT_TEXT		= 1		; texte UTF-8
UDT_FILES		= 2		; chemins de fichiers, un par ligne

; Surface attachee a une fenetre (docs/DESIGN-ELEGANT.md 17) : le programme
; cree un tampon partage (BUF_CREATE) et dit a Elegant que le contenu de sa
; fenetre, ou une zone de ce contenu, est ce tampon ; il signale ensuite les
; rectangles changes. Elegant compose depuis le tampon, sans recopie.
WS_OP_SURFACE_ATTACH	= 25		; a id, b tampon (-1 : detacher), c x | y << 32
					; (zone du contenu), d l | h << 32 ; donnees : +0 format
					; WS_SF_* (32 bits), +4 pas (32 bits, 0 : l * octets),
					; +8 decalage du premier pixel (64 bits) -> 0 ; reponse
					; a = pid d'Elegant (E_PERM : BUF_GRANT, puis recommencer)
WS_OP_SURFACE_DAMAGE	= 26		; a id, b nombre de rectangles (0 : toute la
					; surface) ; donnees : x, y, l, h (16 bits) chacun
; Present asynchrone : en fin de tampon de fenetre (decalage rendu en d par
; CREATE et RESIZE), un mot de 64 bits, la zone salie du contenu, x0 | y0 << 16
; | x1 << 32 | y1 << 48 (0 : aucune), etendue par echange atomique ; le
; programme leve WS_SIG_DAMAGE quand il le fait passer de 0 a une zone, Elegant
; le reprend et le remet a 0 a son tour suivant, sans message ni reponse.
WS_SIG_DAMAGE		= 1		; bit de SYS_WS_SIGNAL : une zone salie, un WD_FRAME
WD_DAMAGE		= 0		; zone salie
WD_FRAME		= 8		; 1 : WEV_FRAME voulu (remis a 0 par Elegant)
WD_SIZE			= 16
WEV_FRAME		= 10		; le contenu a ete pris : le programme peut redessiner
WEV_PAD			= 11		; manette (inc/pad.inc) : +4 manette, +8 boutons PAD_*, +12 bruts, +16 axes, +24 modele
WS_FRAME_MS		= 16		; au plus un WEV_FRAME tous les tant de ms

; Espaces de travail (bureaux virtuels, kapi « desk » d'Onyx) : une fenetre
; normale s'ouvre sur l'espace courant et n'est montree que la ; fond et
; dessus (WF_BACK, WF_TOP) sont sur tous. Le dock en fixe le nombre (dock.ini).
WS_OP_DESK		= 29		; a espace a montrer (-1 : garder), b nombre (0 : garder ;
					; 1..WS_DESK_MAX ; les fenetres des espaces retires passent
					; sur le dernier) -> courant | nombre << 8 | compteur << 16
WS_DESK_MAX		= 8

WS_OP_MAXIMIZE		= 27		; a id : comme le bouton agrandir (WF_RESIZABLE) ;
					; WEV_RESIZE suit ; E_INVAL : pas redimensionnable
; Resolution de l'ecran, a chaud (elegant/screen.inc). Tout programme recoit
; WEV_SCREEN sur chacune de ses fenetres apres un changement ; une fenetre
; agrandie, ou trop grande et WF_RESIZABLE, recoit aussi WEV_RESIZE.
WS_OP_SCREEN_SET	= 30		; a largeur, b hauteur, c attente de confirmation en ms
					; (0 : definitif) -> 0 ; sans WS_OP_SCREEN_KEEP a temps,
					; retour a la taille d'avant ; E_BUSY : une attente en cours ;
					; E_INVAL : taille impossible ; E_IO : refusee par le firmware
WS_OP_SCREEN_KEEP	= 31		; a 1 garder, 0 revenir tout de suite -> 0
WS_OP_SCREEN_MODES	= 32		; -> nombre ; a, b taille native (0 : inconnue), c, d taille
					; actuelle ; donnees : WS_SM_N entrees WSM_* au plus
WS_OP_WIN_PLACE		= 33		; a id (une fenetre du programme), b x, c y -> 0
					; (dock, recentre ; toute couche, sans bornes)
WSM_W			= 0		; 16 bits
WSM_H			= 2		; 16 bits
WSM_FLAGS		= 4		; 32 bits : WSMF_*
WSM_SIZE		= 8
WS_SM_N			= 24
WSMF_EDID		= 1		; l'ecran dit savoir l'afficher sans mise a l'echelle
WSMF_NATIVE		= 2		; sa resolution native
WEV_SCREEN		= 12		; +8 largeur, +12 hauteur de l'ecran

WS_SF_XRGB8888		= 0		; 32 bits 0x00RRGGBB (octet haut ignore)
WS_SF_RGB565		= 1		; 16 bits rrrrrggggggbbbbb
WS_SF_NOREDRAW		= 256		; (avec le format) meme tampon et meme zone : rien a recomposer, DAMAGE le dira
WS_DAMAGE_MAX		= 58		; rectangles par message (464 / 8)
