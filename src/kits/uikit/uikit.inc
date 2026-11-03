; ============================================================================
;  kits/uikit/uikit.inc - UIKit, cote programme : theme, variables de l'interface
;  (macro ui_data, a placer dans le segment inscriptible)
;
;	start:	adr	x0,ui_win		; variables du programme
;		bl	ui_init			; polices de SD0:/fonts
;		mov	x0,LARGEUR ; mov x1,HAUTEUR ; adr x2,titre ; bl ui_window
;		... boucle : mov x0,attente_ms ; bl ui_event ...
;
;  ui_win, ui_surf, ui_w, ui_h : la fenetre courante (plusieurs fenetres par
;  programme : ui_window_use, kits/uikit/uikit_window.inc). La surface (ui_surf,
;  pas = ui_w * 4) est dessinee directement puis signalee avec ui_update.
;  Texte : rasteriseur TrueType maison (ttf.asm), dans le kit.
; ============================================================================

; --- theme -------------------------------------------------------------------
UI_BG		= 0x00F3F4F6
UI_FG		= 0x001F2933
UI_DIM		= 0x00697586
UI_ACCENT	= 0x002E6DB4
UI_ACCENT_FG	= 0x00FFFFFF
UI_BTN		= 0x00E1E5EA
UI_BTN_HI	= 0x00CBD3DD
UI_BORDER	= 0x00B8C0CC
UI_FIELD	= 0x00FFFFFF
UI_SEL		= 0x00CFE1F5

; theme du bureau (ui_theme -> adresse ; SD0:/etc/theme.txt, cles d'Onyx)
UI_STYLE_PLAT	= 2			; moderne (docs/DESIGN-ELEGANT.md), le seul
TH_STYLE	= 0
TH_ACTIVE	= 8			; cadre de la fenetre active
TH_INACTIVE	= 16			; cadres des autres
TH_WINDOW	= 24			; fond des fenetres
TH_ACCENT	= 32			; focus, selection
TH_MENUBAR	= 40
TH_DOCK		= 48
TH_OUTLINE	= 56			; 0 aucun, 1 sombre, 2 noir
TH_SIZE		= 64

; jetons du style plat (ui_jetons -> adresse ; 32 bits chacun ; BARRE, DOCK,
; CONTOUR : octet haut = transparence ; SOMBRE : 1 si theme sombre)
TJ_FACE		= 0
TJ_SURFACE	= 4
TJ_CREUX	= 8
TJ_ELEVE	= 12
TJ_ELEVE_BAS	= 16
TJ_LIGNE	= 20
TJ_LIGNE_FORTE	= 24
TJ_TEXTE1	= 28
TJ_TEXTE2	= 32
TJ_TEXTE3	= 36
TJ_ACCENT	= 40
TJ_ACCENT_CLAIR	= 44
TJ_ACCENT_FONCE	= 48
TJ_ACCENT_VIF	= 52
TJ_DANGER	= 56
TJ_SUCCES	= 60
TJ_ALERTE	= 64
TJ_BARRE	= 68
TJ_DOCK		= 72
TJ_TITRE_ACTIF	= 76
TJ_TITRE_INACTIF = 80
TJ_CONTOUR	= 84
TJ_SOMBRE	= 88
TJ_MENU		= 92
TJ_ACCENT_INK	= 96			; encre lisible sur ACCENT (calculee)
TJ_TABLE	= 96			; (la part des tableaux de jetons)
TJ_SIZE		= 104

UI_SCRATCH	= 0x20000			; par police
UI_CACHE	= 0x10000

; Polices : ui_font (Inter), ui_font_b (Inter SemiBold), ui_font_m (DejaVu Sans Mono)

; r <- jeton t du theme (dans un programme : garde tous les registres, sauf x17)
macro jeton r*, t* {
	stp	x0,x30,[sp,-16]!
	bl	ui_jetons
	mov	x17,x0
	ldp	x0,x30,[sp],16
	ldr	r,[x17,t]
}

UI_FONT_SIZE	= 320			; = TTF_FONT_SIZE (ttf.asm), verifie par le kit

include '../../inc/ws_ext.inc'		; (WF_RESIZABLE, WEV_RESIZE...)

; --- widgets (poignees opaques : uikit_widgets.inc, docs/KITS.md) ----------
; codes rendus par ui_w_event (x1) et passes a la fonction de ui_w_on
UN_CHANGE	= 1			; valeur, selection, texte changes
UN_ACTIVATE	= 2			; double-clic, Entree, clic d'un bouton
UN_SORT		= 3			; liste triee par un clic sur un en-tete
UN_CLOSE	= 4			; barre d'onglets : fermeture demandee (croix)
UN_NEW		= 5			; barre d'onglets : « + »
UN_DROP		= 6			; un depot sur ce widget (ui_drop_get)
UN_CONTEXT	= 7			; bouton droit (liste : la ligne est choisie)
; ui_font_get
UFN_SANS	= 0			; (ui_font)
UFN_BOLD	= 1			; (ui_font_b)
UFN_MONO	= 2			; (ui_font_m)
UFN_ITALIC	= 3			; DejaVuSans-Oblique
UFN_BOLDITALIC	= 4
UFN_MONOBOLD	= 5
UFN_SERIF	= 6
UFN_SERIFBOLD	= 7
UFN_SERIFITALIC	= 8
UFN_SERIFBOLDITALIC = 9
; ui_view
UVW_FOCUS	= 1			; prend le clavier (clic, Tab) et garde Tab
; ui_layout
ULY_VSTACK	= 0
ULY_HSTACK	= 1
ULY_GRID	= 2
; ui_menu_state
UMS_DISABLED	= 1			; entree grisee
UMS_CHECKED	= 2			; entree cochee
; ui_panel
UPN_SURFACE	= 1			; fond SURFACE et contour
; ui_tabstrip
UTS_CLOSE	= 1			; croix de fermeture
UTS_PLUS	= 2			; « + » apres les onglets
; ui_imagebox
UIM_STRETCH	= 1			; remplir (sans garder les proportions)
UIM_NOSCALE	= 2			; taille reelle, centree
; ui_toolbar (ui_w_style)
UTBS_NOLINE	= 1			; sans le trait du bas
; ui_toolbar_add
UTB_TOGGLE	= 1			; a bascule (valeur 0 / 1)
UTB_SEP		= 2			; un espace avant
; ui_scrollbar
USB_HORIZONTAL	= 1
; ui_list
UL_HEADER	= 1			; en-tetes de colonnes
UL_NOSORT	= 2			; un clic sur un en-tete ne trie pas
UL_DRAG		= 4			; on peut glisser un element (son texte)
UL_STRIPES	= 8			; une ligne sur deux teintee (grille de donnees)
UL_GRID		= 16			; traits entre les colonnes
UL_SIDEBAR	= 32			; panneau lateral : « #titre » = une section
UL_HSCROLL	= 64			; barre horizontale quand les colonnes depassent
UL_USERSORT	= 128			; un clic sur un en-tete rend UN_SORT sans trier
					; (ui_list_sorted dit quoi) : le programme trie
UL_ICONS	= 256			; icone en colonne 0 selon la donnee & 3 : 0 fichier,
					; 1 dossier, 2 volume
; ui_list_column
ULC_RIGHT	= 1			; texte aligne a droite
; ui_textbox
UTF_PASSWORD	= 1			; mot de passe : des « • », ni copie ni coupe
UTF_READONLY	= 2			; lecture seule (selection et copie)
UTF_MONO	= 4			; police mono
UTF_RIGHT	= 256			; texte a droite (nombres)
UTF_ERROR	= 512			; contour DANGER : valeur refusee (ui_w_style)
; ui_textarea (avec UTF_READONLY, UTF_MONO)
UTA_LINENUM	= 8			; numeros de ligne dans une marge
UTA_VSCROLL	= 16			; barre de defilement verticale
UTA_HSCROLL	= 32			; barre horizontale (lignes plus longues)
UTA_CODE	= 64			; couleurs du code (assembleur), retrait garde
UTA_TABS	= 128			; Tab insere une tabulation (sinon 4 espaces)
UTA_FORMTAB	= 1024			; champ d'un formulaire : Tab passe au suivant
UTA_BASIC	= 2048			; avec UTA_CODE : couleurs de BASIC (' et REM, chaines ", ui_textarea_keywords)
UTA_CS		= 4096			; avec UTA_CODE : couleurs de C# (ui_textarea_keywords : d'autres mots)
UTA_XML		= 8192			; avec UTA_CODE : couleurs de XML
; ui_textarea_edit
UTE_UNDO	= 1
UTE_REDO	= 2
UTE_CUT		= 3
UTE_COPY	= 4
UTE_PASTE	= 5
UTE_ALL		= 6
; ui_pushbutton
UB_DEFAULT	= 1			; principal (accent ; Entree d'un dialogue)
UB_GHOST	= 2			; fantome (barre d'outils)
; ui_label
ULB_BOLD	= 1
ULB_DIM		= 2			; TEXTE2
ULB_WRAP	= 4			; lignes coupees
ULB_RIGHT	= 8
ULB_CENTER	= 16
ULB_TITLE	= 32			; titre de section (15 px gras)
; ui_message
UM_OK		= 0
UM_OKCANCEL	= 1
UM_YESNO	= 2
UM_YESNOCANCEL	= 3

; disposition des variables (le kit les lit par le pointeur donne a ui_init)
UV_WIN		= 0
UV_SURF		= 8
UV_W		= 16			; UV_W, UV_H contigus
UV_H		= 24
UV_EV		= 32
UV_FONT		= 64
UV_FONT_B	= UV_FONT + UI_FONT_SIZE
UV_FONT_M	= UV_FONT_B + UI_FONT_SIZE

macro ui_data {
		align 8
ui_win		dd 0
ui_surf		dd 0
ui_w		dd 0
ui_h		dd 0
ui_ev		rb 32
ui_font		rb UI_FONT_SIZE
ui_font_b	rb UI_FONT_SIZE
ui_font_m	rb UI_FONT_SIZE
}
