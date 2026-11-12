/* mapsvc - service d'essai de SVC_MAP_CALLER (lancé par maptest), repris de
 * mapsvc.asm. Une requête : op, a adresse, b taille, c valeur ou durée.
 *  1 lire : somme des octets de l'appelant
 *  2 écrire : remplit la zone de la valeur c -> taille
 *  3 tenir : mappe, dort c ms, remplit de 0x5A -> taille (l'en-tête de l'asm dit 0,
 *    son code rend la taille : gardé) (munmap, mort de l'appelant)
 *  4 pages : combien de pages (SVC_CALLER_PHYS), la première non nulle -> nombre
 * Une erreur de map_caller est rendue telle quelle. Commun au Pi et au PC. */
#include "korinrt.h"

#define MC_WRITE 1

static u64 msg[64];
static u64 table[300];

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long chan = korin_sys2(SYS_SVC_REGISTER, "mapsvc", 0);
	if (chan < 0)
		return 0;
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, chan, msg, 60000);
		if (req < 0)
			return 0;
		if (!req)
			continue;
		u64 op = msg[0], a = msg[1], b = msg[2], c = msg[3];
		long r = korin_sys4(SYS_SVC_MAP_CALLER, req, a, b, (op == 1 || op == 4) ? 0 : MC_WRITE);
		if (r >= 0) {
			volatile u8 *p = (volatile u8 *)r;
			if (op == 1) {
				u64 s = 0, i = 0;
				do /* (taille 0 : un octet lu, comme l'asm) */
					s += p[i++];
				while (i < b);
				r = (long)s;
			} else if (op == 2 || op == 3) {
				u8 v = (u8)c;
				if (op == 3) {
					korin_sys1(SYS_SLEEP, c);
					v = 0x5A;
				}
				u64 i = 0;
				do
					p[i++] = v;
				while (i < b);
				r = (long)b; /* (3 aussi : la taille, pas 0, comme l'asm) */
			} else { /* 4, et toute autre op (comme l'asm) */
				r = korin_sys3(SYS_SVC_CALLER_PHYS, r, table, 300);
				if (r >= 0 && !table[0])
					r = -1;
			}
		}
		msg[0] = (u64)r;
		korin_sys2(SYS_SVC_REPLY, req, msg);
	}
}
