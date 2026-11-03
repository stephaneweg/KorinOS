; Module de demarrage (initramfs) : AppKit, pour les services de demarrage
; (le noyau cherche un kit parmi les modules avant SD0:/lib).
include '../inc/module.inc'
module 'appkit.so', '../../../build/lib/appkit.so'
