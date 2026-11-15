---
name: armpi-volumes-sys-home
description: "Renommer les volumes SD0: en SYS: et SD1: en HOME:, mais seulement après le portage complet de Korin sur PC ; BOOT: (partition EFI) monté à la demande"
metadata:
  node_type: memory
  type: project
  originSessionId: f37cb635-d720-4a7d-a203-55460bb5fb2c
  modified: 2026-11-12T19:45:05.579Z
---

Le 12/11/2026, l'utilisateur a décidé de donner des noms logiques aux volumes, parce que SD0: ne reflète plus un support physique (sur le PC, le système sera dans /korin de la partition KORIN du NVMe) :
- **SD0: → SYS:** (système) ;
- **SD1: → HOME:** (données de l'utilisateur) — **REVU le 15/11** : HOME: est un volume virtuel = SYS:/home/<utilisateur de l'appelant>/ (voir [[armpi-multi-utilisateur]]) ; sur le PC SD1: devient inutile, sur le Pi la partition exFAT garde SD1: ou devient DATA: ;
- RAM0:, IMG0:, USB0: restent tels quels.

**En attendant (précision du 12/11)** : SD0: se lit « System Disk 0 » (le disque système, où qu'il soit : carte SD, NVMe, clé), ce qui le rend juste sur le PC ; le renommage en SYS: et HOME: reste prévu quand même, dans un second temps.

**Quand :** « dans un second temps, après le portage complet de Korin ». Pas avant.

**Ampleur au 12/11 :** `SD0:` apparaît 3 724 fois dans 831 fichiers, `SD1:` 124 fois dans 49 fichiers.

**Méthode prévue :**
- d'abord des alias dans vfs (SD0: → SYS:, SD1: → HOME:), pour que les binaires, paquets et scripts existants marchent toujours ;
- puis le renommage par un script, avec construction et suite d'essais des commandes avant et après, et la version des paquets augmentée ;
- attention aux chemins virtuels, à libposix et getcwd, aux ports Python, Rust et .NET (/sd0), à l'index .exec, à selfhost et pibuild, **et aux compétences de l'agent (.agents/skills, et celles du port de Mistral Vibe fait dans une autre session) qui citent des dossiers et fichiers de SD0:/SD1:** (remarque de l'utilisateur, 12/11 : à corriger au moment du renommage, pas à analyser avant).

**Décidé le même jour, pour BOOT: :** il n'est monté qu'à la demande. Le programme qui doit y écrire (pkg pour les fichiers boot=, Réglages, setup, selfhost, pibuild, mkinitramfs) le monte puis le démonte aussitôt. Sur le PC, c'est la partition EFI de Windows.

Voir [[armpi-ntfs-dualboot]], [[armpi-pc-partition-korin]], [[armpi-x86-pc]].
