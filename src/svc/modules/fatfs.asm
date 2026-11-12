; Module de demarrage (initramfs) : le service FatFS (src/svc/fatfs/fatfs.asm : FAT32 et exFAT).
; FILES_C (build.ps1, -d FILES_C=1 quand build/svc/aarch64/fatfs existe) : le fatfs ecrit en C
; (tools/svc/build_c.sh), comme celui de la carte ; sinon l'asm.
include '../inc/module.inc'
if defined FILES_C
module 'fatfs', '../../../build/svc/aarch64/fatfs'
else
module 'fatfs', '../../../build/svc/fatfs.elf'
end if
