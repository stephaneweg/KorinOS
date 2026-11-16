/* Essai d'oauthd sous Linux (WSL), sans Korin : les opérations appelées directement (handle), contre le faux
 * serveur tools/nettest/oauthsrv.py. Le navigateur est simulé : GET de l'adresse d'accord, puis du retour.
 *
 *   python3 -I tools/nettest/oauthsrv.py 8090 &
 *   sh src/svc/oauthd/test/run.sh            (AddressSanitizer ; dossier de travail jetable)
 *
 * NET=1 : essaie aussi le vrai TLS (Google, client inconnu : invalid_client attendu, certificat vérifié). */
#define PROVIDERS_INI "providers.ini"
#define main oauthd_main
#include "../oauthd.c"
#undef main

static int fails, oks;
#define CHECK(c, ...)                                                                                                  \
	do {                                                                                                           \
		if (c) {                                                                                               \
			oks++;                                                                                         \
		} else {                                                                                               \
			fails++;                                                                                       \
			printf("ECHEC %s:%d : ", __FILE__, __LINE__);                                                  \
			printf(__VA_ARGS__);                                                                           \
			printf("  [%s]\n", last_err());                                                                \
		}                                                                                                      \
	} while (0)

static char outbuf[16384];

static const char *last_err(void)
{
	for (int i = 0; i < ERR_MAX; i++)
		if (errs[i].pid == pid_cour)
			return errs[i].text;
	return "";
}

static long call(long op, const char *name, long a, const char *in, char *out)
{
	memset(&msg, 0, sizeof msg);
	msg.op = op;
	msg.a = a;
	msg.pid = 42;
	pid_cour = 42;
	if (name)
		snprintf((char *)msg.data, sizeof msg.data, "%s", name);
	if (in) {
		msg.c = (i64)in;
		msg.d = (i64)strlen(in);
	} else if (out) {
		msg.c = (i64)out;
		msg.d = sizeof outbuf;
		out[0] = 0;
	}
	return handle(&msg);
}

/* la redirection 302 du faux serveur : sa ligne Location (http() ne rend que le corps : on refait la requête
 * à la main) */
static int get_location(const char *url, char *loc, size_t n)
{
	struct url u;
	if (url_parse(url, &u))
		return -1;
	struct addrinfo hints = {0}, *res;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	char ps[8];
	snprintf(ps, sizeof ps, "%d", u.port);
	if (getaddrinfo(u.host, ps, &hints, &res))
		return -1;
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(fd, res->ai_addr, res->ai_addrlen)) {
		freeaddrinfo(res);
		close(fd);
		return -1;
	}
	freeaddrinfo(res);
	char req[4096];
	int k = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", u.path, u.host);
	send(fd, req, (size_t)k, 0);
	char resp[8192];
	int m = 0, r;
	while ((r = (int)recv(fd, resp + m, sizeof resp - 1 - (size_t)m, 0)) > 0)
		m += r;
	resp[m] = 0;
	close(fd);
	char *l = strcasestr(resp, "\r\nLocation: ");
	if (!l)
		return -1;
	l += 12;
	size_t ln = strcspn(l, "\r\n");
	if (ln >= n)
		return -1;
	memcpy(loc, l, ln);
	loc[ln] = 0;
	return atoi(resp + 9);
}

/* le navigateur revient sur http://127.0.0.1:port/?code=... : connexion, requête ; oauthd lit à son poll */
static int browser_return(const char *loc)
{
	struct url u;
	if (url_parse(loc, &u))
		return -1;
	struct sockaddr_in sa = {0};
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)u.port);
	sa.sin_addr.s_addr = htonl(0x7F000001);
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa)) {
		close(fd);
		return -1;
	}
	char req[4096];
	int k = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", u.path);
	send(fd, req, (size_t)k, 0);
	return fd;
}

static void reset_memory(void)
{
	for (int i = 0; i < ACC_MAX; i++) {
		wipe_free(&accs[i].refresh);
		wipe_free(&accs[i].access);
		browser_stop(&accs[i]);
	}
	memset(accs, 0, sizeof accs);
	for (int i = 0; i < ACC_MAX; i++)
		accs[i].listen_fd = -1;
	loaded = 0;
}

int main(void)
{
	mkdir("HOME:", 0700);
	for (int i = 0; i < ACC_MAX; i++)
		accs[i].listen_fd = -1;
	const char *S = "http://127.0.0.1:8090";
	char conf[2048];
	snprintf(conf, sizeof conf,
		 "# un compte d'essai\nprovider = custom\nclient_id = cid\n  scope = files offline_access  \n"
		 "auth_url = %s/auth\ntoken_url = %s/token\ndevice_url = %s/device\nunknown = x\n",
		 S, S, S);
	long r;

	/* ---- configuration */
	CHECK(call(OA_STATUS, "t/dev", 0, 0, 0) == E_NOENT, "statut d'un compte inconnu");
	CHECK(call(OA_SETUP, "bad name", 0, conf, 0) == E_INVAL, "nom invalide refusé");
	CHECK(call(OA_SETUP, "t/x", 0, "provider = custom\n", 0) == E_INVAL, "sans client_id : refusé");
	CHECK(call(OA_STATUS, "t/x", 0, 0, 0) == E_NOENT, "compte refusé : pas créé");
	CHECK(call(OA_SETUP, "t/dev", 0, conf, 0) == 0, "setup");
	CHECK(call(OA_STATUS, "t/dev", 0, 0, 0) == 0, "configuré, pas connecté");
	CHECK(call(OA_INFO, "t/dev", 0, 0, outbuf) > 0 && strstr(outbuf, "scope = files offline_access\n"), "info : %s", outbuf);
	CHECK(call(OA_TOKEN, "t/dev", 0, 0, outbuf) == E_ACCES, "jeton sans connexion");

	/* ---- code d'appareil */
	r = call(OA_DEVICE_BEGIN, "t/dev", 0, 0, outbuf);
	CHECK(r > 0 && !strncmp(outbuf, "ABCD-EFGH\n", 10), "device_begin : %ld %s", r, outbuf);
	CHECK(call(OA_DEVICE_POLL, "t/dev", 0, 0, 0) == E_AGAIN, "poll trop tôt : sans réseau");
	int polls = 0;
	for (int i = 0; i < 20; i++) {
		sleep(1);
		r = call(OA_DEVICE_POLL, "t/dev", 0, 0, 0);
		polls++;
		if (r != E_AGAIN)
			break;
	}
	CHECK(r == 0, "device_poll -> %ld après %d", r, polls);
	CHECK(call(OA_STATUS, "t/dev", 0, 0, 0) == 2, "jeton valable");
	r = call(OA_TOKEN, "t/dev", 0, 0, outbuf);
	CHECK(r > 0 && !strncmp(outbuf, "at-", 3), "jeton : %s", outbuf);
	char first[64];
	scpy(first, outbuf, sizeof first);

	/* tampon trop petit */
	memset(&msg, 0, sizeof msg);
	msg.op = OA_TOKEN;
	strcpy((char *)msg.data, "t/dev");
	char tiny[3];
	msg.c = (i64)tiny;
	msg.d = sizeof tiny;
	CHECK(handle(&msg) == E_RANGE, "tampon trop petit");

	/* ---- rafraîchissement (réponse en morceaux), jeton de rafraîchissement renouvelé et enregistré */
	find("t/dev")->access_until = 0;
	r = call(OA_TOKEN, "t/dev", 0, 0, outbuf);
	CHECK(r > 0 && strcmp(outbuf, first), "rafraîchi : %s (avant %s)", outbuf, first);

	/* ---- relecture depuis les fichiers (secrets déchiffrés) */
	reset_memory();
	CHECK(call(OA_STATUS, "t/dev", 0, 0, 0) == 1, "relu : connecté, pas de jeton d'accès");
	r = call(OA_TOKEN, "t/dev", 0, 0, outbuf);
	CHECK(r > 0 && !strncmp(outbuf, "at-", 3), "jeton après relecture : %s", outbuf);
	{
		size_t n = 0;
		char *s = file_read(SECRETS_FILE, &n);
		CHECK(s && n > 36 && !memcmp(s, SECRETS_MAGIC, 8) && !memmem(s, n, "rt-", 3), "secrets chiffrés (%zu o)", n);
		free(s);
		s = file_read(ACCOUNTS_INI, 0);
		CHECK(s && strstr(s, "[t/dev]") && !strstr(s, "rt-") && !strstr(s, "unknown"), "accounts.ini : %s", s ? s : "");
		free(s);
	}

	/* ---- navigateur : retour sur le port local */
	snprintf(conf, sizeof conf, "provider = custom\nclient_id = cid\nclient_secret = sec\nscope = a b\n"
				    "auth_url = %s/auth\ntoken_url = %s/token\n", S, S);
	CHECK(call(OA_SETUP, "t/web", 0, conf, 0) == 0, "setup t/web");
	r = call(OA_BROWSER_BEGIN, "t/web", 0, 0, outbuf);
	CHECK(r > 0 && strstr(outbuf, "code_challenge_method=S256") && strstr(outbuf, "redirect_uri=http%3A%2F%2F127.0.0.1%3A478"),
	      "browser_begin : %s", outbuf);
	CHECK(call(OA_BROWSER_POLL, "t/web", 0, 0, 0) == E_AGAIN, "poll sans retour");
	char loc[2048];
	int st = get_location(outbuf, loc, sizeof loc);
	CHECK(st == 302 && strstr(loc, "code="), "accord : %d %s", st, loc);
	/* une requête parasite d'abord (favicon) */
	{
		char fav[256];
		snprintf(fav, sizeof fav, "%.*sfavicon.ico", (int)(strstr(loc, "/?") - loc + 1), loc);
		int fd = browser_return(fav);
		CHECK(call(OA_BROWSER_POLL, "t/web", 0, 0, 0) == E_AGAIN, "favicon : toujours en attente");
		close(fd);
	}
	int fd = browser_return(loc);
	CHECK(fd >= 0, "retour du navigateur");
	r = call(OA_BROWSER_POLL, "t/web", 0, 0, 0);
	CHECK(r == 0, "browser_poll -> %ld", r);
	{
		char page[2048];
		int m = 0, k;
		while ((k = (int)recv(fd, page + m, sizeof page - 1 - (size_t)m, 0)) > 0)
			m += k;
		page[m > 0 ? m : 0] = 0;
		CHECK(strstr(page, "200 OK") && strstr(page, "Connexion réussie"), "page rendue au navigateur");
		close(fd);
	}
	CHECK(call(OA_STATUS, "t/web", 0, 0, 0) == 2, "t/web connecté");

	/* ---- navigateur : adresse collée, puis mauvais state, puis code seul refusé (code déjà pris) */
	CHECK(call(OA_BROWSER_BEGIN, "t/web", 0, 0, outbuf) > 0, "browser_begin 2");
	st = get_location(outbuf, loc, sizeof loc);
	char bad[2048];
	snprintf(bad, sizeof bad, "%.*sstate=faux", (int)(strstr(loc, "state=") - loc), loc);
	CHECK(call(OA_BROWSER_FINISH, "t/web", 0, bad, 0) == E_ACCES, "mauvais state refusé");
	CHECK(call(OA_BROWSER_FINISH, "t/web", 0, loc, 0) == 0, "adresse collée : %s", loc);
	CHECK(call(OA_BROWSER_FINISH, "t/web", 0, "code-1", 0) == E_INVAL, "plus de connexion en cours");
	CHECK(call(OA_BROWSER_BEGIN, "t/web", 0, 0, outbuf) > 0, "browser_begin 3");
	CHECK(call(OA_BROWSER_FINISH, "t/web", 0, "http://127.0.0.1:1/?error=access_denied", 0) == E_ACCES,
	      "accord refusé par l'utilisateur");

	/* ---- liste, oubli, révocation, retrait */
	r = call(OA_LIST, 0, 0, 0, outbuf);
	CHECK(r > 0 && strstr(outbuf, "t/dev\t") && strstr(outbuf, "t/web\t"), "liste : %s", outbuf);
	CHECK(get_location("http://127.0.0.1:8090/revoke", loc, sizeof loc) < 0, "révocation (sans redirection)");
	find("t/web")->access_until = 0;
	CHECK(call(OA_TOKEN, "t/web", 0, 0, outbuf) == E_ACCES, "révoqué : à reconnecter");
	CHECK(call(OA_STATUS, "t/web", 0, 0, 0) == 0, "révoqué : jeton oublié");
	CHECK(call(OA_FORGET, "t/dev", 0, 0, 0) == 0 && call(OA_STATUS, "t/dev", 0, 0, 0) == 0, "oubli");
	CHECK(call(OA_REMOVE, "t/dev", 0, 0, 0) == 0 && call(OA_STATUS, "t/dev", 0, 0, 0) == E_NOENT, "retrait");
	reset_memory();
	CHECK(call(OA_STATUS, "t/dev", 0, 0, 0) == E_NOENT && call(OA_STATUS, "t/web", 0, 0, 0) == 0, "retrait relu");

	/* ---- fournisseur livré par le système (providers.ini) : le compte ne donne que provider, comme Mail */
	CHECK(call(OA_SETUP, "t/pre", 0, "provider = fake\n", 0) == E_INVAL, "fournisseur inconnu sans client : refusé");
	{
		FILE *f = fopen(PROVIDERS_INI, "w");
		fprintf(f, "# essai\n[fake]\nclient_id = cid\nclient_secret = sec\nscope = files\n"
			   "auth_url = %s/auth\ntoken_url = %s/token\ndevice_url = %s/device\n", S, S, S);
		fclose(f);
		presets_mtime = -1; /* (même seconde que l'essai précédent : relu de force) */
	}
	CHECK(call(OA_SETUP, "t/pre", 0, "provider = fake\n", 0) == 0, "setup par le fournisseur seul");
	r = call(OA_INFO, "t/pre", 0, 0, outbuf);
	CHECK(r > 0 && strstr(outbuf, "client_id = cid (Korin)") && strstr(outbuf, "scope = files (Korin)") &&
		      strstr(outbuf, "client_secret = (défini) (Korin)"),
	      "info montre les valeurs du système : %s", outbuf);
	CHECK(call(OA_DEVICE_BEGIN, "t/pre", 0, 0, outbuf) > 0, "code d'appareil avec le client du système");
	for (int i = 0; i < 20; i++) {
		sleep(1);
		r = call(OA_DEVICE_POLL, "t/pre", 0, 0, 0);
		if (r != E_AGAIN)
			break;
	}
	CHECK(r == 0, "connecté avec le client du système : %ld", r);
	{
		char *s = file_read(ACCOUNTS_INI, 0);
		char *sec = s ? strstr(s, "[t/pre]") : 0;
		CHECK(sec && !strstr(sec, "client_id"), "accounts.ini ne recopie pas le client du système");
		free(s);
	}
	CHECK(call(OA_SETUP, "t/own", 0, "provider = fake\nclient_id = autre\n", 0) == 0 &&
		      call(OA_INFO, "t/own", 0, 0, outbuf) > 0 && strstr(outbuf, "client_id = autre\n"),
	      "son propre client_id l'emporte : %s", outbuf);
	/* une marque @…@ non remplie par la construction (tools/secrets.py) : comme absente */
	{
		FILE *f = fopen(PROVIDERS_INI, "w");
		fprintf(f, "[fake]\nclient_id = @FAKE_CLIENT_ID@\nclient_secret = @FAKE_SECRET@\ntoken_url = %s/token\n", S);
		fclose(f);
		presets_mtime = -1;
	}
	CHECK(call(OA_SETUP, "t/mark", 0, "provider = fake\n", 0) == E_INVAL, "marque non remplie : client absent");
	unlink(PROVIDERS_INI);

	/* ---- en clair vers une adresse publique : refusé ; client changé : jetons oubliés */
	CHECK(call(OA_SETUP, "t/pub", 0, "client_id = cid\ntoken_url = http://example.com/token\n", 0) == 0, "setup t/pub");
	CHECK(call(OA_DEVICE_BEGIN, "t/pub", 0, 0, outbuf) == E_INVAL, "pas de device_url");
	find("t/pub")->refresh = strdup("rt-x");
	CHECK(call(OA_TOKEN, "t/pub", 0, 0, outbuf) == E_ACCES && strstr(last_err(), "en clair"), "http public refusé");
	CHECK(call(OA_SETUP, "t/pub", 0, "client_id = autre\ntoken_url = http://example.com/token\n", 0) == 0 &&
		      !find("t/pub")->refresh,
	      "autre client : jeton oublié");

	/* ---- vrai TLS (Google) */
	if (getenv("NET")) {
		CHECK(call(OA_SETUP, "t/g", 0, "provider = google\nclient_id = korin-test.apps.googleusercontent.com\nscope = openid\n", 0) == 0,
		      "setup google");
		r = call(OA_DEVICE_BEGIN, "t/g", 0, 0, outbuf);
		CHECK(r == E_IO && strstr(last_err(), "invalid_client"), "Google répond (TLS vérifié) : %ld", r);
		printf("Google : %s\n", last_err());
		CHECK(call(OA_SETUP, "t/bad", 0, "client_id = x\ntoken_url = https://expired.badssl.com/\n", 0) == 0, "setup badssl");
		find("t/bad")->refresh = strdup("x");
		r = call(OA_TOKEN, "t/bad", 0, 0, outbuf);
		CHECK(r == E_ACCES && strstr(last_err(), "certificat"), "certificat expiré refusé : %ld", r);
		printf("badssl : %s\n", last_err());
	}
	reset_memory();
	if (ssl_ctx)
		SSL_CTX_free(ssl_ctx);
	printf("%d réussis, %d échecs\n", oks, fails);
	return fails != 0;
}
