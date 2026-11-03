# OpenSSL sur ARMPI

`libcrypto.a` et `libssl.a` statiques, pour les programmes C et C++ liés à libposix
([POSIX.md](POSIX.md)). OpenSSL est du code C porté, précompilé sur le PC : **hors du selfhost**.
Il sert aux applications C et C++ en général, et à la cryptographie et au HTTPS d'un futur port de
.NET. curl, WebKit et Jet passent par lui : c'est le seul TLS du système (voir la fin).

| Où | Quoi |
|---|---|
| `tools/openssl/build.sh` | construction (WSL) |
| `tools/openssl/armpi.conf` | la cible `armpi-aarch64` de `Configure` |
| `tools/openssl/armpi.patch` | les trois retouches des sources |
| `tools/openssl/openssltest.c`, `openssltest-cxx.cpp` | les essais |
| `build/openssl/include/openssl/`, `lib/` | le résultat : en-têtes, `libcrypto.a`, `libssl.a`, `pkgconfig/` |
| `build/openssl/sdroot/bin/` | les essais, recopiés sur la carte par `build.ps1` |
| `~/.cache/armpi-openssl` (WSL) | l'archive, les sources et l'arbre de construction, hors de git |

## Version

**OpenSSL 3.5.9**, branche LTS (suivie jusqu'en avril 2030 ; la 3.0 arrive en fin de vie). Licence
Apache-2.0. Archive de la version GitHub d'OpenSSL ; le sha256 est écrit dans `build.sh` et vérifié à
chaque construction. Il a été comparé à celui publié sur openssl.org.

## Construire et lier

```sh
sh tools/posix/build.sh             # libposix d'abord (sys/auxv.h, getauxval)
sh tools/openssl/build.sh           # NO_TESTS=1 : les bibliothèques seules
```

Un programme se lie ainsi, sur le PC (`-lssl` avant `-lcrypto`, g++ de la même façon) :

```sh
TC=/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
${TC}gcc -O2 -specs=build/posix/lib/posix.specs -Ibuild/openssl/include prog.c \
    -Lbuild/openssl/lib -lssl -lcrypto -o prog
```

`PKG_CONFIG_LIBDIR=build/openssl/lib/pkgconfig` convient aux projets qui passent par `pkg-config`.
Grâce à `--gc-sections`, le programme ne garde que ce qu'il appelle : `openssltest`, qui touche à
presque tout, fait 4,4 Mo une fois réduit, et `openssltest-cxx`, qui ne fait que du SHA-256, 3,5 Mo.

Les bibliothèques ne sont pas sur la carte : **GCC sous ARMPI ne peut pas encore lier OpenSSL**. Il
faudra copier en-têtes et archives dans `/gcc`, avec la couche d'adaptation prévue dans `/gcc/lib`.

## Configuration

| Option | Pourquoi |
|---|---|
| `no-shared`, `no-dso`, `no-module` | pas de `dlopen`. Le fournisseur `legacy` entre alors dans `libcrypto` (`OSSL_PROVIDER_load (NULL, "legacy")` marche, essayé avec MD4) |
| `no-engine` | les moteurs sont remplacés par les fournisseurs |
| `no-async` | pas d'`ucontext` |
| `no-pic`, `no-pie` | programmes `ET_EXEC` à `0x40_0000_0000` |
| `-DOPENSSL_USE_IPV6=0` | IPv4 seulement |
| `no-afalgeng` | interface de Linux |
| `--with-rand-seed=getrandom` | aléa du noyau |
| `no-apps`, `no-tests`, `no-docs` | les bibliothèques seules : pas de commande `openssl` sur la carte |

**L'assembleur AArch64 d'OpenSSL est gardé** (`linux64`, `asm_arch aarch64`).

## Ce qui a changé pour ARMPI

Dans libposix, un seul ajout : **`getauxval`** (`<sys/auxv.h>`, `misc.c`). Comparaison de tous les
symboles indéfinis des deux archives à libposix, newlib et libgcc, sans `--gc-sections` pour ne
rien cacher : aucun autre ne manque.

Les trois retouches d'`armpi.patch`, toutes sous `__armpi__` :

1. **`crypto/armcap.c` : `getauxval` au lieu des sondes.** Sans `getauxval`, OpenSSL essaie chaque
   instruction en rattrapant `SIGILL`, or ARMPI n'a pas de signaux asynchrones. `getauxval (AT_HWCAP)`
   rend FP et NEON, ce qu'ont tous les cœurs, et rien d'autre. Le Pi 4 (Cortex-A72) n'a pas les
   extensions AES et SHA. **Le Pi 5 les a mais ne s'en servirait pas** : il faudrait que le noyau
   dise ce que sait le processeur. D'ici là, `OPENSSL_armcap=<masque>` les force à la main.
2. **`include/internal/common.h` : le magasin de certificats** (ci-dessous).
3. **`rand_unix.c` : `getentropy` en référence forte.** La référence faible d'origine n'aurait pas
   tiré l'objet de `libposix.a` à coup sûr, et sans elle l'aléa échouait.

## Certificats racine

**`/etc/ca-bundle`** (`SD0:/etc/ca-bundle`, chemin virtuel de `paths.txt`), le paquet Mozilla déjà
sur la carte, celui de curl et de WebKit. Un seul magasin pour tout le système, 121 racines lues
par OpenSSL. C'est le défaut de `SSL_CTX_set_default_verify_paths` ; `SSL_CERT_FILE` et
`SSL_CERT_DIR` le changent.

`OPENSSLDIR` vaut `/etc/ssl`. On garde un chemin sans `:` parce qu'`OPENSSLDIR "/certs"` est une liste
séparée par des `:` : `SD0:/etc/ssl/certs` y serait coupé en deux. Il n'y a pas d'`openssl.cnf` sur la
carte, OpenSSL s'en passe.

## L'essai

Une commande par groupe, comme `posixtest` (le telnet d'essai coupe au bout de 60 s) :

```text
openssltest crypto | tls | threads | net [hote [port]] | speed
openssltest-cxx
```

Sous QEMU (`raspi4b`), 30/10/2026, binaires produits par `build.sh` depuis une arborescence vide :

| Groupe | Réussis | Échecs | Quoi |
|---|---|---|---|
| crypto | 19 | 0 | SHA-1, SHA-2, SHA3, MD5, HMAC, PBKDF2, AES (vecteurs connus) ; AES-GCM et ChaCha20-Poly1305 avec tag faux refusé ; `RAND_bytes` ; RSA-2048, ECDSA P-256, Ed25519, X25519, ML-KEM-768 ; `legacy` |
| tls | 9 | 0 | en mémoire (paire de BIO) : TLS 1.3 et 1.2, certificat et nom vérifiés, 16 Ko dans chaque sens ; certificat inconnu et mauvais nom refusés |
| threads | 3 | 0 | 4 threads, un seul `SSL_CTX`, 25 connexions chacun |
| net | 12 | 0 | `https://example.com`, socket bloquant puis non bloquant (`poll`), TLS 1.3, vérifié par `/etc/ca-bundle` |
| cxx | 1 | 0 | `std::thread` sur `libcrypto` |

`net` a aussi été lancé à la main : `tls-v1-2.badssl.com 1012`, 12 réussis en TLS 1.2 ;
`expired.badssl.com`, `SSL_connect` échoue comme il le doit, certificat expiré.

`speed` sous QEMU, pour information seulement (l'émulation n'a rien à voir avec un vrai A72) :

| | QEMU |
|---|---|
| SHA-256 | 92 Mo/s |
| SHA-512 | 147 Mo/s |
| AES-128-GCM | 30 Mo/s |
| AES-256-CBC | 4 Mo/s |
| ChaCha20-Poly1305 | 109 Mo/s |
| poignée de main TLS 1.3 (ECDSA P-256, en mémoire) | 64 /s |

## Pas essayé

* **Sur le Pi, seul Jet a été essayé** (voir plus bas) : ni les essais, ni les débits. `openssltest
  net` et `speed` sont à lancer sur le PocketTerm.
* **Le rôle de serveur sur de vrais sockets** : le serveur n'a tourné qu'en mémoire (`accept` ne
  connaît pas l'autre bout, [POSIX.md](POSIX.md)).
* **DTLS et QUIC** : compilés, jamais lancés.
* **Les tests d'OpenSSL** (`no-tests`) : l'essai couvre le chemin courant, pas toute la bibliothèque.
* **Le port de .NET lui-même**. À savoir pour plus tard : .NET charge OpenSSL par `dlopen` par
  défaut, il faudra lier son `libSystem.Security.Cryptography.Native.OpenSsl` en statique.

## WebKit, curl : sur OpenSSL (30/10/2026)

**mbedTLS n'est plus dans l'OS** : curl, WebKit et Jet passent par OpenSSL. WebKit
l'utilisait directement (WebCrypto, condensats de PAL, vérification des certificats dans
`CurlSSLVerifier`), mais son code OpenSSL d'origine était encore dans l'arbre.

| Où | Quoi |
|---|---|
| `tools/webkit/ports/openssl/build.sh` | `tools/openssl/build.sh` sur la libposix du sysroot, puis copie dans le sysroot |
| `tools/webkit/ports/curl/build.sh` | `-DCURL_USE_OPENSSL=ON`, même magasin `SD0:/etc/ca-bundle` |
| `tools/webkit/patches/0001-openssl.patch` | `USE_OPENSSL` dans `OptionsOnyx.cmake`, les 18 fichiers de `crypto/openssl/` dans `PlatformOnyx.cmake`, `CryptoDigestOpenSSL.cpp` dans PAL |

`tools/webkit/ports/mbedtls/` est retiré. `tools/webkit/fetch.sh` applique le patch une fois.

**Refaire un arbre existant** (`WebCore` entier change de configuration, environ 1 h sur 16 cœurs) :

```sh
sh tools/webkit/build-deps.sh openssl curl
BUILD=~/armpi-webkit-build/webkit-jit CMAKE_EXTRA="-DENABLE_JIT=ON -DENABLE_DFG_JIT=ON" TARGET=WebKit sh tools/webkit/build-webcore.sh
BUILD=~/armpi-webkit-build/webkit-jit sh tools/webkit/build-jet.sh
```

Essayé sous QEMU, arbre JIT, avec les binaires construits par ces scripts :

| Essai | Résultat |
|---|---|
| `curltest https://example.com/` | 200, certificat vérifié, `libcurl/8.16.0 OpenSSL/3.5.9` |
| `curltest https://expired.badssl.com/` | refusé, code 60, `certificate has expired` |
| `wk2test https://example.com/` | chargée en 9 s, avec `s.js` |
| `wk2test https://www.wikipedia.org/` | chargée en 13 s, 1 198 éléments, images, scripts et SVG |
| `wk2test https://expired.badssl.com/` | refusé, `CurlErrorDomain 60` |
| Jet, `https://example.com/` | la page dans la fenêtre (capture) |
| Jet, `/webkit/crypto.html` (`tools/webkit/tests`) | WebCrypto, 14 réussis sur 14 : SHA, HMAC, PBKDF2, HKDF, AES-GCM / CBC / CTR / KW, ECDSA, ECDH, RSA-PSS, RSASSA, RSA-OAEP (jwk) |

Tailles, réduites sauf mention :

| | mbedTLS | OpenSSL |
|---|---|---|
| Jet | 104,3 Mo | **108,0 Mo** (+3,7 Mo) |
| `curl.elf`, non réduit | 2,7 Mo | 7,5 Mo (5,5 Mo réduit) |

`wk2test` ne se liait plus depuis l'arrivée du compositeur : `build-wk2test.sh` lie maintenant
`gpucomp`, comme `build-jet.sh`.

**Sur le Pi** (30/10/2026, rapporté par l'utilisateur) : Jet ouvre des sites https. Pas mesuré : la
vitesse réelle de la poignée de main par le process réseau. Pas essayé : les vidéos et le son en
https.

## Retrait de TLSKit et de mbedTLS (30/10/2026)

TLSKit et `httpsget`, son seul utilisateur, sont retirés, avec `tools/tlskit`,
`src/kits/tlskit` et `prebuilt/tlskit.so`. `build.ps1 sd` enlève leurs fichiers d'une carte plus
ancienne. `finish.py` sert encore à MediaKit : il est dans `tools/mediakit`.

**Les programmes en assembleur n'ont donc plus de TLS** : ils ne peuvent pas se lier à une archive
C. OpenSSL dans un kit demanderait threads, `malloc`, fichiers et stdio. S'il en faut un, le
chemin le plus simple est un petit service en C lié à OpenSSL, appelé par IPC. Pas fait.
