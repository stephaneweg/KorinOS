; ============================================================================
;  kits/audiokit/audiokit.inc - constantes publiques d'AudioKit : decodage,
;  lecteur, API par paquets (docs/SON.md 3 et 9, docs/KITS.md)
;  (pour les programmes : include '../../kits/audiokit/audiokit.inc')
; ============================================================================

; --- formats (ak_dec_info +AKI_FORMAT, ak_codec_open) ---------------------------
AKF_WAV		= 1		; RIFF/WAVE : PCM 8/16/24/32 bits, flottant 32 bits
AKF_FLAC	= 2		; FLAC (RFC 9639), natif ou dans Ogg
AKF_MP3		= 3		; MPEG-1/2/2.5 couche III (ISO 11172-3, 13818-3)
AKF_VORBIS	= 4		; Vorbis I dans Ogg
AKF_OPUS	= 5		; Opus (RFC 6716) dans Ogg (RFC 7845)
AKF_MIDI	= 6		; fichier MIDI standard (.mid, formats 0 et 1)
AKF_FMS		= 7		; FM Song (.fms, FM Tracker)

; --- ak_dec_info : tampon de 64 octets --------------------------------------------
AKI_FORMAT	= 0		; AKF_*
AKI_RATE	= 8		; frequence (Hz)
AKI_CHANNELS	= 16		; canaux (1..8)
AKI_FRAMES	= 24		; trames en tout (-1 : inconnu)
AKI_DURATION	= 32		; duree (ms, -1 : inconnue)
AKI_BITRATE	= 40		; debit moyen (bits/s, 0 : inconnu)
AKI_POSITION	= 48		; position (trames rendues depuis le debut)
AKI_BITS	= 56		; bits par echantillon de la source (0 : compresse)
AKI_SIZE	= 64

; --- ak_dec_tag -------------------------------------------------------------------
AKT_TITLE	= 0
AKT_ARTIST	= 1
AKT_ALBUM	= 2

; --- erreurs ----------------------------------------------------------------------
AKE_NOMEM	= -12		; memoire insuffisante
AKE_INVAL	= -22		; argument invalide
AKE_FORMAT	= -95		; format inconnu ou non pris en charge
AKE_CORRUPT	= -74		; donnees invalides
AKE_IO		= -5		; lecture impossible

; --- API par paquets : drapeaux de ak_codec_decode ---------------------------------
AKC_S16		= 0		; sortie s16 entrelacee
AKC_F32		= 1		; sortie flottant 32 bits entrelacee (-1..1)

; --- lecteur (ak_play_state) -------------------------------------------------------
AKP_PLAYING	= 1
AKP_PAUSED	= 2
AKP_ENDED	= 3		; fin du fichier (tout est joue)
AKP_ERROR	= 4

; --- synthese FM (ak_fm_*) -----------------------------------------------------------
AK_FM_MAXV	= 64		; voix par synthe (ak_fm_new)
AKW_SQUARE	= 0		; formes d'onde (ak_fm_start)
AKW_SINE	= 1
AKW_TRIANGLE	= 2
AKW_SAW		= 3
AKW_NOISE	= 4
AKW_FM		= 5		; l'instrument de la voix (ak_fm_instrument)
; un instrument : deux operateurs (0 : modulateur, 1 : porteuse), a la maniere de l'OPL2
AKFO_ATTACK	= 0		; 0..15
AKFO_DECAY	= 1		; 0..15
AKFO_SUSTAIN	= 2		; 0..15 (attenuation : 3 dB par pas)
AKFO_RELEASE	= 3		; 0..15
AKFO_LEVEL	= 4		; 0..63 (attenuation : 0,75 dB par pas)
AKFO_KSL	= 5		; 0..3
AKFO_MULT	= 6		; 0..15 (0 : x 1/2)
AKFO_WAVE	= 7		; 0 sinus, 1 demi-sinus, 2 sinus absolu, 3 impulsions
AKFO_FLAGS	= 8		; AKFF_*
AKFO_SIZE	= 10
AKFI_OP0	= 0
AKFI_OP1	= AKFO_SIZE
AKFI_FEEDBACK	= 20		; 0..7
AKFI_CONN	= 21		; 0 FM, 1 additif
AKFI_SIZE	= 24
AKFF_SUSTAINED	= 1		; tenu tant que la touche est enfoncee
AKFF_TREMOLO	= 2
AKFF_VIBRATO	= 4
AKFF_KSR	= 8

; --- synthe MIDI (ak_synth_*) ----------------------------------------------------------
AKS_FM		= 1		; banque FM meme si une SoundFont est la
AKS_NOFX	= 2		; sans reverberation
; ak_sf_preset : tampon de 32 octets
AKSP_BANK	= 0		; 32 bits
AKSP_PROGRAM	= 4		; 32 bits
AKSP_NAME	= 8		; 20 octets, 0 final
AKSP_SIZE	= 32
