---
name: armpi-idee-libs-gcc
description: Pour plus tard (projet stable) : livrer avec le GCC de l'OS les bibliothèques tierces des ports (zlib, OpenSSL, curl, SQLite, Skia, Mesa…) pour les apps compilées dans Korin
metadata:
  type: project
---

Demande de l'utilisateur du 13/11/2026 : quand le projet sera stable, les bibliothèques tierces que les ports construisent hors de Korin (zlib, OpenSSL, curl, SQLite, libffi, FreeType, HarfBuzz, png/jpeg/webp, Skia, libvpx, dav1d, Opus, Mesa EGL/GLES, SDL 3, plus tard OpenCASCADE…) doivent être livrées avec le GCC de l'OS (`/gcc`, Pi et PC : en-têtes, `.a`, `.so`, `.pc`), pour que des apps construites dans Korin les réutilisent. Aujourd'hui seuls libposix, les kits (include/kits, lib<kit>.a), libglkit.a et les .bi de FreeBASIC y sont.

**Why:** pouvoir développer dans Korin des apps qui s'appuient sur ces bibliothèques, comme on le fait déjà avec les kits.

**How to apply:** pas maintenant ; noté à la fin du plan de docs/etude/PC-PILOTES.md (commit 2a708f36). Décidé (13/11) : deux paquets par bibliothèque, `lib<nom>` binaire avec les .so seulement, `<nom>-dev` avec les .a, les en-têtes et le .pc (dépend de lib<nom>) ; une seule construction partagée par les ports. Voir [[armpi-x86-pc]], [[armpi-gcc-sur-armpi]], [[armpi-paquets]].

**18/11 : mis au plan** par [[armpi-ports-dans-korin]] (docs/etude/PORTS-DANS-KORIN.md, étape 1).
