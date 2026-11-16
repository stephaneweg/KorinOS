; Module de demarrage (initramfs) : AppKit, pour les services de demarrage
; (le noyau cherche un kit parmi les modules avant /lib).
; APPKIT_C (build.ps1, -d APPKIT_C=1 quand build/kits/aarch64/appkit.so existe) :
; l'AppKit ecrit en C (tools/kits/build_c.sh), comme celui de la carte ; sinon l'asm.
include '../inc/module.inc'
if defined APPKIT_C
module 'appkit.so', '../../../build/kits/aarch64/appkit.so'
else
module 'appkit.so', '../../../build/lib/appkit.so'
end if
