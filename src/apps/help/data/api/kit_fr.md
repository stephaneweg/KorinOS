# Écrire un kit

Un **kit** est une bibliothèque partagée par tout le système : chargé une seule fois, à la même adresse dans chaque process, il est appelé comme une fonction ordinaire. UIKit, AudioKit, NetKit sont des kits. On en écrit un quand plusieurs programmes ont besoin du même code, ou pour cacher un [service](service_fr.md) derrière des fonctions (PrinterKit devant le service `print`, GpioKit devant `gpio`).

## Ce qu'est un kit

- Un fichier `SD0:/lib/<nom>.so` : un ELF64 assemblé par FASMARM, lié à l'adresse 0, sans aucune adresse absolue (tout passe par `adr`).
- Son point d'entrée est sa **table d'exportation** : `'AKIT'`, le nombre de fonctions (sa **version**), puis le décalage de chaque fonction.
- La table **ne fait que grandir** : une fonction n'est jamais retirée, déplacée ni renommée ; une nouvelle s'ajoute à la fin. Un programme demande une version minimale ; un programme ancien marche avec un kit plus récent.
- Le code est partagé par tous les process ; les variables (le segment inscriptible) sont **recopiées dans chaque process** : chacun a les siennes.
- Un kit chargé **reste en mémoire** jusqu'au redémarrage : après avoir remplacé `SD0:/lib/<nom>.so`, redémarrer pour l'essayer. Le système en garde 16 au plus.
- Les fonctions suivent l'AAPCS64 : arguments dans `x0`–`x7`, résultat dans `x0` (et `x1`, `x2` si besoin), `x19`–`x28` préservés.

## Les fichiers

| Fichier | Rôle |
|---|---|
| `src/kits/<kit>/<kit>.abi` | la liste ordonnée des fonctions, écrite une seule fois : elle construit la table du kit et les relais de ses utilisateurs |
| `src/kits/<kit>/<kit>.asm` | le code |
| `src/kits/<kit>/<kit>.inc` | les constantes publiques (reprises dans l'en-tête C) |

Le guide en garde un exemple complet dans `SD0:/apps/help.app/api/demokit/`.

## DemoKit : le .abi

```
; DemoKit : un kit d'exemple (guide du developpeur)
; Table en ajout seul : ne jamais retirer, deplacer ni renommer une ligne.
kit_begin demokit, 'demokit'
kit_fn dk_add			; x0, x1 -> x0 = x0 + x1
kit_fn dk_count			; -> x0 = appels de dk_add par ce process
kit_fn dk_upper			; x0 = chaine : en majuscules (ASCII), sur place -> x0 = longueur
kit_end
```

Le commentaire de chaque ligne dit ses arguments et son résultat : c'est la documentation du kit, et `tools/kits/abi2c.py` s'en sert pour l'en-tête C.

## DemoKit : le code

```
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header

segment readable executable
include 'demokit.abi'				; la table d'exportation

; x0, x1 -> x0 = x0 + x1 (et un appel de plus)
dk_add:
	adr	x2,dk_calls
	ldr	x3,[x2]
	add	x3,x3,1
	str	x3,[x2]
	add	x0,x0,x1
	ret

; -> x0 = appels de dk_add par ce process
dk_count:
	adr	x0,dk_calls
	ldr	x0,[x0]
	ret

; x0 = chaine : ses lettres a..z en majuscules, sur place -> x0 = longueur
dk_upper:
	mov	x2,x0
	mov	x0,0
.c:	ldrb	w1,[x2,x0]
	cbz	w1,.e
	sub	w3,w1,'a'
	cmp	w3,25
	b.hi	.n
	sub	w1,w1,32
	strb	w1,[x2,x0]
.n:	add	x0,x0,1
	b	.c
.e:	ret

segment readable writeable
dk_calls	dd 0				; (dd : 64 bits avec FASMARM)
```

`kit_header` fait l'en-tête ELF ; inclure le `.abi` dans le segment de code y pose la table. Avec FASMARM, `db` vaut 8 bits, `dh` 16, `dw` 32 et `dd` 64. Un kit peut en utiliser un autre : `kit_use '../../kits/appkit/appkit.abi'` dans son code, `kit_data` dans ses variables, `bl kits_open` avant le premier appel (UIKit importe AppKit ainsi).

## Construire et installer

Sous Korin OS, depuis `SD0:/src/kits/demokit` (les sources du système sont sur la carte, `include` les trouve) :

```sh
mkdir -p SD0:/src/kits/demokit
cp SD0:/apps/help.app/api/demokit/demokit.asm SD0:/apps/help.app/api/demokit/demokit.abi SD0:/src/kits/demokit
cd SD0:/src/kits/demokit
fasmarm demokit.asm SD0:/lib/demokit.so
```

Sur le PC : poser le dossier dans `src/kits/demokit` ; `build.ps1` assemble chaque `src/kits/<kit>/<kit>.asm` et le met dans `SD0:/lib`. Pour le C, ajouter une ligne à `tools/gcc/kits.sh` :

```sh
kit demokit "$K/demokit/demokit.inc"
```

`sh tools/gcc/kits.sh` produit alors `/gcc/include/kits/demokit_abi.h` (les prototypes et `demokit_kit_open ()`) et `/gcc/lib/libdemokit.a` (les relais) ; `pibuild` les prend d'office.

## L'appeler depuis le C, sans en-tête généré

`<armpi/kit.h>` ouvre n'importe quel kit par son nom et donne ses fonctions par leur rang (la ligne `kit_fn`, à partir de 0). C'est ce que font les relais générés ; c'est aussi le moyen d'essayer un kit tout neuf sous Korin OS, où Python (qui génère les en-têtes) n'existe pas.

```c
#include <stdio.h>
#include <armpi/kit.h>

int main (void)
{
	void *k = armpi_kit_open ("demokit", 3);		// au moins 3 fonctions
	if (!k)
	{
		printf ("demokit absent : SD0:/lib/demokit.so, version 3 ou plus\n");
		return 1;
	}
	long (*dk_add) (long, long) = (long (*) (long, long)) armpi_kit_fn (k, 0);
	long (*dk_count) (void) = (long (*) (void)) armpi_kit_fn (k, 1);
	long (*dk_upper) (char *) = (long (*) (char *)) armpi_kit_fn (k, 2);
	printf ("version %u\n", armpi_kit_version (k));
	printf ("2 + 3 = %ld\n", dk_add (2, 3));
	printf ("40 + 2 = %ld\n", dk_add (40, 2));
	printf ("appels : %ld\n", dk_count ());
	char s[] = "bonjour, korin";
	long n = dk_upper (s);
	printf ("%s (%ld lettres)\n", s, n);
	return 0;
}
```

Le même programme sans un kit installé l'apprend par `armpi_kit_open`, qui rend `NULL` : un programme peut ainsi se passer d'un kit facultatif.

## L'appeler depuis l'assembleur

```
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
segment readable executable
kit_use '../../kits/demokit/demokit.abi'	; un relais par fonction
start:	bl	kits_open			; message et fin si le kit manque
	mov	x0,2
	mov	x1,3
	bl	dk_add				; x0 = 5
	...
segment readable writeable
kit_data					; la table d'import, remplie par kits_open
```

## Règles

- **Jamais de changement de sens** : une fonction publiée garde ses arguments et son résultat. Pour faire autrement, une nouvelle fonction à la fin (`dk_add_ex`).
- **Pas d'adresse absolue** : pas de `dd label` dans un kit (FASMARM n'émet pas de relocations) ; `adr` seulement.
- **Les variables sont par process** ; ce qui doit être commun à tous passe par un service ou une mémoire partagée.
- **Un kit n'écrit pas sur la console** d'un programme graphique : il rend des codes d'erreur (négatifs, comme le noyau), et une fonction qui les dit en texte (`gp_error` de GpioKit).
- **Il s'assemble à l'identique** par FASMARM sous Windows et sous Korin OS : pas d'outil externe.
- **Les noms ont un préfixe** propre au kit (`ui_`, `ak_`, `gp_`, `dk_`) : un programme ne peut pas définir une fonction du même nom qu'une fonction d'un kit qu'il utilise.

## Voir aussi

- [Écrire un service](service_fr.md) : quand le code doit tourner dans un seul process pour tous.
- Les kits du système : [sommaire](index_fr.md#les-kits).
