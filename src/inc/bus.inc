; bus.inc - protocole du service "bus" (busd, docs/BUS.md) ; constantes
; publiques : kits/buskit/buskit.inc.
;
; Message (512 octets, SVC_CALL) : +0 op / resultat, +8 a, +16 b, +24 c,
; +40 longueur des donnees, +44 pid de l'appelant (noyau), +48 donnees.

BUS_SVC_NAME	equ 'bus'

; sujets du systeme
BUS_TOPIC_THEME	equ 'korin.theme'	; (gardee) "<theme>, <accent>" : le panneau de
					; configuration le publie, UIKit s'y abonne
BUS_TOPIC_NET	equ 'korin.net.state'	; (gardee) "up <genre> <adresse> <interface>" ou
					; "down" (genre : ethernet, wifi, usb) : netd
BUS_TOPIC_WIFI	equ 'korin.net.wifi'	; (gardee) "<etat> <barres> <ssid>" (etat : absent,
					; starting, ready, joining, connected ; barres 0..4) :
					; le pilote wifi
BUS_TOPIC_AUDIO	equ 'korin.audio.state'	; (gardee) "<generation> <volume> <muet> <sortie>" de
					; la sortie principale : sound, a chaque changement d'etat
BUS_TOPIC_CLOCK	equ 'korin.clock.offset'	; (gardee) decalage de l'heure locale, comme
					; SD0:/etc/clock.cfg : netd (NTP, fuseau), l'Horloge
BUS_TOPIC_USB	equ 'korin.usb.device.'	; + numero (gardee) "vvvv:pppp cc/ss/pp..." :
					; usbd, au branchement ; retiree au retrait
BUS_TOPIC_VOLUME equ 'korin.storage.volume.' ; + volume (gardee) "<source> <service>" :
					; vfs, au montage ; retiree au demontage

BUS_OP_PUBLISH	= 1		; a = BUS_RETAIN | BUS_DELETE, b = octets de la valeur ;
				; donnees : nom, 0, valeur -> abonnes prevenus
BUS_OP_SUBSCRIBE = 2		; a = BUS_CURRENT ; donnees : motif (nom exact, ou
				; prefixe suivi de "*") -> numero d'abonnement (> 0)
BUS_OP_UNSUBSCRIBE = 3		; a = numero (0 : tous ceux de l'appelant) -> 0
BUS_OP_GET	= 4		; a = sujet (0 : par le nom des donnees) -> octets ;
				; reponse : +8 sujet, +16 drapeaux, +48 valeur
BUS_OP_LIST	= 5		; a = sujet de depart (0 : le premier) -> sujet suivant
				; (0 : fin) ; reponse : +16 octets, +24 drapeaux, +48 nom

BUS_DATA	= 48
BUS_DATA_MAX	= 464
