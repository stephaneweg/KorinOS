; ============================================================================
;  inc/sound.inc - service " sound " (src/svc/sound, docs/SON.md) : ops des
;  programmes (par AudioKit), des pilotes audio (svc/inc/sndif.inc), anneaux
;  partages et tampon d'etat
;
;  Message (512 o) : +0 op / resultat, +8 a, +16 b, +24 c, +32 d,
;  +40 longueur des donnees (32 bits), +44 pid (noyau), +48 donnees (464 o)
;
;  Format unique des flux et de la capture : s16 stereo 44 100 Hz (4 octets
;  par trame) ; AudioKit convertit le reste dans le process de l'application.
; ============================================================================

SND_RATE_STD		= 44100
SND_BLOCK		= 1024		; trames melangees par bloc (~23 ms)

; --- ops des programmes (AudioKit) ----------------------------------------------
SND_OP_OPEN		= 1		; a sortie (0 : celle du client) -> flux ; b = tampon
					;   (anneau du flux, SR_*)
SND_OP_CLOSE		= 2		; a flux (lecture ou capture)
SND_OP_REC_OPEN		= 3		; a source (REC_SRC_*) -> flux de capture ; b = tampon
SND_OP_STATE		= 4		; -> b = tampon d'etat (lecture seule, SS_*)
SND_OP_OUT_VOLUME	= 5		; a sortie, b volume 0..100, c muet (0/1)
SND_OP_CLIENT_VOLUME	= 6		; a pid (0 : soi-meme), b volume 0..100, c muet
SND_OP_ROUTE		= 7		; a pid (0 : soi-meme), b sortie (0 : la principale)
SND_OP_MAIN		= 8		; a sortie : principale preferee (SD0:/etc/sound.txt)

; --- ops des pilotes (sndif.inc) -------------------------------------------------
SND_OP_DRV_OUTPUT	= 20		; a type (SND_OUT_*), b frequence, c 1 : canal DMA
					;   voulu ; donnees : nom -> sortie ; b = tampon
					;   (anneau de sortie), c = canal DMA (-1 : aucun)
SND_OP_DRV_INPUT	= 21		; a type (SND_IN_*), b frequence, c 1 : canal DMA ;
					;   donnees : nom -> entree ; b = tampon, c = canal
SND_OP_DRV_ALIVE	= 22		; a sortie ou entree (SND_ID_INPUT) -> 0 ; E_INVAL :
					;   inconnue (sound relance : se reinscrire)
SND_ID_INPUT		= 0x1000	; (DRV_ALIVE) bit des entrees

; types de sortie (et materiel rendu par ak_status +16, comme l'ancien
; appel 31 : 0 virtuelle, 1 PWM, 2 HDMI, 3 USB)
SND_OUT_VIRTUAL		= 0
SND_OUT_PWM		= 1
SND_OUT_HDMI		= 2
SND_OUT_USB		= 3
SND_IN_I2S		= 2		; types d'entree (comme REC_FROM_*)
SND_IN_USB		= 3

; sources de capture (SND_OP_REC_OPEN, ak_rec_open)
REC_SRC_MIC		= 0		; micro : la premiere entree presente (USB, I2S)
REC_SRC_LOOP		= 1		; boucle : ce que joue la sortie principale
REC_SRC_LOOP_OUT	= 0x100		; + n : ce que joue la sortie n
REC_SRC_INPUT		= 0x200		; + n : l'entree n
REC_FROM_LOOP		= 1		; source effective (ak_rec_status +8)
REC_FROM_I2S		= 2
REC_FROM_USB		= 3

; --- anneaux partages (tampons crees par sound) ------------------------------------
; Compteurs d'octets monotones ; position = compteur and (taille - 1)
SR_HEAD			= 0		; ecrit par le producteur
SR_TAIL			= 8		; ecrit par le consommateur
SR_VOLUME		= 16		; flux : volume 0..256 (AudioKit)
SR_SOURCE		= 16		; capture : source effective (REC_FROM_*)
SR_STATE		= 24		; flux : 0 ouvert, 1 sortie perdue, 2 ferme par sound
					; sortie / entree : 1 pilote arrete
SR_OUTPUT		= 32		; flux : sortie courante
SR_PLAYED		= 40		; trames jouees (sortie : par le pilote)
SR_LOST			= 48		; manques (flux, sortie) ou pertes (capture)
SR_RATE			= 56		; sortie / entree : frequence du materiel
SR_OUT_TYPE		= 56		; flux : type de la sortie courante (SND_OUT_*)
SR_WANTED		= 64		; entree : 1 si une capture l'ecoute (le pilote
					;   allume alors son materiel)
SR_DATA			= 4096		; donnees
SR_STREAM_BYTES		= 65536		; flux et capture (~370 ms)
SR_OUT_BYTES		= 32768		; sortie (pilote)
SR_IN_BYTES		= 65536		; entree (pilote)

; --- tampon d'etat (SND_OP_STATE), reecrit par sound --------------------------------
SS_GEN			= 0		; generation (+1 a chaque changement)
SS_MAIN			= 8		; sortie principale (0 : aucune)
SS_NOUT			= 16		; sorties
SS_NCLI			= 24		; clients
SS_NIN			= 32		; entrees
SS_OUTS			= 64		; SS_OUT_MAX entrees de 64 o
SS_OUT_MAX		= 8
SO_ID			= 0		; 32 bits chacun
SO_TYPE			= 4
SO_RATE			= 8
SO_VOLUME		= 12		; 0..100
SO_MUTE			= 16
SO_FLAGS		= 20		; bit 0 principale, bit 1 preferee
SO_PEAK			= 24		; crete du dernier bloc (0..32767)
SO_NAME			= 32		; 32 o (UTF-8)
SS_CLIS			= 64 + SS_OUT_MAX * 64
SS_CLI_MAX		= 32
SC_PID			= 0
SC_VOLUME		= 4
SC_MUTE			= 8
SC_ROUTE		= 12		; 0 : suit la principale
SC_OUTPUT		= 16		; sortie ou il joue
SC_PEAK			= 20
SC_STREAMS		= 24
SC_NAME			= 32		; 32 o
SS_INS			= SS_CLIS + SS_CLI_MAX * 64
SS_IN_MAX		= 4		; entrees : comme les sorties (SO_*)
SS_BYTES		= 4096
