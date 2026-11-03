; usbgamepad - pilote des manettes USB HID, lance par hwmanager (drivers.txt) :
; le corps est celui des pilotes HID (svc/inc/hiddrv.inc), avec la lecture des
; manettes (svc/inc/hidpad.inc). Une interface HID sans sous-classe qui se
; revele etre un clavier ou une souris est traitee comme par usbkeyboard.
PILOTE_NOM equ 'usbgamepad'
PILOTE_PAD = 1
include '../inc/hiddrv.inc'
