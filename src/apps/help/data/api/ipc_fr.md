# Les fonctions d'IPC

Korin OS fait communiquer ses programmes de trois façons :

| Moyen | Pour | Depuis le C |
|---|---|---|
| **Services nommés** | un programme serveur qui répond à des requêtes : `vfs`, `net`, `sound`, `print`... | appels `0x30`–`0x38` ([Écrire un service](service_fr.md)) |
| **Canaux** | un flux ou des paquets entre deux process, avec passage de descripteurs | `socketpair`, `send`, `recv`, `sendmsg` (`SCM_RIGHTS`), `poll` |
| **Mémoire partagée** | de gros volumes sans copie (images, son, documents) | `shm_open`, `memfd_create`, `ftruncate`, `mmap (MAP_SHARED)` |

S'y ajoutent les **tubes** (`pipe`, la sortie d'un programme lancé par `posix_spawn`) et les **futex** (`pthread_mutex`, `pthread_cond`, `sem_t` entre threads d'un même process). Les canaux et la mémoire partagée sont les appels `0x70`–`0x7D` du noyau ; la couche POSIX les sert sous leurs noms habituels, c'est le chemin conseillé.

## Canaux

`socketpair (AF_UNIX, type, 0, fd)` crée deux bouts reliés : ce qui s'écrit dans l'un se lit dans l'autre.

| Type | Comportement |
|---|---|
| `SOCK_STREAM` | un flux d'octets, comme un tube dans les deux sens |
| `SOCK_SEQPACKET` | des messages gardés entiers, dans l'ordre (le plus pratique pour un protocole) |
| `SOCK_DGRAM` | des messages, sans connexion |

Chaque bout a un anneau de réception de 64 Ko ; un message plus gros est refusé (`EMSGSIZE`). `sendmsg` peut porter des descripteurs (`SCM_RIGHTS`) : un autre canal, une mémoire partagée. Un bout se donne à un programme lancé par `posix_spawn` (`posix_spawn_file_actions_adddup2`) : c'est ainsi que Jet parle à ses process WebKit.

Exemple : un thread de travail reçoit des commandes par un canal de paquets et répond par le même canal ; le thread principal attend la réponse avec `poll`.

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>

static int g_fd[2];

// le travailleur : chaque paquet reçu est une commande, il répond par un paquet
static void *worker (void *arg)
{
	(void) arg;
	char buf[256];
	for (;;)
	{
		ssize_t n = recv (g_fd[1], buf, sizeof buf - 1, 0);
		if (n <= 0)
			break;				// 0 : l'autre bout est fermé
		buf[n] = 0;
		char out[300];
		int k = snprintf (out, sizeof out, "reçu « %s » (%d octets)", buf, (int) n);
		send (g_fd[1], out, (size_t) k, 0);
	}
	return 0;
}

int main (void)
{
	if (socketpair (AF_UNIX, SOCK_SEQPACKET, 0, g_fd) < 0)
	{
		perror ("socketpair");
		return 1;
	}
	pthread_t th;
	pthread_create (&th, 0, worker, 0);
	const char *cmds[] = { "bonjour", "ouvrir document.txt", "fin" };
	for (int i = 0; i < 3; i++)
	{
		send (g_fd[0], cmds[i], strlen (cmds[i]), 0);
		struct pollfd p = { g_fd[0], POLLIN, 0 };
		if (poll (&p, 1, 2000) <= 0)		// 2 s au plus
		{
			printf ("pas de réponse\n");
			break;
		}
		char rep[300];
		ssize_t n = recv (g_fd[0], rep, sizeof rep - 1, 0);
		rep[n > 0 ? n : 0] = 0;
		printf ("%s\n", rep);
	}
	close (g_fd[0]);				// le travailleur voit la fin et s'arrête
	pthread_join (th, 0);
	return 0;
}
```

## Mémoire partagée

Un objet de mémoire partagée a des pages à lui, projetées dans chaque process qui l'ouvre : ce que l'un écrit, l'autre le voit aussitôt. `shm_open` le nomme (`/nom`, 55 octets au plus, 256 noms dans le système) ; `memfd_create` le crée sans nom, pour le passer par un canal. Il vit tant qu'un process le tient (descripteur ou projection) ou que son nom existe : `shm_unlink` retire le nom.

Exemple : le même programme écrit (`partage ecrire texte`) puis lit (`partage lire`) un tableau partagé ; lancez les deux depuis deux Terminaux.

```c
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

struct board				// ce que les deux process voient
{
	unsigned version;
	char text[1020];
};

int main (int argc, char **argv)
{
	if (argc < 2)
	{
		printf ("partage ecrire <texte> | lire | effacer\n");
		return 1;
	}
	if (!strcmp (argv[1], "effacer"))
		return shm_unlink ("/korin-tableau") < 0;
	int fd = shm_open ("/korin-tableau", O_RDWR | O_CREAT, 0600);
	if (fd < 0)
	{
		perror ("shm_open");
		return 1;
	}
	ftruncate (fd, sizeof (struct board));	// (sans effet s'il a déjà sa taille)
	struct board *b = (struct board *) mmap (0, sizeof (struct board), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close (fd);				// la projection garde l'objet
	if (b == MAP_FAILED)
	{
		perror ("mmap");
		return 1;
	}
	if (!strcmp (argv[1], "ecrire") && argc > 2)
	{
		snprintf (b->text, sizeof b->text, "%s", argv[2]);
		__atomic_add_fetch (&b->version, 1, __ATOMIC_RELEASE);
		printf ("écrit, version %u\n", b->version);
	}
	else
		printf ("version %u : %s\n", __atomic_load_n (&b->version, __ATOMIC_ACQUIRE), b->text);
	munmap (b, sizeof (struct board));
	return 0;
}
```

> **Remarque** : la mémoire partagée ne synchronise rien. Pour attendre l'autre process, lui envoyer un mot par un canal, ou un `SVC_SIGNAL` s'il est un service.

## Les appels du noyau

Ce que la couche POSIX utilise, pour qui écrit en assembleur ou a besoin d'un détail que POSIX cache. Une **poignée** (1 à 1023) désigne un bout de canal ou un objet partagé dans la table du process.

| N° | Appel | Arguments | Résultat |
|---|---|---|---|
| `0x70` | `HANDLE_CLOSE` | poignée | 0 |
| `0x71` | `HANDLE_INFO` | poignée, sortie de 64 octets | 0 : sorte, type, octets en attente ou taille, état, pid du créateur |
| `0x72` | `HANDLE_WAIT` | entrées {poignée 32 bits, événements 16, prêts 16}, nombre (64 au plus), attente ms (< 0 : sans fin) | nombre d'entrées prêtes (`POLLIN` 1, `POLLOUT` 4, `POLLERR` 8, `POLLHUP` 16) |
| `0x73` | `HANDLE_LIST` | sortie, place, 1 : marques effacées | les poignées reçues du père |
| `0x74` | `HANDLE_INHERIT` | entrées {poignée, marque}, nombre | 0 : le prochain process lancé les reçoit |
| `0x75` | `CHAN_CREATE` | `1` flux / `2` datagrammes / `5` paquets, sortie 2 × 32 bits | 0 : les deux bouts |
| `0x76` | `CHAN_SEND` | poignée, message (iovecs et poignées portées, 32 o), `1` sans attente, attente ms | octets envoyés |
| `0x77` | `CHAN_RECV` | poignée, message, `1` sans attente \| `2` sans retirer, attente ms | octets (0 : la fin) |
| `0x78` | `CHAN_SHUTDOWN` | poignée, `1` lecture \| `2` écriture | 0 |
| `0x79` | `SHM_CREATE` | taille, `1` scellable | poignée (`memfd_create`) |
| `0x7A` | `SHM_OPEN` | nom, `1` créer \| `2` exclusif \| `4` vider \| `8` nom faible | poignée |
| `0x7B` | `SHM_UNLINK` | nom | 0 |
| `0x7C` | `SHM_CTL` | poignée, `1` taille / `2` sceaux / `3` épingler / `4` désépingler, valeur | 0 |
| `0x7D` | `SHM_MAP` | poignée, adresse, taille, droits, drapeaux, décalage | adresse |
| `0x20`–`0x2F` | `FUTEX_WAIT`, `FUTEX_WAKE`... | adresse, valeur, attente | l'attente sur un mot de mémoire (pthread) |

Les détails (limites, verrous, ce qui se passe à la fin d'un process) sont dans `docs/ABI.md`, section `0x70`.

## Choisir

- **Un serveur unique** que plusieurs programmes interrogent (un pilote, un réglage du système) : un [service](service_fr.md).
- **Deux programmes liés** (une app et son process d'aide, un lecteur et son décodeur) : un canal créé par le parent, donné au fils par `posix_spawn`.
- **De gros tampons** (une image, des échantillons) : une mémoire partagée, annoncée par un message sur un canal ou par une requête au service.
- **Des threads d'un même programme** : `pthread_mutex`, `pthread_cond`, `sem_t`, et la mémoire du process elle-même.

## Voir aussi

- [Écrire un service](service_fr.md)
- [La libc et la couche POSIX](libc_fr.md)
