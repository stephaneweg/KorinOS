/* usbkeyboard - pilote des claviers USB (classe HID), lancé par hwmanager
 * (drivers.txt) avec « <périphérique> <interface> ». Le corps est commun aux
 * pilotes HID : src/svc/usbhid (repris de svc/inc/hiddrv.inc, en C pour le Pi et
 * le PC ; l'asm usbkeyboard.asm reste à côté). */
#include "../usbhid/hid.h"

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return hid_driver_run("usbkeyboard", 0);
}
