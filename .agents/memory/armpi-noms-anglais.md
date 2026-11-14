---
name: armpi-noms-anglais
description: Convention ARMPI — noms d'exécutables, fichiers source, fichiers de paramètres et scripts en anglais
metadata:
  type: feedback
---

Les noms d'exécutables, de fichiers de code (.asm, .inc), de fichiers de paramètres (SD0:/etc/*.txt, .ini) et de scripts sont en anglais (ex. files.asm, startup.txt, mounts.txt). Les nouvelles opérations de service (ex. op « launch » d'init2, pas « lancer ») et nouveaux identifiants aussi. Les commentaires, messages et docs restent en français.

**Why:** demandé par l'utilisateur le 28/10/2026 (fichiers.asm → files.asm, demarrage.txt → startup.txt), étendu aux opérations le 29/10/2026.
**How to apply:** tout nouveau fichier reçoit un nom anglais ; signaler les anciens noms français croisés au passage. Voir [[armpi-projet]].
