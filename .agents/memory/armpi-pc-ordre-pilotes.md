---
name: armpi-pc-ordre-pilotes
description: "Pilotes du ZBook : pas de réseau par téléphone (NCM/RNDIS écartés), le Wi-Fi AX201 avant l'Ethernet e1000e"
metadata:
  node_type: memory
  type: feedback
  originSessionId: f37cb635-d720-4a7d-a203-55460bb5fb2c
  modified: 2026-11-12T14:43:46.256Z
---

Le 12/11/2026, l'utilisateur a dit qu'il n'a pas besoin de réseau par téléphone, et qu'il veut le Wi-Fi avant
l'Ethernet, qu'il n'utilise pas vraiment.

**Pourquoi :** il n'y a pas de câble Ethernet chez lui, et il ne se sert pas du partage de connexion du téléphone.

**Comment appliquer :**
- Ne pas ajouter NCM ni RNDIS à usbnet.
- L'AX201 passe avant e1000e ; e1000e seulement en dernier, s'il sert pour mettre netd au point sous QEMU.
- Le Wi-Fi se met donc au point sur le ZBook sans réseau de secours : traces à l'écran, journal sur une clé USB.

L'ordre est noté dans docs/etude/PC-PILOTES.md. Voir [[armpi-x86-pc]].
