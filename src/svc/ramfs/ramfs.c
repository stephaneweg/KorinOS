/* ramfs - service de fichiers en mémoire (docs/MICRO-NOYAU.md, section 4),
 * traduction de ramfs.asm : un volume /tmp (monté par vfs : « ramfs <volume>
 * ram:<taille> »), une arborescence de répertoires et de fichiers, perdue à
 * l'arrêt. Il s'enregistre sous « fs.<volume> » et répond au protocole de
 * src/inc/fs.inc.
 *  * Un fichier ouvert : une entrée de la table « ouverts » (son numéro est
 *    rendu à l'ouverture) : son nœud et sa position ; un ou plusieurs
 *    propriétaires (après un fork) ; libérée avec son dernier propriétaire.
 *    Propriétaires, accès et tampons partagés : fsowner.c.
 *  * Position : b = FS_POS_CUR dans les ops 10, 11, 15, 16 lit ou écrit à la
 *    position du fichier ouvert et l'avance ; une position explicite n'y
 *    touche pas ; d rendu = position après l'op. Op 26 (FS_SEEK) la change.
 *  * Le contenu d'un fichier est une région du noyau (VM_MAP, à la demande),
 *    rendue quand le fichier grandit, est vidé ou effacé : le volume compte ce
 *    qui existe. Limite : NOEUDS nœuds pour tout le volume. Recherche par
 *    parcours de la table, comme l'asm. */
#include "fsowner.h"

#define NOEUDS 4096
#define NOM_MAX 63
#define OUV_MAX 64

struct node {
	char name[64];
	u64 parent;
	u64 attr; /* 0 : libre, ATTR_DIR, ATTR_FILE */
	u64 size;
	u8 *data;
	u64 capa; /* place réservée */
	u64 mtime; /* dernière écriture (secondes UTC) */
	u64 pad[3];
};
struct ouvert {
	u64 node;
	u64 owner; /* pid qui l'a ouvert (0 : entrée libre) */
	i64 pos;   /* position (partagée après un fork) */
	u64 pad;
};

static u64 limite = 16 << 20; /* taille du volume (« ram:16M ») */
static u64 utilise;
static char nom_svc[32];
static struct msg msg __attribute__((aligned(16)));
static struct ouvert ouverts[OUV_MAX];
static u8 ajouts[OUV_MAX]; /* par entrée : ouverte en O_APPEND */
static struct node noeuds[NOEUDS];

/* ---- nœuds */

/* parent, nom (fin : 0 ou « / »), longueur -> l'enfant de ce nom (-1 : aucun) */
static long enfant(u64 parent, const char *name, u64 len)
{
	for (long i = 1; i < NOEUDS; i++) { /* (la racine n'est l'enfant de personne) */
		struct node *n = &noeuds[i];
		if (!n->attr || n->parent != parent)
			continue;
		u64 k = 0;
		while (k < len && n->name[k] == name[k])
			k++;
		if (k == len && !n->name[k])
			return i;
	}
	return -1;
}

struct found {
	long node;        /* -1 : absent */
	long parent;      /* parent du dernier composant (-1 : chemin intermédiaire absent) */
	const char *last; /* dernier composant */
	u64 len;          /* sa longueur */
};

/* chemin relatif (« a/b », « » : racine) */
static struct found chercher(const char *p)
{
	struct found f = {0, -1, p, 0};
	if (!*p)
		return f;
	for (;;) {
		u64 len = 0;
		while (p[len] && p[len] != '/')
			len++;
		if (!p[len]) {
			f.last = p;
			f.len = len;
			if (len) { /* (« a/ ») */
				f.parent = f.node;
				f.node = enfant(f.node, p, len);
			}
			return f;
		}
		if (len) { /* (« // ») */
			long c = enfant(f.node, p, len);
			if (c < 0) {
				f.node = f.parent = -1;
				f.last = p;
				f.len = len;
				return f;
			}
			f.node = c;
		}
		p += len + 1;
	}
}

static long creer(u64 parent, const char *name, u64 len, u64 attr)
{
	if (!len || len > NOM_MAX)
		return E_INVAL;
	for (long i = 1; i < NOEUDS; i++) {
		struct node *n = &noeuds[i];
		if (n->attr)
			continue;
		n->parent = parent;
		n->attr = attr;
		n->size = 0;
		n->data = 0;
		n->capa = 0;
		n->mtime = korin_sys1(SYS_CLOCK_GET, 0);
		memcpy(n->name, name, len);
		n->name[len] = 0;
		return i;
	}
	return E_NOSPC;
}

/* le contenu d'un nœud rendu au noyau, le volume mis à jour */
static void liberer(struct node *n)
{
	u8 *d = n->data;
	if (!d)
		return;
	u64 capa = n->capa;
	utilise -= capa;
	n->data = 0;
	n->capa = 0;
	korin_sys2(SYS_VM_UNMAP, d, capa);
}

static long agrandir(struct node *n, u64 want)
{
	if (want <= n->capa)
		return 0;
	u64 c = n->capa << 1; /* le double, 4 Ko au moins (en pages) */
	if (c < want)
		c = want;
	c = (c + 0xFFF) & ~(u64)0xFFF;
	u64 used = utilise - n->capa + c;
	if (used > limite)
		return E_NOSPC;
	long v = korin_sys4(SYS_VM_MAP, 0, c, PROT_READ | PROT_WRITE, 0);
	if (v < 0)
		return E_NOSPC;
	if (n->data) {
		memmove((void *)v, n->data, n->size);
		liberer(n);
	}
	n->data = (u8 *)v;
	n->capa = c;
	utilise = used;
	return 0;
}

static void dater(struct node *n)
{
	n->mtime = korin_sys1(SYS_CLOCK_GET, 0);
}

/* ---- opérations (message -> résultat) */

static long ouvert_libre(void)
{
	for (long i = 0; i < OUV_MAX; i++)
		if (!ouverts[i].owner)
			return i;
	return -1;
}

/* 1 ouvrir(a drapeaux, chemin) -> b numéro, c attributs, d taille */
static long op_ouvrir(struct msg *m)
{
	open_extras_take(m);
	struct found f = chercher((char *)m->data);
	long node = f.node;
	if (node < 0) {
		if (!(m->a & O_CREATE) || f.parent < 0)
			return E_NOENT;
		if (noeuds[f.parent].attr != ATTR_DIR)
			return E_NOTDIR;
		node = creer(f.parent, f.last, f.len, ATTR_FILE);
		if (node < 0)
			return node;
	}
	struct node *n = &noeuds[node];
	long e = ouvert_libre(); /* une entrée (prise à la fin) */
	if (e < 0)
		return E_MFILE;
	if (n->attr == ATTR_DIR) {
		if (m->a & O_WRITE) /* répertoire : lecture seule */
			return E_ISDIR;
	} else if (m->a & O_TRUNC) {
		n->size = 0;
		liberer(n);
		dater(n);
	}
	long owner = proprietaire(m);
	fo_own_clear(e);
	if (!fo_own_add(e, owner))
		return E_MFILE;
	ouverts[e].node = node;
	ouverts[e].owner = owner;
	m->b = e;
	ajouts[e] = (m->a & O_APPEND) != 0; /* O_APPEND : écrit à la fin réelle */
	ouverts[e].pos = ajouts[e] ? (i64)n->size : 0;
	m->c = n->attr;
	m->d = n->size;
	open_extras_put(m); /* a : notre canal (vfs transmet) */
	return 0;
}

/* 2 stat(chemin) -> b taille, c attributs, d date */
static long op_stat(struct msg *m)
{
	struct found f = chercher((char *)m->data);
	if (f.node < 0)
		return E_NOENT;
	struct node *n = &noeuds[f.node];
	m->b = n->size;
	m->c = n->attr;
	m->d = n->mtime;
	return 0;
}

/* 5 utime(b secondes UTC ; chemin) */
static long op_utime(struct msg *m)
{
	struct found f = chercher((char *)m->data);
	if (f.node < 0)
		return E_NOENT;
	noeuds[f.node].mtime = m->b;
	return 0;
}

/* 6 rename(ancien, 0, nouveau, 0) : le nœud change de parent et de nom ; un
 * fichier à l'arrivée est remplacé */
static long op_rename(struct msg *m)
{
	const char *p = (char *)m->data;
	struct found f = chercher(p);
	if (f.node < 0)
		return E_NOENT;
	if (!f.node) /* la racine */
		return E_INVAL;
	long src = f.node;
	while (*p++)
		;
	struct found t = chercher(p);
	if (t.parent < 0)
		return E_NOENT;
	if (!t.len || t.len > NOM_MAX)
		return E_INVAL;
	if (noeuds[t.parent].attr != ATTR_DIR)
		return E_NOTDIR;
	for (u64 x = t.parent; x; x = noeuds[x].parent) /* un répertoire : pas dans lui-même */
		if ((long)x == src)
			return E_INVAL;
	if (t.node >= 0) {
		if (t.node == src)
			return 0;
		struct node *c = &noeuds[t.node];
		if (c->attr == ATTR_DIR)
			return E_EXIST;
		c->attr = 0;
		liberer(c);
	}
	struct node *n = &noeuds[src];
	n->parent = t.parent;
	memcpy(n->name, t.last, t.len);
	n->name[t.len] = 0;
	return 0;
}

/* 3 mkdir(chemin) */
static long op_mkdir(struct msg *m)
{
	struct found f = chercher((char *)m->data);
	if (f.node >= 0)
		return E_EXIST;
	if (f.parent < 0)
		return E_NOENT;
	if (noeuds[f.parent].attr != ATTR_DIR)
		return E_NOTDIR;
	long r = creer(f.parent, f.last, f.len, ATTR_DIR);
	return r < 0 ? r : 0;
}

/* 4 unlink(chemin) : fichier, ou répertoire vide */
static long op_unlink(struct msg *m)
{
	struct found f = chercher((char *)m->data);
	if (f.node < 0)
		return E_NOENT;
	if (!f.node) /* la racine */
		return E_INVAL;
	struct node *n = &noeuds[f.node];
	if (n->attr == ATTR_DIR)
		for (long i = 0; i < NOEUDS; i++) /* un répertoire : vide ? */
			if (noeuds[i].attr && noeuds[i].parent == (u64)f.node)
				return E_NOTEMPTY;
	n->attr = 0;
	liberer(n);
	return 0;
}

/* message -> entrée ouverte (numéro en a) si l'appelant y a droit, *np = son
 * nœud ; E_INVAL (numéro libre, nœud effacé) ou E_BADF */
static long ouvert_de(struct msg *m, struct node **np)
{
	u64 num = m->a;
	if (num >= OUV_MAX || !ouverts[num].owner)
		return E_INVAL;
	long r = fo_access(m, num);
	if (r)
		return r;
	struct node *n = &noeuds[ouverts[num].node];
	if (!n->attr)
		return E_INVAL;
	*np = n;
	return num;
}

/* -> numéro de l'entrée si c'est un fichier, sinon erreur */
static long fichier_de(struct msg *m, struct node **np)
{
	long e = ouvert_de(m, np);
	if (e < 0)
		return e;
	return (*np)->attr == ATTR_FILE ? e : E_INVAL;
}

/* 12 fermer(a numéro) -> 0 (avis du noyau : fermé pour tous ; appel d'un
 * propriétaire : il le quitte, fermé avec le dernier) */
static long op_fermer(struct msg *m)
{
	u64 num = m->a; /* (nœud effacé depuis : l'entrée est libérée quand même) */
	if (num >= OUV_MAX || !ouverts[num].owner)
		return E_INVAL;
	long r = fo_access(m, num);
	if (r)
		return r;
	if (!fo_own_close(m, num))
		return 0;
	ouverts[num].node = ouverts[num].owner = 0;
	return 0;
}

static void ouvert_liberer(long num)
{
	ouverts[num].node = ouverts[num].owner = 0;
}

static void fin_pid(long pid)
{
	fo_own_end(pid, ouvert_liberer);
	tampons_fin(pid);
}

/* nœud, position, octets voulus, destination -> octets lus (bornés par la taille) */
static long lire_noeud(struct node *n, u64 pos, u64 len, void *dst)
{
	if (pos >= n->size)
		return 0;
	if (len > n->size - pos)
		len = n->size - pos;
	memmove(dst, n->data + pos, len);
	return len;
}

/* nœud, position, octets, source -> octets écrits, ou E_NOSPC ; *size = nouvelle taille */
static long ecrire_noeud(struct node *n, u64 pos, u64 len, const void *src, u64 *size)
{
	u64 end = pos + len;
	long r = agrandir(n, end);
	if (r < 0)
		return r;
	for (u64 i = n->size; i < pos; i++) /* un trou avant ? (à zéro) */
		n->data[i] = 0;
	memmove(n->data + pos, src, len);
	if (end > n->size)
		n->size = end;
	*size = n->size;
	dater(n);
	return len;
}

/* position de l'op : b, ou celle du fichier (b = FS_POS_CUR) */
static i64 position_lire(struct msg *m, long e)
{
	return m->b == FS_POS_CUR ? ouverts[e].pos : m->b;
}

/* d = position après ; celle du fichier avancée (b = FS_POS_CUR) */
static void position_apres(struct msg *m, long e, long done, i64 pos)
{
	if (done < 0)
		return;
	pos += done;
	m->d = pos;
	if (m->b == FS_POS_CUR)
		ouverts[e].pos = pos;
}

/* la fin du fichier si l'entrée est en ajout */
static i64 position_ecrire(struct msg *m, struct node *n, i64 pos)
{
	return ajouts[m->a] ? (i64)n->size : pos;
}

/* 10 lire(a numéro, b position, c octets) -> octets (données), d position après */
static long op_lire(struct msg *m)
{
	struct node *n;
	long e = fichier_de(m, &n);
	if (e < 0)
		return e;
	i64 pos = position_lire(m, e);
	u64 len = (u64)m->c > FS_DATA_MAX ? FS_DATA_MAX : (u64)m->c;
	long r = lire_noeud(n, pos, len, m->data);
	m->len = (u32)r;
	position_apres(m, e, r, pos);
	return r;
}

/* 11 écrire(a numéro, b position, c octets, données) -> octets, b taille, d position après */
static long op_ecrire(struct msg *m)
{
	struct node *n;
	long e = fichier_de(m, &n);
	if (e < 0)
		return e;
	if ((u64)m->c > FS_DATA_MAX)
		return E_INVAL;
	i64 pos = position_ecrire(m, n, position_lire(m, e));
	u64 size;
	long r = ecrire_noeud(n, pos, m->c, m->data, &size);
	if (r < 0)
		return r;
	position_apres(m, e, r, pos); /* (avant b : il y lit FS_POS_CUR) */
	m->b = size;
	return r;
}

/* 15 lire_t, 16 écrire_t (a numéro, b position, c octets) : au début du tampon */
static long rw_t(struct msg *m, int write)
{
	struct node *n;
	long e = fichier_de(m, &n);
	if (e < 0)
		return e;
	u8 *buf = tampon_de(m); /* le tampon de l'appelant */
	if (!buf || (u64)m->c > FS_BUFFER_SIZE)
		return E_INVAL;
	i64 pos = position_lire(m, e);
	if (!write) {
		long r = lire_noeud(n, pos, m->c, buf);
		position_apres(m, e, r, pos);
		return r;
	}
	pos = position_ecrire(m, n, pos);
	u64 size;
	long r = ecrire_noeud(n, pos, m->c, buf, &size);
	if (r < 0)
		return r;
	position_apres(m, e, r, pos);
	m->b = size;
	return r;
}

static long op_lire_t(struct msg *m) { return rw_t(m, 0); }
static long op_ecrire_t(struct msg *m) { return rw_t(m, 1); }

/* 26 position(a numéro, b décalage, c origine 0 début, 1 position, 2 fin)
 * -> nouvelle position (aussi en b) ; E_INVAL si elle serait négative */
static long op_position(struct msg *m)
{
	struct node *n;
	long e = fichier_de(m, &n);
	if (e < 0)
		return e;
	u64 org = m->c;
	if (org > 2)
		return E_INVAL;
	i64 base = org == 1 ? ouverts[e].pos : org == 2 ? (i64)n->size : 0;
	i64 p = (i64)((u64)base + (u64)m->b);
	if (p < 0)
		return E_INVAL;
	ouverts[e].pos = p;
	m->b = p;
	return p;
}

/* 13 lire_rep(a numéro, b rang) -> 1 (données : nom[256], taille, attributs), 0 en fin */
static long op_lire_rep(struct msg *m)
{
	struct node *n;
	long e = ouvert_de(m, &n);
	if (e < 0)
		return e;
	u64 dir = ouverts[e].node, rang = m->b;
	for (long i = 1; i < NOEUDS; i++) {
		struct node *c = &noeuds[i];
		if (!c->attr || c->parent != dir)
			continue;
		if (rang--)
			continue;
		memcpy(m->data, c->name, NOM_MAX + 1);
		u64 *t = (u64 *)(m->data + 256);
		t[0] = c->size;
		t[1] = c->attr;
		m->len = 272;
		return 1;
	}
	return 0;
}

/* 19 info -> a octets du volume, b octets libres, c 0, d FS_T_RAM */
static long op_info(struct msg *m)
{
	m->a = limite;
	m->b = limite < utilise ? 0 : limite - utilise;
	m->c = 0;
	m->d = FS_T_RAM;
	m->data[0] = 0;
	return 0;
}

static const struct {
	long op;
	long (*fn)(struct msg *);
} ops[] = {
	{FS_OPEN, op_ouvrir},   {FS_STAT, op_stat},         {FS_MKDIR, op_mkdir},
	{FS_UNLINK, op_unlink}, {FS_READ, op_lire},         {FS_WRITE, op_ecrire},
	{FS_READDIR, op_lire_rep}, {FS_BUFFER, op_tampon},  {FS_READ_BUF, op_lire_t},
	{FS_WRITE_BUF, op_ecrire_t}, {FS_UTIME, op_utime},  {FS_RENAME, op_rename},
	{FS_INFO, op_info},     {FS_SEEK, op_position},     {0, 0},
};

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *arg = krt_line; /* « <volume> ram:<taille> » */
	if (!arg)
		SVC_QUIT();
	char *o = str_cat(nom_svc, "fs."); /* « fs.<volume> » */
	const char *p = arg;
	while (*p && *p != ' ')
		*o++ = *p++;
	*o = 0;
	if (*p) { /* (sans source : taille par défaut) */
		p++;
		while (*p && *p != ':') /* « ram:<taille>[K|M] » */
			p++;
		if (*p) {
			p++;
			u64 v = parse_dec(p);
			for (; *p; p++) { /* unité (la première trouvée, comme l'asm) */
				char c = *p | 0x20;
				if (c == 'k') {
					v <<= 10;
					break;
				}
				if (c == 'm') {
					v <<= 20;
					break;
				}
			}
			if (v)
				limite = v;
		}
	}
	noeuds[0].attr = ATTR_DIR; /* racine */
	fs_canal = korin_sys2(SYS_SVC_REGISTER, nom_svc, SVF_ENDS | SVF_BIRTHS); /* fins de process, fork */
	if (fs_canal < 0)
		SVC_QUIT();
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, fs_canal, &msg, 60000);
		if (req < 0)
			SVC_QUIT();
		if (!req)
			continue;
		u64 op = msg.op;
		if (op == SVC_OP_END) { /* fin d'un process : ses fichiers */
			fin_pid(msg.a);
			continue;
		}
		if (op == SVC_OP_FORK) { /* fork : le fils tient les fichiers du père */
			fo_own_fork(msg.a, msg.b);
			continue;
		}
		if (op == SVC_OP_BIRTH) /* (avis sans réponse : rien à faire) */
			continue;
		long r;
		if (op == FS_CLOSE) {
			r = op_fermer(&msg);
			if (!msg.pid) /* avis du noyau (pid 0) : sans réponse */
				continue;
		} else {
			r = E_INVAL;
			for (int i = 0; ops[i].op; i++)
				if ((u64)ops[i].op == op) {
					r = ops[i].fn(&msg);
					break;
				}
		}
		msg.op = r;
		korin_sys2(SYS_SVC_REPLY, req, &msg);
	}
}
