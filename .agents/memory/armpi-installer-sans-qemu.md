---
name: armpi-installer-sans-qemu
description: "13/11 : pour les essais sur le ZBook, l'utilisateur veut que D: soit mis à jour directement, sans essai QEMU préalable ; et toujours vérifier que la construction a réussi avant d'annoncer une installation"
metadata:
  type: feedback
---

Le 13/11/2026 au soir, l'utilisateur a dit : « ne teste pas dans qemu, tu mets à jour directement D: et je redémarre ».

**Why:** il attend pour essayer sur le vrai ZBook ; le démarrage QEMU de contrôle retardait chaque installation. Le même jour, une construction en arrière-plan a échoué (code 4, verrou build/pc.lock laissé) et j'avais annoncé « j'installe dès que c'est fini » sans jamais installer : il a testé l'ancienne version (touchpad, Wi-Fi, journal) pour rien.

**How to apply:** pour une mise à jour de D: demandée par l'utilisateur : construire (build.sh, sysroot.sh, apps.sh si besoin, sysroot_apps.sh, sysroot_gcc.sh), vérifier les codes de retour, `korin.ps1 install`, puis `show` (0 absent, date de kernel.elf) — pas de démarrage QEMU. Ne jamais dire « installé » ou « j'installe dès que… » sans avoir vérifié la fin de la construction ; une commande en arrière-plan qui échoue doit être signalée tout de suite. Un verrou build/pc.lock orphelin (aucun agent en cours) peut être retiré. Les agents gardent leurs essais QEMU. Voir [[armpi-x86-pc]], [[armpi-pc-partition-korin]].
