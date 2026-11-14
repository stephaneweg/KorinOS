---
name: armpi-pocketqi
description: "Port de PocketQI (assistant écrit, applet de la barre, skills dans app.txt), dans main (efa815ad), QEMU seulement"
metadata:
  node_type: memory
  type: project
  originSessionId: 474bdb1e-b048-47d3-a052-7ec068b327f2
  modified: 2026-11-04T15:54:00.257Z
---

PocketQI porté le 3/11/2026 (fffe82a0), fusionné dans main par efa815ad (4/11), apps C++ refaites par tools/office/build.sh ; worktree et branche supprimés le 4/11. Voir `src/apps/pocketqi/README.md`.

Choix à garder : pas d'écoute (clavier seul) ; applet = icône bulle à gauche du son dans menubar, `pocketqi --popup <x>`, second clic ferme via `RAM0:/pocketqi.pid` ; compétences = clés `description`, `keywords`, `help`, `llm.<action>`, `run.<action>` des app.txt (docs/ABI.md), `#` interdit dans les valeurs (ini_read d'applist.inc) ; clés d'API dans `SD0:/home/pocketqi/config.ini` seulement ; repli local sans service.

Outils (4/11, fusionnés dans main par 359eb882, poussés ; paquets pocketqi 1.0.2, calendar 1.0.5, notes 1.0.5 publiés (d6c9c6fa)) : météo (Open-Meteo), notes (celles de Notes), agenda (calendar.ics du Calendrier), liste de courses (une note de Notes), faits en C++ par PocketQI (tools.h, data/tools/*.txt), pas en BASIC ; LocalParser FR/EN hors ligne ; voix = Mistral Voxtral seulement (voice.h, 61f73e07 ; pas de PicoTTS, refusé par l'utilisateur) ; essayés sous QEMU seulement.

**How to apply:** essais avec `tools/nettest/llmsrv.py` (faux service, 10.0.2.2:8088), jamais un vrai service sans accord. Une carte faite depuis le build de main garde les anciens app.txt des apps C++ : refaire `tools/office/build.sh` après fusion. Restent : voix Mistral, essai réel, Pi. Voir [[armpi-idee-assistant-ia]], [[armpi-mail-irc-telegram]].
