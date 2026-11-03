; Module de demarrage (initramfs) : le service FatFS (src/svc/fatfs/fatfs.asm : FAT32 et exFAT).
include '../inc/module.inc'
module 'fatfs', '../../../build/svc/fatfs.elf'
