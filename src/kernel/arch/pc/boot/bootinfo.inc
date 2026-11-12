; Bloc de démarrage passé par BOOTX64.EFI au noyau x86-64 (rdi = adresse physique).
; Même disposition que src/kernel/arch/pc/bootinfo.h : changer les deux ensemble.

BB_MAGIC_VALUE  = 0x31544F424E49524B    ; "KRINBOT1"

BB_MAGIC        = 0     ; u64
BB_FB_BASE      = 8     ; u64 adresse physique du framebuffer GOP
BB_FB_SIZE      = 16    ; u64 octets
BB_FB_WIDTH     = 24    ; u32 pixels
BB_FB_HEIGHT    = 28    ; u32 pixels
BB_FB_PITCH     = 32    ; u32 pixels par ligne
BB_FB_FORMAT    = 36    ; u32 0 = RGB, 1 = BGR (format GOP)
BB_MMAP         = 40    ; u64 adresse physique de la carte mémoire UEFI
BB_MMAP_SIZE    = 48    ; u64 octets
BB_MMAP_DESC    = 56    ; u64 taille d'un descripteur
BB_RSDP         = 64    ; u64 adresse physique de la RSDP (ACPI), 0 sinon
BB_KERNEL_PHYS  = 72    ; u64 début physique du bloc du noyau
BB_KERNEL_SIZE  = 80    ; u64 taille du bloc (image + tables de pages du chargeur)
BB_KERNEL_VIRT  = 88    ; u64 adresse virtuelle du début du bloc
BB_INITRD       = 96    ; u64 adresse physique de initrd.tar, 0 sinon
BB_INITRD_SIZE  = 104   ; u64 octets
BB_CMDLINE      = 112   ; char[128] options du noyau (boot.cfg, « cmdline = »), terminées par 0
BB_CMDLINE_MAX  = 128
BB_SIZE         = 240
