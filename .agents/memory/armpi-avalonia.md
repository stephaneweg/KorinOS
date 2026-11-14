---
name: armpi-avalonia
description: "Avalonia 12.1.4 sur ARMPI (2/11/2026) : hôte dotnet-skia, Avalonia.Desktop de même identité, même dll que Windows ; dans main"
metadata:
  node_type: memory
  type: project
  originSessionId: 29f45bc8-96a9-4cfb-8fe6-183ba3ec922e
  modified: 2026-11-01T23:58:37.981Z
---

Rapport : docs/AVALONIA.md, scripts tools/avalonia, travail WSL ~/armpi-avalonia. Essayé sous QEMU ; carte E: mise à jour le 2/11 avec main (64aba06), noyau et sys/svc/v3d de Mesa gardés ; avdemo sur le Pi : 5 s, animation fluide (version sans tampon privé). Prouvé : même SHA-256 que sous Windows, souris et clavier réels, Givre, Onyx Noir, presse-papiers, redimensionnement ; ~15 images/s, ~110 Mo.

Choix à garder : SkiaSharp 3.119.4 sur Skia m119 de mono/skia (pas de greffe sur m154) ; second hôte /dotnet/dotnet-skia, +4,6 Mo ; AppKit le choisit si `<dll>.skia` existe ; notre Avalonia.Desktop.dll prend l'identité d'origine (InternalsVisibleTo), compilée contre lib/, jamais ref/.

Peindre directement dans la surface montre des zones noires : garder le tampon privé d'ArmpiWindow, recopié d'un bloc. `ExtendClientAreaToDecorationsHint` retire la bande de 32 px (barre d'outils sur la ligne des boutons, 120 px libres à droite). Mesa est dans main depuis le 2/11 (c34207e) ; mais la session Mesa pose sur la carte, par FTP, un Elegant et un Jet plus récents que main : avant `build.ps1 sd E:`, comparer la carte par sommes, et recopier seulement ce qui manque si elle a d'autres versions.

GPU : ArmpiGl.cs dans notre Avalonia.Desktop, voir [[armpi-mesa]] ; ligne de journal « avalonia : rendu sur ... » ; gen-table lit les noms sans zero devant (tas de chaines partage). Un .dll compile dans l'OS par un hote a l'ancienne libposix perd ses 16 premiers octets (sh: erreur 22).

**How to apply:** reste Studio sur le Pi, ReadyToRun. Sous Git Bash, `MSYS_NO_PATHCONV=1` pour desk.py. Passer `AVALONIA_TELEMETRY_OPTOUT=1` à dotnet build. Voir [[armpi-but-dotnet]], [[armpi-winforms-portable]].

Worktree et branche claude/avalonia-port-107f38 retirés le 2/11 ; sorties (build/avalonia/sdroot, build/dotnet/sdroot) recopiées dans le build/ du dépôt principal ; travail WSL ~/armpi-avalonia gardé.
