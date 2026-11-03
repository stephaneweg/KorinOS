; ============================================================================
;  inc/usb.inc - constantes USB communes : usbd, ses controleurs, les pilotes
;  de classe (docs/USB.md)
; ============================================================================

; --- service « usb » (usbd) -------------------------------------------------
USB_OP_DESCRIPTEURS	= 1		; a = peripherique -> config (donnees), b = longueur,
					;   c = vendeur << 16 | produit, d = vitesse
USB_OP_CONTROLE		= 2		; a = peripherique, b = SETUP, c = longueur (donnees)
USB_OP_INTR_OUVRIR	= 3		; a = peripherique, b = extremite, c = taille max.,
					;   d = intervalle (ms) -> tuyau
USB_OP_INTR_LIRE	= 4		; a = tuyau -> rapport (reponse differee)
USB_OP_BULK_TAMPON	= 5		; a = taille -> tampon partage cree par usbd et
					;   autorise au pilote (buf_map)
USB_OP_BULK		= 6		; a = peripherique, b = extremite, c = taille max.,
					;   d = decalage ; donnees +0 : longueur -> octets
USB_OP_BULK_NB		= 7		; lecture bulk IN sans attente (usbnet) : comme 6 ;
					;   -> octets (> 0), 0 si rien encore (NAK), < 0 erreur
USB_OP_ISO_OPEN		= 8		; a = peripherique, b = extremite (bit 7 : IN), c = taille
					;   max., d = octets par ms en 16.16 | octets par trame << 32
					;   -> tuyau isochrone ; b = tampon partage (anneau UI_*)
USB_OP_ISO_CLOSE	= 9		; a = tuyau isochrone

; --- anneau d'un tuyau isochrone (cree par usbd, docs/SON.md 5.1) -------------
; OUT : le pilote ecrit (tete), usbd envoie un paquet par ms (queue) ; IN :
; usbd ecrit ce qu'il recoit. Compteurs d'octets monotones.
UI_HEAD			= 0
UI_TAIL			= 8
UI_LOST			= 16		; octets manquants (OUT) ou perdus (IN)
UI_PACKETS		= 24		; paquets faits
UI_STATE		= 32		; 1 : peripherique retire (tuyau ferme)
UI_DATA			= 4096
UI_BYTES		= 32768

; --- service « hwmanager » --------------------------------------------------
HW_OP_USB		= 1		; a = peripherique, b = interface,
					;   c = classe << 16 | sous-classe << 8 | protocole,
					;   d = vendeur << 16 | produit
HW_OP_USB_RETRAIT	= 2		; a = peripherique (retire)

; --- vitesses (usbd_attacher) -------------------------------------------------
USB_FS			= 0
USB_LS			= 1
USB_HS			= 2
USB_SS			= 3

; --- descripteurs -------------------------------------------------------------
USB_DT_DEVICE		= 1
USB_DT_CONFIG		= 2
USB_DT_STRING		= 3
USB_DT_INTERFACE	= 4
USB_DT_ENDPOINT		= 5
USB_DT_HID		= 0x21
USB_DT_REPORT		= 0x22
USB_DT_HUB		= 0x29

; --- requetes standard (bRequest) ----------------------------------------------
USB_REQ_GET_STATUS	= 0
USB_REQ_CLEAR_FEATURE	= 1
USB_REQ_SET_FEATURE	= 3
USB_REQ_SET_ADDRESS	= 5
USB_REQ_GET_DESCRIPTOR	= 6
USB_REQ_SET_CONFIG	= 9
USB_REQ_SET_INTERFACE	= 11

; --- HID (bRequest, type classe vers l'interface : bmRequestType 0x21 / 0xA1) ---
HID_REQ_GET_REPORT	= 1
HID_REQ_SET_REPORT	= 9
HID_REQ_SET_IDLE	= 10
HID_REQ_SET_PROTOCOL	= 11

; paquet SETUP dans un registre (petit-boutiste) :
;   bmRequestType | bRequest << 8 | wValue << 16 | wIndex << 32 | wLength << 48
macro usb_setup reg*, type*, req*, value*, index*, len* {
	movl	reg,(type) or ((req) shl 8) or ((value) shl 16) or ((index) shl 32) or ((len) shl 48)
}
