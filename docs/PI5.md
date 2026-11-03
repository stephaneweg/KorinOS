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

## Ce qui reste à porter (`arch/pi5/stubs.asm` : souches qui annoncent l'absence)

| Pilote | Travail |
|---|---|
| USB (clavier, souris, clés) | lien PCIe vers le RP1, puis ses deux contrôleurs xHCI : le pilote xHCI du Pi 4 est réutilisable, sans la partie VL805. **Priorité** : sans lui, pas de clavier |
| Ethernet | RP1 Cadence MACB/GEM : nouveau pilote (la pile TCP/IP est prête) |
| WiFi | même puce CYW43455 et même `wifi.asm` ; seul l'hôte SDIO du BCM2712 est à écrire |
| Son | PWM et I2S du RP1, HDMI du BCM2712 ; le son USB viendra avec le xHCI |
| GPU | V3D 7.1 (VideoCore VII) : encodage différent du V3D 4.2, projet à part |
| Mémoire au-delà de 1 Go | comme sur le Pi 4 |

## Points à vérifier au premier démarrage

1. La console de débogage affiche-t-elle la bannière ? (sinon : adresse de l'UART, ou
   `kernel_address`).
2. `Coeurs en ligne : 4` : PSCI `CPU_ON` et `cpu_index` sur l'A76.
3. Framebuffer : adresse rendue par la mailbox (masque `0x3FFFFFFF`, mémoire du VideoCore).
4. Carte SD : horloge de 200 MHz, alimentation 3,3 V.
5. Le bureau s'affiche (sans clavier ni souris tant que l'USB n'est pas porté).

Référence : le plan de portage d'Onyx (`$ONYX/docs\PI5-PORT.md`), validé en partie
sur un vrai Pi 5, a servi de carte, avec Circle.
