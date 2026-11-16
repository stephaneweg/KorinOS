/* Essai de gdrivefs sous Linux (WSL), sans Korin : les opérations appelées directement (handle), contre le faux
 * serveur tools/nettest/gdrivesrv.py (port 8091). oauth_token de NetKit est simulé (jeton « at-test »).
 *
 *   sh src/svc/gdrivefs/test/run.sh          (AddressSanitizer et UBSan ; dossier de travail jetable)
 *
 * Couvre : listes (pagination, doublons, « / », Docs cachés), stat, lectures (petites, tampon partagé,
 * directes, au hasard, à la suite), le cache (succès, pas de second téléchargement), la lecture anticipée,
 * l'écriture refusée, FS_INFO, 401 puis nouveau jeton, 429/403 avec délai, connexion gardée, refaite si le
 * serveur la ferme, listes rechargées après leur durée de validité, propriétaires. */
#include <openssl/evp.h>
#include <signal.h>
#include <sys/stat.h>
static unsigned char test_shared[65536] __attribute__((aligned(16)));
#define BUF_VA_BASE ((u64)(uintptr_t)test_shared)
#define main gdrivefs_main
#include "../gdrivefs.c"
#undef main
#include <arpa/inet.h>

#define PORT 8091
#define BIG_SIZE (5 * 1024 * 1024 + 12345)
#define PID 42

static int fails, oks;
#define CHECK(c, ...)                                                                                                  \
	do {                                                                                                           \
		if (c) {                                                                                               \
			oks++;                                                                                         \
		} else {                                                                                               \
			fails++;                                                                                       \
			printf("ECHEC %s:%d : ", __FILE__, __LINE__);                                                  \
			printf(__VA_ARGS__);                                                                           \
			printf("\n");                                                                                  \
		}                                                                                                      \
	} while (0)

/* ---- NetKit simulé */
static int token_calls;
static long token_result;
int netkit_kit_open(void) { return 1; }
long oauth_token(long name, long out, long max)
{
	token_calls++;
	if (token_result < 0)
		return token_result;
	if (strcmp((const char *)name, "gdrive/test"))
		return -2;
	return snprintf((char *)out, (size_t)max, "at-test");
}
long oauth_error(long out, long max)
{
	return snprintf((char *)out, (size_t)max, "gdrive/test : pas connecté (simulé)");
}

/* ---- le faux serveur : /ctl, /stats (une connexion à part) */
static char srv_body[65536];

static int srv_get(const char *path)
{
	struct sockaddr_in sa = {0};
	sa.sin_family = AF_INET;
	sa.sin_port = htons(PORT);
	sa.sin_addr.s_addr = htonl(0x7F000001);
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa)) {
		close(fd);
		return -1;
	}
	char req[512];
	int k = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n", path);
	send(fd, req, (size_t)k, 0);
	static char resp[sizeof srv_body + 4096];
	int m = 0, r;
	while ((r = (int)recv(fd, resp + m, sizeof resp - 1 - (size_t)m, 0)) > 0)
		m += r;
	resp[m] = 0;
	close(fd);
	char *b = strstr(resp, "\r\n\r\n");
	snprintf(srv_body, sizeof srv_body, "%s", b ? b + 4 : "");
	return m > 12 ? atoi(resp + 9) : -1;
}

static u64 stat_of(const char *key)
{
	srv_get("/stats");
	return json_u64(srv_body, key, 0);
}

static u64 big_bytes(void)
{
	srv_get("/stats");
	return json_u64(json_find(srv_body, "bytes_by_id"), "fBig", 0);
}

/* ---- requêtes au service */
static struct msg M;

static long req(long op, long a, long b, long c, long d, const char *path, u32 pid)
{
	memset(&M, 0, sizeof M);
	M.op = op;
	M.a = a;
	M.b = b;
	M.c = c;
	M.d = d;
	M.pid = pid;
	if (path) {
		snprintf((char *)M.data, sizeof M.data, "%s", path);
		M.len = (u32)strlen(path) + 1;
	}
	return handle(&M);
}

/* ouvre en lecture -> numéro (M.d : taille, M.c : attributs) */
static long t_open(const char *path)
{
	long r = req(FS_OPEN, O_K_READ, 0, 0, 0, path, PID);
	return r < 0 ? r : M.b;
}

static void t_close(long num)
{
	req(FS_CLOSE, num, 0, 0, 0, 0, PID);
}

/* lecture directe (dans la mémoire de l'appelant) */
static long t_read(long num, i64 pos, void *dst, long n)
{
	return req(FS_READ_DIRECT, num, pos, n, (long)(uintptr_t)dst, 0, PID);
}

#define LMAX 64
static char names[LMAX][256];
static u64 sizes[LMAX], attrs[LMAX];

static int t_list(const char *path)
{
	long num = t_open(path);
	if (num < 0)
		return (int)num;
	if (!(M.c & ATTR_DIR)) {
		t_close(num);
		return E_NOTDIR;
	}
	int n = 0;
	while (n < LMAX && req(FS_READDIR, num, n, 0, 0, 0, PID) == 1) {
		snprintf(names[n], 256, "%.255s", (char *)M.data);
		sizes[n] = ((u64 *)(M.data + 256))[0];
		attrs[n] = ((u64 *)(M.data + 256))[1];
		n++;
	}
	t_close(num);
	return n;
}

static int has(int n, const char *name)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(names[i], name))
			return i;
	return -1;
}

/* le contenu entier d'un petit fichier par FS_READ (464 octets au plus par requête) */
static long t_slurp(const char *path, char *out, long max)
{
	long num = t_open(path);
	if (num < 0)
		return num;
	long n = 0, r;
	while (n < max - 1 && (r = req(FS_READ, num, FS_POS_CUR, max - 1 - n, 0, 0, PID)) > 0) {
		memcpy(out + n, M.data, (size_t)r);
		n += r;
	}
	out[n] = 0;
	t_close(num);
	return n;
}

static u8 big_byte(u64 i)
{
	return (u8)((((i * 2654435761u) & 0xFFFFFFFFu) >> 24) ^ ((i >> 11) & 255));
}

static int same_big(const u8 *p, u64 pos, u64 n)
{
	for (u64 i = 0; i < n; i++)
		if (p[i] != big_byte(pos + i))
			return 0;
	return 1;
}

static void write_ini(const char *text)
{
	mkdir("HOME:", 0700);
	mkdir("HOME:/gdrive", 0700);
	FILE *f = fopen("HOME:/gdrive/drives.ini", "w");
	fputs(text, f);
	fclose(f);
}

static double secs(void)
{
	return now_ms() / 1000.0;
}

int main(void)
{
	signal(SIGPIPE, SIG_IGN);
	setvbuf(stdout, 0, _IONBF, 0);
	if (srv_get("/ctl?reset=1") != 200) {
		printf("faux serveur absent (python3 tools/nettest/gdrivesrv.py %d)\n", PORT);
		return 2;
	}
	write_ini("; essai\n[autre]\napi_url = http://127.0.0.1:1\n[test]\napi_url = http://127.0.0.1:8091   ; le faux\n"
		  "cache = 16M\n");
	snprintf(volume, sizeof volume, "net1");
	snprintf(source, sizeof source, "gdrive:test");
	canal = 7;

	/* ---- racine : 3 pages, Docs/raccourcis/formulaires et corbeille cachés, doublons, « / » */
	printf("-- racine\n");
	int n = t_list("");
	CHECK(n == 6, "racine : %d entrées (6 attendues)", n);
	CHECK(conf_read && !strcmp(account, "gdrive/test") && cache_bytes == 16u << 20, "réglages : %s %llu", account,
	      (unsigned long long)cache_bytes);
	CHECK(stat_of("list") == 3, "racine : %llu requêtes de liste (3 pages attendues)",
	      (unsigned long long)stat_of("list"));
	int i = has(n, "Documents");
	CHECK(i >= 0 && attrs[i] == ATTR_DIR, "Documents dossier");
	CHECK(has(n, "Photos") >= 0 && has(n, "Photos (2)") >= 0, "Photos et Photos (2)");
	i = has(n, "big.bin");
	CHECK(i >= 0 && sizes[i] == BIG_SIZE && attrs[i] == ATTR_FILE, "big.bin : taille %llu",
	      i >= 0 ? (unsigned long long)sizes[i] : 0ULL);
	CHECK(has(n, "empty.txt") >= 0, "empty.txt");
	CHECK(has(n, "a\xE2\x88\x95" "b.txt") >= 0, "a∕b.txt (barre remplacée)");
	CHECK(has(n, "Mon document") < 0 && has(n, "Raccourci") < 0 && has(n, "Formulaire") < 0, "Docs cachés");
	CHECK(has(n, "jete.txt") < 0, "corbeille cachée");
	CHECK(token_calls == 1, "un seul jeton demandé (%d)", token_calls);

	/* ---- sous-dossiers */
	printf("-- dossiers\n");
	n = t_list("Documents");
	CHECK(n == 5, "Documents : %d entrées (5 attendues)", n);
	CHECK(has(n, "rapport.pdf") >= 0 && has(n, "rapport (2).pdf") >= 0 && has(n, "rapport (3).pdf") >= 0,
	      "doublons suffixés");
	CHECK(has(n, "Budget") < 0, "Sheets caché");
	CHECK(has(n, "Sous-dossier") >= 0 && attrs[has(n, "Sous-dossier")] == ATTR_DIR, "Sous-dossier");
	char text[4096];
	CHECK(t_slurp("Documents/rapport.pdf", text, sizeof text) == 11 && !strcmp(text, "rapport v1\n"),
	      "rapport.pdf = le premier créé : %s", text);
	CHECK(t_slurp("Documents/rapport (2).pdf", text, sizeof text) == 11 && !strcmp(text, "rapport v2\n"),
	      "rapport (2).pdf : %s", text);
	CHECK(t_slurp("Documents/rapport (3).pdf", text, sizeof text) == 11 && !strcmp(text, "rapport v3\n"),
	      "rapport (3).pdf : %s", text);
	n = t_list("Photos");
	CHECK(n == 7, "Photos : %d entrées (7, en 3 pages)", n);
	int sz_ok = 1;
	for (int k = 1; k <= 7; k++) {
		char nm[32];
		snprintf(nm, sizeof nm, "img%02d.jpg", k);
		int j = has(n, nm);
		sz_ok &= j >= 0 && sizes[j] == (u64)(1000 * k);
	}
	CHECK(sz_ok, "Photos : noms et tailles");
	n = t_list("Photos (2)");
	CHECK(n == 1 && !strcmp(names[0], "autre.jpg"), "Photos (2) : %d, %s", n, n > 0 ? names[0] : "");
	n = t_list("Documents/Sous-dossier");
	CHECK(n == 1 && !strcmp(names[0], "profond.txt"), "Sous-dossier");
	CHECK(t_list("documents") == 5, "nom sans la casse (un seul candidat)");
	CHECK(t_list("Nulle part") == E_NOENT, "dossier absent : E_NOENT");

	/* ---- stat (servi par les listes) */
	printf("-- stat\n");
	u64 lists0 = stat_of("list");
	long r = req(FS_STAT, 0, 0, 0, 0, "Documents/notes.txt", PID);
	CHECK(r == 0 && M.b == 46 && M.c == ATTR_FILE && M.d == 1748779200, "stat notes.txt : %ld b=%lld c=%lld d=%lld",
	      r, (long long)M.b, (long long)M.c, (long long)M.d);
	r = req(FS_STAT, 0, 0, 0, 0, "Documents", PID);
	CHECK(r == 0 && M.c == ATTR_DIR && M.b == 0, "stat Documents");
	r = req(FS_STAT, 0, 0, 0, 0, "", PID);
	CHECK(r == 0 && M.c == ATTR_DIR, "stat racine");
	r = req(FS_STAT, 0, 0, 0, 0, "/big.bin", PID);
	CHECK(r == 0 && M.b == BIG_SIZE && M.d == 1751358600, "stat /big.bin : %lld %lld", (long long)M.b, (long long)M.d);
	CHECK(req(FS_STAT, 0, 0, 0, 0, "absent.txt", PID) == E_NOENT, "stat absent : E_NOENT");
	CHECK(req(FS_STAT, 0, 0, 0, 0, "big.bin/x", PID) == E_NOTDIR, "stat sous un fichier : E_NOTDIR");
	CHECK(req(FS_STAT, 0, 0, 0, 0, "Mon document", PID) == E_NOENT, "stat d'un Doc caché : E_NOENT");
	CHECK(stat_of("list") == lists0, "stat sans requête de liste (%llu -> %llu)", (unsigned long long)lists0,
	      (unsigned long long)stat_of("list"));

	/* ---- petits fichiers */
	printf("-- petits fichiers\n");
	CHECK(t_slurp("Documents/notes.txt", text, sizeof text) == 46 &&
		      !strcmp(text, "Bonjour depuis le faux Drive.\nDeuxieme ligne.\n"),
	      "notes.txt : %s", text);
	CHECK(t_slurp("a\xE2\x88\x95" "b.txt", text, sizeof text) == 19 && !strcmp(text, "nom avec une barre\n"),
	      "a∕b.txt : %s", text);
	u64 media0 = stat_of("media");
	long num = t_open("empty.txt");
	CHECK(num >= 0 && M.d == 0, "empty.txt ouvert, taille 0");
	CHECK(req(FS_READ, num, 0, 100, 0, 0, PID) == 0, "empty.txt : 0 octet");
	t_close(num);
	CHECK(stat_of("media") == media0, "empty.txt : aucune requête");
	num = t_open("Photos/img07.jpg");
	CHECK(req(FS_BUFFER, 0, 0, 0, 0, 0, PID) == 0, "FS_BUFFER");
	r = req(FS_READ_BUF, num, 0, 65536, 0, 0, PID);
	int img_ok = r == 7000;
	for (int k = 0; k < 7000 && img_ok; k++)
		img_ok = test_shared[k] == 7;
	CHECK(img_ok, "img07.jpg par le tampon partagé : %ld", r);
	t_close(num);

	/* ---- big.bin au hasard : jamais en entier, le cache sert la seconde fois */
	printf("-- lectures au hasard\n");
	u8 *buf = malloc(1u << 20);
	srv_get("/ctl?reset=1");
	num = t_open("big.bin");
	CHECK(num >= 0 && M.d == BIG_SIZE, "big.bin ouvert");
	u64 pos = 2621440;
	r = t_read(num, pos, buf, 102400);
	CHECK(r == 102400 && same_big(buf, pos, 102400), "100 Ko au milieu : %ld", r);
	u64 got = big_bytes();
	CHECK(got == BLOCK, "un seul bloc téléchargé : %llu octets (fichier %d)", (unsigned long long)got, BIG_SIZE);
	u64 h0 = st.hits, m0 = st.misses;
	r = t_read(num, pos, buf, 102400);
	CHECK(r == 102400 && same_big(buf, pos, 102400), "relu");
	CHECK(big_bytes() == got && st.hits > h0 && st.misses == m0, "relu depuis le cache (succès %llu -> %llu)",
	      (unsigned long long)h0, (unsigned long long)st.hits);
	r = t_read(num, BLOCK - 100, buf, 200);
	CHECK(r == 200 && same_big(buf, BLOCK - 100, 200), "à cheval sur deux blocs");
	r = t_read(num, BIG_SIZE - 50, buf, 100);
	CHECK(r == 50 && same_big(buf, BIG_SIZE - 50, 50), "la fin : %ld", r);
	CHECK(t_read(num, BIG_SIZE + 10, buf, 100) == 0, "après la fin : 0");
	r = req(FS_READ, num, 3 * BLOCK + 7, 464, 0, 0, PID);
	CHECK(r == 464 && same_big(M.data, 3 * BLOCK + 7, 464) && M.d == 3 * BLOCK + 7 + 464, "FS_READ à 3 Mo");
	unsigned seed = 12345;
	int rnd_ok = 1;
	for (int k = 0; k < 40; k++) {
		seed = seed * 1103515245 + 12345;
		u64 p = seed % BIG_SIZE;
		long len = 1 + (long)((seed >> 8) % 70000);
		long want = (u64)len > BIG_SIZE - p ? (long)(BIG_SIZE - p) : len;
		r = t_read(num, (i64)p, buf, len);
		rnd_ok &= r == want && same_big(buf, p, (u64)r);
	}
	CHECK(rnd_ok, "40 lectures au hasard identiques au serveur");
	got = big_bytes();
	CHECK(got <= BIG_SIZE, "au hasard : %llu octets téléchargés (fichier %d)", (unsigned long long)got, BIG_SIZE);
	/* un saut loin remet l'avance à un bloc */
	ouverts[num].ra = 4;
	ouverts[num].next = BLOCK;
	r = req(FS_SEEK, num, 4 * BLOCK, 0, 0, 0, PID);
	CHECK(r == 4 * BLOCK && ouverts[num].ra == 1 && ouverts[num].next == NONE, "saut loin : avance annulée");
	t_close(num);

	/* ---- big.bin à la suite : lecture anticipée, chaque octet une fois, SHA-256 */
	printf("-- lecture à la suite\n");
	CHECK(req(FS_CACHE, FS_CC_DROP, 0, 0, 0, 0, PID) == 0 && M.c == 0, "cache vidé");
	srv_get("/ctl?reset=1");
	num = t_open("big.bin");
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(ctx, EVP_sha256(), 0);
	u64 total = 0;
	while ((r = t_read(num, FS_POS_CUR, buf, 65536)) > 0) {
		EVP_DigestUpdate(ctx, buf, (size_t)r);
		total += (u64)r;
	}
	unsigned char h1[32], h2[32];
	EVP_DigestFinal_ex(ctx, h1, 0);
	EVP_MD_CTX_free(ctx);
	u8 *exp = malloc(BIG_SIZE);
	for (u64 k = 0; k < BIG_SIZE; k++)
		exp[k] = big_byte(k);
	EVP_Digest(exp, BIG_SIZE, h2, 0, EVP_sha256(), 0);
	free(exp);
	CHECK(total == BIG_SIZE && !memcmp(h1, h2, 32), "à la suite : %llu octets, SHA-256 %s",
	      (unsigned long long)total, memcmp(h1, h2, 32) ? "DIFFERENT" : "égal");
	u64 nmedia = stat_of("media");
	CHECK(nmedia == 3, "lecture anticipée : %llu requêtes (1 + 2 + 3 blocs attendus)", (unsigned long long)nmedia);
	CHECK(big_bytes() == BIG_SIZE, "chaque octet une fois : %llu", (unsigned long long)big_bytes());
	CHECK(stat_of("connections") == 0, "connexion gardée (%llu nouvelles)", (unsigned long long)stat_of("connections"));
	CHECK(stat_of("chunked") >= 1, "réponses en morceaux reçues (%llu)", (unsigned long long)stat_of("chunked"));
	r = req(FS_CACHE, 0, 0, 0, 0, 0, PID);
	CHECK(r == 0 && (u64)M.a == st.hits && (u64)M.b == st.misses && M.c == 6 * 16, "FS_CACHE : %lld %lld %lld %lld",
	      (long long)M.a, (long long)M.b, (long long)M.c, (long long)M.d);
	printf("   cache : %llu succès, %llu défauts, %llu remplacés ; %llu requêtes alt=media, %llu octets reçus\n",
	       (unsigned long long)st.hits, (unsigned long long)st.misses, (unsigned long long)st.evictions,
	       (unsigned long long)st.fetches, (unsigned long long)st.fetched);

	/* ---- propriétaires */
	printf("-- propriétaires\n");
	CHECK(req(FS_READ, num, 0, 10, 0, 0, 43) == E_BADF, "autre pid : E_BADF");
	fork_pid(43, PID);
	CHECK(req(FS_READ, num, 0, 10, 0, 0, 43) == 10, "fils après fork : lu");
	fin_pid(PID);
	CHECK(ouverts[num].used, "encore tenu par le fils");
	fin_pid(43);
	CHECK(!ouverts[num].used, "libéré à la fin du dernier");

	/* ---- écriture refusée */
	printf("-- lecture seule\n");
	CHECK(req(FS_OPEN, O_K_READ | O_K_WRITE, 0, 0, 0, "Documents/notes.txt", PID) == E_ACCES, "ouvrir en écriture");
	CHECK(req(FS_OPEN, O_K_WRITE | O_K_CREATE, 0, 0, 0, "Documents/neuf.txt", PID) == E_ACCES, "créer");
	CHECK(req(FS_OPEN, O_K_WRITE | O_K_TRUNC, 0, 0, 0, "big.bin", PID) == E_ACCES, "tronquer");
	CHECK(req(FS_OPEN, O_K_WRITE, 0, 0, 0, "Documents", PID) == E_ISDIR, "dossier en écriture : E_ISDIR");
	CHECK(req(FS_OPEN, O_K_READ, 0, 0, 0, "absent/x", PID) == E_NOENT, "absent : E_NOENT");
	CHECK(req(FS_MKDIR, 0, 0, 0, 0, "Nouveau", PID) == E_ACCES, "mkdir");
	CHECK(req(FS_UNLINK, 0, 0, 0, 0, "Documents/notes.txt", PID) == E_ACCES, "unlink");
	CHECK(req(FS_RENAME, 0, 0, 0, 0, "big.bin", PID) == E_ACCES, "rename");
	CHECK(req(FS_UTIME, 0, 1, 0, 0, "big.bin", PID) == E_ACCES, "utime");
	num = t_open("Documents/notes.txt");
	CHECK(req(FS_WRITE, num, 0, 3, 0, "abc", PID) == E_ACCES, "FS_WRITE");
	CHECK(req(FS_WRITE_BUF, num, 0, 3, 0, 0, PID) == E_ACCES, "FS_WRITE_BUF");
	t_close(num);

	/* ---- FS_INFO */
	printf("-- info\n");
	r = req(FS_INFO, 0, 0, 0, 0, 0, PID);
	CHECK(r == 0 && M.a == 16106127360LL && M.b == 16106127360LL - 5368709120LL && M.d == FS_T_NET,
	      "info : %lld %lld %lld", (long long)M.a, (long long)M.b, (long long)M.d);
	CHECK(!strcmp((char *)M.data, "Google Drive (essai@example.com)"), "étiquette : %s", (char *)M.data);
	u64 ab = stat_of("about");
	req(FS_INFO, 0, 0, 0, 0, 0, PID);
	CHECK(stat_of("about") == ab, "info gardée 60 s");

	/* ---- 401 : un nouveau jeton, un nouvel essai */
	printf("-- 401\n");
	srv_get("/ctl?fail401=1");
	int t0 = token_calls;
	r = req(FS_STAT, 0, 0, 0, 0, "Photos (2)/autre.jpg", PID);
	list_ttl_ms = 0; /* (chaque chemin relu) */
	r = req(FS_STAT, 0, 0, 0, 0, "Photos (2)/autre.jpg", PID);
	CHECK(r == 0 && M.b == 7, "401 puis succès : %ld", r);
	CHECK(token_calls == t0 + 1, "jeton redemandé une fois (%d -> %d)", t0, token_calls);
	srv_get("/ctl?fail401=2");
	r = req(FS_STAT, 0, 0, 0, 0, "Photos (2)/autre.jpg", PID);
	CHECK(r == E_ACCES, "401 deux fois : E_ACCES (%ld)", r);
	r = req(FS_STAT, 0, 0, 0, 0, "Photos (2)/autre.jpg", PID);
	CHECK(r == 0, "puis de nouveau : %ld", r);
	token_result = E_ACCES;
	have_token = 0;
	r = req(FS_STAT, 0, 0, 0, 0, "big.bin", PID);
	CHECK(r == E_ACCES, "pas connecté : E_ACCES (%ld)", r);
	token_result = 0;

	/* ---- 429 et 403 rateLimit : délais 1, 2 s */
	printf("-- 429 / 403\n");
	srv_get("/ctl?fail429=2");
	double t = secs();
	r = req(FS_STAT, 0, 0, 0, 0, "empty.txt", PID);
	t = secs() - t;
	CHECK(r == 0 && t >= 2.9 && t < 6, "429 deux fois puis succès : %ld en %.1f s", r, t);
	srv_get("/ctl?fail403=1");
	t = secs();
	r = req(FS_STAT, 0, 0, 0, 0, "empty.txt", PID);
	t = secs() - t;
	CHECK(r == 0 && t >= 0.9 && t < 3, "403 userRateLimitExceeded puis succès : %ld en %.1f s", r, t);
	srv_get("/ctl?fail429=9");
	r = req(FS_STAT, 0, 0, 0, 0, "empty.txt", PID);
	CHECK(r == E_AGAIN, "429 sans fin : E_AGAIN après 3 nouveaux essais (%ld)", r);
	srv_get("/ctl?fail429=0");

	/* ---- la connexion : fermée par le serveur, refaite */
	printf("-- connexion\n");
	srv_get("/ctl?reset=1");
	srv_get("/ctl?drop=1");
	u64 rt0 = st.retries;
	r = req(FS_STAT, 0, 0, 0, 0, "Documents/notes.txt", PID);
	CHECK(r == 0 && st.retries == rt0 + 1 && stat_of("connections") == 1 && stat_of("dropped") == 1,
	      "fermée sans réponse : refaite une fois (%ld, %llu)", r, (unsigned long long)stat_of("connections"));
	srv_get("/ctl?close=1");
	r = req(FS_STAT, 0, 0, 0, 0, "Documents/notes.txt", PID);
	CHECK(r == 0, "fermée après la réponse");
	r = req(FS_STAT, 0, 0, 0, 0, "Documents/notes.txt", PID);
	CHECK(r == 0 && stat_of("connections") == 2, "puis refaite (%llu connexions)",
	      (unsigned long long)stat_of("connections"));

	/* ---- durée de validité des listes */
	printf("-- validité des listes\n");
	list_ttl_ms = 60000;
	n = t_list("Documents");
	srv_get("/ctl?add=Documents/nouveau.txt");
	n = t_list("Documents");
	CHECK(n == 5 && has(n, "nouveau.txt") < 0, "liste gardée : %d", n);
	list_ttl_ms = 1000;
	usleep(1100000);
	n = t_list("Documents");
	CHECK(n == 6 && has(n, "nouveau.txt") >= 0, "liste relue après sa durée : %d", n);
	CHECK(t_slurp("Documents/nouveau.txt", text, sizeof text) == 8, "nouveau.txt lu");

	/* ---- réglages refusés */
	printf("-- réglages\n");
	write_ini("[test]\napi_url = http://8.8.8.8:80\n");
	conf_read = 0;
	CHECK(req(FS_STAT, 0, 0, 0, 0, "x", PID) == E_INVAL, "api_url en clair vers Internet : refusée");
	snprintf(source, sizeof source, "smb:test");
	CHECK(req(FS_STAT, 0, 0, 0, 0, "x", PID) == E_INVAL, "source sans gdrive: : refusée");

	printf("\n%d réussis, %d échecs ; %llu connexions ouvertes par gdrivefs, %llu listes chargées\n", oks, fails,
	       (unsigned long long)st.connects, (unsigned long long)st.lists);
	free(buf);
	cache_drop();
	free(blks);
	for (int k = 0; k < LIST_MAX; k++)
		list_free(&lists[k]);
	conn_close();
	b_free(&cn.rx);
	SSL_CTX_free(ssl_ctx);
	return fails ? 1 : 0;
}
