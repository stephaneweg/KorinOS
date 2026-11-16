---
name: armpi-installateur
description: "Clé USB d'installation du PC (15/11) : étude docs/etude/INSTALLATEUR.md, rien de fait, réponses de l'utilisateur reçues"
metadata:
  type: project
---

Le 15/11/2026, l'utilisateur a demandé d'étudier une clé USB d'installation pour le PC. Elle doit permettre de partitionner le disque (supprimer, créer, formater, points de montage) en gardant la partition EFI. Elle configure ensuite l'UEFI (Korin, et Windows s'il existe), installe les composants choisis, règle écran, Wi-Fi, clavier et région, puis crée le mot de passe root et le premier utilisateur. L'utilisateur envisage peut-être de vider tout le disque.

L'étude est écrite dans docs/etude/INSTALLATEUR.md (non commitée). Rien n'est fait. Elle propose :
- une clé « live » qui lance le même korin.efi et Setup avec des étapes en plus (`--install --target`) ;
- un mode `nvme -I` avec un plan de disque `BLK_PLAN`, vérifié par le pilote ;
- `src/lib/gpt` pour écrire la GPT ;
- la racine désignée par son GUID (`bootpart=` donné par le chargeur, `blk.part.<GUID>`), et `\korin\local.tar` superposé à l'initrd pour les règles nvme.txt propres à chaque machine ;
- l'entrée NVRAM créée par korin.efi avant ExitBootServices (`firmware = install` dans boot.cfg), sans services d'exécution UEFI dans le noyau ;
- `pkg --root` et un dépôt hors ligne sur la clé.

Réponses de l'utilisateur (15/11), reportées au §10 :
- FAT32, avec un choix du système de fichiers dans une liste pour plus tard ;
- une ESP séparée de / quand le disque est vidé ;
- sur la clé, le système de base et quelques accessoires ; en ligne, on choisit dans le dépôt Internet, sinon parmi les paquets de la clé ;
- l'élévation est permise à une liste d'utilisateurs et de groupes (/etc/elevate.ini), pas forcément admin ; il y a un mot de passe root ;
- réparer et mettre à jour : oui, plus tard ;
- cibles : le NVMe et les disques USB externes seulement (IDE, SATA, PATA peut-être plus tard).

**How to apply:** relire les réponses de l'utilisateur avant de commencer. L'ESP existante n'est jamais écrite. Voir [[armpi-multi-utilisateur]], [[armpi-pc-partition-korin]], [[armpi-x86-pc]].

**Étape 1 faite le 15/11 (QEMU seulement, rien de commité)** : le chargeur passe `bootpart` (GUID ou SSSSSSSS-NN), et le noyau le fournit comme module texte. `\korin\local.tar` passe avant l'initrd. blkserv sert la partition de démarrage en `blk.boot` (les noms `blk.part.<GUID>` sont trop longs : vfs 24 caractères). init accepte `wait A|B` (init.txt du PC : `blk.boot|blk.korin`). Détails : §9.1 de l'étude. Le QEMU de Windows n'est plus installé : les essais PC passent par WSL (`MSYS_NO_PATHCONV=1 wsl python3 -I tools/pc/qemu_nvme.py …`). L'OVMF de WSL n'a pas de shell, donc pas de `startup.nsh` : ajouter `\EFI\BOOT\BOOTX64.EFI` à l'image. Le heredoc de l'outil Bash divise par deux les barres obliques inverses : écrire les scripts Python dans un fichier.
Designs lancés en sessions séparées le 15/11 : docs/DESIGN-INSTALLATEUR.md (plein écran, simple comme Windows) et docs/DESIGN-CONNEXION.md (deux variantes, avec fenêtre ou plein écran).

**16/11 : la suite est confiée à la session du multi-utilisateur (après lui, la clé d'installation).** Étape 1 commitée (dfe8d9d8). Décision de l'utilisateur : noyau et initrd dans la partition EFI montée sur /boot ; FAT32 ou exFAT au choix pour / (étude §10).
