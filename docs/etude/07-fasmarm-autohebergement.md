# 07 — Auto-hébergement : faire tourner FASMARM sous ARMPI

Objectif : ARMPI doit pouvoir **s'assembler lui-même** sur le Raspberry Pi 4, donc FASMARM
doit tourner nativement (ou quasi) sous ARMPI, en 64 bits.

## 1. Le problème : FASMARM est écrit en assembleur x86 32 bits

FASMARM = cœur de **fasm 1.73** (assembleur x86 écrit en x86) + un backend ARM écrit lui
aussi en x86. Mesures (ce dépôt, `tools/fasmarm/source` et `ref/fasm/SOURCE`) :

| Partie | Fichiers | Lignes x86 |
|---|---|---|
| Cœur fasm (préprocesseur, parser, expressions, assemblage, formats, erreurs) | `PREPROCE`, `PARSER`, `EXPRPARS`, `EXPRCALC`, `ASSEMBLE`, `FORMATS`, `ERRORS`, `SYMBDUMP`, `VARIABLE`, `MESSAGES`, `VERSION` | ≈ 15 300 |
| Backend ARM (encodage de toutes les instructions ARM/Thumb/ARM64/VFP/NEON) | `armv8.inc` | 30 304 |
| Tables ARM (mnémoniques, registres, sysregs) | `armtable.inc` | 11 321 |
| Interface système Linux | `source/linux/fasmarm.asm` + `SYSTEM.INC` | ≈ 650 |
| **Total à porter** | | **≈ 57 500** |

`X86_64.INC`, `AVX.INC` et la plupart de `TABLES.INC` (encodeur x86) ne sont **pas** utilisés
par FASMARM.

Idiomes x86 qui compliquent une traduction (comptage `grep`) :

- **sous-registres 8 bits** (`al`, `ah`, `bl`, `ch`…) : ≈ 2 070 occurrences dans `armv8.inc`,
  ≈ 560 dans le préprocesseur et le parser. `ah`/`bh`/`ch`/`dh` (bits 8-15) n'ont pas
  d'équivalent AArch64 → `bfi`/`ubfx` à chaque accès ;
- **retour d'erreur par le Carry** (`stc`/`clc`/`jc`/`jnc`/`adc`/`sbb`) : ≈ 350 occurrences ;
- **instructions chaînes** (`lodsb`, `stosd`, `rep movsb`, `repe cmpsb`, `scasb`) : ≈ 900 ;
- arithmétique 32 bits avec débordements (fasm calcule ses expressions en 64 bits via des
  paires `eax:edx`) ;
- auto-modification : aucune (bon point) ; pas de segments (modèle plat 32 bits).

Interface système très petite — la version Linux n'utilise que **8 appels** `int 0x80` :
`exit(1)`, `read(3)`, `write(4)`, `open(5)`, `close(6)`, `lseek(19)`, `brk(45)` (+ `time`).
C'est le point qui rend les stratégies B et C ci-dessous réalistes.

## 2. Trois stratégies

### A. Réécriture manuelle en AArch64
57 000 lignes de code dense et non commenté, avec des invariants implicites sur les flags.
Plusieurs années-homme, risque élevé de régressions dans l'encodage. **Rejetée** comme
première étape.

### B. Émulateur i386 « userland » dans ARMPI (amorçage rapide)
Un process ARMPI spécial charge le binaire `fasmarm` Linux/ELF32 (fourni dans le paquet
FASMARM : `fasmarm` ELF i386) et l'interprète :

- décodeur i386 limité au **sous-ensemble réellement émis** par fasm (on peut l'extraire
  en désassemblant le binaire : quelques centaines d'opcodes, pas de FPU x87, pas de SSE) ;
- les 8 registres x86 dans `w19-w26`, EIP dans `w27`, flags calculés **paresseusement**
  (on mémorise le dernier résultat et l'opération, on ne calcule CF/ZF/SF/OF qu'à la
  lecture : c'est ce qui rend un émulateur x86 rapide) ;
- `int 0x80` → traduit vers les appels système ARMPI (fichiers FAT32/exFAT, mémoire) ;
- espace d'adressage 32 bits du process = une région de sa table de pages, base dans `x28`.

Estimation : **4 000 à 6 000 lignes asm**. Vitesse : 20 à 50x plus lent que natif. fasm
assemble ≈ 1 Mo/s nativement sur un PC, le RPi4 ≈ 3-5x moins vite, donc l'OS entier
(estimé 60-100 000 lignes asm) s'assemblerait en **dizaines de secondes à quelques minutes**.
Acceptable pour l'amorçage.

Avantage décisif : FASMARM tourne **sans aucune modification**, et les mises à jour
upstream de FASMARM s'appliquent immédiatement.

### C. Traducteur statique x86 → AArch64 (version native définitive)
Un outil (d'abord en Python sur PC, puis réécrit en asm ARMPI) traduit **les sources**
`.INC` de fasm/FASMARM en sources FASMARM AArch64, instruction par instruction :

- mappage fixe : `eax→w0 … edi→w7`, `esp→sp` (pile 64 bits, voir plus bas) ;
- `al`/`ax` → opérations sur `w0` masquées ; `ah` → `ubfx/bfi w0,…,8,8` ;
- flags : la plupart des `cmp`/`test` sont suivis immédiatement d'un `jcc` → traduction
  directe `cmp`+`b.cc` ; le Carry « booléen de retour » (`stc`/`clc`/`jc`) → un registre
  dédié (`w15`) plutôt que le vrai C ARM (dont le sens est inversé pour `sub`/`cmp`) ;
- `lodsb`/`stosb`/`rep movsb` → macros FASMARM (`ldrb w0,[x6],1` etc.) ;
- pointeurs 32 bits : tout le tas de fasm est dans une zone < 4 Go → on garde des
  adresses 32 bits zéro-étendues (`uxtw`), ce qui évite de réécrire les structures.

Le résultat est un FASMARM **natif 64 bits** qui génère exactement le même code (vérifiable
bit à bit : assembler toute la suite `armdoc/*.asm` avec les deux versions et comparer).
Estimation : traducteur ≈ 2 000 lignes Python + passes de correction manuelles sur les
cas non-idiomatiques. Résultat ≈ 80-100 000 lignes AArch64 générées.

## 3. Décision (28/10/2026) : portage natif uniquement

**Pas d'émulateur** (stratégie B écartée). FASMARM sera porté **nativement en AArch64** :

1. Phase noyau : cross-assemblage depuis Windows (`build.ps1`).
2. Traducteur source x86 → AArch64 (stratégie C), écrit en Python sur PC, appliqué à
   `PREPROCE/PARSER/EXPRPARS/EXPRCALC/ASSEMBLE/FORMATS/ERRORS/SYMBDUMP/VARIABLE` puis
   `armv8.inc` et `armtable.inc` ; interface système réécrite à la main pour ARMPI.
3. Validation : le FASMARM natif (d'abord lancé sous ARMPI) doit produire un `kernel8.img`
   **identique octet pour octet** à celui produit par `fasmarm.exe` sous Windows, ainsi que
   pour toute la suite `armdoc/*.asm`.
4. Quand c'est vérifié, ARMPI s'assemble lui-même : jalon « auto-hébergement ».

## 4. Prérequis côté OS

- appels système fichiers : open/read/write/lseek/close, avec chemins longs ;
- `brk` ou allocation mémoire extensible par process (fasm demande quelques dizaines de Mo) ;
- console texte (fenêtre terminal) pour les messages d'erreur ;
- horloge (pour `%t` et les messages de durée).

## 5. Point de licence

FASMARM (revolution) et fasm (Tomasz Grysztar) ont une licence permissive propre à fasm :
redistribution et modification autorisées, avec conservation de la notice. Le traducteur
produit une œuvre dérivée : garder les notices d'origine dans les sources générées.
