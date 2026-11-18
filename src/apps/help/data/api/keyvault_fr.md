# Le coffre à secrets

Une application qui garde une clé d'API, un jeton ou un mot de passe ne l'écrit pas dans un fichier : elle le
range dans le **coffre à secrets** de Korin. Le coffre le chiffre et ne le rend qu'à cette même application. Le
programme ne donne jamais son identité ; le coffre la vérifie lui-même (le chemin de l'exécutable, ou du script
pour un programme en Python, bash ou C#).

```c
#include <stdio.h>
#include <string.h>
#include <keyvault.h>

int main (void)
{
	const char *key = "sk-exemple";
	if (kv_set ("api-key", key, (long) strlen (key)) < 0)
	{
		char e[256] = "";
		kv_error (e, sizeof e);
		printf ("coffre : %s\n", e);
		return 1;
	}
	char k[256];
	long n = kv_get ("api-key", k, sizeof k - 1);	/* -> octets ; -2 : pas de tel secret */
	if (n >= 0) { k[n] = 0; printf ("clé : %s\n", k); }
	return 0;
}
```

```
gcc -I/devtools/gcc/include/kits demo.c -o demo
```

| Fonction | Rend |
|---|---|
| `kv_set (nom, valeur, octets)` | 0 |
| `kv_get (nom, tampon, taille)` | les octets (taille 0 : la longueur seule) ; -2 : pas de tel secret |
| `kv_delete (nom)` | 0 |
| `kv_list (tampon, taille)` | les noms de ce programme, un par ligne |
| `kv_status ()` | 1 ouvert, 0 fermé, 2 pas encore créé ; négatif : pas de coffre |
| `kv_error (tampon, taille)` | le texte de la dernière erreur |

- **Nom** : lettres, chiffres et `. _ - / @ :`, 128 caractères au plus.
- **Valeur** : 64 Kio au plus.
- **Coffre fermé** : Korin demande le mot de passe de l'utilisateur et la fonction attend.

Depuis un autre langage :
- **asm, BASIC** : les mêmes fonctions sont dans AppKit, sous les noms `vault_set`, `vault_get`, `vault_delete`,
  `vault_list`, `vault_status`, `vault_error`.
- **Scripts** : la commande `secret` (`secret set nom valeur`, `secret get nom`) range les secrets du script qui
  la lance.
