//
// korin_vault.cpp -- le coffre a secrets de Korin (src/lib/keyvault/keyvault.h, docs/KEYVAULT.md) sur le Secret
// Service de Linux (le trousseau de GNOME, ou oo7, KWallet...), par libsecret. Les apps appellent kv_set, kv_get,
// kv_delete, kv_status comme sous Korin ; keyvault.h en fait des messages KV_SVC_CALL, que korin_sys.cpp passe ici
// une fois kport_vault_install () appele (Budget seulement : Koton garde ses cles comme avant).
//
// Un secret : schema « org.korin.KeyVault », attributs app (le nom de l'app : « budget »), name (le nom du secret :
// « bank-key ») et home (le dossier de HOME:/, ~/Korin ou $KORIN_HOME : un essai avec un autre KORIN_HOME a ses
// propres secrets). Sous Korin, le coffre reconnait le programme par son executable ; ici, par ces attributs : les
// programmes de l'utilisateur ouvert peuvent lire son trousseau, comme pour toute app de Linux.
//
// Sans libsecret a la construction (pas de libsecret-devel : KPORT_LIBSECRET non defini) ou sans Secret Service a
// l'execution : kv_status () < 0, et l'app fait comme sur un Korin sans coffre (Budget : banque.json et le .pem).
//
#include "korin_port.h"
#include <stdio.h>
#include <string.h>

extern "C" long (*kport_vault_handler) (long n, long a, long b);

#ifdef KPORT_LIBSECRET
#include <libsecret/secret.h>
#include <string>

namespace {

enum { E_NOENT = -2, E_ACCES = -13, E_INVAL = -22, E_NOSYS = -38 };
enum { KV_SET = 1, KV_GET, KV_DELETE, KV_LIST, KV_STATUS, KV_ERROR = 11, KV_WHOAMI = 12 };

const SecretSchema *schema ()
{
	static const SecretSchema s = { "org.korin.KeyVault", SECRET_SCHEMA_NONE,
		{ { "app", SECRET_SCHEMA_ATTRIBUTE_STRING }, { "name", SECRET_SCHEMA_ATTRIBUTE_STRING },
		  { "home", SECRET_SCHEMA_ATTRIBUTE_STRING }, { 0, SECRET_SCHEMA_ATTRIBUTE_STRING } }, 0, 0, 0, 0, 0, 0, 0, 0 };
	return &s;
}

std::string g_err;
int g_state;		// 0 pas encore vu, 1 le Secret Service repond, -1 il ne repond pas

long fail (GError *e, long code)
{
	g_err = e && e->message ? e->message : "erreur du Secret Service";
	bool refused = e && (g_error_matches (e, G_IO_ERROR, G_IO_ERROR_CANCELLED) || g_error_matches (e, SECRET_ERROR, SECRET_ERROR_IS_LOCKED));
	if (e) g_error_free (e);
	return refused ? E_ACCES : code;
}

long status ()
{
	if (!g_state)
	{
		GError *e = 0;
		SecretService *s = secret_service_get_sync (SECRET_SERVICE_NONE, 0, &e);
		g_state = s ? 1 : -1;
		if (s) g_object_unref (s);
		else { g_err = e && e->message ? e->message : "pas de Secret Service"; if (e) g_error_free (e); }
	}
	return g_state > 0 ? 1 : E_NOSYS;		// (KVS_OPEN : le trousseau s'ouvre lui-meme s'il le faut)
}

long copy_out (const char *src, size_t n, unsigned long buf, long max)
{
	if (max <= 0 || !buf) return (long) n;
	size_t k = n < (size_t) max ? n : (size_t) max;
	memcpy ((void *) buf, src, k);
	return (long) k;
}

// un message de keyvault.h (kv_call_) : m[0] = op, m[2] = drapeaux, m[3], m[4] = tampon et taille ; a l'octet 40, la
// taille des donnees, a l'octet 48 les donnees (le nom, fini par 0) -> la reponse dans m[0]
long call (unsigned long *m)
{
	long op = (long) m[0];
	unsigned long buf = m[3];
	long len = (long) m[4];
	unsigned dlen = *(const unsigned *) ((const char *) m + 40);
	std::string name;
	if (dlen > 0 && dlen <= 460) name.assign ((const char *) m + 48, strnlen ((const char *) m + 48, dlen));
	if (m[2] & 2) return E_ACCES;		// (KVF_SYSTEM : l'espace @system n'existe pas ici)
	const char *app = kport_app (), *home = kport_homedir ();
	GError *e = 0;
	switch (op)
	{
	case KV_STATUS:
		return status ();
	case KV_ERROR:
		return copy_out (g_err.data (), g_err.size (), buf, len);
	case KV_WHOAMI:
	{
		std::string id = std::string ("linux:") + app + "@" + home;
		return copy_out (id.data (), id.size (), buf, len);
	}
	case KV_LIST:
	{
		if (status () < 0) return E_NOSYS;
		GList *l = secret_password_search_sync (schema (), SECRET_SEARCH_ALL, 0, &e, "app", app, "home", home, NULL);
		if (e) return fail (e, E_INVAL);
		std::string out;
		for (GList *i = l; i; i = i->next)
		{
			GHashTable *at = secret_retrievable_get_attributes (SECRET_RETRIEVABLE (i->data));
			const char *n = at ? (const char *) g_hash_table_lookup (at, "name") : 0;
			if (n) { out += n; out += '\n'; }
			if (at) g_hash_table_unref (at);
		}
		g_list_free_full (l, g_object_unref);
		return copy_out (out.data (), out.size (), buf, len);
	}
	}
	if (name.empty ()) return E_INVAL;
	if (status () < 0) return E_NOSYS;
	switch (op)
	{
	case KV_SET:
	{
		if (len < 0 || len > 65536) return E_INVAL;
		SecretValue *v = secret_value_new (len ? (const char *) buf : "", len, "application/octet-stream");
		std::string label = std::string ("Korin ") + app + " : " + name;
		gboolean ok = secret_password_store_binary_sync (schema (), SECRET_COLLECTION_DEFAULT, label.c_str (), v, 0, &e,
			"app", app, "name", name.c_str (), "home", home, NULL);
		secret_value_unref (v);
		return ok ? 0 : fail (e, E_INVAL);
	}
	case KV_GET:
	{
		SecretValue *v = secret_password_lookup_binary_sync (schema (), 0, &e, "app", app, "name", name.c_str (), "home", home, NULL);
		if (!v) return e ? fail (e, E_INVAL) : E_NOENT;
		gsize n = 0;
		const gchar *p = secret_value_get (v, &n);
		long r = copy_out (p, n, buf, len);
		secret_value_unref (v);
		return r;
	}
	case KV_DELETE:
	{
		gboolean ok = secret_password_clear_sync (schema (), 0, &e, "app", app, "name", name.c_str (), "home", home, NULL);
		if (e) return fail (e, E_INVAL);
		return ok ? 0 : E_NOENT;
	}
	}
	return E_INVAL;
}

long handler (long n, long a, long b)
{
	(void) a;
	if (n == 0x31) return status () < 0 ? E_NOSYS : 0;	// KV_SVC_OPEN : le canal 0
	unsigned long *m = (unsigned long *) b;
	long r = call (m);
	if (r < 0) return r;
	m[0] = (unsigned long) r;
	return 0;
}

} // namespace

extern "C" int kport_vault_install (void) { kport_vault_handler = handler; return 1; }

#else

extern "C" int kport_vault_install (void) { return 0; }	// (construit sans libsecret : pas de coffre)

#endif
