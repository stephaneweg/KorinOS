/* nslookup - l'adresse d'un nom, par le serveur DNS de netd (docs/OUTILS.md ;
 * comme nslookup.asm) : nslookup NOM. Le serveur est celui du bail DHCP (une
 * adresse donnée en nom est rendue telle quelle, « localhost » : 127.0.0.1).
 * Usage (code 2) si le nombre de mots n'est pas 1 ou si le nom commence par
 * « - » ; pas de réseau ou nom introuvable : code 1. */
#include "korinrt.h"
#include "netkit_import.h"

/* texte : traduit, puis des espaces jusqu'à la colonne 12 (aucune au-delà) */
static void say_col(const char *s)
{
	s = lang_tr(s);
	out_puts(s);
	for (u64 n = (u64)str_len(s); n < 12; n++)
		out_putc(' ');
}

int main(int argc, char **argv)
{
	if (argc != 2 || argv[1][0] == '-') {
		err_say("usage : nslookup NOM\n");
		return 2;
	}
	const char *name = argv[1];
	if (!nt_network()) {
		err_say("nslookup : pas de réseau\n");
		return 1;
	}
	u32 ni[12];
	net_info((long)ni);
	say_col("Serveur :");
	if (ni[NI_DNS / 4])
		nt_put_ip(ni[NI_DNS / 4]);
	else
		out_say("(aucun)");
	out_nl();
	long a = net_resolve((long)name);
	if (a < 0) {
		err_say("** introuvable : ");
		err_puts(name);
		err_puts(" (");
		nt_err_text(a);
		err_puts(")\n");
		return 1;
	}
	say_col("Nom :");
	out_puts(name);
	out_nl();
	say_col("Adresse :");
	nt_put_ip((u32)a);
	out_nl();
	return 0;
}
