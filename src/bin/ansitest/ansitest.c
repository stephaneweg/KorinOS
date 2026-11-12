/* ansitest - essai du Terminal VT : écrit chaque famille de séquences comprise
 * (docs/TERMINAL-ET-FORK.md 1.4), avec son libellé. À comparer à xterm ; aucun
 * ESC ne doit s'afficher. Comme ansitest.asm :
 *	ansitest		tout
 *	ansitest -n		sans la page de l'écran alternatif (ni attente)
 * Seul le premier argument compte (« -n » exactement ; tout autre mot : tout) ;
 * sortie tamponnée (t_puts), vidée avant chaque attente ; code 0. */
#include "korinrt.h"

#define E "\033" /* (toujours une chaîne à part : « \0337 » serait un octet) */

static const char s_part1[] =
	"ansitest : chaque ligne dit ce qu'elle doit montrer ; aucun ESC ne doit s'afficher.\n"
	"\n" E "[1m== 16 couleurs (30-37, 90-97 ; fond 40-47, 100-107)" E "[0m\n"
	E "[30m noir" E "[31m rouge" E "[32m vert" E "[33m jaune" E "[34m bleu"
	E "[35m magenta" E "[36m cyan" E "[37m blanc" E "[39m (defaut)\n"
	E "[90m noir" E "[91m rouge" E "[92m vert" E "[93m jaune" E "[94m bleu"
	E "[95m magenta" E "[96m cyan" E "[97m blanc" E "[39m (vifs)\n"
	E "[40m 0 " E "[41m 1 " E "[42m 2 " E "[43m 3 " E "[44m 4 " E "[45m 5 "
	E "[46m 6 " E "[47;30m 7 " E "[49;39m fonds 40-47\n"
	E "[100m 8 " E "[101m 9 " E "[102m10 " E "[103m11 " E "[104m12 " E "[105m13 "
	E "[106m14 " E "[107;30m15 " E "[m fonds 100-107\n"
	"\n" E "[1m== Attributs" E "[0m\n"
	E "[1mgras" E "[22m " E "[2matténu" E "[22m " E "[3mitalique (droit)" E "[23m "
	E "[4msouligné" E "[24m " E "[7minverse" E "[27m " E "[9mbarré" E "[29m "
	"[" E "[8mcaché" E "[28m] (vide entre crochets)\n"
	E "[1;31mgras rouge = rouge vif" E "[0m " E "[1;4;7;32m gras souligne inverse vert " E "[0m\n";

static const char s_256[] = "\n" E "[1m== 256 couleurs (48;5;n : cube 16-231, gris 232-255)" E "[0m\n";
static const char s_bg5[] = E "[48;5;";
static const char s_bg2[] = E "[48;2;";
static const char s_rnl[] = E "[0m\n";
static const char s_tc[] = "\n" E "[1m== 24 bits (48;2;r;g;b : rouge, vert, bleu)" E "[0m\n";

static const char s_part2[] =
	"\n" E "[1m== Couleur du texte 38;5 et 38;2" E "[0m\n"
	E "[38;5;208morange 208" E "[39m " E "[38;5;33mbleu 33" E "[39m "
	E "[38;2;255;105;180mrose 24 bits" E "[39m " E "[38:2:0:200:200mcyan (deux-points)" E "[0m\n"
	"\n" E "[1m== Curseur" E "[0m\n"
	"ABCDEFGH" E "[5Dx" E "[2Cy\r" E "[30G<- CUB, CUF, CR, CHA : ABCxEFyH\n"
	".........." E "[A" E "[10D" E "[B\r1" E "[3C2" E "[20G<- 1...2..... (CUU, CUB, CUD)\n"
	E "7" "sauve" E "[3G!!" E "8" E "[C" E "[C" E "[C" E "[C" E "[C|  <- sa!!e| (ESC 7 / 8)\n"
	E "[s" "12345" E "[u" "ab" E "[30G<- ab345 (CSI s / u)\n"
	"\n" E "[1m== Effacement" E "[0m\n"
	"XXXXXXXXXXXXXXX" E "[8D" E "[K" E "[30G<- 7 X (EL 0)\n"
	"XXXXXXXXXXXXXXX" E "[8D" E "[1K" E "[30G<- 8 blancs puis 7 X (EL 1)\n"
	"XXXXXXXXXXXXXXX" E "[2K" E "[30G<- rien (EL 2)\n"
	"abcdefgh" E "[6D" E "[2P" E "[30G<- abefgh (DCH)\n"
	"abcdefgh" E "[6D" E "[2@" E "[30G<- ab  cdefgh (ICH)\n"
	"abcdefgh" E "[6D" E "[3X" E "[30G<- ab   fgh (ECH)\n"
	"=" E "[9b" E "[30G<- 10 = (REP)\n"
	E "[41m" E "[K" E "[0m" "fond rouge jusqu'au bord (EL avec le fond courant)\n"
	"\n" E "[1m== Séquences inconnues (rien ne doit apparaître entre les crochets)" E "[0m\n"
	"[" E "[99;99z" E "]0;titre\007" E "]8;;http://x" E "\\" E "P1$r" E "\\"
	E "[?9999h" E "(B" E "#8" E "[>4;1m" E "[ q" E "[6n" E "[c" E "=" E ">" "]\n"
	"[" E "[?2004h" E "[?2004l" E "[?1h" E "[?1l" "]  <- collage encadré, mode des flèches : notés, rien d'affiché\n"
	"\n" E "[1m== UTF-8 (une cellule par caractère)" E "[0m\n"
	"éàüç → ✓ ──┐"
	" λπ <- éàüç, flèche, coche, bord de boîte, lambda pi\n";

static const char s_alt[] =
	"\n" E "[1m== Écran alternatif (quelques secondes), zone de défilement" E "[0m\n"
	E "[?1049h" E "[?25l" E "[H" E "[2J"
	E "[1;1H" E "[7m Écran alternatif (?1049h) : curseur caché (?25l) " E "[0m"
	E "[3;5HCUP 3;5" E "[4;20HCUP 4;20"
	E "[5;1H" E "[33m---- zone de défilement : lignes 6 à 10 (CSI 6;10 r) ----" E "[0m"
	E "[11;1H" E "[33m---- fin de la zone ----" E "[0m"
	E "[12;1Hcette ligne ne doit pas bouger"
	E "[6;10r" E "[10;1H";
static const char s_scl[] = "\nligne ";
static const char s_alt2[] =
	E "[r" E "[13;1H" E "[32mIL/DL : " E "[0m"
	E "[14;1Ha" E "[15;1Hb" E "[16;1Hc" E "[15;1H" E "[L"
	E "[15;5H<- ligne insérée (IL) entre a et b" E "[17;1H" E "[M" E "[17;1H(c supprimée par DL)"
	E "[18;1Hretour à l'écran principal dans 4 s : le texte d'avant doit revenir intact";
static const char s_altq[] =
	E "[?25h" E "[?1049l" E "[1m== Retour de l'écran alternatif (?1049l) : tout ce qui précède est intact" E "[0m\n";

int main(int argc, char **argv)
{
	long quick = 0; /* -n */
	if (argc >= 2)
		quick = str_eq(argv[1], "-n");
	out_puts(s_part1); /* 16 couleurs, attributs */
	out_puts(s_256);   /* 256 couleurs : 36 par ligne (cube), puis les gris */
	for (u64 i = 16; i < 256; i++) {
		out_puts(s_bg5);
		out_putn(i);
		out_puts("m  ");
		u64 r = (i - 15) % 36;
		if (i > 231 ? i == 255 : r == 0)
			out_puts(s_rnl);
	}
	out_puts(s_tc); /* 24 bits : trois dégradés */
	for (u64 c = 0; c < 3; c++) {
		for (u64 i = 0; i < 64; i++) {
			u64 v = i << 2; /* 0..252 */
			out_puts(s_bg2);
			out_putn(c == 0 ? v : 0);
			out_putc(';');
			out_putn(c == 1 ? v : 0);
			out_putc(';');
			out_putn(c == 2 ? v : 0);
			out_puts("m ");
		}
		out_puts(s_rnl);
	}
	out_puts(s_part2); /* curseur, effacement, inconnues, UTF-8 */
	if (!quick) {
		out_puts(s_alt); /* écran alternatif, zone de défilement */
		out_flush();
		sys_sleep(1500);
		for (u64 k = 1; k <= 12; k++) { /* la zone (lignes 6 à 10) défile */
			out_puts(s_scl);
			out_putn(k);
			out_flush();
			sys_sleep(250);
		}
		out_puts(s_alt2);
		out_flush();
		sys_sleep(4000);
		out_puts(s_altq);
	}
	out_puts("fin de ansitest\n");
	return 0;
}
