---
name: armpi-process-sans-plafond
description: "Plus de MAX_PROCS (04/11) : table du noyau qui grandit, pid = génération<<16 | emplacement+1, SYS_PROC_INFO 0x1E, SYS_PROCLIST_FROM 0x1F ; branche claude/procs-unlimited"
metadata:
  node_type: memory
  type: project
  originSessionId: 7400838a-2aca-45ee-b2e7-008de25ae810
  modified: 2026-11-04T21:05:29.271Z
---

Demandé le 04/11/2026 : « il ne faudrait plus limiter » le nombre de process (était MAX_PROCS = 64, pids 1..64 réutilisés).
Dans origin/main (5045ddf9) et sur le Pi (139 process vivants avec proctest 100 ; originaux dans scratchpad/pi_backup_procs ; init2 du Pi gardé, sans le 1,8 V). Fusionné avec SVC_FORWARD dans main (2e610f7b, fusion de l'autre session ; la mienne identique, essayée sous QEMU). 37 fichiers écrits en CRLF par mes scripts Python : corrigé (c4c5c11f) ; écrire avec open(...,'w',newline='
') ou en binaire.
Noyau : proc_tab/proc_cap (double à la demande, ancienne table jamais rendue), proc_lookup/proc_slot ; vfs : fiches par pid
(src/svc/vfs/pids.inc) ; inc/procfind.inc (proc_find nom) ; bin/proctest. QEMU : 336 process vivants, pids 65574, 131110.

**Why:** 38 process au repos sur le Pi : 64 était presque atteint ; pids réutilisés vite.

**How to apply:** un pid n'est plus un indice : PROC_INFO pour un process, PROCLIST_FROM par morceaux. Restent : la libc
POSIX (src/lib/posix proc.c, signal.c, env.c, misc.c : SYS_PROCLIST 64 entrées, à rebuild via WSL), l'ancien dock et
kittest. Envoyer sur le Pi : kernel8.img (construit avec -d ECRAN_W=800 -d ECRAN_H=600), vfs.mod, sys/svc/*, etc. Voir [[armpi-busd]].
