/* uk_undo.c -- UIKit : annuler / rétablir (texte multiligne). Port ligne à
 * ligne de uikit_undo.inc (src/kits/uikit/PORT.md).
 *
 * Un journal par widget (UW_UNDO) : UND_N opérations de 32 octets (genre 1
 * insertion / 2 suppression, position, longueur, octets copiés) ; « top » =
 * opérations appliquées, au-delà : celles à rétablir (effacées par une
 * nouvelle modification). La frappe d'un mot se fond en une opération, les
 * Retour arrière / Suppr successifs aussi. Les modifications passent toutes
 * par uk_tb_insert et uk_tb_delete, qui appellent uk_undo_rec. */
#include "uk.h"

/* adresse de l'opération i du journal */
#define UND_AT(log, i) ((log) + UND_REC + LSL((i), 5))

/* x0 = widget, x1 = genre, x2 = octets (dans son texte), x3 = longueur */
long uk_undo_rec(long w, long kind, long data, long len)
{
	return uk_undo_rec_at(w, kind, data, len, data - LDX(w, UW_TEXT));
}

/* x0 = widget, x1 = genre, x2 = octets, x3 = longueur, x4 = position (l'asm :
 * x4, laissé par uk_undo_rec ou posé par uk_tb_insert) */
long uk_undo_rec_at(long w, long kind, long data, long len, long pos)
{
	long log, top, n, rec, first, nb, e;

	if (!len)
		return 0;
	if (uk.undo_busy)
		return 0;
	if (LDW(w, UW_TYPE) != UT_AREA)
		return 0;
	log = LDX(w, UW_UNDO);
	if (!log) {
		log = uk_alloc(UND_REC + UND_N * 32);
		if (!log)
			return 0;
		STX(w, UW_UNDO, log);
	}
	top = LDX(log, UND_TOP); /* ce qui était à rétablir : oublié */
	for (;;) {
		n = LDX(log, UND_COUNT);
		if (ULE(n, top))
			break;
		n--;
		STX(log, UND_COUNT, n);
		uk_free(LDX(UND_AT(log, n), UR_DATA));
	}
	if (top) { /* fondre avec la précédente ? */
		rec = UND_AT(log, top - 1);
		if (LDX(rec, UR_KIND) != kind)
			goto new_op;
		if (UGT(len, 4)) /* (un caractère tapé, effacé) */
			goto new_op;
		if (UGE(LDX(rec, UR_LEN), 200))
			goto new_op;
		if (LDB(data, 0) == 10) /* (une fin de ligne coupe) */
			goto new_op;
		if (kind == 1) {
			/* insertion juste après : ancien + nouveau */
			if (LDX(rec, UR_POS) + LDX(rec, UR_LEN) != pos)
				goto new_op;
			first = 1;
		} else if (pos + len == LDX(rec, UR_POS)) {
			/* Retour arrière : nouveau + ancien (comme l'asm : la position
			 * change avant l'allocation, même si elle échoue) */
			STX(rec, UR_POS, pos);
			first = 0;
		} else if (pos == LDX(rec, UR_POS)) {
			first = 1; /* Suppr : ancien + nouveau */
		} else {
			goto new_op;
		}
		nb = uk_alloc(LDX(rec, UR_LEN) + len);
		if (!nb)
			return 0;
		if (first) {
			e = uk_memcpy(nb, LDX(rec, UR_DATA), LDX(rec, UR_LEN));
			uk_memcpy(e, data, len);
		} else {
			e = uk_memcpy(nb, data, len);
			uk_memcpy(e, LDX(rec, UR_DATA), LDX(rec, UR_LEN));
		}
		uk_free(LDX(rec, UR_DATA));
		STX(rec, UR_DATA, nb);
		STX(rec, UR_LEN, LDX(rec, UR_LEN) + len);
		return 0;
	}
new_op:
	if (!ULT(LDX(log, UND_COUNT), UND_N)) { /* plein : la plus ancienne part */
		uk_free(LDX(log, UND_REC + UR_DATA));
		uk_memcpy(log + UND_REC, log + UND_REC + 32, (UND_N - 1) * 32);
		STX(log, UND_COUNT, UND_N - 1);
		STX(log, UND_TOP, UND_N - 1);
	}
	nb = uk_alloc(len);
	if (!nb)
		return 0;
	uk_memcpy(nb, data, len);
	top = LDX(log, UND_TOP);
	rec = UND_AT(log, top);
	STX(rec, UR_KIND, kind);
	STX(rec, UR_POS, pos);
	STX(rec, UR_LEN, len);
	STX(rec, UR_DATA, nb);
	STX(log, UND_TOP, top + 1);
	STX(log, UND_COUNT, top + 1);
	return 0;
}

/* x0 = widget : oublie son journal (nouveau texte, fin du widget) */
long uk_undo_clear(long w)
{
	long log = LDX(w, UW_UNDO), n;

	if (!log)
		return 0;
	for (;;) {
		n = LDX(log, UND_COUNT);
		if (!n)
			break;
		n--;
		STX(log, UND_COUNT, n);
		uk_free(LDX(UND_AT(log, n), UR_DATA));
	}
	STX(log, UND_TOP, 0);
	return 0;
}

/* x0 = widget, x1 = 0 annuler / 1 rétablir -> 1 si fait */
long uk_undo_step(long w, long redo)
{
	long log = LDX(w, UW_UNDO), top, rec;

	if (!log)
		return 0;
	top = LDX(log, UND_TOP);
	if (!redo) {
		if (!top)
			return 0;
		top--; /* annuler la dernière */
		STX(log, UND_TOP, top);
	} else {
		if (UGE(top, LDX(log, UND_COUNT)))
			return 0;
		STX(log, UND_TOP, top + 1);
	}
	rec = UND_AT(log, top);
	uk.undo_busy = 1;
	/* insertion annulée / suppression rétablie : on retire ; sinon on remet */
	if (((long)(LDX(rec, UR_KIND) == 1) ^ redo) != 0) {
		uk_tb_delete(w, LDX(rec, UR_POS), LDX(rec, UR_POS) + LDX(rec, UR_LEN));
	} else {
		STX(w, UW_VALUE, LDX(rec, UR_POS));
		STX(w, UW_ANCHOR, LDX(rec, UR_POS));
		uk_tb_insert(w, LDX(rec, UR_DATA), LDX(rec, UR_LEN));
	}
	uk.undo_busy = 0;
	return 1;
}
