; buskit.inc - constantes publiques de BusKit (buskit.abi, docs/BUS.md)

; l'evenement du bus : dans la file d'evenements du programme (ui_event, bus_wait)
WEV_BUS		= 13		; un sujet suivi a change
BEV_SUB		= 4		; (32 bits) l'abonnement (bus_subscribe)
BEV_TOPIC	= 8		; (32 bits) le sujet (bus_get_id, bus_name)
BEV_LEN		= 12		; (32 bits) octets de la valeur ; BUS_DELETED : sujet retire
BEV_DATA	= 16		; les BUS_INLINE premiers octets de la valeur (zeros apres)
BUS_INLINE	= 12
BUS_DELETED	= 0xFFFFFFFF

BUS_NAME_MAX	= 64		; nom ou motif, 0 compris
BUS_VALUE_MAX	= 256

; bus_publish
BUS_RETAIN	= 1		; valeur gardee : rendue aux abonnes BUS_CURRENT a leur arrivee
BUS_DELETE	= 2		; retire le sujet (ses abonnes recoivent BUS_DELETED)
; bus_subscribe
BUS_CURRENT	= 1		; envoie tout de suite les valeurs gardees qui correspondent

; codes d'erreur
BUS_EPERM	= -1		; abonnement d'un autre programme
BUS_ENOENT	= -2		; sujet ou abonnement inconnu
BUS_ENOMEM	= -12		; plus de place (sujets, abonnements)
BUS_ENODEV	= -19		; busd absent
BUS_EINVAL	= -22		; nom, motif ou valeur faux
