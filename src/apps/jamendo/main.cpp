//
// main.cpp -- Jamendo : la musique libre (Creative Commons) de Jamendo dans Korin (docs/etude/MUSIQUE.md §2).
//
// L'API publique de Jamendo (v3.0, https://api.jamendo.com) avec le client_id de Korin (DEFAULT_ID : pas un secret,
// il passe en clair dans les adresses), ou celui de l'utilisateur s'il en a donné un (gratuit, sur
// devportal.jamendo.com ; gardé dans le coffre, « jamendo.client_id ») : les titres populaires, les nouveautés,
// une recherche, et des radios par genre (les titres d'un genre l'un après l'autre, la suite chargée quand la file
// se vide). Un titre se joue par AudioKit (le Player du Media Player : MP3, son thread) après son arrivée dans
// HOME:/.cache/jamendo ; Télécharger le garde dans HOME:/music/Jamendo (le Media Player l'y trouve) quand sa
// licence le permet.
//
// Fermer la fenêtre pendant la lecture la réduit en carte en bas à droite, au-dessus des autres ; la musique
// continue (fermer la carte l'arrête), comme le Media Player.
//
// Le réseau passe par deux fils (une liste, un titre) ; la fenêtre ne les attend jamais.
//
#include "../media/sys.h"
#include "../media/player.h"
#include "../pocketqi/http.h"
#include "../koton/ui/json.hpp"
#include "../../lib/keyvault/keyvault.h"
#include <string>
#include <vector>

using media::i64;
using media::now_ms;

#define TR(s)		((const char *) ui_tr ((long) (s)))
#define API		"https://api.jamendo.com/v3.0/tracks/?format=json&limit=50&audioformat=mp32&include=licenses"
#define KV_NAME		"jamendo.client_id"
#define DEFAULT_ID	"79a18255"	// (le client_id de Korin ; quota partagé : un autre, à soi, dans le coffre le remplace)
#define BAR_H		76
#define SIDE_W		210
#define MINI_W		330
#define MINI_H		92

static struct ui_vars ui;
static const unsigned *s_tk;
static unsigned tok (int t) { return s_tk[t / 4] & 0xFFFFFF; }

// ---- les titres ------------------------------------------------------------------------------------------------------

struct Track
{
	std::string id, name, artist, album, audio, download, license;
	int secs = 0;
	bool canDownload = false;
};

static std::vector<Track> g_tracks;			// la liste montrée (et la file de lecture)
static int g_cur = -1;					// le titre joué (dans g_tracks), -1 : aucun
static Track g_playing;					// sa copie (la liste peut changer pendant qu'il joue)
static std::string g_clientId;

// d'où vient la liste : 1 populaires, 2 nouveautés, 3 recherche, 100 + n : la radio du genre n
static int g_source = 1, g_offset;
static std::string g_query;

struct Genre { const char *label, *tag; };
static const Genre GENRES[] = {
	{ "Rock", "rock" }, { "Électronique", "electronic" }, { "Pop", "pop" }, { "Jazz", "jazz" },
	{ "Classique", "classical" }, { "Hip-hop", "hiphop" }, { "Lounge", "lounge" }, { "Ambient", "ambient" },
	{ "Musiques du monde", "world" }, { "Métal", "metal" }, { "Folk", "folk" }, { "Musiques de film", "soundtrack" },
};
enum { NGENRES = (int) (sizeof GENRES / sizeof GENRES[0]) };

// ---- le réseau : GET en suivant les redirections (les liens audio de Jamendo en font) --------------------------------

static bool keep_head (void *ctx, const std::string &all)
{
	std::string *h = (std::string *) ctx;
	if (h->empty ())
	{
		size_t e = all.find ("\r\n\r\n");
		if (e != std::string::npos) *h = all.substr (0, e);
	}
	return true;
}

// -> code HTTP (0 : pas de réponse, err dit pourquoi), le corps dans body
static int get_follow (std::string url, std::string &body, std::string &err, volatile int *cancel, int timeoutMs)
{
	for (int hop = 0; hop < 5; hop++)
	{
		std::string head;
		int code = pq::http_request (true, url, "", "", body, err, cancel, timeoutMs, keep_head, &head);
		if (code < 300 || code >= 400) return code;
		std::string low = head;
		for (auto &c : low) c = (char) tolower ((unsigned char) c);
		size_t p = low.find ("\nlocation:");
		if (p == std::string::npos) return code;
		size_t b = p + 10, e = head.find ("\r\n", b);
		std::string loc = head.substr (b, e == std::string::npos ? std::string::npos : e - b);
		while (!loc.empty () && loc[0] == ' ') loc.erase (0, 1);
		if (loc.compare (0, 1, "/") == 0)
		{					// (relative : même hôte)
			pq::Url u;
			u.parse (url);
			loc = (u.https ? "https://" : "http://") + u.host + loc;
		}
		url = loc;
	}
	err = "trop de redirections";
	return 0;
}

static std::string urlenc (const std::string &s)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string o;
	for (unsigned char c : s)
		if (isalnum (c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char) c;
		else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
	return o;
}

// les noms de l'API arrivent parfois avec des entités HTML (« Bessonn&amp;sa ») : &amp; &lt; &gt; &quot; &apos; &#N; &#xN;
static void utf8_put (std::string &o, unsigned c)
{
	if (c < 0x80) o += (char) c;
	else if (c < 0x800) { o += (char) (0xC0 | c >> 6); o += (char) (0x80 | (c & 63)); }
	else if (c < 0x10000) { o += (char) (0xE0 | c >> 12); o += (char) (0x80 | (c >> 6 & 63)); o += (char) (0x80 | (c & 63)); }
	else { o += (char) (0xF0 | c >> 18); o += (char) (0x80 | (c >> 12 & 63)); o += (char) (0x80 | (c >> 6 & 63)); o += (char) (0x80 | (c & 63)); }
}

static std::string unhtml (const char *s)
{
	static const struct { const char *n; char c; } NAMED[] = {
		{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
	};
	std::string o;
	for (; *s; s++)
	{
		const char *e = *s == '&' ? strchr (s, ';') : 0;
		if (!e || e - s > 10) { o += *s; continue; }
		std::string n (s + 1, (size_t) (e - s - 1));
		bool done = false;
		if (n.size () > 1 && n[0] == '#')
		{
			unsigned long c = n[1] == 'x' || n[1] == 'X' ? strtoul (n.c_str () + 2, 0, 16) : strtoul (n.c_str () + 1, 0, 10);
			if (c > 0 && c < 0x110000) { utf8_put (o, (unsigned) c); done = true; }
		}
		else
			for (const auto &k : NAMED)
				if (n == k.n) { o += k.c; done = true; break; }
		if (done) s = e;
		else o += *s;
	}
	return o;
}

// ---- les deux travaux en fond : une liste, un titre ---------------------------------------------------------------------

struct Job
{
	pthread_t th;
	bool busy = false;
	volatile int done = 0, cancel = 0;
	// demandé
	std::string url, dest;			// dest : où écrire le corps (un titre) ; vide : une liste
	bool append = false;			// une liste : à la suite (la radio)
	// rendu
	int code = 0;
	std::string err;
	std::vector<Track> tracks;
};
static Job g_list, g_song;
static Track g_songWant;			// le titre dont g_song va chercher le son
static bool g_songSave;				// g_song : Télécharger (et pas jouer)

static std::string home_path (const char *rel) { return std::string ("HOME:/") + rel; }

static void mkdirs (const std::string &p)
{
	for (size_t i = 6; i <= p.size (); i++)
		if (i == p.size () || p[i] == '/') mkdir (p.substr (0, i).c_str (), 0755);
}

static void *job_main (void *a)
{
	Job *j = (Job *) a;
	std::string body;
	j->code = get_follow (j->url, body, j->err, &j->cancel, j->dest.empty () ? 20000 : 60000);
	if (j->code == 200 && j->dest.empty ())
	{					// une liste : ses titres
		json::Doc d;
		if (!d.parse (body.data (), (unsigned long) body.size ())) { j->code = 0; j->err = "réponse illisible"; }
		else
		{
			const json::Value &h = d.root ()["headers"];
			if (strcmp (h["status"].asStr (), "success"))
			{
				j->code = 0;
				j->err = h["error_message"].asStr ("erreur du service");
			}
			for (const json::Value *t = d.root ()["results"].first (); t; t = t->next)
			{
				Track k;
				const json::Value &v = *t;
				k.id = v["id"].asStr ();
				k.name = unhtml (v["name"].asStr ());
				k.artist = unhtml (v["artist_name"].asStr ());
				k.album = unhtml (v["album_name"].asStr ());
				k.audio = v["audio"].asStr ();
				k.download = v["audiodownload"].asStr ();
				k.canDownload = v["audiodownload_allowed"].asBool () && !k.download.empty ();
				k.license = v["license_ccurl"].asStr ();
				k.secs = v["duration"].asInt ();
				if (!k.audio.empty ()) j->tracks.push_back (k);
			}
		}
	}
	else if (j->code == 200)
	{					// un titre : dans son fichier
		mkdirs (j->dest.substr (0, j->dest.rfind ('/')));
		std::string tmp = j->dest + ".part";
		FILE *f = fopen (tmp.c_str (), "wb");
		if (!f || fwrite (body.data (), 1, body.size (), f) != body.size ())
		{
			j->code = 0;
			j->err = "écriture impossible";
		}
		if (f) fclose (f);
		if (j->code == 200) rename (tmp.c_str (), j->dest.c_str ());
		else unlink (tmp.c_str ());
	}
	__atomic_store_n (&j->done, 1, __ATOMIC_RELEASE);
	return 0;
}

static bool job_start (Job &j, const std::string &url, const std::string &dest)
{
	if (j.busy) return false;
	j.url = url;
	j.dest = dest;
	j.done = j.cancel = 0;
	j.code = 0;
	j.err.clear ();
	j.tracks.clear ();
	j.busy = pthread_create (&j.th, 0, job_main, &j) == 0;
	return j.busy;
}

// fini -> 1 (les résultats sont à lire, le fil est rejoint)
static bool job_finished (Job &j)
{
	if (!j.busy || !__atomic_load_n (&j.done, __ATOMIC_ACQUIRE)) return false;
	pthread_join (j.th, 0);
	j.busy = false;
	return true;
}

// ---- la fenêtre -------------------------------------------------------------------------------------------------------

static long g_side, g_search, g_go, g_list_w, g_status, g_bar, g_mini;
static media::Player g_player;
static bool g_miniMode, g_quit;
static int g_restore[2];
static long g_restorePos[2] = { -1, -1 };
static int g_lastEnded;
static std::string g_pendingUrl;			// une liste demandée pendant qu'une autre arrive
static bool g_pendingAppend;

static void status (const char *s) { ui_w_set_text (g_status, (long) s); }

static void list_fill ()
{
	ui_list_clear (g_list_w);
	char row[600], dur[16];
	for (size_t i = 0; i < g_tracks.size (); i++)
	{
		const Track &t = g_tracks[i];
		snprintf (dur, sizeof dur, "%d:%02d", t.secs / 60, t.secs % 60);
		snprintf (row, sizeof row, "%s\t%s\t%s\t%s", t.name.c_str (), t.artist.c_str (), t.album.c_str (), dur);
		ui_list_add (g_list_w, (long) row, (long) i);
	}
	ui_w_draw (g_list_w);			// (la liste arrive du fil : sans cela elle reste blanche jusqu'au survol)
	ui_update_all ();
}

// la liste de la source choisie (append : la suite, pour la radio)
static void load_list (bool append)
{
	if (g_clientId.empty ())
	{
		status (TR ("Il faut une clé d'accès de Jamendo : Fichier, Clé d'accès Jamendo…"));
		return;
	}
	std::string url = std::string (API) + "&client_id=" + urlenc (g_clientId);
	if (g_source == 1) url += "&order=popularity_week";
	else if (g_source == 2) url += "&order=releasedate_desc";
	else if (g_source == 3) url += "&search=" + urlenc (g_query);
	else url += "&order=popularity_month&tags=" + std::string (GENRES[g_source - 100].tag);
	if (!append) g_offset = 0;
	url += "&offset=" + std::to_string (g_offset);
	if (g_list.busy)
	{					// (la précédente est abandonnée, celle-ci suivra)
		g_list.cancel = 1;
		g_pendingUrl = url;
		g_pendingAppend = append;
		return;
	}
	g_list.append = append;
	job_start (g_list, url, "");
	status (TR ("Chargement…"));
}

static std::string cache_path (const Track &t) { return home_path (".cache/jamendo/") + t.id + ".mp3"; }

static void play_index (int i)
{
	if (i < 0 || i >= (int) g_tracks.size ()) return;
	g_cur = i;
	g_playing = g_tracks[i];
	ui_w_set_value (g_list_w, i);
	std::string p = cache_path (g_playing);
	struct stat st;
	if (stat (p.c_str (), &st) == 0 && st.st_size > 0)
	{					// (déjà là)
		g_player.play (p.c_str ());
		status ("");
	}
	else
	{
		if (g_song.busy) g_song.cancel = 1;	// (un autre arrivait : il est abandonné, on attend sa fin)
		g_songWant = g_playing;
		g_songSave = false;
		if (!g_song.busy) job_start (g_song, g_playing.audio, p);
		status (TR ("Chargement du titre…"));
	}
	ui_view_dirty (g_bar);
	ui_view_dirty (g_mini);
}

static void play_next ()
{
	if (g_cur + 1 < (int) g_tracks.size ()) play_index (g_cur + 1);
	else if (g_source >= 100 && !g_list.busy)
	{					// la radio : la suite du genre
		g_offset += 50;
		load_list (true);
	}
}

static void play_prev () { if (g_cur > 0) play_index (g_cur - 1); }

static void toggle_play ()
{
	if (g_player.state == media::PS_PLAYING) g_player.pause ();
	else if (g_player.state == media::PS_PAUSED) g_player.resume ();
	else if (g_cur >= 0) play_index (g_cur);
	else if (!g_tracks.empty ()) play_index (0);
}

static void save_current ()
{
	if (g_cur < 0) { status (TR ("Choisissez d'abord un titre.")); return; }
	if (!g_playing.canDownload) { status (TR ("La licence de ce titre ne permet pas de le télécharger.")); return; }
	if (g_song.busy) { status (TR ("Un titre arrive déjà : réessayez dans un instant.")); return; }
	std::string name = g_playing.artist + " - " + g_playing.name;
	for (auto &c : name) if (strchr ("/\\:*?\"<>|", c)) c = '_';
	g_songWant = g_playing;
	g_songSave = true;
	job_start (g_song, g_playing.download, home_path ("music/Jamendo/") + name + ".mp3");
	status (TR ("Téléchargement…"));
}

// ---- la barre de lecture (et le mini-lecteur : la même, en carte) -------------------------------------------------------

enum { H_PREV = 1, H_PLAY, H_NEXT, H_SEEK, H_EXPAND };
struct Hit { int x, y, w, h, kind; };
static std::vector<Hit> g_hits;
static int g_hot = -1;

static void fmt_time (char *b, size_t n, i64 ms) { int s = (int) (ms / 1000); snprintf (b, n, "%d:%02d", s / 60, s % 60); }

static void tri (int x, int y, int s, bool left, unsigned c)
{
	int32_t p[6];
	if (left) { p[0] = (x + s) * 16; p[1] = y * 16; p[2] = (x + s) * 16; p[3] = (y + s) * 16; p[4] = x * 16; p[5] = (y + s / 2) * 16; }
	else { p[0] = x * 16; p[1] = y * 16; p[2] = x * 16; p[3] = (y + s) * 16; p[4] = (x + s) * 16; p[5] = (y + s / 2) * 16; }
	ui_vp_poly ((long) p, 3, c);
}

static void paint_controls (long me, long W, long H, bool mini)
{
	unsigned bg = mini ? tok (TJ_SURFACE) : tok (TJ_FACE), ink = tok (TJ_TEXT1), dim = tok (TJ_TEXT2), acc = tok (TJ_ACCENT);
	ui_fill (0, 0, W, H, bg);
	if (!mini) ui_fill (0, 0, W, 1, tok (TJ_LINE));
	g_hits.clear ();
	bool has = g_cur >= 0;
	// les boutons : précédent, lecture, suivant
	int by = mini ? 52 : (int) H / 2 - 16, bx = mini ? 14 : 16;
	auto hot = [&] (int k) { return g_hot >= 0 && g_hot < (int) g_hits.size () && g_hits[g_hot].kind == k; };
	g_hits.push_back ({ bx, by, 32, 32, H_PREV });
	tri (bx + 9, by + 9, 14, true, hot (H_PREV) ? acc : ink);
	ui_fill (bx + 7, by + 9, 2, 14, hot (H_PREV) ? acc : ink);
	int px = bx + 38;
	g_hits.push_back ({ px, by - 2, 36, 36, H_PLAY });
	ui_vp_disc ((px + 18) * 16, (by + 16) * 16, 18 * 16, acc);
	unsigned on = 0xFFFFFF;
	if (g_player.state == media::PS_PLAYING) { ui_fill (px + 12, by + 8, 4, 16, on); ui_fill (px + 20, by + 8, 4, 16, on); }
	else tri (px + 13, by + 8, 16, false, on);
	int nx = px + 44;
	g_hits.push_back ({ nx, by, 32, 32, H_NEXT });
	tri (nx + 9, by + 9, 14, false, hot (H_NEXT) ? acc : ink);
	ui_fill (nx + 23, by + 9, 2, 14, hot (H_NEXT) ? acc : ink);
	// le titre, l'artiste
	int tx = mini ? 14 : nx + 48, tw = (int) W - tx - (mini ? 40 : 150);
	const char *title = has ? g_playing.name.c_str () : TR ("Rien en lecture");
	ui_text_fit ((long) title, tx, mini ? 24 : (int) H / 2 - 6, ink, 14, (long) ui.font_b, tw);
	if (has) ui_text_fit ((long) g_playing.artist.c_str (), tx, mini ? 42 : (int) H / 2 + 12, dim, 12, (long) ui.font, tw);
	// la position
	i64 len = g_player.lenMs > 0 ? g_player.lenMs : (i64) g_playing.secs * 1000, pos = g_player.posMs;
	int sx = mini ? px + 46 + 44 : tx, sw = mini ? (int) W - sx - 14 : (int) W - tx - 150, sy = mini ? by + 15 : (int) H - 12;
	if (!mini) { sx = tx; sw = (int) W - tx - 160; }
	int fill = len > 0 ? (int) (pos * sw / len) : 0;
	if (fill > sw) fill = sw;
	if (sw > 20)
	{
		ui_rbox (sx, sy, sw, 4, 2, tok (TJ_LINE), tok (TJ_LINE));
		if (fill > 0) ui_rbox (sx, sy, fill, 4, 2, acc, acc);
		g_hits.push_back ({ sx, sy - 6, sw, 16, H_SEEK });
	}
	if (!mini)
	{
		char t[40], a[16], b[16];
		fmt_time (a, sizeof a, pos);
		fmt_time (b, sizeof b, len);
		snprintf (t, sizeof t, "%s / %s", a, b);
		long w = ui_text_width ((long) t, 12, (long) ui.font);
		ui_text ((long) t, W - 16 - w, H / 2 + 4, dim, 12, (long) ui.font, 0);
	}
	else
	{					// la carte : agrandir (en haut à droite)
		g_hits.push_back ({ (int) W - 32, 6, 26, 26, H_EXPAND });
		unsigned c = hot (H_EXPAND) ? acc : dim;
		ui_frame (W - 26, 12, 14, 12, c);
		ui_fill (W - 26, 12, 14, 3, c);
	}
	(void) me;
}

static void bar_paint (long me, long w, long h) { paint_controls (me, w, h, false); }
static void mini_paint (long me, long w, long h) { paint_controls (me, w, h, true); }

static void enter_mini (bool on);

static long ctl_mouse (long me, long kind, long x, long y, long info)
{
	(void) info;
	int nh = -1;
	for (size_t i = 0; i < g_hits.size (); i++)
	{
		const Hit &h = g_hits[i];
		if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) nh = (int) i;
	}
	if (nh != g_hot) { g_hot = nh; ui_view_dirty (me); }
	if (kind != 1 || nh < 0) return 0;
	const Hit &h = g_hits[nh];
	switch (h.kind)
	{
	case H_PREV: play_prev (); break;
	case H_PLAY: toggle_play (); break;
	case H_NEXT: play_next (); break;
	case H_EXPAND: enter_mini (false); break;
	case H_SEEK:
		if (g_player.lenMs > 0) g_player.seek (g_player.lenMs * (x - h.x) / (h.w ? h.w : 1));
		break;
	}
	ui_view_dirty (me);
	return 0;
}

// ---- la disposition, le mini-lecteur ------------------------------------------------------------------------------------

static long on_resize (long w, long h)
{
	ui_fill (0, 0, w, h, tok (TJ_FACE));
	if (g_miniMode)
	{
		ui_w_move (g_mini, 0, 0, w, h);
		ui_view_dirty (g_mini);
	}
	else
	{
		int ctl = (int) ui_metric (UMT_CTL), m = 12, x0 = SIDE_W + m;
		ui_w_move (g_side, 0, 0, SIDE_W, h - BAR_H);
		ui_w_move (g_search, x0, m, w - x0 - FRAME_CAP_W - 110, ctl);
		ui_w_move (g_go, w - FRAME_CAP_W - 100, m, 92, ctl);
		int ly = m + ctl + 10;
		ui_w_move (g_list_w, x0, ly, w - x0 - m, h - BAR_H - ly - 28);
		ui_w_move (g_status, x0, h - BAR_H - 24, w - x0 - m, 20);
		ui_w_move (g_bar, 0, h - BAR_H, w, BAR_H);
		ui_view_dirty (g_bar);
	}
	ui_w_draw (0);
	ui_view_flush ();
	ui_update_all ();
	return 0;
}

static void enter_mini (bool on)
{
	if (on == g_miniMode) return;
	g_miniMode = on;
	long ws[] = { g_side, g_search, g_go, g_list_w, g_status, g_bar };
	for (long w : ws) ui_w_show (w, !on);
	ui_w_show (g_mini, on);
	g_hot = -1;
	if (on)
	{					// une carte en bas à droite, au-dessus des autres
		g_restore[0] = (int) ui.w;
		g_restore[1] = (int) ui.h;
		if (ui_window_pos ((long) g_restorePos) < 0) g_restorePos[0] = -1;
		ui_min_size (MINI_W, MINI_H);
		ui_resize (MINI_W, MINI_H);
		long scr[4] = { 0, 0, 0, 0 };
		if (ui_screen ((long) scr) >= 0 && scr[0] > 0)
			ui_window_place (scr[0] - MINI_W - FRAME_BORDER - 16, scr[1] - scr[3] - MINI_H - FRAME_BORDER - 16);
		ui_window_top (1);
	}
	else
	{
		ui_window_top (0);
		ui_min_size (640, 420);
		ui_resize (g_restore[0], g_restore[1]);
		if (g_restorePos[0] >= 0) ui_window_place (g_restorePos[0], g_restorePos[1]);
	}
	on_resize (ui.w, ui.h);
}

// ---- la clé d'accès -------------------------------------------------------------------------------------------------------

static void ask_key ()
{
	char b[128];
	snprintf (b, sizeof b, "%s", g_clientId.c_str ());
	if (!ui_ask ((long) TR ("Clé d'accès Jamendo"),
		     (long) TR ("Le client_id d'une application Jamendo à vous (gratuit, sur devportal.jamendo.com) ; vide : celui de Korin :"), (long) b, sizeof b))
		return;
	media::tidy (b);
	if (!b[0] || !strcmp (b, DEFAULT_ID))	// (vide : de retour à celui de Korin)
	{
		kv_delete (KV_NAME);
		g_clientId = DEFAULT_ID;
		load_list (false);
		return;
	}
	g_clientId = b;
	if (kv_set (KV_NAME, b, (long) strlen (b)) != 0) status (TR ("Le coffre n'a pas pu garder la clé : elle vaut jusqu'à la fermeture."));
	load_list (false);
}

// ---- les menus, les événements ----------------------------------------------------------------------------------------------

enum { M_PLAY = 1, M_NEXT, M_PREV, M_SAVE, M_KEY, M_CLOSE, M_SEARCH };
static const char MENU[] = "MFichier\nI4\tTélécharger le titre\tCtrl+S\nI5\tClé d'accès Jamendo…\n-\nI6\tFermer\tCtrl+W\n"
	"MLecture\nI1\tLecture / pause\tEspace\nI2\tSuivant\tCtrl+→\nI3\tPrécédent\tCtrl+←\n-\nI7\tRechercher\tCtrl+F\n";

static void command (long id)
{
	switch (id)
	{
	case M_PLAY: toggle_play (); break;
	case M_NEXT: play_next (); break;
	case M_PREV: play_prev (); break;
	case M_SAVE: save_current (); break;
	case M_KEY: ask_key (); break;
	case M_CLOSE: g_quit = true; break;
	case M_SEARCH: if (g_miniMode) enter_mini (false); ui_w_focus (g_search); break;
	}
	ui_view_dirty (g_bar);
}

static void side_fill ()
{
	ui_list_add (g_side, (long) (std::string ("#") + TR ("Découvrir")).c_str (), -1);
	ui_list_add (g_side, (long) TR ("Populaires cette semaine"), 1);
	ui_list_add (g_side, (long) TR ("Nouveautés"), 2);
	ui_list_add (g_side, (long) (std::string ("#") + TR ("Radios")).c_str (), -1);
	for (int i = 0; i < NGENRES; i++) ui_list_add (g_side, (long) TR (GENRES[i].label), 100 + i);
}

static void side_pick ()
{
	long n = ui_w_get_value (g_side), data = -1;
	if (n < 0) return;
	uikit_list_get (g_side, n, &data);
	if (data < 0) return;
	g_source = (int) data;
	load_list (false);
}

static void poll_jobs ()
{
	if (job_finished (g_list))
	{
		if (!g_pendingUrl.empty ())
		{					// (une autre a été demandée entre-temps)
			std::string u = g_pendingUrl;
			g_pendingUrl.clear ();
			g_list.append = g_pendingAppend;
			job_start (g_list, u, "");
		}
		else if (g_list.code != 200)
			status ((std::string (TR ("Jamendo ne répond pas : ")) + (g_list.err.empty () ? std::to_string (g_list.code) : g_list.err)).c_str ());
		else
		{
			bool more = g_list.append;
			if (!more) { g_tracks = g_list.tracks; g_cur = -1; }
			else g_tracks.insert (g_tracks.end (), g_list.tracks.begin (), g_list.tracks.end ());
			list_fill ();
			char t[80];
			snprintf (t, sizeof t, TR ("%d titres"), (int) g_tracks.size ());
			status (g_tracks.empty () ? TR ("Aucun titre.") : t);
			if (more && g_cur + 1 < (int) g_tracks.size ()) play_index (g_cur + 1);	// (la radio continue)
		}
	}
	if (job_finished (g_song))
	{
		if (g_song.code != 200)
		{
			if (!g_song.cancel) status ((std::string (TR ("Le titre n'a pas pu venir : ")) + g_song.err).c_str ());
		}
		else if (g_songSave)
			status ((std::string (TR ("Téléchargé dans ")) + g_song.dest).c_str ());
		else if (g_songWant.id == g_playing.id)
		{
			g_player.play (g_song.dest.c_str ());
			status ("");
		}
		if (!g_songSave && g_songWant.id != g_playing.id && g_cur >= 0)
			play_index (g_cur);		// (un autre titre a été choisi pendant ce temps)
	}
}

int main (int argc, char **argv)
{
	(void) argc;
	(void) argv;
	if (!uikit_open (&ui)) return 1;
	if (!audiokit_kit_open ()) return 1;	// (le Player du Media Player : ak_play…, sans le kit un appel à 0)
	s_tk = (const unsigned *) ui_tokens ();
	long sw = 980, sh = 640, scr[4] = { 0, 0, 0, 0 };
	if (ui_screen ((long) scr) >= 0 && scr[0] > 0)
	{
		if (sw > scr[0] - 60) sw = scr[0] - 60;
		if (sh > scr[1] - scr[2] - scr[3] - 80) sh = scr[1] - scr[2] - scr[3] - 80;
	}
	if (ui_window_ex (-1, -1, sw, sh, (long) "Jamendo", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (640, 420);
	ui_on_resize ((long) on_resize, 0);
	ui_menu ((long) MENU);

	g_side = ui_list (0, 0, 10, 10, UL_SIDEBAR);
	g_search = ui_textbox (0, 0, 10, 30, 0);
	ui_textbox_hint (g_search, (long) TR ("Rechercher des titres, des artistes, des albums"));
	g_go = ui_pushbutton (0, 0, 10, 30, (long) TR ("Chercher"), UB_DEFAULT);
	g_list_w = ui_list (0, 0, 10, 10, UL_HEADER | UL_STRIPES | UL_NOSORT);
	ui_list_column (g_list_w, (long) TR ("Titre"), 260, 0);
	ui_list_column (g_list_w, (long) TR ("Artiste"), 170, 0);
	ui_list_column (g_list_w, (long) TR ("Album"), 170, 0);
	ui_list_column (g_list_w, (long) TR ("Durée"), 60, ULC_RIGHT);
	g_status = ui_label (0, 0, 10, 20, (long) "", ULB_DIM);
	g_bar = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_bar, (long) bar_paint, (long) ctl_mouse, 0, 0);
	g_mini = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_mini, (long) mini_paint, (long) ctl_mouse, 0, 0);
	ui_w_show (g_mini, 0);
	side_fill ();
	ui_w_set_value (g_side, 1);

	char key[128];
	long n = kv_get (KV_NAME, key, sizeof key - 1);
	if (n > 0) { key[n] = 0; g_clientId = key; }
	else g_clientId = DEFAULT_ID;
	g_player.start ();
	g_lastEnded = g_player.endedGen;
	on_resize (ui.w, ui.h);
	if (g_clientId.empty ()) ask_key ();
	else load_list (false);

	i64 lastPaint = 0;
	while (!g_quit)
	{
		long t = ui_event (50);
		if (t == WEV_CLOSE)
		{					// la musique continue en carte ; la carte fermée : tout s'arrête
			if (!g_miniMode && g_player.state == media::PS_PLAYING) { enter_mini (true); continue; }
			break;
		}
		if (t == WEV_THEME) { s_tk = (const unsigned *) ui_tokens (); on_resize (ui.w, ui.h); }
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0, wd = uikit_w_event (&code, &taken);
			if (code == UN_ACTIVATE && (wd == g_go || wd == g_search))
			{
				g_query = (const char *) ui_w_get_text (g_search);
				if (!g_query.empty ()) { g_source = 3; load_list (false); }
			}
			else if (code == UN_ACTIVATE && wd == g_list_w) play_index ((int) ui_w_get_value (g_list_w));
			else if (code == UN_CHANGE && wd == g_side) side_pick ();
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) command (id);
				else if (ui_ev_field (&ui, EV_CODE) == 0x2C) toggle_play ();	// Espace
			}
		}
		else ui_w_idle ();
		poll_jobs ();
		g_player.poll ();
		if (g_player.endedGen != g_lastEnded) { g_lastEnded = g_player.endedGen; play_next (); }
		if (now_ms () - lastPaint > 500) { lastPaint = now_ms (); ui_view_dirty (g_miniMode ? g_mini : g_bar); }
		ui_view_flush ();
	}
	g_player.quit ();
	g_list.cancel = g_song.cancel = 1;
	if (g_list.busy) pthread_join (g_list.th, 0);
	if (g_song.busy) pthread_join (g_song.th, 0);
	return 0;
}
