---
name: armpi-viewport3d
description: "System.3D : Viewport3D sur GpuKit, dans main, GPU pas encore essayé sur le Pi"
metadata:
  node_type: memory
  type: project
  originSessionId: 19abba7f-296d-48fe-b7f8-85358ff59d4a
  modified: 2026-10-31T22:40:00.668Z
---

Fusionné dans `main` le 31/10/2026 (d848d02), à la demande de l'utilisateur, branche `claude/dotnet-viewport3d-b39bc5`. `tools/dotnet/3d` → `System.3D.dll`, espace de noms `System.Windows.Media.Media3D` (un espace de noms C# ne peut pas commencer par un chiffre). Doc et liste d'essais pour le Pi : docs/VIEWPORT3D.md. Démo `/apps/view3d.app/main [--cubes] [--software] [--dry] [--bench N] [--out f]`.

État : marche sous QEMU par le repli C# ; `gk_render` à blanc (`GK_DBG_DRY`) accepte les scènes ; rien de mesuré sur le vrai GPU.

`gk_phys` et `gk_free` décalaient de 24 bits alors que `BUF_SLOT` vaut 64 Mo depuis 30931dc : corrigé, porté seul dans `main` (3339cd0) le 31/10/2026 à la demande de l'utilisateur.

**Why:** le but est un système à la WPF sur l'OS ([[armpi-but-dotnet]]).

**How to apply:** ne pas rendre directement dans la surface sans échange de tampons : Elegant compose à tout moment et montrerait des images partielles (vu sous QEMU). Sous WSL, `mmd` sur un dossier existant attend une réponse et bloque un script.
