---
name: armpi-idee-godot
description: "Idée du 13/11 : porter Godot 4 sur Korin (runtime + éditeur) pour faire des jeux qui CIBLENT Korin, pas pour faire tourner les projets existants de l'utilisateur"
metadata:
  type: project
---

Le 13/11/2026, l'utilisateur a demandé ce qu'il faudrait pour Godot sur Korin, puis précisé : **si on porte Godot, ce sera pour faire des jeux qui ciblent Korin** (pas pour faire tourner ses projets Godot existants de C:\Projects).

Ce qu'il faudrait (analyse du 13/11) : une plateforme `korin` dans Godot (DisplayServer sur UIKit/Elegant, contexte GL par GlKit/Mesa, AudioDriver sur AudioKit/sound, OS_Korin repris d'OS_Unix avec les chemins à volumes, manettes par EmuKit/HIDManager) ; renderer **Compatibility** (GL ES 3.0 : v3d du Pi au minimum, iris du PC largement) ; pas de Vulkan sur Korin (v3dv/anv de Mesa : chantier à part) ; éditeur = même code avec les outils, en Compatibility ; C# seulement après .NET. Les jeux faits pour Korin se conçoivent donc d'emblée en Compatibility et GDScript (C# plus tard).

**Why:** Godot comme outil de création de jeux pour Korin.

**How to apply:** placé à la fin du plan le 13/11 : après les idées pour Vibe, le générateur de Koton et le GPU NVIDIA ; seul le noyau du Pi en C vient après. Dépend de GlKit sur le PC (en cours) et sur le Pi (fait). Voir [[armpi-x86-pc]], [[armpi-mesa]], [[armpi-sdl]].
