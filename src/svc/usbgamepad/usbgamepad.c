/* usbgamepad - pilote des manettes USB HID, lancé par hwmanager (drivers.txt),
 * avec la lecture des manettes (usbhid/hidpad.c) ; une interface HID sans
 * sous-classe qui se révèle être un clavier ou une souris (la tablette de QEMU)
 * est traitée comme par usbkeyboard. Lancé avec « <périphérique> <interface> ». Le
 * corps est commun aux
 * pilotes HID : src/svc/usbhid (repris de svc/inc/hiddrv.inc, en C pour le Pi et
 * le PC ; l'asm usbgamepad.asm reste à côté). */
#include "../usbhid/hid.h"

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return hid_driver_run("usbgamepad", 1);
}
