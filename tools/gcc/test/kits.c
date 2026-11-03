/* essai des kits depuis le C sous ARMPI (docs/GCC.md) : gcc kits.c -luikit -laudiokit -lnetkit */
#include <stdio.h>
#include <uikit.h>
#include <audiokit_abi.h>
#include <netkit_abi.h>

int main (void)
{
	int ui = uikit_kit_open (), ak = audiokit_kit_open (), nk = netkit_kit_open ();
	char ip[32];
	if (nk)
		fmt_ip (0x0A000202, (long) ip);
	printf ("uikit %d (%d fonctions), audiokit %d, netkit %d, %s\n", ui, UIKIT_KIT_FUNCTIONS, ak, nk,
		nk ? ip : "?");
	printf ("%s\n", ui && ak && nk ? "KITS OK" : "KITS ABSENTS");
	return 0;
}
