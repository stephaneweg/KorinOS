//
// main.cpp -- Spotify dans Korin (docs/etude/MUSIQUE.md §1) : un compte Premium (Famille l'est).
//
// Connexion : OAuth de Spotify par oauthd (compte « spotify/moi », fournisseur custom, le client_id d'une app que
// l'utilisateur crée sur developer.spotify.com, adresse de retour http://127.0.0.1:8898/) ; la page d'accord s'ouvre
// dans Jet, oauthd garde et rafraîchit les jetons (dans le coffre). Parcourir : l'API web de Spotify (titres
// aimés, mes playlists, recherche). Jouer : librespot construit pour Korin (src/lib/korinspot, tools/spotify) avec le
// jeton ; son PCM va dans un flux AudioKit, dans un fil.
//
// Fermer la fenêtre pendant la lecture la réduit en carte en bas à droite, au-dessus des autres ; la musique
// continue (fermer la carte l'arrête), comme le Media Player et Jamendo. Pas encore : Spotify Connect.
//
#include "../media/sys.h"
#include "../pocketqi/http.h"
#include "../koton/ui/json.hpp"
#include <netkit_abi.h>
#include <spawn.h>
#include "korinspot.h"
#include <string>
#include <vector>

extern char **environ;
using media::i64;
using media::now_ms;

#define TR(s)		((const char *) ui_tr ((long) (s)))
#define ACCOUNT		"spotify/moi"
#define PORT		8898
#define SCOPES		"streaming user-read-email user-read-private playlist-read-private playlist-read-collaborative user-library-read"
#define API		"https://api.spotify.com/v1"
#define BAR_H		76
#define SIDE_W		220
#define MINI_W		330
#define MINI_H		92

static struct ui_vars ui;
static const unsigned *s_tk;
static unsigned tok (int t) { return s_tk[t / 4] & 0xFFFFFF; }

// ---- les titres ------------------------------------------------------------------------------------------------------

struct Track { std::string uri, name, artist, album; int ms = 0; };
struct Playlist { std::string id, name; };

static std::vector<Track> g_tracks;
static std::vector<Playlist> g_playlists;
static int g_cur = -1;
static Track g_playing;
static std::string g_token;				// le jeton d'accès (oauthd le rafraîchit)

static std::string urlenc (const std::string &s)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string o;
	for (unsigned char c : s)
		if (isalnum (c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char) c;
		else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
	return o;
}

// ---- les travaux en fond : une liste (l'API web), la connexion de librespot ----------------------------------------------

enum { J_TRACKS, J_PLAYLISTS };
struct Job
{
	pthread_t th;
	bool busy = false;
	volatile int done = 0, cancel = 0;
	int kind = J_TRACKS;
	std::string url, token;
	int code = 0;
	std::string err;
	std::vector<Track> tracks;
	std::vector<Playlist> lists;
};
static Job g_list;

static void parse_track (const json::Value &t, std::vector<Track> &out)
{
	if (t.isNull () || !t["uri"].asStr ()[0] || !t["is_playable"].asBool (true)) return;
	Track k;
	k.uri = t["uri"].asStr ();
	k.name = t["name"].asStr ();
	for (const json::Value *a = t["artists"].first (); a; a = a->next)
	{
		if (!k.artist.empty ()) k.artist += ", ";
		k.artist += (*a)["name"].asStr ();
	}
	k.album = t["album"]["name"].asStr ();
	k.ms = t["duration_ms"].asInt ();
	out.push_back (k);
}

static void *job_main (void *a)
{
	Job *j = (Job *) a;
	std::string body;
	j->code = pq::http_request (true, j->url, "Authorization: Bearer " + j->token + "\r\n", "", body, j->err, &j->cancel, 20000);
	std::fill (j->token.begin (), j->token.end (), 0);
	if (j->code == 200)
	{
		json::Doc d;
		if (!d.parse (body.data (), (unsigned long) body.size ())) { j->code = 0; j->err = "réponse illisible"; }
		else if (j->kind == J_PLAYLISTS)
			for (const json::Value *p = d.root ()["items"].first (); p; p = p->next)
				j->lists.push_back ({ (*p)["id"].asStr (), (*p)["name"].asStr () });
		else
		{
			const json::Value &r = d.root ();
			const json::Value &items = !r["tracks"].isNull () ? r["tracks"]["items"] : r["items"];
			for (const json::Value *it = items.first (); it; it = it->next)
				parse_track (!(*it)["track"].isNull () ? (*it)["track"] : *it, j->tracks);
		}
	}
	else if (j->code == 401) j->err = "jeton refusé";
	__atomic_store_n (&j->done, 1, __ATOMIC_RELEASE);
	return 0;
}

static bool job_finished (Job &j)
{
	if (!j.busy || !__atomic_load_n (&j.done, __ATOMIC_ACQUIRE)) return false;
	pthread_join (j.th, 0);
	j.busy = false;
	return true;
}

// la connexion de librespot (quelques secondes) : un fil à part
static pthread_t g_loginTh;
static bool g_loginBusy, g_loggedIn;
static volatile int g_loginDone, g_loginRc;
static std::string g_loginToken;
static void *login_main (void *)
{
	g_loginRc = ks_login (g_loginToken.c_str ());
	std::fill (g_loginToken.begin (), g_loginToken.end (), 0);
	__atomic_store_n (&g_loginDone, 1, __ATOMIC_RELEASE);
	return 0;
}

// ---- le son : le PCM de librespot dans un flux AudioKit ---------------------------------------------------------------------

static pthread_t g_audioTh;
static volatile bool g_audioQuit;
static void *audio_main (void *)
{
	long s = ak_open (44100, 2, 16, 0);
	if (s < 0) return 0;
	static int16_t buf[1024 * 2];
	while (!g_audioQuit)
	{
		size_t n = ks_read (buf, 1024);
		if (n) ak_write_all (s, (long) buf, (long) (n * 4));	// (attend la place : cadence le fil)
		else media::sleep_ms (10);
	}
	ak_close (s);
	return 0;
}

// ---- la fenêtre -------------------------------------------------------------------------------------------------------

static long g_side, g_search, g_go, g_list_w, g_status, g_bar, g_mini;
static bool g_miniMode, g_quit;
static int g_restore[2];
static long g_restorePos[2] = { -1, -1 };
static struct ks_status g_st;
static uint32_t g_lastEnded, g_lastUnavail;
static int g_source;					// 1 titres aimés, 2 recherche, 100 + n : la playlist n
static std::string g_query, g_pendingUrl;
static int g_pendingKind;
static bool g_authWait;					// une connexion par le navigateur est en cours

static void status (const char *s) { ui_w_set_text (g_status, (long) s); }

static bool fresh_token ()
{
	static char t[4096];
	long n = oauth_token ((long) ACCOUNT, (long) t, sizeof t - 1);
	if (n <= 0) return false;
	t[n] = 0;
	g_token = t;
	memset (t, 0, (size_t) n);
	return true;
}

static void list_fill ()
{
	ui_list_clear (g_list_w);
	char row[700], dur[16];
	for (size_t i = 0; i < g_tracks.size (); i++)
	{
		const Track &t = g_tracks[i];
		snprintf (dur, sizeof dur, "%d:%02d", t.ms / 60000, t.ms / 1000 % 60);
		snprintf (row, sizeof row, "%s\t%s\t%s\t%s", t.name.c_str (), t.artist.c_str (), t.album.c_str (), dur);
		ui_list_add (g_list_w, (long) row, (long) i);
	}
}

static void start_list (int kind, const std::string &url)
{
	if (!fresh_token ()) { status (TR ("Connectez-vous d'abord : Fichier, Se connecter à Spotify…")); return; }
	if (g_list.busy) { g_list.cancel = 1; g_pendingUrl = url; g_pendingKind = kind; return; }
	g_list.kind = kind;
	g_list.url = url;
	g_list.token = g_token;
	g_list.done = g_list.cancel = 0;
	g_list.code = 0;
	g_list.err.clear ();
	g_list.tracks.clear ();
	g_list.lists.clear ();
	g_list.busy = pthread_create (&g_list.th, 0, job_main, &g_list) == 0;
	status (TR ("Chargement…"));
}

static void load_source ()
{
	if (g_source == 1) start_list (J_TRACKS, API "/me/tracks?limit=50");
	else if (g_source == 2) start_list (J_TRACKS, API "/search?type=track&limit=50&q=" + urlenc (g_query));
	else if (g_source >= 100 && g_source - 100 < (int) g_playlists.size ())
		start_list (J_TRACKS, API "/playlists/" + g_playlists[g_source - 100].id + "/tracks?limit=100");
}

static void side_fill ()
{
	ui_list_clear (g_side);
	ui_list_add (g_side, (long) (std::string ("#") + TR ("Ma musique")).c_str (), -1);
	ui_list_add (g_side, (long) TR ("Titres aimés"), 1);
	ui_list_add (g_side, (long) (std::string ("#") + TR ("Playlists")).c_str (), -1);
	for (size_t i = 0; i < g_playlists.size (); i++) ui_list_add (g_side, (long) g_playlists[i].name.c_str (), (long) (100 + i));
}

static void play_index (int i)
{
	if (i < 0 || i >= (int) g_tracks.size ()) return;
	if (!g_loggedIn) { status (TR ("La connexion au lecteur n'est pas encore faite.")); return; }
	g_cur = i;
	g_playing = g_tracks[i];
	ui_w_set_value (g_list_w, i);
	if (ks_load (g_playing.uri.c_str (), 1, 0) != 0)
	{
		char e[200];
		ks_error (e, sizeof e);
		status (e);
	}
	else status ("");
	ui_view_dirty (g_bar);
	ui_view_dirty (g_mini);
}

static void play_next () { if (g_cur + 1 < (int) g_tracks.size ()) play_index (g_cur + 1); }
static void play_prev () { if (g_cur > 0) play_index (g_cur - 1); }

static void toggle_play ()
{
	if (g_st.state == KS_PLAYING) ks_pause ();
	else if (g_st.state == KS_PAUSED) ks_play ();
	else if (g_cur >= 0) play_index (g_cur);
	else if (!g_tracks.empty ()) play_index (0);
}

static void login_player ()
{
	if (g_loginBusy || !fresh_token ()) return;
	g_loginToken = g_token;
	g_loginDone = 0;
	g_loginBusy = pthread_create (&g_loginTh, 0, login_main, 0) == 0;
	status (TR ("Connexion au lecteur de Spotify…"));
}

// la première fois : le client_id, puis l'accord dans le navigateur
static void connect_account ()
{
	char cid[128] = "";
	if (!ui_ask ((long) TR ("Se connecter à Spotify"),
		     (long) TR ("Le Client ID de votre app Spotify (developer.spotify.com ; adresse de retour : http://127.0.0.1:8898/) :"),
		     (long) cid, sizeof cid))
		return;
	media::tidy (cid);
	if (!cid[0]) return;
	std::string cfg = std::string ("provider = custom\nclient_id = ") + cid +
		"\nauth_url = https://accounts.spotify.com/authorize\ntoken_url = https://accounts.spotify.com/api/token\nscope = " SCOPES "\n";
	if (oauth_setup ((long) ACCOUNT, (long) cfg.c_str ()) != 0) { status (TR ("oauthd refuse le compte.")); return; }
	static char url[2048];
	long n = oauth_browser_begin ((long) ACCOUNT, PORT, (long) url, sizeof url - 1);
	if (n <= 0) { status (TR ("oauthd ne peut pas ouvrir la connexion (port 8898 pris ?).")); return; }
	url[n] = 0;
	char *argv[] = { (char *) "/apps/jet.app/main", url, 0 };
	pid_t pid;
	posix_spawn (&pid, argv[0], 0, 0, argv, environ);
	g_authWait = true;
	status (TR ("Acceptez dans Jet ; la connexion se termine ici d'elle-même."));
}

// ---- la barre de lecture et la carte (comme Jamendo) ----------------------------------------------------------------------

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

static void paint_controls (long W, long H, bool mini)
{
	unsigned bg = mini ? tok (TJ_SURFACE) : tok (TJ_FACE), ink = tok (TJ_TEXT1), dim = tok (TJ_TEXT2), acc = tok (TJ_ACCENT);
	ui_fill (0, 0, W, H, bg);
	if (!mini) ui_fill (0, 0, W, 1, tok (TJ_LINE));
	g_hits.clear ();
	auto hot = [&] (int k) { return g_hot >= 0 && g_hot < (int) g_hits.size () && g_hits[g_hot].kind == k; };
	int by = mini ? 52 : (int) H / 2 - 16, bx = mini ? 14 : 16;
	g_hits.push_back ({ bx, by, 32, 32, H_PREV });
	tri (bx + 9, by + 9, 14, true, hot (H_PREV) ? acc : ink);
	ui_fill (bx + 7, by + 9, 2, 14, hot (H_PREV) ? acc : ink);
	int px = bx + 38;
	g_hits.push_back ({ px, by - 2, 36, 36, H_PLAY });
	ui_vp_disc ((px + 18) * 16, (by + 16) * 16, 18 * 16, acc);
	if (g_st.state == KS_PLAYING) { ui_fill (px + 12, by + 8, 4, 16, 0xFFFFFF); ui_fill (px + 20, by + 8, 4, 16, 0xFFFFFF); }
	else tri (px + 13, by + 8, 16, false, 0xFFFFFF);
	int nx = px + 44;
	g_hits.push_back ({ nx, by, 32, 32, H_NEXT });
	tri (nx + 9, by + 9, 14, false, hot (H_NEXT) ? acc : ink);
	ui_fill (nx + 23, by + 9, 2, 14, hot (H_NEXT) ? acc : ink);
	bool has = g_cur >= 0;
	int tx = mini ? 14 : nx + 48, tw = (int) W - tx - (mini ? 40 : 150);
	ui_text_fit ((long) (has ? g_playing.name.c_str () : TR ("Rien en lecture")), tx, mini ? 24 : (int) H / 2 - 6, ink, 14,
		     (long) ui.font_b, tw);
	if (has) ui_text_fit ((long) g_playing.artist.c_str (), tx, mini ? 42 : (int) H / 2 + 12, dim, 12, (long) ui.font, tw);
	i64 len = g_st.duration_ms ? g_st.duration_ms : (i64) g_playing.ms, pos = g_st.position_ms;
	int sx = mini ? nx + 44 : tx, sw = mini ? (int) W - sx - 14 : (int) W - tx - 160, sy = mini ? by + 15 : (int) H - 12;
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
	{
		g_hits.push_back ({ (int) W - 32, 6, 26, 26, H_EXPAND });
		unsigned c = hot (H_EXPAND) ? acc : dim;
		ui_frame (W - 26, 12, 14, 12, c);
		ui_fill (W - 26, 12, 14, 3, c);
	}
}

static void bar_paint (long, long w, long h) { paint_controls (w, h, false); }
static void mini_paint (long, long w, long h) { paint_controls (w, h, true); }
static void enter_mini (bool on);

static long ctl_mouse (long me, long kind, long x, long y, long)
{
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
	{
		i64 len = g_st.duration_ms ? g_st.duration_ms : (i64) g_playing.ms;
		if (len > 0) ks_seek ((uint32_t) (len * (x - h.x) / (h.w ? h.w : 1)));
		break;
	}
	}
	ui_view_dirty (me);
	return 0;
}

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
	{
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

enum { M_PLAY = 1, M_NEXT, M_PREV, M_CONNECT, M_FORGET, M_CLOSE, M_SEARCH };
static const char MENU[] = "MFichier\nI4\tSe connecter à Spotify…\nI5\tSe déconnecter\n-\nI6\tFermer\tCtrl+W\n"
	"MLecture\nI1\tLecture / pause\tEspace\nI2\tSuivant\tCtrl+→\nI3\tPrécédent\tCtrl+←\n-\nI7\tRechercher\tCtrl+F\n";

static void command (long id)
{
	switch (id)
	{
	case M_PLAY: toggle_play (); break;
	case M_NEXT: play_next (); break;
	case M_PREV: play_prev (); break;
	case M_CONNECT: connect_account (); break;
	case M_FORGET:
		ks_stop ();
		oauth_forget ((long) ACCOUNT);
		g_loggedIn = false;
		g_tracks.clear ();
		g_playlists.clear ();
		list_fill ();
		side_fill ();
		status (TR ("Déconnecté."));
		break;
	case M_CLOSE: g_quit = true; break;
	case M_SEARCH: if (g_miniMode) enter_mini (false); ui_w_focus (g_search); break;
	}
	ui_view_dirty (g_bar);
}

static void connected ()
{
	login_player ();
	start_list (J_PLAYLISTS, API "/me/playlists?limit=50");
	g_source = 1;
}

static void poll_all ()
{
	if (g_authWait)
	{
		static i64 last;
		if (now_ms () - last > 1000)
		{
			last = now_ms ();
			long r = oauth_browser_poll ((long) ACCOUNT);
			if (r == 0) { g_authWait = false; status (TR ("Connecté.")); connected (); }
			else if (r != E_AGAIN) { g_authWait = false; status (TR ("La connexion a échoué.")); }
		}
	}
	if (g_loginBusy && __atomic_load_n (&g_loginDone, __ATOMIC_ACQUIRE))
	{
		pthread_join (g_loginTh, 0);
		g_loginBusy = false;
		g_loggedIn = g_loginRc == 0;
		if (!g_loggedIn)
		{
			char e[200];
			ks_error (e, sizeof e);
			status ((std::string (TR ("Le lecteur de Spotify refuse la connexion (Premium ?) : ")) + e).c_str ());
		}
	}
	if (job_finished (g_list))
	{
		if (!g_pendingUrl.empty ())
		{
			std::string u = g_pendingUrl;
			g_pendingUrl.clear ();
			start_list (g_pendingKind, u);
		}
		else if (g_list.code != 200)
			status ((std::string (TR ("Spotify ne répond pas : ")) + (g_list.err.empty () ? std::to_string (g_list.code) : g_list.err)).c_str ());
		else if (g_list.kind == J_PLAYLISTS)
		{
			g_playlists = g_list.lists;
			side_fill ();
			ui_w_set_value (g_side, 1);
			load_source ();
		}
		else
		{
			g_tracks = g_list.tracks;
			g_cur = -1;
			list_fill ();
			char t[80];
			snprintf (t, sizeof t, TR ("%d titres"), (int) g_tracks.size ());
			status (g_tracks.empty () ? TR ("Aucun titre.") : t);
		}
	}
	ks_status (&g_st);
	if (g_st.ended != g_lastEnded) { g_lastEnded = g_st.ended; play_next (); }
	if (g_st.unavailable != g_lastUnavail)
	{
		g_lastUnavail = g_st.unavailable;
		status (TR ("Ce titre n'est pas disponible : le suivant."));
		play_next ();
	}
}

int main (int, char **)
{
	if (!uikit_open (&ui)) return 1;
	if (!netkit_kit_open ()) return 1;
	s_tk = (const unsigned *) ui_tokens ();
	long sw = 980, sh = 640, scr[4] = { 0, 0, 0, 0 };
	if (ui_screen ((long) scr) >= 0 && scr[0] > 0)
	{
		if (sw > scr[0] - 60) sw = scr[0] - 60;
		if (sh > scr[1] - scr[2] - scr[3] - 80) sh = scr[1] - scr[2] - scr[3] - 80;
	}
	if (ui_window_ex (-1, -1, sw, sh, (long) "Spotify", WF_RESIZABLE) <= 0) return 1;
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
	on_resize (ui.w, ui.h);
	pthread_create (&g_audioTh, 0, audio_main, 0);
	if (oauth_status ((long) ACCOUNT) >= 1) connected ();
	else status (TR ("Fichier, Se connecter à Spotify… (il faut un compte Premium)"));

	i64 lastPaint = 0;
	while (!g_quit)
	{
		long t = ui_event (50);
		if (t == WEV_CLOSE)
		{
			if (!g_miniMode && g_st.state == KS_PLAYING) { enter_mini (true); continue; }
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
				if (!g_query.empty ()) { g_source = 2; load_source (); }
			}
			else if (code == UN_ACTIVATE && wd == g_list_w) play_index ((int) ui_w_get_value (g_list_w));
			else if (code == UN_CHANGE && wd == g_side)
			{
				long n = ui_w_get_value (g_side), data = -1;
				if (n >= 0) uikit_list_get (g_side, n, &data);
				if (data >= 0) { g_source = (int) data; load_source (); }
			}
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) command (id);
				else if (ui_ev_field (&ui, EV_CODE) == 0x2C) toggle_play ();
			}
		}
		else ui_w_idle ();
		poll_all ();
		if (now_ms () - lastPaint > 500) { lastPaint = now_ms (); ui_view_dirty (g_miniMode ? g_mini : g_bar); }
		ui_view_flush ();
	}
	ks_stop ();
	g_audioQuit = true;
	pthread_join (g_audioTh, 0);
	g_list.cancel = 1;
	if (g_list.busy) pthread_join (g_list.th, 0);
	if (g_loginBusy) pthread_join (g_loginTh, 0);
	return 0;
}
