; ============================================================================
;  kits/archivekit/archivekit.inc - constantes publiques d'ArchiveKit
;  (pour les programmes : include '../../kits/archivekit/archivekit.inc' ;
;  pour le C : archivekit_abi.h, genere par tools/gcc/kits.sh)
; ============================================================================

; --- enveloppes (ar_deflate, ar_inflate) ------------------------------------------
AR_RAW		= 0		; le flux deflate seul (une entree de ZIP)
AR_ZLIB		= 1		; en-tete zlib et Adler-32 (PNG, PDF)
AR_GZIP		= 2		; en-tete gzip et CRC-32 (.gz)
AR_AUTO		= 3		; (inflate seulement) zlib ou gzip, vu aux premiers octets

; --- niveaux ----------------------------------------------------------------------
AR_STORE	= 0		; aucune compression
AR_FAST		= 1
AR_DEFAULT	= 6		; aussi le niveau -1
AR_BEST		= 9

; --- formats d'archive ------------------------------------------------------------
AR_FMT_ZIP	= 1		; lu (ZIP64 compris) et ecrit
AR_FMT_TAR	= 2		; lu (ustar, GNU, pax) et ecrit (ustar, noms longs GNU)
AR_FMT_TGZ	= 3		; tar.gz : lu et ecrit
AR_FMT_GZIP	= 4		; .gz d'un seul fichier : lu et ecrit (une seule entree)

; --- ar_entry : tampon de AR_ENTRY_SIZE octets (long info[8] en C) ----------------
ARE_NAME	= 0		; pointeur sur le nom (UTF-8, '/' entre les parties, sans '/' final),
				;   valable jusqu'a ar_close
ARE_SIZE	= 8		; taille d'origine
ARE_PACKED	= 16		; taille dans l'archive
ARE_TIME	= 24		; date (secondes UTC depuis 1970, 0 : aucune)
ARE_DIR		= 32		; 1 : un dossier
ARE_METHOD	= 40		; 0 stocke, 8 deflate (ZIP ; tar : 0 ; gz : 8)
ARE_CRC		= 48		; CRC-32 annonce (0 : aucun)
ARE_FLAGS	= 56		; ARF_ENCRYPTED
AR_ENTRY_SIZE	= 64
ARF_ENCRYPTED	= 1		; entree chiffree : ar_read rend AR_E_CRYPT

; --- ar_formats : tableau d'enregistrements de ARFI_SIZE octets ---------------------
ARFI_ID		= 0		; AR_FMT_*
ARFI_READ	= 8		; 1 : se lit
ARFI_WRITE	= 16		; 1 : se cree
ARFI_NAME	= 24		; nom (16 octets, 0 final) : ZIP, TAR, TAR.GZ, GZIP
ARFI_EXT	= 40		; extensions (88 octets, 0 final), en minuscules, separees par un blanc
ARFI_SIZE	= 128

; --- erreurs (valeurs negatives ; aussi celles d'AppKit, E_NOENT par exemple) -------
AR_E_IO		= -5		; lecture ou ecriture impossible
AR_E_NOMEM	= -12		; plus de memoire
AR_E_CRYPT	= -13		; entree chiffree (ZipCrypto, AES : pas pris en charge)
AR_E_INVAL	= -22		; poignee, indice, enveloppe ou niveau invalide
AR_E_FBIG	= -27		; trop grand pour le format (ZIP ecrit : 4 Gio par entree)
AR_E_SPACE	= -28		; tampon de sortie trop petit
AR_E_METHOD	= -38		; methode de compression inconnue
AR_E_CORRUPT	= -74		; donnees abimees ou tronquees
AR_E_CRC	= -84		; somme de controle fausse
AR_E_FORMAT	= -95		; pas un format connu
