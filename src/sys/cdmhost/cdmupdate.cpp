// cdmupdate.cpp -- installe ou met à jour le module Widevine de Google pour Jet (docs/etude/WIDEVINE.md, étape 7),
// par le canal de Chrome : le serveur de mise à jour de composants de Google (protocole Omaha 3.1, composant
// « WidevineCdm », oimompecagnajdejgnnjijobebaeigek) donne la version courante, ses adresses chez Google, sa taille et
// son SHA-256 ; l'archive .crx3 est téléchargée chez Google, vérifiée, et son module Linux x86-64 installé.
//
//   cdmupdate [--dir DOSSIER] [--check]
//
// DOSSIER : $HOME/.jet/widevine par défaut (là où CDMWidevineOnyx le cherche) : libwidevinecdm.so, manifest.json,
// version.txt (la version installée), checked.txt (la date de la dernière demande, en secondes). Remplacé d'un coup
// (un fichier neuf renommé) ; la version installée n'est pas retéléchargée. --check : seulement dire s'il y a une
// version plus récente (sortie 0 : à jour, 2 : une version plus récente, 1 : erreur).
// Sortie 0 : installé ou déjà à jour ; 1 : une erreur (message sur stderr). La dernière ligne de stdout dit ce
// qui a été fait (Jet la montre).
//
// Le module n'est jamais dans nos paquets : seul Google le distribue ; il est téléchargé chez lui, à la demande de
// l'utilisateur (la case de Jet).
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <openssl/sha.h>
#include <zlib.h>

#include "net.h"

static const char *APPID = "oimompecagnajdejgnnjijobebaeigek";
static const char *SERVER = "https://update.googleapis.com/service/update2/json";
static const char *MODULE_IN_ZIP = "_platform_specific/linux_x64/libwidevinecdm.so";

// ---- un peu de JSON : la valeur texte ou nombre d'une clé, à partir d'une position ----
static std::string json_value (const std::string &j, const char *key, size_t from = 0, size_t *at = nullptr)
{
	std::string k = std::string ("\"") + key + "\"";
	size_t p = j.find (k, from);
	if (p == std::string::npos)
		return "";
	p = j.find (':', p + k.size ());
	if (p == std::string::npos)
		return "";
	p++;
	while (p < j.size () && (j[p] == ' ' || j[p] == '\n' || j[p] == '\r' || j[p] == '\t'))
		p++;
	if (at)
		*at = p;
	if (p < j.size () && j[p] == '"')
	{
		size_t e = j.find ('"', p + 1);
		return e == std::string::npos ? "" : j.substr (p + 1, e - p - 1);
	}
	size_t e = j.find_first_of (",}]", p);
	return j.substr (p, e == std::string::npos ? std::string::npos : e - p);
}

static bool read_file (const std::string &path, std::string &out)
{
	FILE *f = fopen (path.c_str (), "rb");
	if (!f)
		return false;
	char b[4096];
	size_t n;
	out.clear ();
	while ((n = fread (b, 1, sizeof b, f)) > 0)
		out.append (b, n);
	fclose (f);
	return true;
}

static bool write_file (const std::string &path, const void *data, size_t size)
{
	// (un fichier neuf, renommé à la fin : jamais un module à moitié écrit)
	std::string tmp = path + ".new";
	FILE *f = fopen (tmp.c_str (), "wb");
	if (!f)
		return false;
	bool ok = fwrite (data, 1, size, f) == size;
	ok = fclose (f) == 0 && ok;
	if (ok)
	{
		unlink (path.c_str ());
		ok = rename (tmp.c_str (), path.c_str ()) == 0;
	}
	if (!ok)
		unlink (tmp.c_str ());
	return ok;
}

// une version a.b.c.d plus récente que b
static bool newer (const std::string &a, const std::string &b)
{
	unsigned x[4] = {}, y[4] = {};
	sscanf (a.c_str (), "%u.%u.%u.%u", &x[0], &x[1], &x[2], &x[3]);
	sscanf (b.c_str (), "%u.%u.%u.%u", &y[0], &y[1], &y[2], &y[3]);
	for (int i = 0; i < 4; i++)
		if (x[i] != y[i])
			return x[i] > y[i];
	return false;
}

static uint32_t le32 (const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24; }
static uint32_t le16 (const uint8_t *p) { return p[0] | p[1] << 8; }

// une entrée d'un zip (le répertoire central, puis l'en-tête local) : ses octets décompressés et vérifiés
static bool zip_entry (const uint8_t *z, size_t n, const char *name, std::vector<uint8_t> &out)
{
	// la fin du répertoire central (EOCD), en partant de la fin
	if (n < 22)
		return false;
	size_t eocd = n - 22;
	while (eocd > 0 && le32 (z + eocd) != 0x06054b50)
		eocd--;
	if (le32 (z + eocd) != 0x06054b50)
		return false;
	size_t count = le16 (z + eocd + 10), cd = le32 (z + eocd + 16);
	size_t nlen = strlen (name);
	for (size_t i = 0, p = cd; i < count && p + 46 <= n && le32 (z + p) == 0x02014b50; i++)
	{
		size_t fl = le16 (z + p + 28), xl = le16 (z + p + 30), cl = le16 (z + p + 32);
		if (p + 46 + fl > n)
			return false;
		if (fl == nlen && !memcmp (z + p + 46, name, nlen))
		{
			uint32_t method = le16 (z + p + 10), crc = le32 (z + p + 16);
			size_t packed = le32 (z + p + 20), size = le32 (z + p + 24), local = le32 (z + p + 42);
			if (local + 30 > n || le32 (z + local) != 0x04034b50)
				return false;
			size_t data = local + 30 + le16 (z + local + 26) + le16 (z + local + 28);
			if (data + packed > n || size > (256u << 20))
				return false;
			out.resize (size);
			if (method == 0)
			{
				if (packed != size)
					return false;
				memcpy (out.data (), z + data, size);
			}
			else if (method == 8)
			{
				z_stream s = {};
				if (inflateInit2 (&s, -15) != Z_OK)
					return false;
				s.next_in = (Bytef *) (z + data);
				s.avail_in = (uInt) packed;
				s.next_out = out.data ();
				s.avail_out = (uInt) size;
				int r = inflate (&s, Z_FINISH);
				inflateEnd (&s);
				if (r != Z_STREAM_END || s.total_out != size)
					return false;
			}
			else
				return false;
			return crc32 (0, out.data (), (uInt) out.size ()) == crc;
		}
		p += 46 + fl + xl + cl;
	}
	return false;
}

static void say_done (const char *text, const std::string &version)
{
	printf ("%s %s\n", text, version.c_str ());
	fflush (stdout);
}

int main (int argc, char **argv)
{
	const char *home = getenv ("HOME");
	std::string dir = std::string (home && *home ? home : "/home") + "/.jet/widevine";
	bool check = false;
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp (argv[i], "--dir") && i + 1 < argc)
			dir = argv[++i];
		else if (!strcmp (argv[i], "--check"))
			check = true;
		else
		{
			fprintf (stderr, "usage : cdmupdate [--dir DOSSIER] [--check]\n");
			return 1;
		}
	}
	std::string installed;
	read_file (dir + "/version.txt", installed);
	installed = installed.substr (0, installed.find_first_of ("\r\n"));
	struct stat st;
	bool present = stat ((dir + "/libwidevinecdm.so").c_str (), &st) == 0;

	// 1. la version courante, chez Google (comme Chrome sous Linux x86-64)
	std::string q = std::string ("{\"request\":{\"@os\":\"linux\",\"@updater\":\"chromecrx\",\"acceptformat\":\"crx3\",") +
		"\"app\":[{\"appid\":\"" + APPID + "\",\"enabled\":true,\"installsource\":\"ondemand\",\"ping\":{\"r\":-2},"
		"\"updatecheck\":{},\"version\":\"" + (present && !installed.empty () ? installed : "0.0.0.0") + "\"}],"
		"\"arch\":\"x64\",\"dedup\":\"cr\",\"ismachine\":false,\"nacl_arch\":\"x86-64\","
		"\"os\":{\"arch\":\"x86_64\",\"platform\":\"Linux\",\"version\":\"6.8.0\"},\"prodchannel\":\"stable\","
		"\"prodversion\":\"142.0.7444.59\",\"protocol\":\"3.1\",\"updaterchannel\":\"stable\",\"updaterversion\":\"142.0.7444.59\"}}";
	std::vector<uint8_t> resp;
	int status = 0;
	if (!https_post (SERVER, std::vector<uint8_t> (q.begin (), q.end ()), resp, &status, "application/json") || status != 200)
	{
		fprintf (stderr, "cdmupdate : le serveur de Google ne répond pas (HTTP %d)\n", status);
		return 1;
	}
	std::string j (resp.begin (), resp.end ());
	time_t now = time (nullptr);
	std::string stamp = std::to_string ((long long) now) + "\n";
	mkdir ((std::string (home && *home ? home : "/home") + "/.jet").c_str (), 0700);
	mkdir (dir.c_str (), 0700);
	std::string uc_status = json_value (j, "status", j.find ("\"updatecheck\""));
	if (uc_status == "noupdate")
	{
		write_file (dir + "/checked.txt", stamp.data (), stamp.size ());
		say_done ("à jour :", installed);
		return 0;
	}
	if (uc_status != "ok")
	{
		fprintf (stderr, "cdmupdate : réponse de Google inattendue (%s) : %.300s\n", uc_status.c_str (), j.c_str ());
		return 1;
	}
	size_t man = j.find ("\"manifest\"");
	std::string version = json_value (j, "version", man);
	std::string sha = json_value (j, "hash_sha256", man);
	std::string name = json_value (j, "name", man);
	size_t size = (size_t) strtoull (json_value (j, "size", man).c_str (), nullptr, 10);
	if (version.empty () || sha.size () != 64 || name.empty () || !size)
	{
		fprintf (stderr, "cdmupdate : réponse de Google incomplète : %.300s\n", j.c_str ());
		return 1;
	}
	if (present && !newer (version, installed))
	{
		write_file (dir + "/checked.txt", stamp.data (), stamp.size ());
		say_done ("à jour :", installed);
		return 0;
	}
	if (check)
	{
		say_done ("disponible :", version);
		return 2;
	}

	// 2. l'archive, chez Google (les adresses https d'abord), vérifiée
	std::vector<std::string> bases;
	for (size_t at = j.find ("\"codebase\""); at != std::string::npos; at = j.find ("\"codebase\"", at + 10))
	{
		std::string b = json_value (j, "codebase", at);
		if (!b.compare (0, 8, "https://"))
			bases.insert (bases.begin (), b);
		else
			bases.push_back (b);
	}
	std::vector<uint8_t> crx;
	for (const std::string &b : bases)
	{
		fprintf (stderr, "cdmupdate : %s%s\n", b.c_str (), name.c_str ());
		if (!http_get (b + name, crx, &status, size + 1) || status != 200 || crx.size () != size)
		{
			fprintf (stderr, "cdmupdate : échec (HTTP %d, %zu octets sur %zu)\n", status, crx.size (), size);
			crx.clear ();
			continue;
		}
		uint8_t md[SHA256_DIGEST_LENGTH];
		SHA256 (crx.data (), crx.size (), md);
		char hex[65];
		for (int i = 0; i < 32; i++)
			snprintf (hex + 2 * i, 3, "%02x", md[i]);
		if (strcasecmp (hex, sha.c_str ()))
		{
			fprintf (stderr, "cdmupdate : empreinte SHA-256 fausse (%s)\n", hex);
			crx.clear ();
			continue;
		}
		break;
	}
	if (crx.empty ())
	{
		fprintf (stderr, "cdmupdate : l'archive %s n'a pas pu être téléchargée\n", version.c_str ());
		return 1;
	}

	// 3. le .crx3 : « Cr24 », version 3, la taille de son en-tête, puis le zip
	if (crx.size () < 12 || memcmp (crx.data (), "Cr24", 4) || le32 (crx.data () + 4) != 3 || 12 + (size_t) le32 (crx.data () + 8) > crx.size ())
	{
		fprintf (stderr, "cdmupdate : ce n'est pas une archive .crx3\n");
		return 1;
	}
	size_t zip = 12 + le32 (crx.data () + 8);
	std::vector<uint8_t> module, manifest;
	if (!zip_entry (crx.data () + zip, crx.size () - zip, MODULE_IN_ZIP, module) ||
	    !zip_entry (crx.data () + zip, crx.size () - zip, "manifest.json", manifest))
	{
		fprintf (stderr, "cdmupdate : l'archive n'a pas %s (ou elle est abîmée)\n", MODULE_IN_ZIP);
		return 1;
	}
	if (module.size () < 4 || memcmp (module.data (), "\x7f" "ELF", 4))
	{
		fprintf (stderr, "cdmupdate : le module n'est pas un ELF\n");
		return 1;
	}

	// 4. l'installation : le module d'abord, sa version ensuite
	std::string ver = version + "\n";
	if (!write_file (dir + "/libwidevinecdm.so", module.data (), module.size ()) ||
	    !write_file (dir + "/manifest.json", manifest.data (), manifest.size ()) ||
	    !write_file (dir + "/version.txt", ver.data (), ver.size ()) ||
	    !write_file (dir + "/checked.txt", stamp.data (), stamp.size ()))
	{
		fprintf (stderr, "cdmupdate : écriture dans %s impossible (%s)\n", dir.c_str (), strerror (errno));
		return 1;
	}
	fprintf (stderr, "cdmupdate : module %s installé dans %s (%zu octets)\n", version.c_str (), dir.c_str (), module.size ());
	say_done ("installé :", version);
	return 0;
}
