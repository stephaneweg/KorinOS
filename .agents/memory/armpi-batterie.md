---
name: armpi-batterie
description: Batterie du PC (17/11) : acpid sur ACPICA, alertes, extinction à 5 %, profil d'énergie, icône de la barre ; QEMU seulement
metadata:
  type: project
---

Demande du 17/11 : niveau (% et temps restant), alerte en fin de batterie, passage en économie.
Fait sur claude/x86-64-pc (de554ad7) : service `acpid` = ACPICA 20260930 (tools/acpica/build.sh,
cache ~/.cache/korin-acpica, pas dans le dépôt), EC du ZBook \_SB.PC00.LPCB.EC0 (0x62/0x66, `_REG` pose
ECRG avant ECRI), mode souple d'ACPICA nécessaire (TM03 lit une locale non initialisée). Noyau : IO_PORT
16/32 bits, DEV_MAP ACPI/NVS en cache, ACPI_TABLE "RPTR", POWER_PROFILE (EPP/turbo). Barre du haut, commande
`battery`, /etc/power.ini, sujet korin.power.battery. Étude : docs/etude/BATTERIE-PC.md.

Essais : QEMU avec fausse batterie (tools/acpica/fakebat.asl, `--qemu=-acpitable`) ; tables du ZBook dans
acpiexec (copiées du registre Windows HKLM\HARDWARE\ACPI, hors dépôt). **Pas encore sur le ZBook.**

Pièges d'essai sur ce PC : pas de QEMU Windows ; QEMU de WSL avec `. ~/korin-cloud/env.sh` (edk2 avec shell
UEFI) ; l'image de mkkorindisk.py se fait par le Python de Windows (le fasm de WSL échoue) ; /tmp de WSL
vidé au redémarrage de WSL.

SCI fait le 17/11 au soir : mode ACPI, GPE, GPE de l'EC (QR_EC, _Qxx), Notify, couvercle (korin.power.lid), bouton d'alimentation (korin.power.button -> panel -> /sys/shutdown) ; QEMU : bouton OK ; repli events = 0 dans power.ini.
Générique (18/11, demande de l'utilisateur : rien de propre au ZBook, chaque fonction allumée parce que le
matériel est découvert) : GET d'acpid dit profiles, lids, power_button, ec, events ; mAh sans tension -> 11,1 V
nominale ; volet Énergie de Réglages (applets/power.cpp) et « Réglages d'énergie… » dans la barre (profils
cachés sans HWP/EIST) ; essayés sous QEMU avec et sans batterie.
Reste : S3 (l'écran GOP ne revient pas au réveil sans pilote d'affichage ; tous les pilotes à relancer ; le
DSDT du ZBook a _S3), thermique, luminosité.
Voir [[armpi-x86-pc]].
