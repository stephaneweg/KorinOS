---
name: armpi-pieges-fasmarm
description: "Pièges FASMARM et outillage ARMPI rencontrés (tailles de données, mots réservés, échappements)"
metadata:
  node_type: memory
  type: reference
  originSessionId: 5720a561-b887-4de4-aad5-6b04aff7366c
  modified: 2026-10-31T20:33:45.176Z
---

- FASMARM : `db`=8, `dh`=16, `dw`=32, `dd`=64 bits (doubles flottants en `dd`) ; pas de `dq`.
- Immédiats sans `#` ; logiques non encodables / grands → `movl` (macro) ou registre ; `cmp x,sp` interdit ; ldp/stp ±504.
- Mots réservés comme symboles : R1.., `word`, `st`, `sel`, `err`, `f0`..`f7`, `heap`, `MARGE`, `rw` ; un nom d'instruction + condition
  (`streq`, `bleq`...) est lu comme instruction conditionnelle. Immédiat de `add` : 12 bits (sinon movl). `align 4` après des données avant du code.
- Sous QEMU raspi4b, accéder à un bloc non émulé (PWM, PCIe, V3D, GENET) = erreur de bus : détecter QEMU via
  `mbox_get_fw_rev` < 0x50000000.
- QEMU branche la carte SD sur l'Arasan 0xFE300000 (vrai Pi : EMMC2 0xFE340000) ; sd_init essaie les deux.
- Éditer build.ps1 via Python : `\f`, `\a` deviennent des caractères de contrôle → utiliser chr(92) ou l'outil Edit.
- Tests : `python -I tools/qemu_drive.py script` (QMP : clics, touches, captures) ; `/sd/demarrage.txt` choisit l'app.
- Fonction feuille (sans prologue) qui fait `bl` : x30 écrasé → `ret` boucle sur elle-même. Bug réel (sfn_to_name,
  noms 8.3 en minuscules écrits par Windows) qui figeait le vrai Pi. Les images de mkdisk.py ont toujours des LFN.
- Sources en CRLF (certaines en LF) : ne pas utiliser `sed -i` ni écrire en texte Python sans garder la fin de ligne
  (srcedit.py du scratchpad, ou reconvertir d'après `git ls-files --eol`).
- ELF exécutable : `format ELF64 executable 3 at 0` fonctionne (kits liés à 0) ; FASMARM n'émet pas de
  relocations : code de kit sans adresse absolue (adr seulement, pas de `dd label`).

- Tailles en pages calculées depuis l'écran : toujours arrondir vers le haut ((n+PAGE_SIZE-1)/PAGE_SIZE). 800x600x4 = 468,75 pages : le tampon arrière tronqué écrasait les kits (bug du 28/10/2026).

QEMU ne modélise pas les caches ni la lenteur du vrai firmware (28/10/2026) : un oubli de vidage de cache
(mailbox : x0 écrasé avant dcache_flush_range) ou un délai trop court (500 ms coupait l'allumage USB) ne se
voient que sur le PocketTerm. La mailbox a maintenant un verrou (mbox_lock) et une révision firmware gardée ;
SD0:/journal.txt (bin/journal dans demarrage.txt) est l'outil de diagnostic sur le vrai Pi.
- sed -i de git bash convertit CRLF en LF et \r dans le motif ne correspond pas : modifier les fichiers CRLF (appkit.abi) en Python binaire.
- x86toa64.py n'est pas deterministe (hachage Python) : pour changer le gabarit, ne remplacer que la partie RUNTIME de fasmarm.asm.
- Sur le Pi : ne jamais remplacer kernel8.img ni un .mod par FTP sans CRC verifie AVANT reboot (le Wi-Fi corrompt) ; un .mod = l'enveloppe de build\modules (src/svc/modules), pas le programme brut ; le faire demarrer sous QEMU d'abord. Telnet 192.168.0.16, ftp SD0:/chemin ; bin/journal reecrit SD0:/journal.txt (garder la session ouverte 70 s).
- `ret x7` s'assemble comme `ret` (x30) : utiliser `br x7`. `bfi w,w,0,8` est refusé : `bfxil w,w,0,8`. Masques non encodables (0x7D, 0x8FFF, 0x81) : `mov` + `bic`/`and`. Macros à définir avant usage. Un label `local` de macro casse la portée des `.label` de la fonction.
- Conversion de .inc C vers asm : retirer les commentaires // avant d'extraire les 0x... (des 1.0f pris pour des instructions QPU ont bloque le GPU).

`qemu_drive.py` : l'op `run` est bloquante (le script attend la fin de la commande, même avec `start /b`) ; un client qui doit vivre pendant les `move` se lance à part, avant `qemu_drive`.
- Paramètre de macro nommé `b` : remplace aussi l'instruction `b` dans le corps. Labels `s10`..`s31`, `v..` : registres, à éviter (préfixes `o`, `k`). `mrs x16,nzcv` marche : drapeaux ARM7 = A64. `ldrh/strb` n'acceptent pas `lsl 2` (échelle de la taille). Un `@@` dans une macro ne casse pas les `.label` ; un `label:` global, si (run_frame/step_next). Un gros tirage de `cmp w,0x80000000` : refusé, tester le bit par `tbnz`.

Piège du 4/11/2026 : un script Python sous Windows qui fait open(p,'w') écrit en CRLF ; le dépôt a `* -text`,
donc ça part tel quel dans git (37 fichiers corrigés par c4c5c11f). Toujours open(p,'w',encoding=...,newline='\n')
ou lire/écrire en binaire ; vérifier avec `tr -cd '\r' < f | wc -c`.
