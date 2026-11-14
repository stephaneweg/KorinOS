---
name: armpi-pas-de-securite-attaquant
description: ARMPI — plus de travaux de sécurité contre un attaquant ; seulement résistance aux bugs et performance (29/10/2026)
metadata:
  node_type: memory
  type: feedback
  originSessionId: dddd2629-eb88-420d-aecf-5f977a24ffef
  modified: 2026-10-29T18:58:31.177Z
---

Décision de l'utilisateur (29/10/2026, chantier IPC) : on ne fait plus de sécurité contre un attaquant
(noms de services réservés, droits par zone de `dev_map`, filtre de `fw_call`, `SD0:/sys` en lecture
seule, signatures…). Seulement la **résistance aux bugs** (une application boguée ne doit pas casser
le système ni la partition personnelle) et la **performance**. Ce qui est déjà fait reste
(« si ça coûte peu tu laisses »).

**Why:** appareil personnel à un seul utilisateur qui installe lui-même ses programmes ; il trouvait
qu'on « en faisait un peu trop pour un petit OS maison ».

**How to apply:** ne pas proposer de mesure dont le seul intérêt est de contrer un attaquant ; quand
une mesure est proposée, dire si elle protège des bugs ou sert la performance. Exception à signaler
seulement : les services réseau (ftpd, telnet, httpd, rdpd, vncd) exposés à l'extérieur. Voir
[[armpi-feuille-de-route]], [[armpi-pocketterm]].
