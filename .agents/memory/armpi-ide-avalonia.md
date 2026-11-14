---
name: armpi-ide-avalonia
description: "IDE .NET voulu complet (2/11/2026) : Studio refait en Avalonia, projets .csproj, csbuild, éditeur d'interface pour tout Avalonia ; Mesa lancé à part"
metadata:
  type: project
---

Décisions de l'utilisateur du 2/11/2026 : l'IDE doit être le plus complet possible (code-behind, éditeur d'interface couvrant tout Avalonia, user controls réutilisables dans un autre `.ui`) ; le format de projet est le `.csproj` de Visual Studio, et le build s'en sert ; le Studio est refait en Avalonia.

Fait, dans main depuis le 2/11 : `tools/dotnet/project` (lecteur partagé), `tools/dotnet/csbuild` (lancé par pibuild.mk si un .csproj existe ; comparé à MSBuild, `csbuild/test/parity.py`), `tools/avalonia/studio` (/apps/avstudio.app : arbre, AvaloniaEdit, aperçu .axaml, Compiler). Un projet Avalonia se compile dans l'OS, XAML compris (QEMU). Le Studio WinForms reste jusqu'à parité.

Bug trouvé : `__posix_pwrite` écrivait le trou à la position courante (corrigé dans file.c) ; relier les programmes liés à libposix après fusion.

Éditeur d'interface fait (QEMU, 2/11) : XamlText (texte de référence, `avstudio --selftest`), aperçu marqué par Tag, propriétés et événements par réflexion, palette, code-behind, user controls ; en détail dans docs/AVALONIA.md. Suite : glisser, structure à essayer, poignées, complétion Roslyn, chargement du compilateur XAML hors du thread d'interface, finition ([[armpi-ide-avalonia-finition]]).

**Why:** il veut développer dans l'OS comme dans Visual Studio, projets interchangeables.

Design revu le 2/11 sur remarque de l'utilisateur (« ça fait cheap », onglets et boutons trop gros) : DensityStyle Compact, barre plate à icônes (Button.tool), onglets 12 px, barre d'état à la couleur d'accent, accueil et récents, onglets fermables, ouvert agrandi.

Dossiers virtuels (2/11) : Link, LinkBase, Update, Folder et motifs hors du dossier dans Armpi.Project, csbuild et parity.py ; Studio : arbre virtuel, Dossier, Ranger…. Elegant : `recompute_reserve` ne descend plus une fenêtre agrandie. Dans main le 2/11 ; sur E: le soir (Studio, csbuild, Armpi.Project, test/multi seulement), sur le Pi, l'utilisateur a compilé et exécuté gpudemo depuis le Studio Avalonia (2/11).

**How to apply:** stage.sh de tools/dotnet efface kits.sh : stage puis kits. Une DLL partagée entre kits.sh et un projet SDK doit fixer sa version (Armpi.Project : 1.0.0.0). Essai rapide de csbuild dans WSL : ARMPI_DOTNET, outils x64. Les essais pibuild demandent build/gcc avec make (tools/gcc/make.sh). GPU : voie Mesa dans la session mesa-v3d. Voir [[armpi-avalonia]], [[armpi-but-dotnet]].
