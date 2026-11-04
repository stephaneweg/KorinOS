# Écrire un service

Un **service** est un programme qui s'inscrit sous un nom auprès du noyau (`vfs`, `net`, `sound`, `print`...) et répond aux requêtes des autres programmes. Le client ouvre le service par son nom, envoie un message de 512 octets et reçoit la réponse dans le même message : c'est l'IPC de base de Korin OS, synchrone et sans copie de plus que le message.

## Le message

| Décalage | Champ | Rôle |
|---|---|---|
| +0 | `op` | la requête ; dans la réponse : le résultat (rendu aussi par `SVC_CALL`) |
| +8, +16, +24, +32 | `a`, `b`, `c`, `d` | quatre arguments de 64 bits |
| +40 | `len` (32 bits) | octets utiles de `data` |
| +44 | `pid` (32 bits) | le process appelant, rempli par le noyau |
| +48 | `data` | 464 octets de données |

Pour plus de 464 octets : la mémoire partagée ([IPC](ipc_fr.md#mémoire-partagée)), ou plusieurs requêtes.

## Les appels

| N° | Appel | Arguments | Résultat |
|---|---|---|---|
| `0x30` | `SVC_REGISTER` | nom (31 octets au plus), drapeaux | numéro de service ; seulement pour un programme de `SD0:/sys` ; une erreur si le nom est déjà pris |
| `0x31` | `SVC_OPEN` | nom, attente en ms | canal ; `E_NOENT` (-2) si le service n'est pas là à la fin de l'attente |
| `0x32` | `SVC_CALL` | canal, message | le résultat (`op` de la réponse) ; `E_PIPE` (-32) si le service s'arrête |
| `0x33` | `SVC_NEXT` | numéro de service, message, attente en ms | numéro de la requête (0 : rien pendant l'attente) |
| `0x34` | `SVC_REPLY` | numéro de la requête, message | 0 |
| `0x35` | `SVC_SIGNAL` | canal, bits | 0 : réveille le service sans attendre ; il reçoit l'op `0xFFFD`, `a` = les bits |

Drapeaux de `SVC_REGISTER` : 1 (`SVF_FINS`) : le service est prévenu de la fin de chaque process (op `0xFFFF`, `a` = pid, `b` = code de sortie), utile pour libérer ce qu'un client tenait ; 2 : prévenu des naissances (op `0xFFFE`) ; 4 : il garde de la mémoire qu'il peut rendre. Ces avis du noyau n'attendent pas de réponse. Les autres appels (`0x36` à `0x38` : mémoire de l'appelant, pour les pilotes) sont décrits dans `docs/ABI.md`.

Tous ces appels passent par `svc 0`, le numéro dans `x8` : voir `sys3` dans les exemples. La libc n'a pas de fonction pour eux.

## Un service : compteur

Le service « compteur » garde un total : `CT_ADD` lui ajoute `a`, `CT_GET` le rend, `CT_HELLO` répond un texte dans `data`.

```c
// compteur -- un service d'exemple : s'inscrit sous « compteur » et garde un total
#include <stdio.h>
#include <string.h>

struct svc_msg					// le message de 512 octets
{
	long op, a, b, c, d;
	unsigned len, pid;
	char data[464];
};
enum { SYS_SVC_REGISTER = 0x30, SYS_SVC_NEXT = 0x33, SYS_SVC_REPLY = 0x34 };
enum { CT_ADD = 1, CT_GET = 2, CT_HELLO = 3 };	// les requêtes du service
enum { SVF_FINS = 1, OP_NOTICES = 0xFFF0 };	// (op >= 0xFFF0 : un avis du noyau, sans réponse)

static long sys3 (long n, long a, long b, long c)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x8) : "memory", "cc");
	return x0;
}

int main (void)
{
	long svc = sys3 (SYS_SVC_REGISTER, (long) "compteur", SVF_FINS, 0);
	if (svc < 0)
	{
		printf ("compteur : inscription refusée (%ld) : déjà lancé, ou pas dans SD0:/sys\n", svc);
		return 1;
	}
	static struct svc_msg m;
	long total = 0;
	for (;;)
	{
		long rq = sys3 (SYS_SVC_NEXT, svc, (long) &m, 60000);
		if (rq < 0)
			return 1;
		if (rq == 0 || m.op >= OP_NOTICES)		// rien, ou un avis (fin d'un process...)
			continue;
		long r;
		switch (m.op)
		{
		case CT_ADD: total += m.a; r = total; break;
		case CT_GET: r = total; break;
		case CT_HELLO:
			m.len = (unsigned) snprintf (m.data, sizeof m.data, "Bonjour, process %u : le total est %ld.", m.pid, total);
			r = 0;
			break;
		default: r = -22;				// EINVAL
		}
		m.op = r;
		sys3 (SYS_SVC_REPLY, rq, (long) &m, 0);
	}
}
```

## Un client

N'importe quel programme peut ouvrir un service, sans droits particuliers.

```c
// ajoute : envoie un nombre au service « compteur »
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct svc_msg
{
	long op, a, b, c, d;
	unsigned len, pid;
	char data[464];
};
enum { SYS_SVC_OPEN = 0x31, SYS_SVC_CALL = 0x32 };
enum { CT_ADD = 1, CT_GET = 2, CT_HELLO = 3 };

static long sys3 (long n, long a, long b, long c)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x8) : "memory", "cc");
	return x0;
}

int main (int argc, char **argv)
{
	long ch = sys3 (SYS_SVC_OPEN, (long) "compteur", 2000, 0);	// attend le service 2 s au plus
	if (ch < 0)
	{
		printf ("le service compteur ne tourne pas (%ld)\n", ch);
		return 1;
	}
	static struct svc_msg m;
	memset (&m, 0, sizeof m);
	m.op = CT_ADD;
	m.a = argc > 1 ? atol (argv[1]) : 1;
	long total = sys3 (SYS_SVC_CALL, ch, (long) &m, 0);
	printf ("total : %ld\n", total);
	memset (&m, 0, sizeof m);
	m.op = CT_HELLO;
	if (sys3 (SYS_SVC_CALL, ch, (long) &m, 0) >= 0)
		printf ("%.*s\n", (int) m.len, m.data);
	return 0;
}
```

## Construire, lancer

Un service s'inscrit seulement depuis `SD0:/sys` : il se construit en type `sys` ou `svc`.

```sh
pibuild svc SD0:/home/projets/compteur compteur
pibuild bin SD0:/home/projets/ajoute ajoute
SD0:/sys/svc/compteur
ajoute 5
```

| Pour | Faire |
|---|---|
| l'essayer | le lancer depuis le Terminal : un programme de `SD0:/sys` passe par `init2`, qui lui donne les droits ; sa sortie va au journal, pas au Terminal |
| le lancer à chaque démarrage | ajouter `SD0:/sys/svc/compteur` à `SD0:/etc/services.txt` : `init2` le lance avant les apps et le relance s'il tombe |
| le lancer avec le bureau | ajouter sa ligne à `SD0:/etc/session` (programmes lancés par Elegant) |
| le lancer à la demande | le client ouvre le service ; s'il n'est pas là, il demande à `init2` de le lancer (`INIT_OP_LAUNCH`), puis rouvre avec une attente |

## Conseils

- **Une requête courte** : le client attend la réponse (30 s au plus). Un travail long se fait dans un thread du service ; la requête rend tout de suite un numéro de travail, que le client interroge ensuite (c'est ce que fait le service d'impression, `src/sys/printd`).
- **`SVF_FINS`** : à la fin d'un client, libérer ce qu'il tenait (verrous, fichiers ouverts pour lui).
- **Des numéros d'op stables** : comme les kits, un service garde ses ops ; il en ajoute, il n'en change pas le sens.
- **Un en-tête partagé** entre le service et ses clients (`compteur.h` : les ops, la structure), plutôt que des nombres recopiés.
- Un service peut aussi répondre en différé : garder le numéro de requête et appeler `SVC_REPLY` plus tard, depuis un autre thread.

## Voir aussi

- [Les fonctions d'IPC](ipc_fr.md) : canaux, mémoire partagée, attente de plusieurs poignées.
- [Écrire un kit](kit_fr.md) : une bibliothèque qui cache le service à ses clients (comme PrinterKit devant `print`).
