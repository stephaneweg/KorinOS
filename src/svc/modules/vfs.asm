; Module de demarrage (initramfs) : le service vfs (src/svc/vfs/vfs.asm).
; FILES_C (build.ps1, -d FILES_C=1 quand build/svc/aarch64/vfs existe) : le vfs ecrit en C
; (tools/svc/build_c.sh), comme celui de la carte ; sinon l'asm.
include '../inc/module.inc'
if defined FILES_C
module 'vfs', '../../../build/svc/aarch64/vfs'
else
module 'vfs', '../../../build/svc/vfs.elf'
end if
