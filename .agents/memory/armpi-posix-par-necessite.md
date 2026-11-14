---
name: armpi-posix-par-necessite
description: "POSIX n'est pas un but de conception d'ARMPI : il existe seulement pour porter des apps et des libs, au fur et à mesure"
metadata:
  node_type: memory
  type: project
  originSessionId: 52ea7130-db86-4847-a36b-ee89fc65ff48
  modified: 2026-10-30T15:58:22.702Z
---

POSIX est sur ARMPI par nécessité, pas par design : il sert à porter des apps ou des libs au fur et à mesure (dit par l'utilisateur le 30/10/2026). Le taux de couverture n'est pas un objectif.

**Why:** l'OS garde son propre modèle (assembleur, kits, services, pas de `fork`) ; [[armpi-gcc-sur-armpi]] et [[armpi-webkit]] sont des portages qui ont tiré ce qu'il fallait.

**How to apply:** ajouter à `libposix` ce qu'un portage réel demande, sans viser la conformité ni déformer le noyau pour elle. Mettre une fonction POSIX manquante au compte d'un portage précis, pas d'une liste à compléter.
