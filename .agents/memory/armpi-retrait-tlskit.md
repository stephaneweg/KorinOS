---
name: armpi-retrait-tlskit
description: "Depuis le 30/10/2026, OpenSSL est le seul TLS d'ARMPI ; httpsget, TLSKit et mbedTLS retirés, les programmes en assembleur n'ont plus de TLS"
metadata:
  node_type: memory
  type: project
  originSessionId: 242584a4-fa4b-4bdb-98e3-e94a92bd4cb7
  modified: 2026-10-30T21:42:26.468Z
---

Décision de l'utilisateur (30/10/2026), une fois Jet sur OpenSSL validé sur le Pi : `httpsget`, TLSKit et mbedTLS sont retirés (branche `claude/chantier-openssl-22d547`).

**Why:** `httpsget` était le dernier utilisateur de TLSKit et `curltest` le remplace.

**How to apply:** ne pas proposer TLSKit pour un nouveau besoin. Un programme en assembleur qui a besoin de TLS demandera un service C sur OpenSSL, appelé par IPC (pas fait) ; SqlKit est concerné. Voir `docs/OPENSSL.md`.
