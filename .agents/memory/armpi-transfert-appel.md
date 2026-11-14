---
name: armpi-transfert-appel
description: "Transfert d'appel SVC_FORWARD (0x39) : implémenté le 04/11/2026 sur claude/svc-forward, dans les paquets armpi depuis 2026.11.14, utilisé sur le Pi ; vfs transmet 6 ops au service de fichiers"
metadata:
  node_type: memory
  type: project
  originSessionId: 6431c0ee-2470-4101-a837-5d02f76197ba
  modified: 2026-11-04T19:29:51.628Z
---

Étudié le 01/11 (étude seule), puis demandé le 04/11 : « on va commencer par le transfert d'appel ». L'utilisateur a aussi proposé un thread par requête dans vfs ; j'ai conseillé un groupe fixe de threads (risque : SR_MAX = 128 pour tout le système), à faire plus tard, combinable avec le transfert.

Fait sur `claude/svc-forward`, fusionné et poussé dans main le 04/11 (2e610f7b), avec les pids à génération de l'autre session (fsowner : SYS_PROC_INFO) : `sys_svc_forward` dans svc.asm ; vfs transmet V_OUVRIR, V_STAT, V_MKDIR, V_UNLINK, V_UTIME, V_RENAME (`transmettre`, `answered` dans path.inc) ; V_EXEC et V_CHDIR restent synchrones. fatfs et ramfs : `open_extras_take` / `open_extras_put` (fsowner.inc) mettent le canal en a et ajoutent ATTR_SYS/ATTR_LIB demandés par vfs dans c ; `proprietaire` prend MSG_PID quand d = 0, sans SYS_PROCLIST.

**Why:** le vrai motif est que vfs ne reste plus bloqué par un volume lent (tubes et autres volumes servis pendant ce temps) ; le gain de latence (~3 µs) est négligeable.

**How to apply:** essais QEMU sans préchargement : vfstest 0 échec, fstest2 0 échec, posixtest 254/0. Avec SD0:/etc/preload.txt actif, init2 précharge 4 images (jet.app...) pendant 75 à 90 s, en même temps que posixtest, et fausse ses deux tests qui comptent les pages libres (fichier mappé, fils tué avec 8 Mo) : vider preload.txt de l'image pour mesurer. Avec le transfert, l'échec ipc venait 7 fois sur 7 de ce chevauchement, pas d'une fuite. Dans tous les paquets armpi depuis 2026.11.14 (vérifié le 8/11) ; l'utilisateur dit l'utiliser sur le Pi sans problème depuis. fstest2 et proctest ensemble : fstest2 échoue (RamFS n'a que 64 fichiers ouverts, OUV_MAX), les lancer séparément. Voir [[armpi-chantier-perf]].
