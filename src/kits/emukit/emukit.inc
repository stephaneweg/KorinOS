; EmuKit : constantes publiques (docs/EMULATEURS.md). Le descripteur d'un emulateur :
; des mots de 64 bits, la structure emu_desc de emukit.h en C.
EMD_NAME	= 0		; nom de la console (titre de la fenetre)
EMD_EXT		= 8		; motifs des ROM, « *.nes » (ui_file_filter)
EMD_W		= 16		; taille de l'image
EMD_H		= 24
EMD_MAXW	= 32		; taille maximale (0 : EMD_W, EMD_H) ; emu_frame_size la fait varier
EMD_MAXH	= 40
EMD_FPS		= 48		; millihertz (60099)
EMD_RATE	= 56		; frequence du son (s16 stereo)
EMD_CTX		= 64		; premier argument de toutes les fonctions
EMD_LOAD	= 72		; (ctx, rom, octets) -> 1 ; 0 : refusee. La ROM reste en memoire
EMD_FRAME	= 80		; (ctx, pixels 0x00RRGGBB, pas en octets, boutons PAD_*, EMF_*)
EMD_AUDIO	= 88		; (ctx, lr s16, trames au plus) -> trames ; 0 : pas de son
EMD_RESET	= 96		; (ctx) ; thread de la machine
EMD_SAVE	= 104		; (ctx, &pointeur) -> octets a ecrire (0 : rien de change), 0 si pas de sauvegarde
EMD_RESTORE	= 112		; (ctx, donnees, octets) : <rom>.sav au chargement
EMD_MENU	= 120		; texte de menus en plus (ui_menu), numeros >= 100
EMD_CMD		= 128		; (ctx, numero) ; thread de la machine, entre deux images
EMD_MOUSE	= 136		; (ctx, genre EV_KIND, x, y dans l'image, boutons) ; thread de la fenetre
EMD_BUFS	= 144		; trois pointeurs : les images (0 : le kit les alloue)
EMD_SKIP	= 152		; 1 : EMF_SKIP accepte (la machine rattrape son retard sans dessiner)
EMD_ERROR	= 160		; texte du refus de la ROM (0 : un texte commun)
EMD_SIZE	= 168

EMF_SKIP	= 1		; image a ne pas dessiner (la machine est en retard)

EME_CANCEL	= 1		; emu_run : aucune ROM choisie

; emu_game : un jeu plutot qu'une console. La fonction des touches (ctx, code HID, 1 appui / 0 relache, caractere,
; modificateurs) tourne sur le thread de la fenetre : toutes les touches lui vont, sauf F9 et F12, celles de la vue.
; code EMG_KEY_WHEEL : molette, la valeur a la place de l'appui ; EMG_KEY_NONE : le clavier est parti, tout est relache.
EMG_NOROM	= 1		; pas de ROM : EMD_LOAD (ctx, 0, 0), ni Open ni Reset ; la manette 0 (le clavier) n'est pas lue
EMG_KEY_WHEEL	= 0x1000
EMG_KEY_NONE	= 0x1001
