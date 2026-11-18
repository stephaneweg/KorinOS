# The Secret Vault

An app that keeps an API key, a token or a password does not write it to a file. It stores it in Korin's
**secret vault**, which encrypts it and gives it back to that same app only. The program never states its
identity: the vault checks it itself, from the path of the executable, or of the script for a Python, bash or C#
program.

```c
#include <stdio.h>
#include <string.h>
#include <keyvault.h>

int main (void)
{
	const char *key = "sk-example";
	if (kv_set ("api-key", key, (long) strlen (key)) < 0)
	{
		char e[256] = "";
		kv_error (e, sizeof e);
		printf ("vault: %s\n", e);
		return 1;
	}
	char k[256];
	long n = kv_get ("api-key", k, sizeof k - 1);	/* -> bytes; -2: no such secret */
	if (n >= 0) { k[n] = 0; printf ("key: %s\n", k); }
	return 0;
}
```

```
gcc -I/devtools/gcc/include/kits demo.c -o demo
```

| Function | Returns |
|---|---|
| `kv_set (name, value, bytes)` | 0 |
| `kv_get (name, buffer, size)` | the bytes (size 0: the length only); -2: no such secret |
| `kv_delete (name)` | 0 |
| `kv_list (buffer, size)` | this program's names, one per line |
| `kv_status ()` | 1 open, 0 locked, 2 not created yet; negative: no vault |
| `kv_error (buffer, size)` | the text of the last error |

- **Name**: letters, digits and `. _ - / @ :`, up to 128 characters.
- **Value**: up to 64 KiB.
- **Locked vault**: Korin asks for the user's password, and the function waits.

From another language:
- **asm, BASIC**: the same functions are in AppKit, named `vault_set`, `vault_get`, `vault_delete`,
  `vault_list`, `vault_status`, `vault_error`.
- **Scripts**: the `secret` command (`secret set name value`, `secret get name`) stores the secrets of the script
  that runs it.
