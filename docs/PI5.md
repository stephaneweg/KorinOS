# ARMPI sur Raspberry Pi 5 — état du portage

Le Raspberry Pi 5 (BCM2712 + puce RP1) se construit avec la même source que le Pi 4 :

```powershell
fasmarm kernel.asm kernel_2712.img -d ARCH=pi5      # aussi : .\build.ps1 (build\kernel_2712.img)
```

`build.ps1` assemble toujours les deux noyaux, et `.\build.ps1 sd E:` copie les deux sur la
carte : `config.txt` a une section `[pi4]` (`kernel8.img`) et une section `[pi5]`
(`kernel_2712.img`), une même carte démarre donc les deux modèles. Sous ARMPI, `selfhost`
réassemble aussi le noyau du Pi 5, identique octet pour octet à celui de Windows.

**Rien n'a encore tourné sur un Pi 5** : QEMU n'émule pas ce modèle. Le premier essai demande
un Pi 5 et, idéalement, un câble série branché sur l'UART de débogage (connecteur JST 3 broches,
3,3 V) : c'est là qu'apparaît la console du noyau.

## Ce qui est écrit (d'après la documentation BCM2712 / RP1, non testé)

| Élément | Fichier | Notes |
|---|---|---|
| Adresses | `arch/pi5/board.inc` | périphériques à `0x10_7C00_0000`, GIC à `0x10_7FFF_9000`, mailbox à `+0x13880`, SD à `0x10_00FF_F000`, RP1 à `0x1F_0000_0000` |
| Numéro de cœur | `inc/macros.inc` (`cpu_index`) | Cortex-A76 : bit MT de MPIDR, numéro dans Aff1 (commun aux deux cartes) |
| Démarrage des cœurs 1-3 | `arch/pi5/board.asm` (`smp_wake`) | PSCI `CPU_ON` (`smc`, firmware BL31) ; le reste est commun (`arch/common/smp.asm`) |
| MMU | `arch/pi5/mmu.asm` | RAM du 1er Go + blocs de 1 Go pour les périphériques ; adresses physiques sur 40 bits |
| Console | `arch/pi5/uart.asm` + `arch/common/pl011.asm` | UART de débogage, laissée telle que le firmware l'a programmée |
| Mailbox, framebuffer | `arch/common/mailbox.asm`, `fb.asm` | même protocole que le Pi 4 |
| Carte SD | `arch/common/sdhci.asm` | contrôleur SDHCI, horloge de base fixe de 200 MHz |
| GPIO du connecteur | `arch/pi5/gpio.asm` | RP1 : fonction SYS_RIO, plots, registres RIO ; sert à l'I2C logiciel du tactile |
| Tout le code générique | `src/*.asm` | systèmes de fichiers, réseau (pile), interface, applications, FASMARM |

## Préparé sans Pi 5 (4/11/2026, non testé sur matériel)

Le Pi 4 est inchangé : son noyau est identique octet pour octet, et il démarre sous QEMU avec ces services.

| Élément | Fichier | Notes |
|---|---|---|
| Carte SD | `svc/sd/sd.asm` | troisième contrôleur essayé, `0x10_00FF_F000` ; horloge du firmware (id 1), sinon 200 MHz. `dev_map` refuse les adresses de l'autre carte : chaque contrôleur s'essaie sans connaître la carte |
| USB | `svc/usbd/xhci.inc` | premier xHCI du RP1 (`0x1F_0020_0000`), déjà relié par le firmware : ni PCIe ni VL805 ; adresses DMA vues du RP1 = physique + `0x10_0000_0000` (zone, transferts bulk). Le second xHCI (les deux autres prises) n'est pas encore pris |
| Services du Pi 4 seulement | `svc/inc/board.inc` | `board_pi5` (révision de carte, processeur 4) : `v3d`, `pwmaudio` (pas de jack), `gpio` (connecteur sur le RP1) sortent avec le code 0, sans relance ; GpuKit et Mesa passent sur le CPU, GpioKit sur son simulateur |
| Alpha | `boot/config.txt`, `arch/common/fb.asm` | le Pi 5 lit l'octet alpha et nos pixels ont 0 (écran noir pour Onyx sur un Pi 500) : `framebuffer_ignore_alpha=1`, et le noyau du Pi 5 demande « alpha ignoré » (tag `0x00048007`) |
| Démarrage | `boot/config.txt`, `firmware/` | la ligne `initramfs` passe dans `[all]` (une seule : `mkinitramfs` la refait) ; `bcm2712*.dtb` et `overlays/bcm2712d0.dtbo` (repris d'Onyx) copiés par `build.ps1 sd`, `mincard.py`, `publish.sh` (paquet `pi-firmware`) |
| Mesa | `tools/mesa` | il contient déjà le code du V3D 7.1. `gltest --dry --v71` le fait se croire sur le 7.1 (identifiants du drm-shim de Mesa) : sous QEMU, « V3D 7.1.7.0 », shaders compilés, jobs construits, PASS |

## Comparé à Circle (4/11/2026) et corrigé

Une relecture du démarrage, de la carte SD, du framebuffer et du xHCI contre Circle, vérifiée dans
le DTB du Pi 5 (`firmware/bcm2712-rpi-5-b.dtb`), a trouvé et corrigé :

| Défaut | Correction |
|---|---|
| RNG200 à l'adresse du Pi 4 (`+0x104000`) : figeage probable après le montage de SD0: | `RNG200_BASE` dans `board.inc` : `+0x1208000` sur le Pi 5 (`rng@7d208000`) |
| Passage EL2 → EL1 incomplet : sans armstub, FP/SIMD piégés en EL2, MPIDR virtuel indéfini | `drop_to_el1` règle aussi `CPTR_EL2`, `HSTR_EL2`, `VPIDR_EL2`, `VMPIDR_EL2` (`EL2_FULL_SETUP`) |
| Pile de boot sous 0x80000 : c'est la zone de BL31 (`atf@0`) | `boot_stack` de 128 Ko dans le BSS (`BOOT_STACK_BSS`) |
| Lien PCIe vers le RP1 remis à zéro par le firmware | `pciex4_reset=0` dans `[pi5]` |
| Interruption de la carte SD : 158 (Pi 4) ; le noyau refusait toute ligne au-delà de 256 | `IRQ_SD` = 305 (SPI 273), `IRQ_MAX` = 512 sur le Pi 5 ; le service `sd` prend la ligne du contrôleur trouvé |
| Pixels à alpha 0 si le firmware ne l'ignore pas | console du noyau (`PIXEL_ALPHA`) et `flush_rect` d'Elegant (`fb_alpha`, hors Pi 4) en pixels opaques |

Le noyau du Pi 4 reste identique octet pour octet : ce qui diffère est dans `board.inc`.
Encore à faire, d'après la même relecture : le second xHCI ; le comptage des événements isochrones du
xHCI (codes 14, 15, 23), pour le son USB ; la fenêtre DMA du RP1 lue dans le DTB plutôt que fixée ;
vérifier la profondeur rendue par la mailbox (32) ; `CPU_ON` : journaliser son code de retour.

## Ce qui reste à porter

| Pilote | Travail |
|---|---|
| GPU | service `v3d` : mise en route du V3D 7.1 (domaine `PM_GRAFX_2712`, SMS, interruption 249 ; carte : `kernel/sys/v3d.cpp` d'Onyx sous `RASPPI >= 5`) ; GpuKit : paquets et shaders 7.1 (Onyx : `v3d_shaders71.qasm`, `genpackets.py`). Mesa n'a besoin que du service |
| USB | le second xHCI du RP1 (`0x1F_0030_0000`) : le pilote n'a qu'une instance |
| Ethernet | RP1 Cadence MACB/GEM : nouveau pilote (la pile TCP/IP est prête) |
| WiFi | même puce CYW43455 et même service ; seul l'hôte SDIO du BCM2712 est à écrire |
| Son | pas de jack ; HDMI du BCM2712 ; I2S du RP1 ; le son USB marche dès que l'USB marche |
| GPIO, I2C (tactile) | service `gpio` et `inc/i2c.inc` sur le RP1 (le noyau a déjà `arch/pi5/gpio.asm`) |
| Ventilateur, RTC, bouton | comme Onyx (§6, §10 de son plan) |
| Mémoire au-delà de 1 Go | comme sur le Pi 4 |

## Points à vérifier au premier démarrage

1. La console de débogage affiche-t-elle la bannière ? (sinon : adresse de l'UART, ou
   `kernel_address`).
2. `Coeurs en ligne : 4` : PSCI `CPU_ON` et `cpu_index` sur l'A76.
3. Framebuffer : adresse rendue par la mailbox (masque `0x3FFFFFFF`, mémoire du VideoCore).
4. Carte SD : « sd : bus 4 bits » dans le journal (horloge de 200 MHz, alimentation 3,3 V).
5. Le bureau s'affiche, pas en noir (alpha).
6. « usbd : xhci : RP1 (Pi 5) », puis le clavier sur l'une des deux prises du premier xHCI.

Référence : le plan de portage d'Onyx (`$ONYX/docs\PI5-PORT.md`), validé en partie
sur un vrai Pi 5, a servi de carte, avec Circle.
