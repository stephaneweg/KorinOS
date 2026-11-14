---
name: armpi-winforms-portable
description: "WinForms portable (1/11/2026) : la même dll tourne sous Windows et ARMPI si nos assemblys prennent l'identité de Windows ; dans main"
metadata:
  node_type: memory
  type: project
  originSessionId: 7c434a54-387c-41fc-a3fb-26933626e1e0
  modified: 2026-11-01T21:45:46.497Z
---

Étude et preuve du 1/11/2026, branche `claude/winforms-portable-da76f8`, rapport `docs/WINFORMS-PORTABLE.md`, **fusionnée dans main le 1/11/2026 (c586c06)**. `wfhello.dll`, compilée une fois en `net10.0-windows`, tourne sous Windows et sous QEMU, et les sources compilées contre nos DLL tournent aussi sur le vrai WinForms.

Recommandation : nos trois assemblys sous l'identité de Windows (`System.Windows.Forms`, `.Primitives`, `System.Drawing.Common`, version 10.0.0.0, `-publicsign`), pas de façade ni de sélecteur ; extensions ARMPI à sortir dans un assembly à part.

**Why:** un appel compilé porte le type déclarant et la signature exacte ; le nom seul ne suffit pas. CoreCLR exige une version au moins égale, mais ne contrôle pas le jeton de clé.

État au 1/11/2026 : main intégré à la branche, build complet et `pibuild` essayés sous QEMU . `make` (tools/gcc/make.sh) manquait dans build/ du dépôt principal : `pibuild` s'y arrête tant qu'il n'est pas construit.

**How to apply:** après cette fusion, relancer `stage.sh`, `kits.sh` et `build.ps1` dans le dépôt principal ; `build/` n'y est pas à jour. Toute évolution de `tools/dotnet/forms` vérifiée par `tools/dotnet/portable/apidiff` (`surface`, `check`) : pas de membre public absent de Windows sur un type partagé, sinon `TypeLoadException` à Windows. Voir [[armpi-but-dotnet]].
