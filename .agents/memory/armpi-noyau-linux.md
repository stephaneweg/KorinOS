---
name: armpi-noyau-linux
description: "20/11 : Korin sur un noyau Linux (Elegant sur DRM/KMS, kits portés) écarté pour l'instant : l'utilisateur garde le micronoyau écrit depuis zéro ; à étudier seulement en cas de vrai blocage"
metadata:
  type: project
---
Question de l'utilisateur (20/11/2026) : un Korin sur un noyau Linux, avec Elegant mais sans X, Wayland ni GTK.
Réponse donnée : faisable, à la manière d'Android. Il faudrait :
- Elegant qui affiche par DRM/KMS et lit les entrées par evdev ;
- les kits qui touchent au système, portés sous Linux (UIKit en socket Unix et memfd, AppKit, NetKit, AudioKit sur
  ALSA, les kits GPU sur Mesa, GpioKit et BusKit) ;
- une macro `sys` qui appelle une bibliothèque, pour l'assembleur ;
- une vraie libc à la place de libposix, et un init à nous.

**Décision de l'utilisateur** : « une idée à étudier, le jour où on a un vrai blocage ; mais jusqu'à présent je veux
garder l'esprit micronoyau construit from scratch ».

**How to apply:** ne pas proposer de passer sous Linux, ni d'étude sur ce sujet, sans un blocage réel (matériel
impossible à gérer, chantier qui n'aboutit pas). Les programmes Linux passent par la couche de compatibilité sur le
noyau Korin (liblinux, linuxhost : [[armpi-widevine]]), pas l'inverse.
