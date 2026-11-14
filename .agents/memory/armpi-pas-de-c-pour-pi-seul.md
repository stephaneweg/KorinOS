---
name: armpi-pas-de-c-pour-pi-seul
description: "Ce qui ne sert qu'au Pi (GPU V3D, DMA, GpuKit, essais du matériel du Pi) reste en asm : pas de réécriture en C"
metadata:
  node_type: memory
  type: feedback
  originSessionId: f37cb635-d720-4a7d-a203-55460bb5fb2c
  modified: 2026-11-12T13:34:05.823Z
---

Le 12/11/2026, l'utilisateur a demandé de ne plus porter en C ce qui n'est utile qu'au Pi.

**Pourquoi :** la réécriture en C sert à partager le code entre le Pi et le PC ; un programme propre au matériel du Pi n'a rien à partager.

**Comment appliquer :**
- Avant de porter un kit, un service, un pilote ou une commande, se demander s'il servira au PC.
- Si non (gpuinfo, gputest, gpuhang, dmareach, GpuKit du Pi, pilotes du matériel du Pi…), il reste en asm.
- Les fichiers C inachevés de gpu*/dmareach ont été retirés de src/bin le 12/11.

Voir [[armpi-x86-pc]], [[armpi-renommage-francais]].
