; Module de demarrage (initramfs) : le service init (src/svc/init/init.asm).
; INIT_C (build.ps1, -d INIT_C=1 quand build/svc/aarch64/init existe et qu'init est dans la liste
; des services C verifies) : l'init ecrit en C (tools/svc/build_c.sh, src/svc/init/init.c),
; comme celui de la carte ; sinon l'asm.
include '../inc/module.inc'
if defined INIT_C
module 'init', '../../../build/svc/aarch64/init'
else
module 'init', '../../../build/svc/init.elf'
end if
