; ============================================================================
;  kits/mediakit/mediakit.inc - MediaKit, cote programme : structures rendues
;  (champs de 64 bits) et erreurs (docs/MEDIAKIT.md ; tools/mediakit/mediakit.c)
; ============================================================================

; erreurs (negatives, numeros d'ARMPI)
MK_E_NOENT	= -2			; fichier introuvable
MK_E_IO		= -5
MK_E_NOMEM	= -12
MK_E_INVAL	= -22
MK_E_SPIPE	= -29			; recherche impossible (pas d'index)
MK_E_NODATA	= -61			; pas de piste de ce genre
MK_E_BADMSG	= -74			; donnees illisibles
MK_E_UNSUP	= -95			; format ou codec non pris (son : pour AudioKit)

; codecs (AV_C_* de la bibliotheque av d'Onyx)
MK_C_VP8	= 1
MK_C_VP9	= 2
MK_C_AV1	= 3
MK_C_H264	= 4
MK_C_OPUS	= 64
MK_C_VORBIS	= 65
MK_C_AAC	= 66
MK_C_MP3	= 67
MK_C_FLAC	= 68

; mk_info
MKI_DURATION	= 0			; us (0 inconnue)
MKI_NTRACKS	= 8
MKI_VTRACK	= 16			; pistes choisies (-1 : aucune)
MKI_ATRACK	= 24
MKI_WIDTH	= 32			; taille d'affichage
MKI_HEIGHT	= 40
MKI_FPS1000	= 48			; images/s x 1000 (0 inconnu)
MKI_RATE	= 56			; son
MKI_CHANNELS	= 64
MKI_VCODEC	= 72
MKI_ACODEC	= 80
MKI_VOK		= 88			; 1 : decode par MediaKit
MKI_AOK		= 96
MKI_CONTAINER	= 104			; 16 o : « webm », « mp4 »...
MKI_VNAME	= 120			; 16 o : « vp9 »...
MKI_ANAME	= 136			; 16 o : « opus »...
MKI_SIZE	= 152

; mk_track
MKT_KIND	= 0			; 1 video, 2 audio
MKT_CODEC	= 8
MKT_WIDTH	= 16
MKT_HEIGHT	= 24
MKT_RATE	= 32
MKT_CHANNELS	= 40
MKT_DURATION	= 48
MKT_EXTRA	= 56			; donnees d'initialisation du codec (au kit)
MKT_EXTRA_LEN	= 64
MKT_SUPPORTED	= 72
MKT_NAME	= 80			; 16 o
MKT_CODEC_ID	= 96			; 32 o (« V_VP9 », « avc1 »)
MKT_DELAY	= 128			; us (pre-skip d'Opus)
MKT_SIZE	= 136

; mk_video_next, mk_video_read
MKF_PTS		= 0			; us
MKF_DUR		= 8
MKF_WIDTH	= 16
MKF_HEIGHT	= 24
MKF_DECODE_US	= 32
MKF_SIZE	= 40

; mk_audio_packet (paquet brut, au kit jusqu'a l'appel suivant)
MKP_DATA	= 0
MKP_SIZE	= 8
MKP_PTS		= 16
MKP_DUR		= 24
MKP_KEY		= 32
MKP_CODEC	= 40
MKP_SIZEOF	= 48

; mk_stats
MKS_VFRAMES	= 0
MKS_VDECODE_US	= 8
MKS_AFRAMES	= 16
MKS_ADECODE_US	= 24
MKS_DROPPED	= 32
MKS_HEAP_BIG	= 40
MKS_SIZE	= 48
