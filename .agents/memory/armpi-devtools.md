---
name: armpi-devtools
description: "22-23/11/2026 : cmake, perl, ruby, m4, bison, gperf, unifdef, pkgconf et 13 paquets <lib>-dev pour Korin PC (tools/devtools) ; publiés sur la branche claude/serene-newton-051kkg, pas encore dans main"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-23T00:00:00.000Z
---

Demande de l'utilisateur du 22/11 (docs/agents/JET-LOCAL-BUILD.md, points 4 et 5) : les outils pour reconstruire WebKit dans Korin, en paquets, et les bibliothèques du SDK de Jet en `<nom>-dev`.

- `tools/devtools/build.sh` (+ `perl.sh` perl-cross tout statique, `ruby.sh` coroutines amd64 sans gemmes livrées, `cmake.sh` libuv de CMake sur poll avec `cmake/korin.c`, Platform/ARMPI.cmake car uname dit « ARMPI ») ; `stage-dev.sh` : zlib, libpng, libwebp, freetype, harfbuzz, icu, skia, sqlite, libxml2, brotli, nghttp2, openssl, curl `-dev` depuis le sysroot de WebKit (Tiger Lake). Tout dans /devtools/gcc.
- Essayés dans Korin sous QEMU : perl (File::Spec, JSON::PP…), ruby (digest, json, pathname), cmake -P + execute_process, m4, bison, unifdef, pkgconf. **flex** se construit mais se bloque à l'exécution (son filtre m4 par fork et tubes) : pas en paquet.
- Publiés par `pub.py --new --base HEAD --no-push` sur la branche : la fusion dans main les rend disponibles.

**Why:** reconstruire WebKit entièrement dans Korin (CMake + Ninja), et servir à d'autres portages.

**How to apply:** pièges : un `-lc` explicite fait prendre le getentropy de newlib (korin-cc le retire) ; dev_t de newlib 16 bits (Ruby : SHORT2NUM) ; les .pm des extensions statiques de perl-cross à refaire (pm_to_blib) ; harfbuzz-dev a besoin d'icu-dev publié avant. Voir [[armpi-paquets]], [[armpi-fichiers-paralleles]].
