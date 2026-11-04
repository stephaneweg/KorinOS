; ============================================================================
;  armpi.inc - interface des applications ARMPI (EL0, ELF64)
;
;  En-tete type d'une application :
;	include '../../inc/armpi.inc'
;	app_header			; format ELF64, base 0x40_0000_0000
;	segment readable executable
;	start: ...
;
;  Appels systeme : svc 0, x8 = numero, x0..x5 arguments, retour x0.
; ============================================================================

macro app_header base:0x4000000000 {
	format	ELF64 executable 3 at base
	processor cpu64_v8 +cpu64_fp +cpu64_simd
	code64
	entry	start
}

include 'syscalls.inc'		; numeros des appels systeme (SYS_*)

SND_RATE		= 44100
E_AGAIN			= -11
E_TIMEDOUT		= -110
DMA_CACHED		= 1 shl 63	; (dma_alloc, ou dans la taille) memoire cachee : DC CVAC avant le DMA
PROC_RIGHTS		= 0x100		; (proc_flags) programme de SD0:/sys
INIT_OP_LAUNCH		= 1		; (service init2) donnees : chemin SD0:/sys/..., 0, arguments, 0 -> pid
INIT_OP_KIT_LOAD	= 2		; (service init2) donnees : nom du kit -> 0 (inscrit : lib_open ensuite)
INIT_OP_REBOOT		= 3		; (service init2) a bit 0 : tryboot, bit 1 : arret -> 0, puis le Pi redemarre ou s'arrete (PM)
INIT_REBOOT_HALT	= 2		; (INIT_OP_REBOOT) arret : le firmware ne relance pas
SVF_FINS		= 1		; svc_register : prevenu des fins de process
SVC_OP_FIN		= 0xFFFF	; (noyau -> service) a = pid termine
SVF_NAISSANCES		= 2		; svc_register : prevenu des naissances (avis 0xFFFE)
SVF_MEMORY		= 4		; svc_register : garde de la memoire qu'il peut rendre (SVC_SIG_LOWMEM)
SVF_POST		= 8		; svc_register : peut deposer des evenements chez les programmes (WS_POST : busd)
SVC_OP_NAISSANCE	= 0xFFFE	; (noyau -> service) a = fils, b = pere (0 : noyau)
BUF_LOW			= 1 shl 62	; (buf_create, dans la taille) pages sous 1 Go (adresse physique pour le materiel)
BUF_NOCACHE		= 1 shl 61	; (buf_create, dans la taille) vue non cachee, de partout (docs/MESA.md)
BUF_VA_BASE		= 0x6800000000	; tampon n : BUF_VA_BASE + n * BUF_SLOT
BUF_SLOT		= 0x4000000

; message au serveur (512 o) : +0 op / resultat, +8 a, +16 b, +24 c, +32 d,
; +40 longueur, +44 pid de l'appelant (noyau), +48 donnees
WS_MSG			= 512
WM_OP			= 0
WM_A			= 8
WM_B			= 16
WM_C			= 24
WM_D			= 32
WM_LEN			= 40		; longueur des donnees (32 bits)
WM_PID			= 44
WM_DATA			= 48
WS_OP_CREATE		= 1		; a x, b y (-1 : auto), c l, d h, donnees : role (WF_*), titre (+8)
					; -> id ; a tampon, b cadre actif, c cadre inactif (decalages)
WS_OP_UPDATE		= 2		; a id, b x, c y, d l, donnees : h (64 bits)
WS_OP_CLOSE		= 3		; a id
WS_OP_TITLE		= 4		; a id, donnees titre
WS_OP_SCREEN		= 5		; -> a largeur, b hauteur, c reserve en bas (dock), d en haut
WS_OP_FRAME		= 6		; a id : le cadre a ete redessine
WS_OP_LIST		= 7		; a premiere -> nombre ; donnees : 7 entrees WL_* de 64 o
WS_OP_RAISE		= 8		; a id : au premier plan (restauree si reduite)
WS_OP_MESURE		= 9		; a images, b 1 : sans transparence -> ms (outil mesure)
WS_OP_MENU_SET		= 10		; a id, b decalage, c longueur totale, +40 morceau ; donnees : texte des menus
WS_OP_MENU_GET		= 11		; a decalage -> numero de serie (0 : pas de menu) ; b id, c longueur,
					; d morceau ; donnees : titre (WS_MENU_TITLE o), puis le morceau
WS_OP_MENU_COMMAND	= 12		; a entree (-1 : Quitter), b numero de serie (0 : sans controle)
WS_OP_SNAPSHOT		= 13		; b generation vue -> 1 copie / 0 rien ; a l, b h, c generation,
					;   d tampon (taille de l'ecran) ; donnees : x0, y0, x1, y1
WS_OP_WIN_LIST		= 14		; a premiere -> nombre ; a id au clavier (0 : aucune) ;
					;   donnees : 7 entrees WE_* de 64 o, du fond vers l'avant (rdpd)
WS_OP_WIN_READ		= 15		; a id, b partie (0 contenu, 1/2 cadre actif/inactif),
					;   c x | y << 32, d l | h << 32 -> 0 ; rectangle dans le tampon de
					;   l'appelant (celui de SNAPSHOT, pas l * 4) ; a, b generations,
					;   c taille du tampon, d son numero (l = 0 : rien copie)
WS_OP_WIN_MOVE		= 16		; a id, b x, c y : contenu place la (fenetres normales)
WS_OP_WIN_CLOSE		= 17		; a id : WEV_CLOSE a son programme (case de fermeture)
WS_WIN_DESKTOP		= 0xFFFFFFFF	; (WIN_READ) le fond et les fenetres WF_BACK
WS_MENU_MAX		= 4096		; texte des menus d'une fenetre (octets, sans le NUL)
WS_MENU_PIECE		= 464		; MENU_SET : morceau max (toutes les donnees)
WS_MENU_TITLE		= 128		; MENU_GET : titre en tete des donnees
WS_MENU_GPIECE		= 336		; MENU_GET : morceau max apres le titre
WS_OP_CURSOR_GET	= 28		; -> forme UC_* dessinee (rdpd : le curseur du client)
WS_OP_EXIT		= 0xFFFF	; (noyau) a = pid termine
E_NODEV			= -19

; role d'une fenetre (WS_OP_CREATE)
WF_FRAMELESS		= 1		; sans cadre, placee ou demande
WF_BACK			= 2		; sous toutes les autres (fond d'ecran)
WF_TOP			= 4		; au-dessus des autres (barre de menus, dock, menus)
WF_NOFOCUS		= 8		; ne prend pas le clavier
WF_NOLIST		= 16		; absente de la liste des fenetres (dock)
WF_LISTEN		= 32		; recoit WEV_WINLIST quand la liste change
WF_GRAB			= 64		; recoit toute la souris et le clavier (menu ouvert)
WF_RESERVE		= 128		; reserve son bord de l'ecran (haut ou bas)
WF_ALPHA		= 256		; octet haut des pixels = transparence (0 opaque)

; liste des fenetres (WS_OP_LIST) : entree de 64 octets
WL_ID			= 0
WL_PID			= 8		; 32 bits
WL_STATE		= 12		; bit 0 : active, bit 1 : reduite, bit 2 : sur un autre espace de travail
WL_TITLE		= 16		; 48 octets
WL_SIZE			= 64
WL_PER_MSG		= 7

; liste etendue (WS_OP_WIN_LIST, rdpd) : entree de 64 octets
WE_ID			= 0		; 32 bits
WE_PID			= 4		; 32 bits
WE_X			= 8		; 16 bits signes : contenu, coordonnees de l'ecran
WE_Y			= 10
WE_W			= 12		; 16 bits : taille du contenu
WE_H			= 14
WE_FLAGS		= 16		; 32 bits : role (WF_*)
WE_STATE		= 20		; 32 bits : WES_*
WE_GEN			= 24		; 32 bits : generation du contenu (+1 a chaque WS_OP_UPDATE)
WE_CGEN			= 28		; 32 bits : generation du cadre (+1 a chaque WS_OP_FRAME)
WE_TITLE		= 32		; 32 octets (UTF-8, tronque)
WE_SIZE			= 64
WE_PER_MSG		= 7
WES_KEYS		= 1		; recoit le clavier
WES_MINI		= 2		; cachee (reduite, ou sur un autre espace de travail)
WES_REDUITE		= 4		; reduite
WES_DESK		= 8		; bits 8.. : espace de travail + 1 (0 : tous)

; cadre d'une fenetre (dessine par UIKit, deux copies : active, inactive)
FRAME_TITLE_H		= 12		; haut du cadre : comme les bords, il n'y a plus de barre de titre
FRAME_BORDER		= 12		; (l'ombre)
FRAME_GAP		= 4		; du contenu au bord de l'ecran, au plus pres
FRAME_HIT		= 6		; bord saisissable autour du contenu ; au-dela, l'ombre laisse passer
FRAME_RADIUS		= 10
FRAME_CAP_BTN		= 28		; boutons de la fenetre, en haut a droite du contenu :
FRAME_CAP_W		= 3*FRAME_CAP_BTN+18	; depuis la droite fermer, agrandir, reduire, poignee (invisible)
FRAME_CAP_H		= 26		; (les barres d'outils laissent cette place libre)
FRAME_CAP_MARGIN	= 5
FRAME_CORNER		= 8		; lignes du contenu coupees aux coins

; evenements de fenetre (32 octets)
WEV_KEY			= 1		; +4 appui/relache, +16 code HID, +20 caractere, +24 mods
WEV_MOUSE		= 2		; +4 0 depl./1 appui/2 relache, +8 x, +12 y, +16 boutons
WEV_CLOSE		= 3
WEV_WHEEL		= 4		; +12 crans
WEV_FOCUS		= 5		; +4 1 gagne / 0 perdu
WEV_WINLIST		= 6		; la liste des fenetres a change (WF_LISTEN)
WEV_MENU		= 7		; choix dans la barre de menus : +16 numero de l'entree
EV_TYPE			= 0
EV_KIND			= 4
EV_X			= 8
EV_Y			= 12
EV_CODE			= 16
EV_CHAR			= 20
EV_MODS			= 24
EV_WIN			= 28		; fenetre (id) de l'evenement

; codes HID utiles
KEY_ENTER		= 0x28
KEY_ESC			= 0x29
KEY_BACKSPACE		= 0x2A
KEY_TAB			= 0x2B
KEY_DELETE		= 0x4C
KEY_RIGHT		= 0x4F
KEY_LEFT		= 0x50
KEY_DOWN		= 0x51
KEY_UP			= 0x52
KEY_HOME		= 0x4A
KEY_END			= 0x4D
KEY_PGUP		= 0x4B
KEY_PGDN		= 0x4E

O_READ			= 1
O_WRITE			= 2
O_CREATE		= 4
O_TRUNC			= 8
O_APPEND		= 16
O_NONBLOCK		= 32		; (tube) read rend E_AGAIN plutot qu'attendre

ATTR_DIR		= 0x10
ATTR_SYS		= 0x100		; (vfs, ops 30 et 36) fichier sous SD0:/sys/
ATTR_LIB		= 0x200		; (vfs, ops 30 et 36) fichier sous SD0:/lib/ (kits)
DIRENT_NAME		= 0		; nom UTF-8 (256 octets)
DIRENT_SIZE		= 256
DIRENT_ATTR		= 264
DIRENT_BYTES		= 272

; Appel systeme. Les appels de fichiers (SYS_OPEN..SYS_SPAWN_PIPE, 0x80-0x8C)
; et print (SYS_PRINT, etape 4 : sortie standard) d'un programme qui
; definit FICHIERS_APPKIT = 1 (et importe AppKit) vont au client de fichiers
; d'AppKit, fichier_sys (docs/FICHIERS.md, section 3) : memes registres
; rendus qu'un svc 0, sauf x16 (relais du kit) ; x30 garde ici (16 o de pile
; le temps de l'appel).
macro sys n* {
	mov	x8,n
  if defined FICHIERS_APPKIT
   if FICHIERS_APPKIT & (((n) >= SYS_APPKIT_FIRST & (n) <= SYS_APPKIT_LAST) | (n) = SYS_PRINT)
	str	x30,[sp,-16]!
	bl	fichier_sys
	ldr	x30,[sp],16
   else
	svc	0
   end if
  else
	svc	0
  end if
}

macro movl reg*, val* {
	movz	reg,(val) and 0xFFFF
  if ((val) shr 16) and 0xFFFF <> 0
	movk	reg,((val) shr 16) and 0xFFFF,lsl 16
  end if
  if ((val) shr 32) and 0xFFFF <> 0
	movk	reg,((val) shr 32) and 0xFFFF,lsl 32
  end if
  if ((val) shr 48) and 0xFFFF <> 0
	movk	reg,((val) shr 48) and 0xFFFF,lsl 48
  end if
}

macro prologue {
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
}

macro epilogue {
	ldp	x29,x30,[sp],16
	ret
}
