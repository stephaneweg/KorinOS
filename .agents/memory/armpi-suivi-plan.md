---
name: armpi-suivi-plan
description: "Artefact de suivi du plan (18/11) : https://claude.ai/artifact/1Tf4nfDXtzpNVmQrfoDGkd, une ligne par chantier dans la base « items », à tenir à jour par ArtifactData"
metadata:
  type: reference
---
Demandé par l'utilisateur le 18/11/2026 : un artefact de suivi du plan, **https://claude.ai/artifact/1Tf4nfDXtzpNVmQrfoDGkd**
(« Plan Korin OS », privé à l'utilisateur). Les lignes sont dans sa base, collection `items`, un document par
chantier : `section` (now, prio, music, next, study, end, later), `order` (dans prio, music, end), `title`,
`detail`, `ref` (documents, séparés par « ; »), `note`, `status` (todo, doing, wait, done, drop), `updated`
(AAAA-MM-JJ). L'utilisateur change l'état depuis la page ; les autres n'écrivent pas (règle write = admin).

**How to apply:** quand un chantier avance, finit, ou qu'un point est ajouté au plan, mettre la ligne à jour par
l'outil ArtifactData (update avec if_version, ou set pour une ligne neuve) ; ne pas republier la page pour cela.
Voir [[armpi-a-faire]], [[armpi-x86-pc]].
