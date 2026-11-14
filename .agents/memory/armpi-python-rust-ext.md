---
name: armpi-python-rust-ext
description: "Étape 5 de Python (12/11/2026) : modules pyo3 en cdylib, build-rust-ext.sh, caisses patchées, OpenSSL exporté par python3 ; dans main (e68c0a7d)"
metadata:
  node_type: memory
  type: project
  originSessionId: 70b1a0c9-5dae-40aa-84d6-23968574ff63
  modified: 2026-11-12T08:23:58.206Z
---

Étape 5 faite sous QEMU le 12/11/2026 dans main (e68c0a7d, poussé le 12/11) :
pydantic-core, rpds-py, cryptography chacun en `cdylib` (plus de caisse parapluie), `tools/python/build-rust-ext.sh`
sans maturin. python3 exporte OpenSSL entier ; cryptography n'en porte pas de copie.
`tools/rust` vend target-lexicon et getrandom patchées pour `korin`. Pas essayé sur le Pi.

**Why:** étape avant le harness de Vibe (étape 6), qui se construira par le même script.

**How to apply:** une nouvelle caisse qui refuse `korin` → un patch de plus dans `tools/rust` (liste VENDOR de
build.sh) ; essais telnet : lignes coupées vers 256 caractères, et `/x` vise le volume courant. Voir
[[armpi-cpython]], [[armpi-rust]].
