//
// main.cpp -- Media Player, Onyx's music library (user/Apps/media, docs/media/README.md there) on ARMPI's kits: the songs of
// the folders it watches (SD0:/home/music), MP3, OGG, Opus, FLAC, WAV, MIDI and FM Songs, by artists, albums, songs, genres,
// folders and playlists; a now-playing view (a MIDI file: its notes scrolling as coloured lines); a mini player. Closing the
// window stops the music. Tags are read, never written; covers come from the files and their folders, nothing is
// downloaded. The videos (films, clips, episodes: WebM, Matroska, MP4) are played by MediaKit, in the window or full
// screen, each resumed where it was left.
//
// No bridge: the window is UIKit's (ui_view, ui_list, ui_textbox, ui_surface...), the sound AudioKit's (ak_*), the pictures
// MediaKit's (mk_*). Its parts: tags.h, midi.h, lib.h (the library, its scan thread), covers.h (their loader thread),
// videos.h (the videos' facts), thumbs.h (their frames' thread), player.h (the music), watch.h (a video playing, its
// thread), ui.h, sys.h. Its files: SD0:/home/media/settings.ini, library.tsv, stats.tsv, videos.tsv, thumbs/*.jpg;
// SD0:/home/music/Playlists/*.m3u.
//
#include "lib.h"
#include "ui.h"
#include "covers.h"
#include "thumbs.h"
#include "player.h"
#include "watch.h"

namespace media {

#define SETTINGS "SD0:/home/media/settings.ini"
static const char *const MEDIA_FILTER = "*.mp3;*.ogg;*.oga;*.opus;*.flac;*.wav;*.mid;*.midi;*.kar;*.rmi;*.fms;*.m3u;*.m3u8;*.webm;*.mkv;*.mp4;*.m4v;*.mov";
static const char *const VIDEO_FILTER = "*.webm;*.mkv;*.mp4;*.m4v;*.mov";

// ---- the state -------------------------------------------------------------------------------------------
static Library *L;				// the library shown
static Covers *g_covers;
static Player g_player;
static VideoLib *VL;				// the videos
static Thumbs *g_thumbs;
static VideoPlay g_vp;				// the video open (in the window or full screen)
static int g_vidx = -1;				// its index in V, -1 none
static unsigned g_vpSaveT;			// (the position saved)
static bool g_vpEnded, g_surfaced;		// (the surface: on the window)
static ScanState *g_scan; static pthread_t g_scanTid;
static char g_folders[8][200]; static int g_nfolders;
static int g_volume = 80; static bool g_muted, g_shuffle; static int g_repeat;	// 0 off, 1 all, 2 one
static int g_albumSort = 3; static bool g_albumList;
static int g_kitsOk;				// MediaKit is there

// songs that are not in the library (a file opened from outside): ids -1, -2...
static Song g_ext[16]; static int g_next;
static const Song *song (int id) { if (id >= 0) return L && id < L->n ? &L->s[id] : 0; int k = -id - 1; return k < g_next ? &g_ext[k] : 0; }

// the queue: what plays, in its order (shuffled or not); g_qorig: the order it was made in
static IntList g_queue, g_qorig; static int g_qpos = -1;
static int g_playing = -1;			// the song playing / paused (an id), -1 none (id -1 is a file from outside: see g_hasSong)
static bool g_hasSong;				// (g_playing means something)
static bool g_counted;				// (a play counted once it has been heard enough)
#define PLAYING_LIB (g_hasSong && g_playing >= 0)

// the playlists (SD0:/home/music/Playlists/*.m3u): their songs as paths (the library can be scanned again)
struct Plist { char name[96]; char path[300]; char **paths; int n; };
static Plist *g_pl; static int g_npl;

// ---- the pages -------------------------------------------------------------------------------------------
enum { P_HOME, P_ALBUMS, P_ALBUM, P_ARTISTS, P_ARTIST, P_SONGS, P_GENRES, P_GENRE, P_FOLDERS, P_FOLDER, P_PLAYLIST, P_NOW, P_SEARCH, P_WELCOME,
       P_VIDEOS, P_WATCH };				// P_VIDEOS: arg a kind (VK_*, -1 all); P_WATCH: arg the video
enum { PL_FAV = -1, PL_RECENT = -2 };
struct Page { int kind, arg, scroll, sortCol; bool sortDesc; };
static Page g_hist[64]; static int g_nhist, g_hpos;	// back / forward
static Page &page () { return g_hist[g_hpos]; }
static char g_search[128];

static long g_vTop, g_vContent, g_vNow, g_vMini, g_vWatch, g_vFoot;	// the views
static long g_side, g_scroll, g_searchBox;
static long g_barMax = -1, g_barPage, g_barPos;		// (what the scroll bar was told)
static bool g_miniMode; static int g_restore[2];	// the window before the mini player
static unsigned g_tick;
static bool g_anim;					// the content drew something that moves
static int g_winW, g_winH;

static void navigate (int kind, int arg = 0);
static void play_list (const IntList &ids, int start, bool shuffle);
static void refresh_all ();
static void save_settings ();
static void video_close ();
static void go_back_page ();
static void go_back ();
static void go_fwd ();
static void start_scan ();
static void play_video (int vi, bool fromStart);
static void open_file (const char *p);
static void m_open_video ();
static void video_full_screen ();

// ---- settings -------------------------------------------------------------------------------------------
static void load_settings ()
{
	g_nfolders = 0;
	char *b = read_all (SETTINGS, 16384);
	if (b)
	{
		for (char *p = b; *p; )
		{
			char *e = strchr (p, '\n'); if (e) *e = 0;
			char *eq = strchr (p, '=');
			if (eq && *p != '#')
			{
				char *k = p, *v = eq + 1; *eq = 0;
				while (*v == ' ') v++;
				int kl = (int) strlen (k); while (kl && k[kl - 1] == ' ') k[--kl] = 0;
				int vl = (int) strlen (v); while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\r')) v[--vl] = 0;
				if (!strcmp (k, "folder") && g_nfolders < 8 && v[0]) scopy (g_folders[g_nfolders++], v, 200);
				else if (!strcmp (k, "volume")) g_volume = atoi (v);
				else if (!strcmp (k, "shuffle")) g_shuffle = atoi (v) != 0;
				else if (!strcmp (k, "repeat")) g_repeat = atoi (v) % 3;
				else if (!strcmp (k, "album_sort")) g_albumSort = atoi (v) & 3;
				else if (!strcmp (k, "album_list")) g_albumList = atoi (v) != 0;
			}
			if (!e) break;
			p = e + 1;
		}
		free (b);
	}
	else { scopy (g_folders[0], MUSIC_DIR, 200); g_nfolders = 1; }
	if (g_volume < 0 || g_volume > 100) g_volume = 80;
}
static void save_settings ()
{
	make_dir (LIB_DIR);
	char b[2048]; int n = snprintf (b, sizeof b, "# Media Player's settings (the app writes them)\n");
	for (int i = 0; i < g_nfolders; i++) n += snprintf (b + n, sizeof b - n, "folder = %s\n", g_folders[i]);
	n += snprintf (b + n, sizeof b - n, "volume = %d\nshuffle = %d\nrepeat = %d\nalbum_sort = %d\nalbum_list = %d\n",
		       g_volume, g_shuffle ? 1 : 0, g_repeat, g_albumSort, g_albumList ? 1 : 0);
	save_all (SETTINGS, b, (size_t) n);
}

// ---- playlists --------------------------------------------------------------------------------------------
static void playlists_free () { for (int i = 0; i < g_npl; i++) { for (int k = 0; k < g_pl[i].n; k++) free (g_pl[i].paths[k]); free (g_pl[i].paths); } free (g_pl); g_pl = 0; g_npl = 0; }
static void playlist_read (Plist &p)
{
	p.paths = 0; p.n = 0;
	char *b = read_all (p.path, 2u << 20);
	if (!b) return;
	int cap = 0;
	for (char *q = b; *q; )
	{
		char *e = strchr (q, '\n'); if (e) *e = 0;
		int l = (int) strlen (q); while (l && (q[l - 1] == '\r' || q[l - 1] == ' ')) q[--l] = 0;
		if (q[0] && q[0] != '#')
		{
			char full[320];
			if (strchr (q, ':')) scopy (full, q, sizeof full); else snprintf (full, sizeof full, "%s/%s", PLAYLIST_DIR, q);
			for (char *c = full; *c; c++) if (*c == '\\') *c = '/';
			if (p.n == cap) { cap = cap ? cap * 2 : 32; p.paths = (char **) realloc (p.paths, sizeof (char *) * cap); }
			p.paths[p.n++] = sdup (full);
		}
		if (!e) break;
		q = e + 1;
	}
	free (b);
}
static void playlists_load ()
{
	playlists_free ();
	DirReader d;
	if (!d.open (PLAYLIST_DIR)) return;
	int cap = 0;
	while (d.next ())
	{
		if (d.isDir || (strcasecmp (ext_of (d.name), "m3u") && strcasecmp (ext_of (d.name), "m3u8"))) continue;
		if (g_npl == cap) { cap = cap ? cap * 2 : 8; g_pl = (Plist *) realloc (g_pl, sizeof (Plist) * cap); }
		Plist &p = g_pl[g_npl++];
		scopy (p.name, d.name, sizeof p.name); char *dot = strrchr (p.name, '.'); if (dot) *dot = 0;
		snprintf (p.path, sizeof p.path, "%s/%s", PLAYLIST_DIR, d.name);
		playlist_read (p);
	}
}
static void playlist_save (Plist &p)
{
	make_dir (MUSIC_DIR); make_dir (PLAYLIST_DIR);
	size_t cap = 64; for (int i = 0; i < p.n; i++) cap += strlen (p.paths[i]) + 400;
	char *b = (char *) malloc (cap); size_t n = (size_t) snprintf (b, cap, "#EXTM3U\n");
	for (int i = 0; i < p.n; i++)
	{
		int id = L ? L->find (p.paths[i]) : -1;
		if (id >= 0) n += (size_t) snprintf (b + n, cap - n, "#EXTINF:%d,%s - %s\n", L->s[id].durMs / 1000, L->artist_of (L->s[id]), L->s[id].title);
		n += (size_t) snprintf (b + n, cap - n, "%s\n", p.paths[i]);
	}
	save_all (p.path, b, n);
	free (b);
}
static int playlist_new (const char *name)
{
	g_pl = (Plist *) realloc (g_pl, sizeof (Plist) * (g_npl + 1));
	Plist &p = g_pl[g_npl];
	scopy (p.name, name, sizeof p.name);
	char safe[96]; int k = 0; for (const char *c = name; *c && k < 90; c++) safe[k++] = strchr ("\\/:*?\"<>|", *c) ? '_' : *c; safe[k] = 0;
	snprintf (p.path, sizeof p.path, "%s/%s.m3u", PLAYLIST_DIR, safe);
	p.paths = 0; p.n = 0;
	playlist_save (p);
	return g_npl++;
}
static void playlist_add (int pl, const IntList &ids)
{
	Plist &p = g_pl[pl];
	p.paths = (char **) realloc (p.paths, sizeof (char *) * (p.n + ids.n + 1));
	for (int i = 0; i < ids.n; i++) { const Song *s = song (ids.v[i]); if (s) p.paths[p.n++] = sdup (s->path); }
	playlist_save (p);
}

// ---- the songs of a page --------------------------------------------------------------------------------
static bool matches (const char *s, const char *q)
{
	if (!q[0]) return true;
	int ql = (int) strlen (q);
	for (const char *p = s; *p; p++) if (!strncasecmp (p, q, (size_t) ql)) return true;
	return false;
}
static void page_songs (const Page &pg, IntList &out)
{
	out.clear ();
	if (!L) return;
	switch (pg.kind)
	{
	case P_ALBUM: if (pg.arg >= 0 && pg.arg < L->nal) for (int i = 0; i < L->al[pg.arg].n; i++) out.push (L->al[pg.arg].songs[i]); return;
	case P_ARTIST: if (pg.arg >= 0 && pg.arg < L->nar) for (int i = 0; i < L->ar[pg.arg].n; i++) out.push (L->ar[pg.arg].songs[i]); return;
	case P_GENRE: if (pg.arg >= 0 && pg.arg < L->nge) for (int i = 0; i < L->ge[pg.arg].n; i++) out.push (L->ge[pg.arg].songs[i]); return;
	case P_FOLDER: if (pg.arg >= 0 && pg.arg < L->nfo) for (int i = 0; i < L->fo[pg.arg].n; i++) out.push (L->fo[pg.arg].songs[i]); return;
	case P_SONGS: for (int i = 0; i < L->nar; i++) for (int k = 0; k < L->ar[i].n; k++) out.push (L->ar[i].songs[k]); return;
	case P_SEARCH:
		for (int i = 0; i < L->nar; i++) for (int k = 0; k < L->ar[i].n; k++)
		{ const Song &x = L->s[L->ar[i].songs[k]]; if (matches (x.title, g_search) || matches (x.artist, g_search) || matches (x.album, g_search)) out.push (L->ar[i].songs[k]); }
		return;
	case P_PLAYLIST:
		if (pg.arg == PL_FAV) { for (int i = 0; i < L->nar; i++) for (int k = 0; k < L->ar[i].n; k++) if (L->s[L->ar[i].songs[k]].fav) out.push (L->ar[i].songs[k]); }
		else if (pg.arg == PL_RECENT)
		{
			for (int i = 0; i < L->n; i++) out.push (i);
			sort_idx (out.v, out.n, [] (const void *, int a, int b) -> int { const Song &x = L->s[a], &y = L->s[b];
				if (x.added != y.added) return y.added > x.added ? 1 : -1; return strcmp (x.path, y.path); }, 0);
			if (out.n > 100) out.n = 100;
		}
		else if (pg.arg >= 0 && pg.arg < g_npl) for (int i = 0; i < g_pl[pg.arg].n; i++) { int id = L->find (g_pl[pg.arg].paths[i]); if (id >= 0) out.push (id); }
		return;
	}
}
// sorted by a column: 1 title, 2 artist, 3 album, 4 time, 5 format
static void sort_songs (IntList &l, int col, bool desc)
{
	if (!col) return;
	struct C { int col; bool desc; } c = { col, desc };
	sort_idx (l.v, l.n, [] (const void *c_, int a, int b) -> int {
		const C *cc = (const C *) c_; const Song &x = L->s[a], &y = L->s[b]; int r = 0;
		switch (cc->col)
		{
		case 1: r = name_cmp (x.title, y.title); break;
		case 2: r = name_cmp (L->artist_of (x), L->artist_of (y)); if (!r) r = name_cmp (x.album, y.album); if (!r) r = x.track - y.track; break;
		case 3: r = name_cmp (x.album, y.album); if (!r) r = x.track - y.track; break;
		case 4: r = x.durMs - y.durMs; break;
		case 5: r = x.fmt - y.fmt; break;
		}
		return cc->desc ? -r : r; }, &c);
}

// ---- playing -------------------------------------------------------------------------------------------------
static void count_play ()
{
	if (g_counted || !PLAYING_LIB) return;
	g_counted = true;
	if (L) { L->s[g_playing].plays++; L->s[g_playing].lastPlayed = now_stamp (); L->save_stats (); }
}
static void stop_all () { g_player.stop (); g_hasSong = false; g_playing = -1; refresh_all (); }
static void start_song (int qpos)
{
	if (page ().kind == P_WATCH) go_back_page ();		// (the music takes the sound back from the video)
	video_close ();
	if (qpos < 0 || qpos >= g_queue.n) { g_qpos = -1; stop_all (); return; }
	g_qpos = qpos; g_playing = g_queue[qpos]; g_hasSong = true;
	const Song *s = song (g_playing);
	if (!s) { stop_all (); return; }
	g_player.volume = g_muted ? 0 : g_volume;
	g_player.play (s->path);
	g_counted = false;
	refresh_all ();
}
static void shuffle_from (int keepFirst)
{	// the queue after its first `keepFirst` songs shuffled
	unsigned r = ticks () * 2654435761u + 7;
	for (int i = g_queue.n - 1; i > keepFirst; i--)
	{
		r = r * 1103515245u + 12345u;
		int j = keepFirst + (int) ((r >> 8) % (unsigned) (i - keepFirst + 1));
		int t = g_queue[i]; g_queue[i] = g_queue[j]; g_queue[j] = t;
	}
}
static void play_list (const IntList &ids, int start, bool shuffle)
{
	if (!ids.n) return;
	count_play ();
	g_qorig.copy (ids);
	g_queue.copy (ids);
	if (shuffle) g_shuffle = true;
	int first = start >= 0 && start < ids.n ? start : 0;
	if (g_shuffle)
	{
		if (start < 0) { unsigned r = ticks () * 2654435761u; first = (int) (r % (unsigned) ids.n); }
		int t = g_queue[0]; g_queue[0] = g_queue[first]; g_queue[first] = t;
		shuffle_from (0);
		first = 0;
	}
	start_song (first);
}
static void play_next (bool user)
{
	count_play ();
	if (!g_queue.n) return;
	if (!user && g_repeat == 2) { start_song (g_qpos); return; }
	int n = g_qpos + 1;
	if (n >= g_queue.n) { if (g_repeat == 1 || user) n = 0; else { stop_all (); return; } }
	start_song (n);
}
static void play_prev ()
{
	if (g_player.posMs > 3000 || g_qpos <= 0) { g_player.seek (0); return; }
	count_play ();
	start_song (g_qpos - 1);
}
static void video_toggle ();
static void toggle_play ()
{
	if (page ().kind == P_WATCH && g_vp.active ()) { video_toggle (); return; }
	if (g_player.state == PS_PLAYING) g_player.pause ();
	else if (g_player.state == PS_PAUSED) g_player.resume ();
	else if (g_queue.n) start_song (g_qpos >= 0 ? g_qpos : 0);
	else if (L && L->n) { IntList all; page_songs (Page { P_SONGS, 0, 0, 0, false }, all); play_list (all, -1, g_shuffle); }
}
static void set_shuffle (bool on)
{
	g_shuffle = on;
	if (g_queue.n)
	{
		int cur = g_playing;
		if (on) { int p = g_queue.find (cur); if (p > 0) { int t = g_queue[0]; g_queue[0] = g_queue[p]; g_queue[p] = t; } shuffle_from (0); g_qpos = 0; }
		else { g_queue.copy (g_qorig); g_qpos = g_queue.find (cur); }
	}
	save_settings ();
}
static void play_next_ids (const IntList &ids, bool next)
{
	if (!g_queue.n) { play_list (ids, 0, false); return; }
	int at = next ? g_qpos + 1 : g_queue.n;
	for (int i = 0; i < ids.n; i++) { g_queue.insert (at + i, ids.v[i]); g_qorig.push (ids.v[i]); }
}
static void apply_volume () { g_player.set_volume (g_muted ? 0 : g_volume); g_vp.set_volume (g_volume, g_muted); }

// ---- the window's parts ------------------------------------------------------------------------------------
static const int TOP_H = 52, NOW_H = 80, SB_W = 14;
static int side_w () { return g_winW < 900 ? 164 : 208; }
static unsigned col_dim () { return C_DIM; }

struct Hit { int x, y, w, h, kind, a, b; };
struct HitList
{
	Hit h[512]; int n;
	void clear () { n = 0; }
	void add (int x, int y, int w, int hh, int kind, int a = 0, int b = 0) { if (n < 512) h[n++] = Hit { x, y, w, hh, kind, a, b }; }
	const Hit *at (int x, int y) const { for (int i = n - 1; i >= 0; i--) if (x >= h[i].x && y >= h[i].y && x < h[i].x + h[i].w && y < h[i].y + h[i].h) return &h[i]; return 0; }
};
// What a click asked, done by the main loop once out of UIKit's event (a dialog, a menu are not opened inside it).
enum { FROM_NONE, FROM_TOP, FROM_CONTENT, FROM_NOW, FROM_MINI, FROM_WATCH, FROM_FOOT };
static struct { int from; Hit h; int x, y, mods, clicks; bool right; } g_click;
static void click_on (int from, const Hit *h, int x, int y, int mods, int clicks, bool right)
{
	g_click.from = from; if (h) g_click.h = *h; else g_click.from = FROM_NONE; g_click.x = x; g_click.y = y; g_click.mods = mods; g_click.clicks = clicks; g_click.right = right;
}

// ---- the sidebar: UIKit's side list (UL_SIDEBAR), its items with their icons; under it a foot: a new playlist, the scan --
enum { SB_PAGE = 1, SB_PLAYLIST, SB_VIDEOS };
static inline int sb_id (int kind, int arg) { return kind == SB_PAGE ? 100 + arg : kind == SB_VIDEOS ? 200 + arg : 302 + arg; }	// (PL_RECENT: 300)
struct SideItem { int id, ic; };
static SideItem g_sideItems[96]; static int g_nside;
static unsigned g_sideSig = ~0u;			// (the playlists the items were made for)
static void side_add (int id, int ic, const char *label)
{
	if (g_nside >= 96) return;
	g_sideItems[g_nside].id = id; g_sideItems[g_nside].ic = ic; g_nside++;
	ui_list_add (g_side, (long) label, id);
}
static long side_cell (long, long row, long, long x, long y, long w, long h, long sel)
{
	if (row < 0 || row >= g_nside || g_sideItems[row].id < 0) return 0;
	unsigned ink = sel ? ink_on (C_ACCENT) : C_TEXT;
	int ic = g_sideItems[row].ic;
	long d; const char *t = (const char *) uikit_list_get (g_side, row, &d);
	icon (ic, (int) x + 6, (int) y + ((int) h - 16) / 2, 16, ic == I_HEART && !sel ? jet (J_DANGER) : ink, sel ? C_ACCENT : C_BG);
	text_v ((int) x + 30, (int) y, (int) h, t, ink, F_UI, sel ? 2 : 0, (int) w - 36);
	return 1;
}
static void side_select ()			// the item of the page shown (an album's page: Albums...)
{
	int k = page ().kind, a = page ().arg, id = -1;
	switch (k)
	{
	case P_HOME: case P_SONGS: id = sb_id (SB_PAGE, k); break;
	case P_ALBUMS: case P_ALBUM: id = sb_id (SB_PAGE, P_ALBUMS); break;
	case P_ARTISTS: case P_ARTIST: id = sb_id (SB_PAGE, P_ARTISTS); break;
	case P_GENRES: case P_GENRE: id = sb_id (SB_PAGE, P_GENRES); break;
	case P_FOLDERS: case P_FOLDER: id = sb_id (SB_PAGE, P_FOLDERS); break;
	case P_VIDEOS: if (a == VK_FILM || a == VK_CLIP) id = sb_id (SB_VIDEOS, a); break;
	case P_PLAYLIST: id = sb_id (SB_PLAYLIST, a); break;
	}
	for (int i = 0; i < g_nside; i++) if (g_sideItems[i].id == id) { if (ui_w_get_value (g_side) != i) ui_w_set_value (g_side, i); return; }
	if (ui_w_get_value (g_side) != -1) ui_w_set_value (g_side, -1);
}
static void side_heading (const char *t) { char b[64]; snprintf (b, sizeof b, "#%s", t); if (g_nside < 96) { g_sideItems[g_nside].id = -1; g_sideItems[g_nside].ic = 0; g_nside++; ui_list_add (g_side, (long) b, -1); } }
static void side_build ()			// the items: made again when the playlists change
{
	unsigned sig = 2166136261u;
	for (int i = 0; i < g_npl; i++) for (const char *c = g_pl[i].name; ; c++) { sig = (sig ^ (unsigned char) *c) * 16777619u; if (!*c) break; }
	if (sig == g_sideSig) { side_select (); return; }
	g_sideSig = sig;
	ui_list_clear (g_side); g_nside = 0;
	side_add (sb_id (SB_PAGE, P_HOME), I_HOME, TR ("Home"));
	side_heading (TR ("LIBRARY"));
	side_add (sb_id (SB_PAGE, P_ARTISTS), I_PERSON, TR ("Artists"));
	side_add (sb_id (SB_PAGE, P_ALBUMS), I_DISC, TR ("Albums"));
	side_add (sb_id (SB_PAGE, P_SONGS), I_NOTE, TR ("Songs"));
	side_add (sb_id (SB_PAGE, P_GENRES), I_TAG, TR ("Genres"));
	side_add (sb_id (SB_PAGE, P_FOLDERS), I_FOLDER, TR ("Folders"));
	side_heading (TR ("VIDEOS"));
	side_add (sb_id (SB_VIDEOS, VK_FILM), I_FILM, TR ("Films"));
	side_add (sb_id (SB_VIDEOS, VK_CLIP), I_TV, TR ("Clips and series"));
	side_heading (TR ("PLAYLISTS"));
	side_add (sb_id (SB_PLAYLIST, PL_FAV), I_HEART, TR ("Favourites"));
	side_add (sb_id (SB_PLAYLIST, PL_RECENT), I_CLOCK, TR ("Recently added"));
	for (int i = 0; i < g_npl; i++) side_add (sb_id (SB_PLAYLIST, i), I_LIST, g_pl[i].name);
	side_select ();
}
static void side_chosen ()
{
	long i = ui_w_get_value (g_side), id = -1;
	if (i < 0 || i >= g_nside) return;
	id = g_sideItems[i].id;
	if (id >= 300) navigate (P_PLAYLIST, (int) id - 302);
	else if (id >= 200) navigate (P_VIDEOS, (int) id - 200);
	else if (id >= 100) navigate ((int) id - 100);
}

// the foot of the sidebar
enum { SF_NEWPL = 1, SF_FOLDERS };
static HitList g_footHits; static int g_footHot = -1;
static void foot_paint (long me, long w, long h)
{
	begin_paint (me);
	ui_fill (0, 0, w, h, C_BG);
	hline (0, 0, (int) w, C_LINE);
	g_footHits.clear ();
	int by = 6;
	bool ho = g_footHits.n == g_footHot;
	char np[80]; snprintf (np, sizeof np, "+  %s", TR ("New playlist"));
	text_v (18, by, 26, np, ho ? C_ACCENT : C_TEXT, F_UI, 0, (int) w - 30);
	g_footHits.add (8, by, (int) w - 16, 26, SF_NEWPL);
	char st[96];
	if (g_scan && !g_scan->done) snprintf (st, sizeof st, TR ("Looking for songs...  %d"), g_scan->found + g_scan->vfound);
	else if (VL && VL->n) snprintf (st, sizeof st, TR ("%d songs  \xC2\xB7  %d videos"), L ? L->n : 0, VL->n);
	else snprintf (st, sizeof st, TR ("%d songs"), L ? L->n : 0);
	ho = g_footHits.n == g_footHot;
	text_v (18, by + 28, 22, st, ho ? C_ACCENT : col_dim (), F_SMALL, 0, (int) w - 30);
	g_footHits.add (8, by + 28, (int) w - 16, 22, SF_FOLDERS);
}
static long foot_mouse (long me, long kind, long x, long y, long info)
{
	const Hit *ht = kind == 3 ? 0 : g_footHits.at ((int) x, (int) y);
	int nh = ht ? (int) (ht - g_footHits.h) : -1;
	if (nh != g_footHot) { g_footHot = nh; ui_view_dirty (me); }
	if (kind == 2 && ht) click_on (FROM_FOOT, ht, (int) x, (int) y, 0, 1, false);
	return 0;
}

// ---- the bar on top: back / forward, where we are, the search, grid or list ----------------------------------------
enum { TB_BACK = 1, TB_FWD, TB_CRUMB, TB_GRID, TB_LIST };
static HitList g_topHits; static int g_topHot = -1;
static int search_w () { return g_winW < 900 ? 170 : 230; }
static void top_round (int x, int y, int ic, bool on, bool h)
{
	unsigned f = h && on ? tone (C_BG, 140) : C_BG;
	disc (x + 15, y + 15, 15, C_LINE); disc (x + 15, y + 15, 14, f);
	icon (ic, x + 8, y + 8, 14, on ? C_TEXT : mix (C_BG, C_TEXT, 90));
}
// Where we are: an entry-like field, the levels in it as links -- the page shown in the accent, underlined.
static void top_crumbs (int bx, int by, int bw, int bh)
{
	const char *a = 0, *b = 0; int ak = -1;
	const Page &p = page ();
	static char t[160];
	switch (p.kind)
	{
	case P_HOME: a = TR ("Home"); break;
	case P_ALBUMS: a = TR ("Music"); b = TR ("Albums"); break;
	case P_ALBUM: a = TR ("Albums"); ak = P_ALBUMS; b = L && p.arg < L->nal ? L->al[p.arg].title : ""; break;
	case P_ARTISTS: a = TR ("Music"); b = TR ("Artists"); break;
	case P_ARTIST: a = TR ("Artists"); ak = P_ARTISTS; b = L && p.arg < L->nar ? L->ar[p.arg].name : ""; break;
	case P_SONGS: a = TR ("Music"); b = TR ("Songs"); break;
	case P_GENRES: a = TR ("Music"); b = TR ("Genres"); break;
	case P_GENRE: a = TR ("Genres"); ak = P_GENRES; b = L && p.arg < L->nge ? L->ge[p.arg].name : ""; break;
	case P_FOLDERS: a = TR ("Music"); b = TR ("Folders"); break;
	case P_FOLDER: a = TR ("Folders"); ak = P_FOLDERS; b = L && p.arg < L->nfo ? L->fo[p.arg].name : ""; break;
	case P_PLAYLIST: a = TR ("Playlists"); b = p.arg == PL_FAV ? TR ("Favourites") : p.arg == PL_RECENT ? TR ("Recently added") : p.arg < g_npl ? g_pl[p.arg].name : ""; break;
	case P_NOW: a = TR ("Now playing"); break;
	case P_SEARCH: snprintf (t, sizeof t, TR ("Search: \xE2\x80\x9C%s\xE2\x80\x9D"), g_search); a = t; break;
	case P_WELCOME: a = TR ("Music"); b = TR ("Folders to watch"); break;
	case P_VIDEOS: a = TR ("Videos"); b = p.arg == VK_FILM ? TR ("Films") : p.arg == VK_CLIP ? TR ("Clips and series") : 0; if (b) ak = P_VIDEOS; break;
	case P_WATCH: a = TR ("Videos"); b = VL && p.arg >= 0 && p.arg < VL->n ? VL->v[p.arg].title : ""; break;
	}
	unsigned field = C_FIELD, ink = C_TEXT, dim = col_dim ();
	panel (bx, by, bw, bh, 8, field, C_LINE);
	const char *seg[2] = { a, b };
	int n = b ? 2 : 1, x = bx + 14, y = by + (bh - fh ()) / 2, right = bx + bw - 14;
	for (int i = 0; i < n; i++)
	{
		if (i) { icon (I_FWD, x + 1, by + bh / 2 - 6, 12, dim); x += 18; }
		bool cur = i == n - 1, link = !cur && ak >= 0, h = link && g_topHits.n == g_topHot;
		int w = tw (seg[i], F_UI, cur ? 2 : 0);
		if (w > right - x) w = right - x;
		if (w <= 0) break;
		text (x, y, seg[i], cur ? C_ACCENT : ink, F_UI, cur ? 2 : 0, w);
		if (cur) ui_fill (x, y + fh () + 1, w, 2, C_ACCENT);
		else if (h) ui_fill (x, y + fh () + 1, w, 1, ink);
		if (link) g_topHits.add (x, by, w, bh, TB_CRUMB, ak);
		x += w + 10;
	}
}
static void top_paint (long me, long width, long height)
{
	begin_paint (me);
	ui_fill (0, 0, width, height, C_BG);
	hline (0, (int) height - 1, (int) width, C_LINE);
	g_topHits.clear ();
	bool canB = g_hpos > 0, canF = g_hpos < g_nhist - 1;
	top_round (12, 11, I_BACK, canB, g_topHot == g_topHits.n); g_topHits.add (12, 11, 30, 30, TB_BACK);
	top_round (48, 11, I_FWD, canF, g_topHot == g_topHits.n); g_topHits.add (48, 11, 30, 30, TB_FWD);
	bool grid = page ().kind == P_ALBUMS;
	int sx = (int) width - search_w () - (grid ? 174 : 116);	// (left of the window's buttons)
	top_crumbs (92, 10, sx - 12 - 92, 32);
	if (grid)
	{
		int vx = (int) width - 162;
		panel (vx, 12, 50, 28, 6, C_FIELD, C_LINE);
		if (!g_albumList) rbox (vx + 2, 14, 23, 24, 5, C_ACCENT); else rbox (vx + 25, 14, 23, 24, 5, C_ACCENT);
		icon (I_GRID, vx + 6, 18, 15, !g_albumList ? ink_on (C_ACCENT) : C_TEXT); g_topHits.add (vx, 12, 25, 28, TB_GRID);
		icon (I_ROWS, vx + 29, 18, 15, g_albumList ? ink_on (C_ACCENT) : C_TEXT); g_topHits.add (vx + 25, 12, 25, 28, TB_LIST);
	}
}
static long top_mouse (long me, long kind, long x, long y, long info)
{
	const Hit *ht = kind == 3 ? 0 : g_topHits.at ((int) x, (int) y);
	int nh = ht ? (int) (ht - g_topHits.h) : -1;
	if (nh != g_topHot) { g_topHot = nh; ui_view_dirty (me); }
	if (kind == 2 && ht) click_on (FROM_TOP, ht, (int) x, (int) y, 0, 1, false);
	return 0;
}

// ---- the content ----------------------------------------------------------------------------------------------
enum { H_ALBUM = 1, H_ALBUM_PLAY, H_ARTIST, H_GENRE, H_FOLDER, H_ROW, H_HEAD, H_PLAY, H_SHUFFLE, H_MORE, H_SORT, H_QUEUE,
       H_ADDFOLDER, H_DELFOLDER, H_DONE, H_RESUME, H_SEEALL, H_NOWCLOSE, H_VIDEO, H_VRESUME, H_OPENVID };
static HitList g_hits; static int g_hot = -1, g_contentH, g_cw, g_ch;
static IntList g_rows; static bool g_rowsValid; static char *g_sel; static int g_selCap, g_anchorRow = -1;
static inline int &sy () { return page ().scroll; }
static void reset_selection () { if (g_sel) memset (g_sel, 0, (size_t) g_selCap); g_anchorRow = -1; }
static void ensure_sel () { int need = L ? L->n + 1 : 1; if (g_selCap < need) { g_sel = (char *) realloc (g_sel, (size_t) need); memset (g_sel + g_selCap, 0, (size_t) (need - g_selCap)); g_selCap = need; } }
static void selected_ids (IntList &out) { out.clear (); ensure_sel (); for (int i = 0; i < g_rows.n; i++) if (g_rows.v[i] >= 0 && g_sel[g_rows.v[i]]) out.push (g_rows.v[i]); }

static void band_button (int x, int y, int w, int ic, const char *label, bool accent, int kind)
{
	bool h = g_hits.n == g_hot;
	unsigned fill = accent ? tone (C_ACCENT, h ? 150 : 128) : h ? jet (J_ELEVE) : jet (J_ELEVE_BAS);
	unsigned ink = ink_on (fill);
	if (accent) rbox (x, y, w, 34, 6, fill); else panel (x, y, w, 34, 6, fill, C_LINE);
	if (label) { icon (ic, x + 12, y + 9, 16, ink, fill); text_v (x + 34, y, 34, label, ink, F_UI, accent ? 2 : 0, w - 38); }
	else icon (ic, x + (w - 16) / 2, y + 9, 16, ink, fill);
	g_hits.add (x, y, w, 34, kind);
}
static void section_head (int x, int y, int w, const char *title, const char *sub, const char *right, int rightKind)
{
	text (x, y, title, C_TEXT, F_H1, 2);
	if (sub)				// (cut where the page ends, or the button at the right begins: a narrow window)
	{
		int sx = x + tw (title, F_H1, 2) + 14, room = x + w - sx - (right ? tw (right) + 52 : 0);
		if (room > 40) text (sx, y + 10, sub, col_dim (), F_UI, 0, room);
	}
	if (right)
	{
		int rw = tw (right) + 40; bool h = g_hits.n == g_hot;
		panel (x + w - rw, y + 2, rw, 30, 6, h ? jet (J_ELEVE) : C_BG, C_LINE);
		text_v (x + w - rw + 12, y + 2, 30, right, C_TEXT);
		icon (I_DOWN, x + w - 22, y + 11, 12, C_TEXT);
		g_hits.add (x + w - rw, y + 2, rw, 30, rightKind);
	}
}
static void album_tile (int x, int y, int S, int a, bool showArtist = true)
{
	const Album &al = L->al[a];
	bool h = g_hits.n == g_hot;
	g_covers->draw (a, x, y, S, 7, C_FIELD);
	const Song *ps = g_hasSong ? song (g_playing) : 0;
	bool playingHere = ps && g_playing >= 0 && ps->alb == a;
	if (playingHere && !h) { disc (x + S - 20, y + S - 20, 14, 0x000000); eq_bars (x + S - 27, y + S - 27, 14, 0xFFFFFF, g_tick); g_anim = true; }
	int titleY = y + S + 7;
	g_hits.add (x, y, S, S + 44, H_ALBUM, a);
	if (h)
	{
		disc (x + 28, y + S - 28, 18, C_ACCENT);
		icon (I_PLAY, x + 18, y + S - 38, 20, ink_on (C_ACCENT));
		g_hits.add (x + 8, y + S - 48, 40, 40, H_ALBUM_PLAY, a);
	}
	text (x, titleY, al.title, C_TEXT, F_UI, 2, S);
	if (showArtist) text (x, titleY + 19, al.artist, col_dim (), F_SMALL, 0, S);
	else { char t[16]; snprintf (t, sizeof t, "%d", al.year); if (al.year) text (x, titleY + 19, t, col_dim (), F_SMALL); }
}
// a table of songs: columns by width; the playing one marked; sel, hot
static int song_table (int x, int y, int w, IntList &ids, bool showAlbum, bool showNum)
{
	ensure_sel ();
	int colTitle = x + (showNum ? 44 : 12), colArtist = x + (int) (w * (showAlbum ? 0.40 : 0.55)), colAlbum = x + (int) (w * 0.66);
	int colTime = x + w - 74, colFmt = x + w - 62;
	unsigned zebra = mix (C_FIELD, C_BG, 40), over = mix (C_FIELD, C_BG, 110);
	rbox (x, y, w, 28, 5, C_BG);
	const Page &pg = page ();
	struct { const char *s; int x; int col; bool right; } heads[] = { { "#", x + 30, 0, true }, { TR ("Title"), colTitle, 1, false }, { TR ("Artist"), colArtist, 2, false },
		{ TR ("Album"), colAlbum, 3, false }, { TR ("Time"), colTime, 4, true }, { "", colFmt, 5, false } };
	for (auto &hd : heads)
	{
		if (hd.col == 0 && !showNum) continue;
		if (hd.col == 3 && !showAlbum) continue;
		unsigned ink = pg.sortCol == hd.col && hd.col ? C_ACCENT : col_dim ();
		if (hd.right) text_r (hd.x, y, 28, hd.s, ink, F_SMALL, 2); else text_v (hd.x, y, 28, hd.s, ink, F_SMALL, 2);
		if (hd.col) g_hits.add (hd.right ? hd.x - 40 : hd.x, y, 120, 28, H_HEAD, hd.col);
	}
	y += 30;
	for (int i = 0; i < ids.n; i++)
	{
		int ry = y + i * 32;
		if (ry + 32 < 0 || ry > g_ch) continue;
		int id = ids.v[i]; const Song &s = L->s[id];
		bool on = g_sel[id], pl = g_hasSong && id == g_playing, h = g_hits.n == g_hot;
		if (on) rbox (x + 4, ry, w - 8, 30, 5, mix (C_FIELD, C_ACCENT, 70));
		else if (h) rbox (x + 4, ry, w - 8, 30, 5, over);
		else if (i % 2) rbox (x + 4, ry, w - 8, 30, 5, zebra);
		unsigned ink = pl ? C_ACCENT : C_TEXT, dim = col_dim ();
		char t[16];
		if (showNum)
		{
			if (pl && g_player.state == PS_PLAYING) { eq_bars (x + 16, ry + 9, 12, C_ACCENT, g_tick); g_anim = true; }
			else if (pl) icon (I_PAUSE, x + 16, ry + 8, 14, C_ACCENT);
			else { snprintf (t, sizeof t, "%d", s.track ? s.track : i + 1); text_r (x + 30, ry, 30, t, dim, F_SMALL); }
		}
		else if (pl) { eq_bars (x + 2, ry + 9, 10, C_ACCENT, g_tick); g_anim = true; }
		int tx = colTitle + (!showNum && pl ? 8 : 0);
		text_v (tx, ry, 30, s.title, ink, F_UI, pl ? 2 : 0, colArtist - tx - 12);
		if (s.fav) icon (I_HEART, colArtist - 26, ry + 8, 13, jet (J_DANGER));
		text_v (colArtist, ry, 30, s.artist[0] ? s.artist : L->artist_of (s), dim, F_UI, 0, (showAlbum ? colAlbum : colTime - 50) - colArtist - 12);
		if (showAlbum) text_v (colAlbum, ry, 30, s.album, dim, F_UI, 0, colTime - 50 - colAlbum - 10);
		fmt_time (t, sizeof t, s.durMs); text_r (colTime, ry, 30, t, dim, F_SMALL);
		const char *f = s.fmt >= 0 && s.fmt < FMT_N ? FMT_NAME[s.fmt] : "?";
		int fw = tw (f, F_SMALL, 2) + 12;
		unsigned fc = s.fmt == FMT_MIDI ? jet (J_ACCENT_VIF) : mix (C_FIELD, C_TEXT, 100);
		rbox (colFmt + 6, ry + 7, fw, 17, 8, fc);
		text_c (colFmt + 6, ry + 7, fw, 17, f, ink_on (fc), F_SMALL, 2);
		g_hits.add (x, ry, w, 30, H_ROW, id, i);
	}
	return y + ids.n * 32;
}
// over a video's frame: its length, the part watched (a red line), seen to the end, a codec not built in
static void video_badges (int x, int y, int w, int h, const Video &v)
{
	char t[24]; fmt_time (t, sizeof t, v.durMs);
	if (v.durMs > 0)
	{
		int bw = tw (t, F_SMALL, 2) + 12;
		rbox (x + w - bw - 6, y + h - 25, bw, 18, 4, 0x101014);
		text_c (x + w - bw - 6, y + h - 25, bw, 18, t, 0xFFFFFF, F_SMALL, 2);
	}
	if (v.posMs > 0 && v.durMs > 0)
	{
		int bw = w - 14, f = (int) ((long long) v.posMs * bw / v.durMs); if (f > bw) f = bw; if (f < 3) f = 3;
		ui_fill (x + 7, y + h - 6, bw, 3, 0x000000);
		ui_fill (x + 7, y + h - 6, f, 3, jet (J_DANGER));
	}
	else if (v.watched)
	{
		disc (x + w - 15, y + 15, 10, 0x101014);
		icon (I_CHECK, x + w - 22, y + 8, 14, 0xFFFFFF);
	}
	if (!v.playable)
	{
		const char *c = codec_label (v.vcodec);
		int bw = tw (c, F_SMALL, 2) + 12;
		rbox (x + 6, y + 6, bw, 18, 4, 0x707078);
		text_c (x + 6, y + 6, bw, 18, c, 0xFFFFFF, F_SMALL, 2);
	}
}
// a video's frame w wide (16:9), its length, how much was watched; under the pointer: play
static void video_tile (int x, int y, int w, int vi, bool texts = true)
{
	const Video &v = VL->v[vi];
	int h = w * 9 / 16;
	bool hv = g_hits.n == g_hot;
	g_hits.add (x, y, w, h + (texts ? 44 : 0), H_VIDEO, vi);
	if (y + h + 44 < 0 || y > g_ch) return;
	g_thumbs->draw (vi, x, y, w, h, 7, C_FIELD);
	video_badges (x, y, w, h, v);
	if (hv)
	{
		veil (x, y, w, h, 0, 60);
		disc (x + w / 2, y + h / 2, 22, 0xF4F4F4);
		icon (I_PLAY, x + w / 2 - 10, y + h / 2 - 11, 22, 0x202028);
	}
	if (!texts) return;
	text (x, y + h + 7, v.title, C_TEXT, F_UI, 2, w);
	char k[96]; video_kind_line (v, k, sizeof k);
	if (!v.playable) { char t[140]; snprintf (t, sizeof t, TR ("%s  \xC2\xB7  %s: not played here"), k, codec_label (v.vcodec)); text (x, y + h + 26, t, col_dim (), F_SMALL, 0, w); }
	else text (x, y + h + 26, k, col_dim (), F_SMALL, 0, w);
}
static void small_button (int x, int y, int w, int ic, const char *label, int kind, int a)
{
	bool h = g_hits.n == g_hot;
	unsigned fill = tone (C_ACCENT, h ? 150 : 128), ink = ink_on (fill);
	rbox (x, y, w, 26, 5, fill);
	icon (ic, x + 9, y + 6, 14, ink);
	text_v (x + 28, y, 26, label, ink, F_UI, 2, w - 30);
	g_hits.add (x, y, w, 26, kind, a);
}
// what to go on with: the video left half way -> its card at (x, y), cw wide
static void resume_video_card (int x, int y, int cw, int vi)
{
	const Video &v = VL->v[vi];
	unsigned card = jet (J_ELEVE);
	panel (x, y, cw, 78, 9, card, C_LINE);
	g_hits.add (x, y, cw, 78, H_VRESUME, vi);
	int tw_ = 110, th = 62;
	g_thumbs->draw (vi, x + 8, y + 8, tw_, th, 5, card);
	video_badges (x + 8, y + 8, tw_, th, v);
	int tx = x + 8 + tw_ + 12, tmax = cw - (tx - x) - 10;
	text (tx, y + 10, v.title, ink_on (card), F_BIG, 2, tmax);
	char k[64], line[140], left[24]; video_kind_line (v, k, sizeof k);
	int rest = v.durMs - v.posMs;
	if (rest < 60000) snprintf (left, sizeof left, "%d s", rest > 0 ? rest / 1000 : 0); else fmt_long (left, sizeof left, rest);
	snprintf (line, sizeof line, TR ("%s  \xC2\xB7  %s left"), k, left);
	text (tx, y + 31, line, col_dim (), F_SMALL, 0, tmax);
	small_button (tx, y + 46, 96, I_PLAY, TR ("Resume"), H_VRESUME, vi);
}
// the song playing (else the last played) -> its card
static void resume_song_card (int x, int y, int cw, int cur, bool half)
{
	const Song &s = *song (cur);
	unsigned card = jet (J_ELEVE);
	panel (x, y, cw, 78, 9, card, C_LINE);
	if (s.alb >= 0 && cur >= 0) g_covers->draw (s.alb, x + 9, y + 9, 60, 5, card);
	bool isCur = g_hasSong && cur == g_playing, playing = isCur && g_player.state == PS_PLAYING;
	int tmax = cw - 82 - (half ? 56 : 160);
	text (x + 82, y + 12, s.title, ink_on (card), F_BIG, 2, tmax);
	char line[300];
	int pos = -1; if (s.alb >= 0 && cur >= 0) for (int i = 0; i < L->al[s.alb].n; i++) if (L->al[s.alb].songs[i] == cur) pos = i;
	if (half && pos >= 0) snprintf (line, sizeof line, TR ("%s  \xC2\xB7  song %d of %d"), L->artist_of (s), pos + 1, L->al[s.alb].n);
	else snprintf (line, sizeof line, "%s  \xC2\xB7  %s", L->artist_of (s), s.album);
	text (x + 82, y + 34, line, col_dim (), F_SMALL, 0, tmax);
	if (playing) { eq_bars (x + 82, y + 55, 12, C_ACCENT, g_tick); g_anim = true; text (x + 100, y + 54, TR ("Playing"), C_ACCENT, F_SMALL, 2); }
	else text (x + 82, y + 54, isCur ? TR ("Paused") : TR ("Played last"), col_dim (), F_SMALL);
	if (half)
	{
		bool h = g_hits.n == g_hot;
		disc (x + cw - 34, y + 39, 18, h ? tone (C_ACCENT, 150) : C_ACCENT);
		icon (playing ? I_PAUSE : I_PLAY, x + cw - 43, y + 30, 18, ink_on (C_ACCENT));
		g_hits.add (x + cw - 52, y + 21, 36, 36, H_RESUME, cur);
	}
	else { band_button (x + cw - 130, y + 22, 118, playing ? I_PAUSE : I_PLAY, playing ? TR ("Pause") : isCur ? TR ("Resume") : TR ("Play"), true, H_RESUME); g_hits.h[g_hits.n - 1].a = cur; }
}

// ---- the pages ----
static int draw_home (int x, int y, int w)
{
	time_t now = time (0); struct tm tm; localtime_r (&now, &tm); int hr = tm.tm_hour;
	const char *hello = hr < 5 ? TR ("Good night") : hr < 12 ? TR ("Good morning") : hr < 18 ? TR ("Good afternoon") : TR ("Good evening");
	char sub[120];
	if (VL->n) snprintf (sub, sizeof sub, TR ("%d songs  \xC2\xB7  %d videos  \xC2\xB7  %d playlists"), L->n, VL->n, g_npl);
	else snprintf (sub, sizeof sub, TR ("%d songs  \xC2\xB7  %d albums  \xC2\xB7  %d playlists"), L->n, L->nal, g_npl);
	section_head (x, y, w, hello, sub, 0, 0);
	y += 50;
	// go on with: the video left half way; the song playing, else the last played
	int cur = PLAYING_LIB ? g_playing : -1;
	if (cur < 0) { unsigned long long best = 0; for (int i = 0; i < L->n; i++) if (L->s[i].lastPlayed > best) { best = L->s[i].lastPlayed; cur = i; } }
	bool songCard = cur >= 0 && song (cur);
	int vres = VL->resume_candidate ();
	if (vres >= 0 && songCard)
	{
		int cw = (w - 16) / 2;
		resume_video_card (x, y, cw, vres);
		resume_song_card (x + cw + 16, y, w - cw - 16, cur, true);
		y += 96;
	}
	else if (vres >= 0) { resume_video_card (x, y, w, vres); y += 96; }
	else if (songCard) { resume_song_card (x, y, w, cur, false); y += 96; }
	// rows: the albums played lately, the videos, the albums added lately
	const int S = 128, G = 18;
	int cols = (w + G) / (S + G); if (cols < 1) cols = 1;
	for (int row = 0; row < 3; row++)
	{
		if (row == 1)
		{
			IntList vl; VL->recent (vl);
			if (!vl.n) continue;
			text (x, y, TR ("Videos"), C_TEXT, F_BIG, 2);
			bool h = g_hits.n == g_hot;
			text_r (x + w, y, 22, TR ("See all  \xE2\x80\xBA"), h ? C_ACCENT : C_TEXT);
			g_hits.add (x + w - 80, y, 80, 22, H_SEEALL, 2);
			y += 32;
			const int VW = 166, VG = 16;
			int vc = (w + VG) / (VW + VG); if (vc < 1) vc = 1;
			for (int i = 0; i < vl.n && i < vc; i++) video_tile (x + i * (VW + VG), y, VW, vl.v[i]);
			y += VW * 9 / 16 + 62;
			continue;
		}
		IntList al;
		int *o = (int *) malloc (sizeof (int) * (L->nal ? L->nal : 1));
		if (row == 0)
		{
			for (int i = 0; i < L->nal; i++) { unsigned long long lp = 0; for (int k = 0; k < L->al[i].n; k++) if (L->s[L->al[i].songs[k]].lastPlayed > lp) lp = L->s[L->al[i].songs[k]].lastPlayed; if (lp) al.push (i); }
			sort_idx (al.v, al.n, [] (const void *, int a, int b) -> int {
				unsigned long long x = 0, y = 0;
				for (int k = 0; k < L->al[a].n; k++) if (L->s[L->al[a].songs[k]].lastPlayed > x) x = L->s[L->al[a].songs[k]].lastPlayed;
				for (int k = 0; k < L->al[b].n; k++) if (L->s[L->al[b].songs[k]].lastPlayed > y) y = L->s[L->al[b].songs[k]].lastPlayed;
				return y > x ? 1 : y < x ? -1 : 0; }, 0);
		}
		else { L->album_order (o, 3); for (int i = 0; i < L->nal; i++) al.push (o[i]); }
		free (o);
		if (!al.n) continue;
		text (x, y, row == 0 ? TR ("Recently played") : TR ("Recently added"), C_TEXT, F_BIG, 2);
		bool h = g_hits.n == g_hot;
		text_r (x + w, y, 22, TR ("See all  \xE2\x80\xBA"), h ? C_ACCENT : C_TEXT);
		g_hits.add (x + w - 80, y, 80, 22, H_SEEALL, row == 0 ? 0 : 1);
		y += 32;
		for (int i = 0; i < al.n && i < cols; i++) album_tile (x + i * (S + G), y, S, al.v[i]);
		y += S + 62;
	}
	return y;
}
// the videos of a kind: a grid of their frames
static int draw_videos (int x, int y, int w)
{
	int kind = page ().arg;
	IntList vl; VL->list (vl, kind);
	long long ms = 0; for (int i = 0; i < vl.n; i++) ms += VL->v[vl.v[i]].durMs;
	char sub[96], d[24]; fmt_long (d, sizeof d, ms);
	snprintf (sub, sizeof sub, vl.n == 1 ? TR ("%d video  \xC2\xB7  %s") : TR ("%d videos  \xC2\xB7  %s"), vl.n, d);
	section_head (x, y, w, kind == VK_FILM ? TR ("Films") : kind == VK_CLIP ? TR ("Clips and series") : TR ("Videos"), sub, 0, 0);
	y += 54;
	if (!vl.n)
	{
		if (g_scan && !g_scan->done) { char t[64]; snprintf (t, sizeof t, TR ("Looking for videos...  %d"), g_scan->vfound); text_c (x, y + 80, w, 24, t, C_TEXT, F_BIG, 2); return y + 120; }
		disc (x + w / 2, y + 70, 40, C_ACCENT); icon (I_FILM, x + w / 2 - 20, y + 50, 40, ink_on (C_ACCENT), C_ACCENT);
		text_c (x, y + 124, w, 24, kind == VK_FILM ? TR ("No films yet.") : kind == VK_CLIP ? TR ("No clips or episodes yet.") : TR ("No videos yet."), C_TEXT, F_BIG, 2);
		text_c (x, y + 154, w, 20, TR ("Put them in SD0:/home/video (Films, Series, Clips...) or in a folder watched."), col_dim ());
		text_c (x, y + 174, w, 20, TR ("WebM, Matroska, MP4 -- VP8, VP9, AV1; Opus, Vorbis, MP3, FLAC."), col_dim ());
		band_button (x + w / 2 - 85, y + 210, 170, I_PLAY, TR ("Open a video..."), true, H_OPENVID);
		return y + 260;
	}
	const int S = 206, G = 22;
	int cols = (w + G) / (S + G); if (cols < 1) cols = 1;
	int gx = x + (w - (cols * S + (cols - 1) * G)) / 2, rowH = S * 9 / 16 + 64;
	for (int i = 0; i < vl.n; i++) video_tile (gx + (i % cols) * (S + G), y + (i / cols) * rowH, S, vl.v[i]);
	return y + ((vl.n + cols - 1) / cols) * rowH;
}
static int draw_albums (int x, int y, int w)
{
	char sub[64]; long long ms = 0; for (int i = 0; i < L->nal; i++) ms += L->al[i].durMs;
	char d[24]; fmt_long (d, sizeof d, ms);
	snprintf (sub, sizeof sub, TR ("%d albums  \xC2\xB7  %d songs  \xC2\xB7  %s"), L->nal, L->n, d);
	const char *SORTS[4] = { TR ("Sort: Name"), TR ("Sort: Artist"), TR ("Sort: Year"), TR ("Sort: Recently added") };
	section_head (x, y, w, TR ("Albums"), sub, SORTS[g_albumSort & 3], H_SORT);
	y += 54;
	int *o = (int *) malloc (sizeof (int) * (L->nal ? L->nal : 1));
	L->album_order (o, g_albumSort);
	if (g_albumList)
	{
		for (int i = 0; i < L->nal; i++)
		{
			int ry = y + i * 56; const Album &a = L->al[o[i]];
			if (ry + 56 >= 0 && ry <= g_ch)
			{
				bool h = g_hits.n == g_hot;
				unsigned over = mix (C_FIELD, C_BG, 110);
				if (h) rbox (x, ry, w, 52, 6, over);
				g_covers->draw (o[i], x + 6, ry + 4, 44, 4, h ? over : C_FIELD);
				text (x + 62, ry + 8, a.title, C_TEXT, F_UI, 2, w / 2 - 70);
				text (x + 62, ry + 27, a.artist, col_dim (), F_SMALL, 0, w / 2 - 70);
				char t[64], dd[24]; fmt_long (dd, sizeof dd, a.durMs);
				if (a.year) snprintf (t, sizeof t, "%d", a.year); else t[0] = 0;
				text_v (x + w / 2, ry, 52, t, col_dim ());
				snprintf (t, sizeof t, TR ("%d songs  \xC2\xB7  %s"), a.n, dd); text_r (x + w - 10, ry, 52, t, col_dim (), F_SMALL);
			}
			g_hits.add (x, ry, w, 52, H_ALBUM, o[i]);
		}
		y += L->nal * 56;
	}
	else
	{
		const int S = 140, G = 22;
		int cols = (w + G) / (S + G); if (cols < 1) cols = 1;
		int gx = x + (w - (cols * S + (cols - 1) * G)) / 2;
		for (int i = 0; i < L->nal; i++)
		{
			int tx = gx + (i % cols) * (S + G), ty = y + (i / cols) * (S + 64);
			if (ty + S + 64 < 0 || ty > g_ch) { g_hits.add (tx, ty, S, S + 44, H_ALBUM, o[i]); continue; }
			album_tile (tx, ty, S, o[i]);
		}
		y += ((L->nal + cols - 1) / cols) * (S + 64);
	}
	free (o);
	return y;
}
static int draw_header_band (int x, int y, int w, int album, const char *kind, const char *title, const char *by, const char *meta, bool circle)
{
	unsigned tn = album >= 0 ? g_covers->tone (album) : 0x384858;		// (the cover's colours: not the theme's)
	unsigned top = mix (tn, 0x000000, 60), bot = mix (tn, 0x000000, 120), mid = mix (top, bot, 100);
	rbox (x, y, w, 200, 10, top, bot);
	if (album >= 0) g_covers->draw (album, x + 20, y + 20, 160, circle ? 80 : 8, mid);
	else { disc (x + 100, y + 100, 80, mix (tn, 0xFFFFFF, 60)); icon (I_PERSON, x + 60, y + 60, 80, 0xFFFFFF); }
	int tx = x + 204;
	unsigned lite = mix (tn, 0xFFFFFF, 200);
	text (tx, y + 26, kind, lite, F_SMALL, 2);
	text (tx, y + 44, title, 0xFFFFFF, F_H1, 2, w - 224);
	if (by) text (tx, y + 82, by, 0xF0F0F0, F_MID, 0, w - 224);
	text (tx, y + 106, meta, lite, F_SMALL, 0, w - 224);
	band_button (tx, y + 140, 100, I_PLAY, TR ("Play"), true, H_PLAY);
	band_button (tx + 110, y + 140, 112, I_SHUFFLE, TR ("Shuffle"), false, H_SHUFFLE);
	band_button (tx + 232, y + 140, 40, I_MORE, 0, false, H_MORE);
	return y + 216;
}

// the MIDI song shown by now playing (parsed here: AudioKit's own is its thread's)
static MidiSong *g_roll; static int g_rollFor = -999;
static MidiSong *roll_for (int id)
{
	if (id == g_rollFor) return g_roll;
	delete g_roll; g_roll = 0; g_rollFor = id;
	const Song *s = g_hasSong ? song (id) : 0;
	if (s && s->fmt == FMT_MIDI) { g_roll = new MidiSong; char e[64]; if (!g_roll->load (s->path, e, sizeof e)) { delete g_roll; g_roll = 0; } }
	return g_roll;
}
static unsigned roll_colour (int ch)
{
	static const unsigned CH[16] = { 0xF06E5A, 0xFABE46, 0x78D26E, 0x50BEE6, 0xAA82FA, 0xFA78C8, 0x5ADCC8, 0xE6E66E, 0xF09650, 0x9696A0,
					 0x6E96FA, 0xDC6EDC, 0x96DC50, 0x50C8A0, 0xC8A078, 0xA0B4DC };
	return CH[ch & 15];
}
// a MIDI file's notes, scrolling under the playhead: a line a note, a colour a channel, the bars numbered (a screen of its
// own: dark whatever the theme)
static void piano_roll (int x, int y, int w, int h, MidiSong *m, long long posMs)
{
	const unsigned BG = 0x18161F;
	rbox (x, y, w, h, 10, BG);
	if (!m) return;
	int lo = m->lowKey - 1, hi = m->highKey + 1; if (hi - lo < 24) { int c = (lo + hi) / 2; lo = c - 12; hi = c + 12; }
	int keys = hi - lo + 1, kx = x + 50, kw = w - 60, top = y + 26, kh = h - 34;
	int rowH = kh / keys; if (rowH < 3) rowH = 3;
	int rows = kh / rowH; if (rows < keys) { hi = lo + rows - 1; keys = rows; }
	top = y + 26 + (kh - keys * rowH) / 2;
	for (int k = 0; k < keys; k++)					// the keyboard
	{
		int key = hi - k, n = key % 12; bool black = n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
		ui_fill (x + 10, top + k * rowH, black ? 22 : 32, rowH - 1, black ? 0x3C3A46 : 0xE6E4EC);
	}
	i64 now = posMs * SOUND_RATE / 1000;
	// the span: about 8 bars (else 10 s), the playhead at 30 %
	i64 span = SOUND_RATE * 10;
	if (m->nbars > 2) { int b = 0; while (b + 1 < m->nbars && m->bars[b + 1] <= now) b++; int e = b + 8 < m->nbars ? b + 8 : m->nbars - 1; if (e > b) span = (m->bars[e] - m->bars[b]) * 8 / (e - b); }
	if (span < SOUND_RATE * 3) span = SOUND_RATE * 3;
	i64 t0 = now - span * 3 / 10;
	auto X = [&] (i64 t) { return kx + (int) ((t - t0) * kw / span); };
	for (int b = 0; b < m->nbars; b++)				// the bars
	{
		int bx = X (m->bars[b]);
		if (bx < kx || bx > kx + kw) continue;
		ui_fill (bx, y + 8, 1, h - 16, mix (BG, 0xFFFFFF, 34));
		char t[12]; snprintf (t, sizeof t, "%d", b + 1); text (bx + 4, y + 7, t, 0x8A889A, F_SMALL);
	}
	for (int i = 0; i < m->nnotes; i++)
	{
		const MidiNote &n = m->notes[i];
		if (n.off < t0 || n.on > t0 + span || n.ch == 9 || n.key > hi || n.key < lo) continue;
		int nx0 = X (n.on), nx1 = X (n.off);
		if (nx0 < kx) nx0 = kx;
		if (nx1 > kx + kw) nx1 = kx + kw;
		if (nx1 - nx0 < 4) nx1 = nx0 + 4;
		int ny = top + (hi - n.key) * rowH;
		bool sounding = n.on <= now && n.off > now, past = n.off <= now;
		unsigned c = roll_colour (n.ch); if (past) c = mix (c, BG, 110);
		int nh = rowH > 4 ? rowH - 1 : rowH;
		if (sounding) rbox (nx0 - 1, ny - 1, nx1 - nx0 + 1, nh + 2, 3, 0xFFFFFF);
		rbox (nx0, ny, nx1 - nx0 - 1, nh, nh / 2 < 3 ? nh / 2 : 3, c);
	}
	int ph = kx + kw * 3 / 10;					// the playhead
	ui_fill (ph - 1, y + 6, 2, h - 12, 0xFFFFFF);
	int tp[] = { V (ph - 6), V (y + 6), V (ph + 6), V (y + 6), V (ph), V (y + 13) }; ui_vp_poly ((long) tp, 3, 0xFFFFFF);
}
// the instruments of the file: a dot of their colour, their name -> the height used
static int roll_legend (int x, int y, int w, MidiSong *m, unsigned ink)
{
	if (!m) return 0;
	int lx = x, ly = y;
	for (int c = 0; c < 16; c++)
	{
		if (!m->used[c] || c == 9) continue;
		const char *nm = m->instrument (c);
		int ww = tw (nm, F_SMALL) + 30;
		if (lx + ww > x + w) { lx = x; ly += 20; }
		disc (lx + 6, ly + 9, 5, roll_colour (c));
		text (lx + 16, ly + 2, nm, ink, F_SMALL);
		lx += ww;
	}
	return ly - y + 20;
}
static const char *soundfont_name ()
{
	return g_player.sfName[0] ? g_player.sfName : TR ("a SoundFont");		// (the player's thread asked AudioKit)
}
// a MIDI file playing: its header (cover, title, what plays it), its notes large, its instruments
static int draw_midi (int x, int y, int w, int hRoll)
{
	const Song *s = g_hasSong ? song (g_playing) : 0;
	MidiSong *m = roll_for (g_playing);
	if (!s) return y;
	g_anim = true;
	if (s->alb >= 0 && g_playing >= 0) g_covers->draw (s->alb, x, y, 104, 6, C_FIELD);
	int tx = x + 124;
	text (tx, y + 2, g_player.state == PS_PLAYING ? TR ("MIDI  \xC2\xB7  NOW PLAYING") : TR ("MIDI  \xC2\xB7  PAUSED"), C_ACCENT, F_SMALL, 2);
	text (tx, y + 19, s->title, C_TEXT, F_H2, 2, w - 124);
	int ninst = 0; if (m) for (int c = 0; c < 16; c++) if (m->used[c] && c != 9) ninst++;
	char line[300];
	snprintf (line, sizeof line, ninst == 1 ? TR ("%s  \xC2\xB7  %d instrument%s  \xC2\xB7  \xE2\x99\xA9 = %d") : TR ("%s  \xC2\xB7  %d instruments%s  \xC2\xB7  \xE2\x99\xA9 = %d"), g_playing >= 0 ? L->artist_of (*s) : s->artist, ninst,
		  m && m->used[9] ? TR (" and drums") : "", m ? m->bpm : 120);
	text (tx, y + 48, line, col_dim (), F_SMALL, 0, w - 124);
	text_v (tx, y + 70, 28, TR ("Played by"), col_dim (), F_SMALL);
	const char *sf = soundfont_name ();
	int fx = tx + 64, fw = tw (sf) + 24;
	panel (fx, y + 70, fw, 28, 6, C_BG, C_LINE);
	text_v (fx + 12, y + 70, 28, sf, C_TEXT);
	y += 122;
	piano_roll (x, y, w, hRoll, m, g_player.posMs);
	y += hRoll + 12;
	y += roll_legend (x, y, w, m, col_dim ());
	return y + 8;
}
static bool midi_here (int album) { const Song *s = g_hasSong ? song (g_playing) : 0; return s && g_playing >= 0 && s->fmt == FMT_MIDI && s->alb == album && album >= 0; }
static int draw_album (int x, int y, int w)
{
	const Page &pg = page ();
	if (!L || pg.arg < 0 || pg.arg >= L->nal) return y;
	const Album &a = L->al[pg.arg];
	if (midi_here (pg.arg))
	{
		int hRoll = g_ch - 122 - 22 - 60; if (hRoll < 160) hRoll = 160;
		y = draw_midi (x, y, w, hRoll);
		return song_table (x, y + 8, w, g_rows, false, true);
	}
	char meta[300], d[24]; fmt_long (d, sizeof d, a.durMs);
	const Song &f = L->s[a.songs[0]];
	char yr[16] = ""; if (a.year) snprintf (yr, sizeof yr, "%d  \xC2\xB7  ", a.year);
	snprintf (meta, sizeof meta, TR ("%s%s%s%d songs  \xC2\xB7  %s  \xC2\xB7  %s"), yr, a.genre[0] ? a.genre : "", a.genre[0] ? "  \xC2\xB7  " : "", a.n, d, FMT_NAME[f.fmt >= 0 ? f.fmt : 0]);
	y = draw_header_band (x, y, w, pg.arg, TR ("ALBUM"), a.title, a.artist, meta, false);
	g_hits.add (x + 204, y - 216 + 82, tw (a.artist, F_MID), 22, H_ARTIST, f.art);
	return song_table (x, y, w, g_rows, false, true);
}
static int draw_artists (int x, int y, int w)
{
	char sub[48]; snprintf (sub, sizeof sub, TR ("%d artists"), L->nar);
	section_head (x, y, w, TR ("Artists"), sub, 0, 0);
	y += 54;
	const int S = 128, G = 26;
	int cols = (w + G) / (S + G); if (cols < 1) cols = 1;
	int gx = x + (w - (cols * S + (cols - 1) * G)) / 2;
	for (int i = 0; i < L->nar; i++)
	{
		int tx = gx + (i % cols) * (S + G), ty = y + (i / cols) * (S + 60);
		g_hits.add (tx, ty, S, S + 44, H_ARTIST, i);
		if (ty + S + 60 < 0 || ty > g_ch) continue;
		const Group &g = L->ar[i];
		bool h = g_hits.n - 1 == g_hot;
		int al = g.na ? g.albums[0] : -1;
		if (h) disc (tx + S / 2, ty + S / 2, S / 2 + 3, C_ACCENT);
		if (al >= 0) g_covers->draw (al, tx, ty, S, S / 2, h ? C_ACCENT : C_FIELD);
		text_c (tx - 10, ty + S + 6, S + 20, 20, g.name, C_TEXT, F_UI, 2);
		char t[48]; snprintf (t, sizeof t, g.na == 1 ? TR ("%d album") : TR ("%d albums"), g.na);
		text_c (tx, ty + S + 26, S, 18, t, col_dim (), F_SMALL);
	}
	return y + ((L->nar + cols - 1) / cols) * (S + 60);
}
static int draw_artist (int x, int y, int w)
{
	const Page &pg = page ();
	if (pg.arg < 0 || pg.arg >= L->nar) return y;
	const Group &g = L->ar[pg.arg];
	char meta[96], d[24]; fmt_long (d, sizeof d, g.durMs);
	snprintf (meta, sizeof meta, TR ("%d albums  \xC2\xB7  %d songs  \xC2\xB7  %s"), g.na, g.n, d);
	y = draw_header_band (x, y, w, g.na ? g.albums[0] : -1, TR ("ARTIST"), g.name, 0, meta, true);
	text (x, y + 4, TR ("Albums"), C_TEXT, F_BIG, 2); y += 34;
	const int S = 120, G = 18;
	int cols = (w + G) / (S + G); if (cols < 1) cols = 1;
	for (int i = 0; i < g.na; i++) album_tile (x + (i % cols) * (S + G), y + (i / cols) * (S + 62), S, g.albums[i], false);
	y += ((g.na + cols - 1) / cols) * (S + 62) + 8;
	text (x, y, TR ("Songs"), C_TEXT, F_BIG, 2); y += 34;
	return song_table (x, y, w, g_rows, true, false);
}
// the library has no song yet: where to add a folder, in sight
static int empty_library (int x, int y, int w)
{
	if (g_scan && !g_scan->done)
	{
		char t[64]; snprintf (t, sizeof t, TR ("Looking for songs...  %d"), g_scan->found);
		text_c (x, y + 110, w, 24, t, C_TEXT, F_BIG, 2);
		return y + 150;
	}
	text_c (x, y + 110, w, 24, TR ("No songs yet."), C_TEXT, F_BIG, 2);
	text_c (x, y + 140, w, 20, TR ("Add the folders where your music is:"), col_dim ());
	text_c (x, y + 160, w, 20, TR ("Media Player finds the songs in them, and looks again at each start."), col_dim ());
	band_button (x + w / 2 - 85, y + 196, 170, I_PLUS, TR ("Add a folder..."), true, H_ADDFOLDER);
	return y + 250;
}
static int draw_groups (int x, int y, int w, Group *gs, int n, const char *title, int kind, bool paths)
{
	char sub[48]; snprintf (sub, sizeof sub, "%d", n);
	section_head (x, y, w, title, sub, 0, 0);
	if (paths)
	{	// the folders: where to add one, in sight (the menu's Folders to Watch... too)
		band_button (x + w - 170, y, 170, I_PLUS, TR ("Add a folder..."), true, H_ADDFOLDER);
		if (!n) return empty_library (x, y, w);
	}
	y += 54;
	if (!paths)
	{	// tiles of colour
		const int TW = 200, TH = 92, G = 16;
		int cols = (w + G) / (TW + G); if (cols < 1) cols = 1;
		for (int i = 0; i < n; i++)
		{
			int tx = x + (i % cols) * (TW + G), ty = y + (i / cols) * (TH + G);
			g_hits.add (tx, ty, TW, TH, kind, i);
			unsigned c = hash_str (gs[i].name); c = mix (0x30405A, (c & 0x7F7F7F) + 0x303030, 160);
			bool h = g_hits.n - 1 == g_hot;
			rbox (tx, ty, TW, TH, 10, tone (c, h ? 150 : 138), tone (c, h ? 120 : 108));
			text (tx + 16, ty + 16, gs[i].name, 0xFFFFFF, F_BIG, 2, TW - 32);
			char t[48]; snprintf (t, sizeof t, gs[i].na == 1 ? TR ("%d songs  \xC2\xB7  %d album") : TR ("%d songs  \xC2\xB7  %d albums"), gs[i].n, gs[i].na);
			text (tx + 16, ty + TH - 30, t, 0xF0F0F0, F_SMALL);
		}
		return y + ((n + cols - 1) / cols) * (TH + G);
	}
	for (int i = 0; i < n; i++)
	{
		int ry = y + i * 44;
		g_hits.add (x, ry, w, 40, kind, i);
		bool h = g_hits.n - 1 == g_hot;
		if (h) rbox (x, ry, w, 40, 6, mix (C_FIELD, C_BG, 110));
		icon (I_FOLDER, x + 10, ry + 10, 20, 0);
		text_v (x + 42, ry, 40, gs[i].name, C_TEXT, F_UI, 2, w - 200);
		char t[48]; snprintf (t, sizeof t, TR ("%d songs"), gs[i].n); text_r (x + w - 12, ry, 40, t, col_dim (), F_SMALL);
	}
	return y + n * 44;
}
static int draw_list_page (int x, int y, int w, const char *kind, const char *title, int coverAlbum)
{
	long long ms = 0; for (int i = 0; i < g_rows.n; i++) ms += L->s[g_rows.v[i]].durMs;
	char meta[96], d[24]; fmt_long (d, sizeof d, ms); snprintf (meta, sizeof meta, TR ("%d songs  \xC2\xB7  %s"), g_rows.n, d);
	y = draw_header_band (x, y, w, coverAlbum, kind, title, 0, meta, false);
	if (!g_rows.n)
	{
		const char *m = page ().kind == P_PLAYLIST && page ().arg == PL_FAV ? TR ("No favourites yet: the \xE2\x99\xA5 of a song, of the bar below, adds it here.")
			: page ().kind == P_PLAYLIST ? TR ("This playlist is empty: right click songs, then \xE2\x80\x9C" "Add to a playlist\xE2\x80\x9D.") : TR ("Nothing here.");
		text (x, y + 20, m, col_dim ());
		return y + 60;
	}
	return song_table (x, y, w, g_rows, true, false);
}
static int draw_search (int x, int y, int w)
{
	char sub[64]; snprintf (sub, sizeof sub, TR ("%d songs"), g_rows.n);
	section_head (x, y, w, TR ("Search"), sub, 0, 0);
	y += 54;
	// the albums and artists that match
	int na = 0;
	const int S = 110, G = 16; int cols = (w + G) / (S + G);
	for (int i = 0; i < L->nal && na < cols; i++)
		if (matches (L->al[i].title, g_search) || matches (L->al[i].artist, g_search)) { album_tile (x + na * (S + G), y, S, i); na++; }
	if (na) y += S + 62;
	// the videos that match
	int nv = 0;
	const int VW = 166, VG = 16; int vc = (w + VG) / (VW + VG); if (vc < 1) vc = 1;
	for (int i = 0; i < VL->n && nv < vc; i++)
		if (!VL->v[i].ext && matches (VL->v[i].title, g_search)) { video_tile (x + nv * (VW + VG), y, VW, i); nv++; }
	if (nv) y += VW * 9 / 16 + 62;
	if (!g_rows.n && !na && !nv) { text (x, y, TR ("Nothing found."), col_dim ()); return y + 40; }
	return song_table (x, y, w, g_rows, true, false);
}
// s centred in (x, w), wrapped when it is wider -> the y under it
static int text_cw (int x, int y, int w, const char *s, unsigned c)
{
	if (tw (s) <= w) { text_c (x, y, w, 20, s, c); return y + 20; }
	long h = ui_text_wrap ((long) s, x, y + 14, w, c, face (0), FACE_PX[F_UI], 0);
	return y + (int) h + 4;
}
static int draw_welcome (int x, int y, int w)
{
	int mx = x + w / 2;
	y += 30;
	static const int ORDER[3] = { 0, 2, 1 };				// (the middle one on top)
	for (int j = 0; j < 3 && L && L->nal; j++) { int k = ORDER[j]; g_covers->draw (k % L->nal, mx - 110 + k * 70 - 45, y + (k == 1 ? 0 : 10), 90, 8, C_FIELD); }
	if (!L || !L->nal) { disc (mx, y + 45, 45, C_ACCENT); icon (I_NOTE, mx - 24, y + 21, 48, ink_on (C_ACCENT)); }
	y += 120;
	if (tw (TR ("Your music, all in one place"), F_H1, 2) > w) text_c (x, y, w, 34, TR ("Your music, all in one place"), C_TEXT, F_H2, 2);
	else text_c (x, y, w, 34, TR ("Your music, all in one place"), C_TEXT, F_H1, 2);
	int ty = text_cw (x, y + 40, w, TR ("Media Player finds the songs in the folders you give it, and looks again at each start."), col_dim ());
	ty = text_cw (x, ty, w, TR ("MP3, OGG, Opus, FLAC, WAV and MIDI; videos: WebM, Matroska, MP4."), col_dim ());
	int fw = w < 460 ? w : 460, fx = mx - fw / 2, fy = ty + 20;
	int fh_ = 10 + (g_nfolders ? g_nfolders : 1) * 36;
	unsigned card = jet (J_ELEVE), ink = ink_on (card), dim = col_dim ();
	panel (fx, fy, fw, fh_, 8, card, C_LINE);
	for (int i = 0; i < g_nfolders; i++)
	{
		int ry = fy + 5 + i * 36;
		icon (I_FOLDER, fx + 14, ry + 8, 20, 0);
		text_v (fx + 44, ry, 36, g_folders[i], ink, F_UI, 2, fw - 200);
		int cnt = 0, vc = 0; int pl = (int) strlen (g_folders[i]);
		for (int k = 0; L && k < L->n; k++) if (!strncasecmp (L->s[k].path, g_folders[i], (size_t) pl)) cnt++;
		for (int k = 0; VL && k < VL->n; k++) if (!strncasecmp (VL->v[k].path, g_folders[i], (size_t) pl)) vc++;
		char t[64];
		if (g_scan && !g_scan->done) snprintf (t, sizeof t, TR ("looking...  %d"), g_scan->found + g_scan->vfound);
		else if (vc) snprintf (t, sizeof t, TR ("%d songs, %d videos"), cnt, vc); else snprintf (t, sizeof t, TR ("%d songs found"), cnt);
		text_r (fx + fw - 44, ry, 36, t, dim, F_SMALL);
		bool h = g_hits.n == g_hot;
		icon (I_CLOSE, fx + fw - 30, ry + 11, 14, h ? jet (J_DANGER) : dim);
		g_hits.add (fx + fw - 36, ry + 4, 28, 28, H_DELFOLDER, i);
	}
	if (!g_nfolders) text_c (fx, fy + 5, fw, 36, TR ("No folder: add one."), dim);
	fy += fh_ + 18;
	int bw = tw (TR ("Add a folder...")) + 50;
	if (bw < 150) bw = 150;
	band_button (mx - bw - 10, fy, bw, I_PLUS, TR ("Add a folder..."), false, H_ADDFOLDER);
	band_button (mx + 10, fy, bw, I_DISC, TR ("Done"), true, H_DONE);
	return fy + 60;
}
// ---- now playing: the cover large (a MIDI file: its notes), up next ----
static int draw_now (int x, int y, int w)
{
	const Song *s = g_hasSong ? song (g_playing) : 0;
	if (s && s->fmt == FMT_MIDI)
	{
		int hRoll = g_ch - 122 - 22 - 60; if (hRoll < 160) hRoll = 160;
		return draw_midi (x, y, w, hRoll);
	}
	// (a backdrop of the cover's colours, dark: not the theme's)
	unsigned tn = s && s->alb >= 0 && g_playing >= 0 ? g_covers->tone (s->alb) : 0x2A3448;
	unsigned top = mix (tn, 0x0A0E18, 150), bot = mix (tn, 0x0A0E18, 215), mid = mix (top, bot, 100);
	ui_rbox (0, 0, g_cw, g_ch, 0, top, bot);
	int x0 = 24, y0 = 18;
	disc (x0 + 16, y0 + 16, 16, mix (top, 0xFFFFFF, 40));
	icon (I_DOWN, x0 + 8, y0 + 8, 16, 0xFFFFFF);
	g_hits.add (x0, y0, 32, 32, H_NOWCLOSE);
	text_v (x0 + 44, y0, 32, TR ("Now playing"), 0xFFFFFF, F_UI, 2);
	if (!s) { text (x0, y0 + 80, TR ("Nothing is playing. Choose a song, an album..."), 0xE0E0E0, F_MID); return y + g_ch; }
	int qw = g_cw >= 760 ? 330 : 0, avail = g_cw - 48 - (qw ? qw + 32 : 0);
	int S = avail < 320 ? avail : 320; if (S > g_ch - 200) S = g_ch - 200; if (S < 120) S = 120;
	int cx = x0, cy = y0 + 46;
	if (s->alb >= 0 && g_playing >= 0) g_covers->draw (s->alb, cx, cy, S, 10, mid);
	int ty = cy + S + 16;
	text (cx, ty, s->title, 0xFFFFFF, F_H1, 2, avail);
	char line[300]; snprintf (line, sizeof line, "%s  \xC2\xB7  %s", g_playing >= 0 ? L->artist_of (*s) : s->artist, s->album);
	text (cx, ty + 38, line, 0xD8E0E8, F_MID, 0, avail);
	char fmtl[120];
	if (g_player.rate) snprintf (fmtl, sizeof fmtl, TR ("%s  \xC2\xB7  %d.%d kHz  \xC2\xB7  %d bit  \xC2\xB7  %d kbit/s"), g_player.fmt, g_player.rate / 1000, g_player.rate % 1000 / 100,
					  g_player.bits ? g_player.bits : 16, g_player.kbps);
	else fmtl[0] = 0;
	if (g_player.rate && !g_player.kbps) { char *e = strrchr (fmtl, 0xC2); if (e && e > fmtl + 2) e[-2] = 0; }	// (no rate known: an FM Song)
	text (cx, ty + 62, fmtl, 0xA8B4C0, F_SMALL);
	if (!qw) return y + g_ch;
	// up next
	int qx = g_cw - 24 - qw, qy = y0 + 46, qh = g_ch - qy - 24;
	rbox (qx, qy, qw, qh, 12, mix (mid, 0xFFFFFF, 26));
	unsigned qbg = mix (mid, 0xFFFFFF, 26);
	text (qx + 18, qy + 14, TR ("Up next"), 0xFFFFFF, F_BIG, 2);
	int rowsMax = (qh - 60) / 56;
	int shown = 0;
	for (int i = g_qpos + 1; i < g_queue.n && shown < rowsMax; i++, shown++)
	{
		const Song *q = song (g_queue[i]); if (!q) continue;
		int ry = qy + 50 + shown * 56;
		bool h = g_hits.n == g_hot;
		if (h) rbox (qx + 8, ry - 4, qw - 16, 52, 8, mix (qbg, 0xFFFFFF, 40));
		if (q->alb >= 0 && g_queue[i] >= 0) g_covers->draw (q->alb, qx + 18, ry, 44, 4, h ? mix (qbg, 0xFFFFFF, 40) : qbg);
		text (qx + 74, ry + 4, q->title, 0xFFFFFF, F_UI, 2, qw - 150);
		text (qx + 74, ry + 24, q->artist[0] ? q->artist : q->albumArtist, 0xC0CCD8, F_SMALL, 0, qw - 150);
		char t[16]; fmt_time (t, sizeof t, q->durMs); text_r (qx + qw - 18, ry + 2, 40, t, 0xC0CCD8, F_SMALL);
		g_hits.add (qx + 8, ry - 4, qw - 16, 52, H_QUEUE, i);
	}
	if (!shown) text (qx + 18, qy + 56, g_repeat == 1 ? TR ("The queue starts again.") : TR ("Nothing after this song."), 0xB0BCC8);
	else if (g_queue.n - g_qpos - 1 > shown) { char t[48]; snprintf (t, sizeof t, TR ("%d more"), g_queue.n - g_qpos - 1 - shown); text (qx + 18, qy + qh - 26, t, 0xB0BCC8, F_SMALL); }
	return y + g_ch;
}

static void content_paint (long me, long width, long height)
{
	begin_paint (me);
	g_cw = (int) width; g_ch = (int) height; g_anim = false;
	ui_fill (0, 0, width, height, C_FIELD);
	g_hits.clear ();
	if (!L) return;
	if (!g_rowsValid) { page_songs (page (), g_rows); sort_songs (g_rows, page ().sortCol, page ().sortDesc); g_rowsValid = true; }
	// the pages draw in the view's coordinates: their top at 22 - the page's scroll
	int sy_ = sy (), x = 28, w = (int) width - 56 - SB_W, y0 = 22 - sy_, yEnd = y0;
	switch (page ().kind)
	{
	case P_ALBUMS: case P_ARTISTS: case P_SONGS: case P_GENRES:
		if (!L->n)
		{
			const char *T[] = { TR ("Albums"), TR ("Artists"), TR ("Songs"), TR ("Genres") };
			int k = page ().kind == P_ALBUMS ? 0 : page ().kind == P_ARTISTS ? 1 : page ().kind == P_SONGS ? 2 : 3;
			section_head (x, y0, w, T[k], "0", 0, 0);
			yEnd = empty_library (x, y0, w);
			break;
		}
		if (page ().kind == P_ALBUMS) yEnd = draw_albums (x, y0, w);
		else if (page ().kind == P_ARTISTS) yEnd = draw_artists (x, y0, w);
		else if (page ().kind == P_GENRES) yEnd = draw_groups (x, y0, w, L->ge, L->nge, TR ("Genres"), H_GENRE, false);
		else { char sub[32]; snprintf (sub, sizeof sub, TR ("%d songs"), g_rows.n); section_head (x, y0, w, TR ("Songs"), sub, 0, 0); yEnd = song_table (x, y0 + 54, w, g_rows, true, false); }
		break;
	case P_HOME: yEnd = draw_home (x, y0, w); break;
	case P_ALBUM: yEnd = draw_album (x, y0, w); break;
	case P_ARTIST: yEnd = draw_artist (x, y0, w); break;
	case P_GENRE: if (page ().arg >= 0 && page ().arg < L->nge) yEnd = draw_list_page (x, y0, w, TR ("GENRE"), L->ge[page ().arg].name, L->ge[page ().arg].na ? L->ge[page ().arg].albums[0] : -1); break;
	case P_FOLDERS: yEnd = draw_groups (x, y0, w, L->fo, L->nfo, TR ("Folders"), H_FOLDER, true); break;
	case P_FOLDER: if (page ().arg >= 0 && page ().arg < L->nfo) yEnd = draw_list_page (x, y0, w, TR ("FOLDER"), L->fo[page ().arg].name, L->fo[page ().arg].na ? L->fo[page ().arg].albums[0] : -1); break;
	case P_PLAYLIST:
	{
		const char *nm = page ().arg == PL_FAV ? TR ("Favourites") : page ().arg == PL_RECENT ? TR ("Recently added") : page ().arg < g_npl ? g_pl[page ().arg].name : "";
		int cov = g_rows.n ? L->s[g_rows.v[0]].alb : -1;
		yEnd = draw_list_page (x, y0, w, TR ("PLAYLIST"), nm, cov);
		break;
	}
	case P_NOW: draw_now (x, y0, w); g_contentH = (int) height; return;
	case P_SEARCH: yEnd = draw_search (x, y0, w); break;
	case P_WELCOME: yEnd = draw_welcome (x, y0, w); break;
	case P_VIDEOS: yEnd = draw_videos (x, y0, w); break;
	case P_WATCH: yEnd = y0; break;
	}
	g_contentH = yEnd + sy_ + 30;
}
static long content_mouse (long me, long kind, long mx, long my, long info)
{
	const Hit *ht = kind == 3 ? 0 : g_hits.at ((int) mx, (int) my);
	int nh = ht ? (int) (ht - g_hits.h) : -1;
	if (nh != g_hot) { g_hot = nh; ui_view_dirty (me); }
	static int btn;
	if (kind == 1) btn = (int) (info >> 8 & 255);
	if (kind == 1 && (btn & 2) && ht) click_on (FROM_CONTENT, ht, (int) mx, (int) my, (int) (info >> 16 & 255), 1, true);
	if (kind == 1 && (btn & 1) && ht && ht->kind == H_ROW) click_on (FROM_CONTENT, ht, (int) mx, (int) my, (int) (info >> 16 & 255), (int) (info & 255), false);
	if (kind == 2 && (btn & 1) && ht && ht->kind != H_ROW) click_on (FROM_CONTENT, ht, (int) mx, (int) my, (int) (info >> 16 & 255), 1, false);
	return 0;
}
static void scroll_to (int s)
{
	int maxS = g_contentH - g_ch; if (maxS < 0) maxS = 0;
	sy () = s < 0 ? 0 : s > maxS ? maxS : s;
	ui_view_dirty (g_vContent);
}
static long content_wheel (long, long n) { scroll_to (sy () - (int) n * 60); return 0; }

// the scroll bar follows what was drawn (after the view's turn: it is its child)
static void scroll_sync ()
{
	int max = g_contentH > g_ch ? g_contentH : 0;
	if (max != g_barMax || g_ch != g_barPage)
	{
		g_barMax = max; g_barPage = g_ch;
		ui_w_set_range (g_scroll, 0, max > 0 ? max : 1, g_ch, 60);
		ui_w_show (g_scroll, max > 0 && !g_miniMode && page ().kind != P_WATCH);
	}
	if (sy () != g_barPos) { g_barPos = sy (); ui_w_set_value (g_scroll, g_barPos); }
}

// ---- dialogs and menus -------------------------------------------------------------------------------------------
// a menu at a point of the window: its labels (0x01 first: greyed), its ids
struct Popup
{
	char b[1024]; int n, cnt, id[32];
	Popup () : n (0), cnt (0) {}
	void add (const char *label, int ident, bool on = true)
	{
		if (cnt >= 32 || n + (int) strlen (label) + 3 >= (int) sizeof b) return;
		if (!on) b[n++] = 1;
		n += snprintf (b + n, sizeof b - n, "%s", label) + 1;
		id[cnt++] = ident;
	}
	void separator () { add ("-", -1); }
	int run (int x, int y, int w = 220)
	{
		b[n] = 0;
		long r = ui_popup_local (x, y, (long) b, w);
		return r >= 0 && r < cnt ? id[r] : -1;
	}
};
static bool ask_text (const char *title, const char *msg, const char *init, char *out, int cap)
{
	char b[96]; scopy (b, init, sizeof b);
	if (!ui_ask ((long) title, (long) msg, (long) b, sizeof b)) return false;
	scopy (out, b, cap); tidy (out);
	return out[0] != 0;
}
// a thing's facts, read only: pairs of lines
struct Facts
{
	char l[12][2][160]; int n;
	Facts () : n (0) {}
	void add (const char *k, const char *v) { if (!v || !v[0] || n >= 12) return; scopy (l[n][0], k, 160); scopy (l[n][1], v, 160); n++; }
	void run (const char *title)
	{
		int W = 520, H = 70 + n * 24;
		long d = ui_dialog_begin (W, H, (long) title);
		if (!d) return;
		for (int i = 0; i < n; i++)
		{
			ui_label (16, 12 + i * 24, 110, 22, (long) l[i][0], ULB_DIM);
			ui_label (130, 12 + i * 24, W - 146, 22, (long) l[i][1], ULB_BOLD);
		}
		ui_dialog_button (d, 0, (long) TR ("Close"), 1, 0, W - 16);
		ui_dialog_cancel (d, 0);
		ui_dialog_run (d, 0, 0);
		ui_dialog_end (d);
	}
};
static void song_props (const Song &s)
{
	Facts f; char t[64];
	f.add (TR ("Title"), s.title); f.add (TR ("Artist"), s.artist); f.add (TR ("Album artist"), s.albumArtist); f.add (TR ("Album"), s.album); f.add (TR ("Genre"), s.genre);
	if (s.year) { snprintf (t, sizeof t, "%d", s.year); f.add (TR ("Year"), t); }
	if (s.track) { snprintf (t, sizeof t, s.disc ? TR ("%d (disc %d)") : "%d", s.track, s.disc); f.add (TR ("Track"), t); }
	fmt_time (t, sizeof t, s.durMs); f.add (TR ("Length"), t);
	snprintf (t, sizeof t, TR ("%s  \xC2\xB7  %.1f MB"), s.fmt >= 0 ? FMT_NAME[s.fmt] : "?", s.size / 1048576.0); f.add (TR ("File"), t);
	f.add (TR ("Where"), s.path);
	snprintf (t, sizeof t, "%d", s.plays); f.add (TR ("Played"), t);
	f.run (TR ("Properties"));
}
static void video_props (const Video &x)
{
	Facts f; char t[160], k[64];
	f.add (TR ("Title"), x.title);
	video_kind_line (x, k, sizeof k); f.add (TR ("Kind"), k);
	fmt_time (t, sizeof t, x.durMs); f.add (TR ("Length"), t);
	if (x.w) { snprintf (t, sizeof t, "%d \xC3\x97 %d", x.w, x.h); f.add (TR ("Picture"), t); }
	snprintf (t, sizeof t, "%s%s", codec_label (x.vcodec), x.playable ? "" : TR ("  (not decoded yet)")); f.add (TR ("Video"), t);
	if (x.acodec[0]) f.add (TR ("Sound"), codec_label (x.acodec));
	snprintf (t, sizeof t, TR ("%s  \xC2\xB7  %.1f MB"), ext_of (x.path), x.size / 1048576.0); f.add (TR ("File"), t);
	f.add (TR ("Where"), x.path);
	if (x.posMs > 0) { char a[24]; fmt_time (a, sizeof a, x.posMs); snprintf (t, sizeof t, TR ("left at %s"), a); f.add (TR ("Watched"), t); }
	else if (x.watched) f.add (TR ("Watched"), TR ("to the end"));
	f.run (TR ("Properties"));
}
static void new_playlist_dialog (const IntList *ids)
{
	char name[96];
	if (!ask_text (TR ("New playlist"), TR ("Its name:"), "", name, sizeof name)) return;
	int k = playlist_new (name);
	if (ids && ids->n) playlist_add (k, *ids);
	refresh_all ();
}

// ---- navigation --------------------------------------------------------------------------------------------
static bool video_start (int vi, bool fromStart);
// the page changed: leaving a video closes it (where it was left kept); coming back to one opens it again
static void page_changed (int oldKind)
{
	if (oldKind == P_WATCH && page ().kind != P_WATCH) video_close ();
	if (page ().kind == P_WATCH && !g_vp.active ())
	{
		int vi = page ().arg;
		if (vi < 0 || vi >= (VL ? VL->n : 0) || !VL->v[vi].playable || !video_start (vi, false)) { page ().kind = P_HOME; page ().arg = 0; }
	}
	g_hot = -1;
}
static void navigate (int kind, int arg)
{
	Page &cur = page ();
	if (cur.kind == kind && cur.arg == arg && kind != P_SEARCH) return;
	int old = cur.kind;
	if (g_hpos < 63) g_hpos++; else memmove (g_hist, g_hist + 1, sizeof (Page) * 63);
	g_hist[g_hpos] = Page { kind, arg, 0, kind == P_SONGS ? 2 : 0, false };
	g_nhist = g_hpos + 1;
	g_rowsValid = false; reset_selection ();
	page_changed (old);
	refresh_all ();
}
static void go_back () { if (g_hpos > 0) { int old = page ().kind; g_hpos--; g_rowsValid = false; reset_selection (); page_changed (old); refresh_all (); } }
static void go_fwd () { if (g_hpos < g_nhist - 1) { int old = page ().kind; g_hpos++; g_rowsValid = false; reset_selection (); page_changed (old); refresh_all (); } }
static void go_back_page ()
{	// (out of a video: back, else home)
	if (g_hpos > 0) go_back ();
	else { int old = page ().kind; g_hist[0] = Page { P_HOME, 0, 0, 0, false }; page_changed (old); refresh_all (); }
}

// ---- the content's clicks ---------------------------------------------------------------------------------------
static void playlist_menu (int mx, int my, const IntList &ids)
{
	Popup p;
	p.add (TR ("New playlist..."), 1000); if (g_npl) p.separator ();
	for (int i = 0; i < g_npl && i < 12; i++) p.add (g_pl[i].name, i);
	int r = p.run (mx + 20, my + 20);
	if (r == 1000) new_playlist_dialog (&ids); else if (r >= 0) playlist_add (r, ids);
}
static void context_menu (int mx, int my, IntList &ids, bool inPlaylist)
{
	if (!ids.n) return;
	const Song &f = L->s[ids.v[0]];
	Popup m;
	m.add (TR ("Play"), 1);
	m.add (TR ("Play next"), 2);
	m.add (TR ("Add to the queue"), 3);
	m.separator ();
	m.add (TR ("Add to a playlist..."), 4);
	m.add (f.fav ? TR ("Remove from Favourites") : TR ("Add to Favourites"), 5);
	if (inPlaylist) m.add (TR ("Remove from this playlist"), 10);
	m.separator ();
	m.add (TR ("Go to the album"), 6, ids.n == 1);
	m.add (TR ("Go to the artist"), 7, ids.n == 1);
	m.separator ();
	m.add (TR ("Properties..."), 8, ids.n == 1);
	switch (m.run (mx, my))
	{
	case 1: play_list (ids, 0, false); break;
	case 2: play_next_ids (ids, true); break;
	case 3: play_next_ids (ids, false); break;
	case 4: playlist_menu (mx, my, ids); break;
	case 5: { bool on = !f.fav; for (int i = 0; i < ids.n; i++) L->s[ids.v[i]].fav = on; L->save_stats (); break; }
	case 6: navigate (P_ALBUM, f.alb); break;
	case 7: navigate (P_ARTIST, f.art); break;
	case 8: song_props (f); break;
	case 10:
	{
		Plist &p = g_pl[page ().arg];
		for (int i = 0; i < ids.n; i++)
			for (int k = 0; k < p.n; k++) if (!strcmp (p.paths[k], L->s[ids.v[i]].path)) { free (p.paths[k]); memmove (p.paths + k, p.paths + k + 1, sizeof (char *) * (p.n - k - 1)); p.n--; break; }
		playlist_save (p);
		g_rowsValid = false;
		break;
	}
	}
	refresh_all ();
}
static void more_menu (int mx, int my)
{
	IntList &rows = g_rows;
	bool userPl = page ().kind == P_PLAYLIST && page ().arg >= 0;
	Popup m;
	m.add (TR ("Play next"), 1, rows.n > 0);
	m.add (TR ("Add to the queue"), 2, rows.n > 0);
	m.add (TR ("Add to a playlist..."), 3, rows.n > 0);
	m.add (TR ("Add to Favourites"), 4, rows.n > 0);
	if (userPl) { m.separator (); m.add (TR ("Rename the playlist..."), 6); m.add (TR ("Delete the playlist"), 7); }
	switch (m.run (mx, my))
	{
	case 1: play_next_ids (rows, true); break;
	case 2: play_next_ids (rows, false); break;
	case 3: playlist_menu (mx, my, rows); break;
	case 4: for (int i = 0; i < rows.n; i++) L->s[rows.v[i]].fav = true; L->save_stats (); break;
	case 6:
	{
		Plist &p = g_pl[page ().arg]; char nm[96];
		if (ask_text (TR ("Rename the playlist"), TR ("Its new name:"), p.name, nm, sizeof nm))
		{
			char np[300]; snprintf (np, sizeof np, "%s/%s.m3u", PLAYLIST_DIR, nm);
			if (rename (p.path, np) == 0) { scopy (p.path, np, sizeof p.path); scopy (p.name, nm, sizeof p.name); }
		}
		break;
	}
	case 7:
		if (ui_message ((long) TR ("Delete the playlist"), (long) TR ("Delete this playlist? (Its songs stay in the library.)"), UM_YESNO) == 1)
		{ unlink (g_pl[page ().arg].path); playlists_load (); go_back (); }
		break;
	}
	refresh_all ();
}
static void add_folder ()
{
	char d[200];
	if (g_nfolders < 8 && ui_folder_choose ((long) "SD0:/home", (long) d, sizeof d))
	{
		int l = (int) strlen (d); while (l > 5 && d[l - 1] == '/') d[--l] = 0;
		bool have = false; for (int i = 0; i < g_nfolders; i++) if (!strcmp (g_folders[i], d)) have = true;
		if (!have) { scopy (g_folders[g_nfolders++], d, 200); save_settings (); start_scan (); }
	}
	ui_view_dirty (g_vContent);
}
static void video_menu (int mx, int my, int vi);
static void content_click (const Hit &ht, int mx, int my, int mods, int clicks, bool right)
{
	int ax = side_w (), ay = TOP_H;					// (the view's place in the window)
	if (right)
	{	// a right click: the row's menu (the selection, if it is in it)
		if (ht.kind == H_VIDEO) video_menu (ax + mx, ay + my, ht.a);
		if (ht.kind == H_ROW)
		{
			ensure_sel ();
			if (!g_sel[ht.a]) { reset_selection (); g_sel[ht.a] = 1; g_anchorRow = ht.b; }
			IntList ids; selected_ids (ids);
			context_menu (ax + mx, ay + my, ids, page ().kind == P_PLAYLIST && page ().arg >= 0);
		}
		return;
	}
	switch (ht.kind)
	{
	case H_ALBUM: navigate (P_ALBUM, ht.a); break;
	case H_ALBUM_PLAY: { IntList l; for (int i = 0; i < L->al[ht.a].n; i++) l.push (L->al[ht.a].songs[i]); play_list (l, 0, false); break; }
	case H_ARTIST: if (ht.a >= 0) navigate (P_ARTIST, ht.a); break;
	case H_GENRE: navigate (P_GENRE, ht.a); break;
	case H_FOLDER: navigate (P_FOLDER, ht.a); break;
	case H_HEAD:
		if (page ().sortCol == ht.a) page ().sortDesc = !page ().sortDesc; else { page ().sortCol = ht.a; page ().sortDesc = false; }
		g_rowsValid = false; ui_view_dirty (g_vContent); break;
	case H_ROW:
	{
		ensure_sel ();
		if (clicks >= 2 && !(mods & (UI_MOD_CTRL | UI_MOD_SHIFT))) { play_list (g_rows, ht.b, false); break; }
		if (mods & UI_MOD_CTRL) { g_sel[ht.a] = !g_sel[ht.a]; g_anchorRow = ht.b; }
		else if ((mods & UI_MOD_SHIFT) && g_anchorRow >= 0)
		{
			int anc = g_anchorRow; reset_selection (); g_anchorRow = anc;		// (reset_selection forgets it)
			int a = anc < ht.b ? anc : ht.b, b = anc < ht.b ? ht.b : anc;
			for (int i = a; i <= b && i < g_rows.n; i++) g_sel[g_rows.v[i]] = 1;
		}
		else { reset_selection (); g_sel[ht.a] = 1; g_anchorRow = ht.b; }
		ui_view_dirty (g_vContent);
		break;
	}
	case H_PLAY: play_list (g_rows, 0, false); break;
	case H_SHUFFLE: play_list (g_rows, -1, true); break;
	case H_MORE: more_menu (ax + ht.x, ay + ht.y + ht.h + 2); break;
	case H_SORT:
	{
		Popup m;
		m.add (TR ("Name"), 0); m.add (TR ("Artist"), 1); m.add (TR ("Year"), 2); m.add (TR ("Recently added"), 3);
		int r = m.run (ax + ht.x, ay + ht.y + ht.h + 2); if (r >= 0) { g_albumSort = r; save_settings (); }
		ui_view_dirty (g_vContent); break;
	}
	case H_QUEUE: start_song (ht.a); break;
	case H_RESUME:
		if (g_hasSong && ht.a == g_playing) toggle_play ();
		else { const Song &s = L->s[ht.a]; IntList l; int st = 0; for (int i = 0; s.alb >= 0 && i < L->al[s.alb].n; i++) { if (L->al[s.alb].songs[i] == ht.a) st = i; l.push (L->al[s.alb].songs[i]); } play_list (l, st, false); }
		break;
	case H_SEEALL: if (ht.a == 0) navigate (P_SONGS); else if (ht.a == 2) navigate (P_VIDEOS, -1); else navigate (P_PLAYLIST, PL_RECENT); break;
	case H_VIDEO: case H_VRESUME: play_video (ht.a, false); break;
	case H_OPENVID: m_open_video (); break;
	case H_NOWCLOSE: go_back (); break;
	case H_ADDFOLDER: add_folder (); break;
	case H_DELFOLDER:
		for (int i = ht.a; i < g_nfolders - 1; i++) memcpy (g_folders[i], g_folders[i + 1], 200);
		g_nfolders--; save_settings (); start_scan (); ui_view_dirty (g_vContent); break;
	case H_DONE: navigate (P_HOME); break;
	}
}

// ---- the bar of what plays, at the bottom ---------------------------------------------------------------------
enum { N_COVER = 1, N_FAV, N_SHUFFLE, N_PREV, N_PLAY, N_NEXT, N_REPEAT, N_SEEK, N_QUEUE, N_MINI, N_MUTE, N_VOL };
static void enter_mini (bool on);
static HitList g_nowHits; static int g_nowHot = -1, g_nowDrag; static long long g_nowDragMs; static int g_nowW;
// Its layout by its width: 1000 px and more the desktop's; less, a shorter line; under 760 the line across the bar, the
// cover alone at the left, the queue and the sound's button at the right.
static bool now_narrow () { return g_nowW < 760; }
static int seek_w () { return now_narrow () ? g_nowW - 88 : g_nowW >= 1000 ? 400 : 300; }
static int seek_x () { return now_narrow () ? 44 : g_nowW / 2 - seek_w () / 2; }
static void now_paint (long me, long width, long height)
{
	begin_paint (me);
	g_nowW = (int) width;
	unsigned f = C_BG, dim = col_dim ();
	ui_fill (0, 0, width, height, f);
	hline (0, 0, (int) width, C_LINE);
	HitList &hits = g_nowHits;
	hits.clear ();
	const Song *s = g_hasSong ? song (g_playing) : 0;
	if (s && now_narrow ())
	{
		if (s->alb >= 0 && g_playing >= 0) g_covers->draw (s->alb, 10, 5, 38, 5, f);
		else { rbox (10, 5, 38, 38, 5, C_ACCENT); icon (I_NOTE, 18, 13, 22, ink_on (C_ACCENT)); }
		hits.add (10, 5, 38, 38, N_COVER);
	}
	else if (s)
	{
		if (s->alb >= 0 && g_playing >= 0) g_covers->draw (s->alb, 12, 12, 56, 5, f);
		else { rbox (12, 12, 56, 56, 5, C_ACCENT); icon (I_NOTE, 24, 24, 32, ink_on (C_ACCENT)); }
		hits.add (12, 12, 56, 56, N_COVER);
		int maxw = seek_x () - 80 - 40;
		if (maxw < 120) maxw = 120;
		text (80, 18, s->title, C_TEXT, F_UI, 2, maxw);
		char line[300]; snprintf (line, sizeof line, "%s  \xC2\xB7  %s", s->artist[0] ? s->artist : L ? L->artist_of (*s) : "", s->album);
		text (80, 40, line, dim, F_SMALL, 0, maxw + 30);
		int hx = 80 + (tw (s->title, F_UI, 2) < maxw ? tw (s->title, F_UI, 2) : maxw) + 10;
		if (g_playing >= 0) { icon (s->fav ? I_HEART : I_HEART_O, hx, 18, 16, s->fav || hits.n == g_nowHot ? jet (J_DANGER) : dim); hits.add (hx - 3, 15, 22, 22, N_FAV); }
	}
	else if (!now_narrow ()) text_v (16, 0, (int) height, TR ("Choose a song, an album, then \xE2\x96\xB6"), dim, F_UI, 0, (int) width / 2 - 140 - 16);
	// the transport
	int mx = (int) width / 2;
	auto btn = [&] (int x, int y, int sz, int ic, unsigned c, int kind) { bool h = hits.n == g_nowHot; icon (ic, x, y, sz, h ? C_ACCENT : c); hits.add (x - 4, y - 4, sz + 8, sz + 8, kind); };
	btn (mx - 122, 15, 18, I_SHUFFLE, g_shuffle ? C_ACCENT : dim, N_SHUFFLE);
	btn (mx - 72, 12, 22, I_PREV, C_TEXT, N_PREV);
	{ bool h = hits.n == g_nowHot; disc (mx, 24, 19, h ? tone (C_ACCENT, 150) : C_ACCENT);
	  icon (g_player.state == PS_PLAYING || g_player.state == PS_LOADING ? I_PAUSE : I_PLAY, mx - 10, 14, 20, ink_on (C_ACCENT)); hits.add (mx - 19, 5, 38, 38, N_PLAY); }
	btn (mx + 50, 12, 22, I_NEXT, C_TEXT, N_NEXT);
	btn (mx + 104, 15, 18, g_repeat == 2 ? I_REPEAT1 : I_REPEAT, g_repeat ? C_ACCENT : dim, N_REPEAT);
	// the position
	long long pos = g_nowDrag == N_SEEK ? g_nowDragMs : g_player.posMs, len = g_player.lenMs > 0 ? g_player.lenMs : s ? s->durMs : 0;
	int bx = seek_x (), bw = seek_w ();
	char t[16];
	fmt_time (t, sizeof t, pos); text_r (bx - 10, 50, 18, t, dim, F_SMALL);
	fmt_time (t, sizeof t, len); text (bx + bw + 10, 52, t, dim, F_SMALL);
	int fill = len > 0 ? (int) (pos * bw / len) : 0; if (fill > bw) fill = bw;
	rbox (bx, 57, bw, 5, 2, C_LINE);
	if (fill > 0) rbox (bx, 57, fill, 5, 2, C_ACCENT);
	if (s) { disc (bx + fill, 59, 7, C_ACCENT); disc (bx + fill, 59, 5, jet (J_ELEVE)); }
	hits.add (bx - 6, 48, bw + 12, 22, N_SEEK);
	// the right: the queue, the mini player, the volume
	int rx = (int) width - 16, vw = 90;
	if (now_narrow ())
	{
		btn (rx - 18, 15, 18, g_muted || !g_volume ? I_MUTE : I_VOLUME, C_TEXT, N_MUTE);
		btn (rx - 50, 15, 18, I_QUEUE, page ().kind == P_NOW ? C_ACCENT : C_TEXT, N_QUEUE);
		return;
	}
	int vf = g_muted ? 0 : g_volume * vw / 100;
	unsigned knob = mix (f, C_TEXT, 170);
	rbox (rx - vw, 38, vw, 4, 2, C_LINE);
	if (vf) rbox (rx - vw, 38, vf, 4, 2, knob);
	disc (rx - vw + vf, 40, 6, knob); disc (rx - vw + vf, 40, 4, jet (J_ELEVE));
	hits.add (rx - vw - 6, 30, vw + 12, 20, N_VOL);
	btn (rx - vw - 30, 31, 18, g_muted || !g_volume ? I_MUTE : I_VOLUME, C_TEXT, N_MUTE);
	btn (rx - vw - 62, 31, 18, I_MINI, C_TEXT, N_MINI);
	btn (rx - vw - 94, 31, 18, I_QUEUE, page ().kind == P_NOW ? C_ACCENT : C_TEXT, N_QUEUE);
}
static long now_mouse (long me, long kind, long x, long y, long info)
{
	int mx = (int) x, my = (int) y;
	if (g_nowDrag)
	{
		if (g_nowDrag == N_SEEK)
		{
			long long len = g_player.lenMs; int bx = seek_x (), bw = seek_w ();
			int p = mx - bx; if (p < 0) p = 0; if (p > bw) p = bw;
			g_nowDragMs = len * p / bw;
			if (kind == 2) { g_player.seek (g_nowDragMs); g_nowDrag = 0; }
		}
		else
		{
			int rx = g_nowW - 16, vw = 90, p = mx - (rx - vw); if (p < 0) p = 0; if (p > vw) p = vw;
			g_volume = p * 100 / vw; g_muted = false; apply_volume ();
			if (kind == 2) { g_nowDrag = 0; save_settings (); }
		}
		ui_view_dirty (me);
		return 0;
	}
	const Hit *ht = kind == 3 ? 0 : g_nowHits.at (mx, my);
	int nh = ht ? (int) (ht - g_nowHits.h) : -1;
	if (nh != g_nowHot) { g_nowHot = nh; ui_view_dirty (me); }
	if (kind == 1 && ht)
	{
		if (ht->kind == N_SEEK && g_hasSong) { g_nowDrag = N_SEEK; return now_mouse (me, 0, x, y, info); }
		if (ht->kind == N_VOL) { g_nowDrag = N_VOL; return now_mouse (me, 0, x, y, info); }
	}
	if (kind == 2 && ht) click_on (FROM_NOW, ht, mx, my, 0, 1, false);
	return 0;
}
static long now_wheel (long me, long n)
{
	g_volume += (int) n * 5; if (g_volume < 0) g_volume = 0; if (g_volume > 100) g_volume = 100;
	apply_volume (); ui_view_dirty (me);
	return 0;
}
static void now_click (const Hit &ht)
{
	switch (ht.kind)
	{
	case N_COVER: case N_QUEUE: if (page ().kind == P_NOW) go_back (); else navigate (P_NOW); break;
	case N_FAV: if (PLAYING_LIB) { L->s[g_playing].fav = !L->s[g_playing].fav; L->save_stats (); refresh_all (); } break;
	case N_SHUFFLE: set_shuffle (!g_shuffle); break;
	case N_PREV: play_prev (); break;
	case N_PLAY: toggle_play (); break;
	case N_NEXT: play_next (true); break;
	case N_REPEAT: g_repeat = (g_repeat + 1) % 3; save_settings (); break;
	case N_MUTE: g_muted = !g_muted; apply_volume (); break;
	case N_MINI: enter_mini (true); break;
	}
	ui_view_dirty (g_vNow);
}

// ---- the mini player: the window reduced to a card ---------------------------------------------------------------
enum { M_PREV = 1, M_PLAY, M_NEXT, M_BACK, M_SEEK };
static HitList g_miniHits; static int g_miniHot = -1;
static void mini_paint (long me, long width, long height)
{
	begin_paint (me);
	const Song *s = g_hasSong ? song (g_playing) : 0;
	unsigned tn = s && s->alb >= 0 && g_playing >= 0 ? g_covers->tone (s->alb) : 0x2A3448;
	unsigned top = mix (tn, 0x101018, 140), bot = mix (tn, 0x101018, 200), mid = mix (top, bot, 128);
	ui_rbox (0, 0, width, height, 0, top, bot);
	HitList &hits = g_miniHits;
	hits.clear ();
	int H = (int) height, W = (int) width;
	if (s && s->alb >= 0 && g_playing >= 0) g_covers->draw (s->alb, 10, 10, H - 20, 6, mid);
	else { rbox (10, 10, H - 20, H - 20, 6, C_ACCENT); icon (I_NOTE, 24, 24, H - 48, ink_on (C_ACCENT)); }
	int x = H + 4, w = W - x - 12;
	text (x, 10, s ? s->title : TR ("Nothing playing"), 0xFFFFFF, F_UI, 2, w - 30);
	if (s) text (x, 29, s->artist[0] ? s->artist : s->albumArtist, 0xD0D4DC, F_SMALL, 0, w - 30);
	long long len = g_player.lenMs > 0 ? g_player.lenMs : s ? s->durMs : 0, pos = g_player.posMs;
	int fill = len > 0 ? (int) (pos * w / len) : 0; if (fill > w) fill = w;
	ui_fill (x, 50, w, 3, mix (bot, 0xFFFFFF, 70)); ui_fill (x, 50, fill, 3, 0xFFFFFF);
	hits.add (x, 44, w, 14, M_SEEK);
	int my = 60, cx = x + 46;
	auto btn = [&] (int bx, int ic, int kind, int sz) { bool h = hits.n == g_miniHot; icon (ic, bx, my + (22 - sz) / 2, sz, h ? mix (0xFFFFFF, C_ACCENT, 150) : 0xFFFFFF); hits.add (bx - 4, my - 4, sz + 8, 30, kind); };
	btn (cx - 42, I_PREV, M_PREV, 18);
	{ bool h = hits.n == g_miniHot; disc (cx + 11, my + 11, 13, h ? 0xE8E8F0 : 0xFFFFFF);
	  icon (g_player.state == PS_PLAYING ? I_PAUSE : I_PLAY, cx + 3, my + 3, 16, 0x202030); hits.add (cx - 2, my - 2, 26, 26, M_PLAY); }
	btn (cx + 34, I_NEXT, M_NEXT, 18);
	char t[32], a[16], b[16]; fmt_time (a, sizeof a, pos); fmt_time (b, sizeof b, len); snprintf (t, sizeof t, "%s / %s", a, b);
	text_r (W - 12, my, 22, t, 0xD0D4DC, F_SMALL);
	{ bool h = hits.n == g_miniHot; disc (W - 19, 19, 10, mix (top, 0xFFFFFF, h ? 90 : 45)); icon (I_EXPAND, W - 26, 12, 14, 0xFFFFFF); hits.add (W - 30, 8, 22, 22, M_BACK); }
}
static long mini_mouse (long me, long kind, long x, long y, long info)
{
	const Hit *ht = kind == 3 ? 0 : g_miniHits.at ((int) x, (int) y);
	int nh = ht ? (int) (ht - g_miniHits.h) : -1;
	if (nh != g_miniHot) { g_miniHot = nh; ui_view_dirty (me); }
	if (kind == 2 && ht) click_on (FROM_MINI, ht, (int) x, (int) y, 0, 1, false);
	return 0;
}
static void mini_click (const Hit &ht, int mx)
{
	switch (ht.kind)
	{
	case M_PREV: play_prev (); break;
	case M_PLAY: toggle_play (); break;
	case M_NEXT: play_next (true); break;
	case M_BACK: enter_mini (false); break;
	case M_SEEK: if (g_player.lenMs > 0) g_player.seek (g_player.lenMs * (mx - ht.x) / (ht.w ? ht.w : 1)); break;
	}
	ui_view_dirty (g_vMini);
}

// ---- the videos: playing one ----------------------------------------------------------------------------------------
// where it was left (seen to the end: from the start next time, marked watched)
static void video_save_pos ()
{
	if (g_vidx < 0 || g_vidx >= VL->n || !g_vp.active ()) return;
	Video &x = VL->v[g_vidx];
	long long pos = g_vp.pos_ms (), len = g_vp.len_ms () > 0 ? g_vp.len_ms () : x.durMs;
	if (g_vp.ended || (len > 0 && pos >= len - (len > 600000 ? 60000 : len / 20))) { x.posMs = 0; if (g_vp.ended || pos > 0) x.watched = true; }
	else x.posMs = pos < 5000 ? 0 : (int) pos;
	if (len > 0 && !x.durMs) x.durMs = (int) len;
	VL->save ();
}
static void video_close ()
{
	if (!g_vp.active ()) { g_vidx = -1; g_thumbs->busy = false; return; }
	video_save_pos ();
	g_vp.close ();
	g_vp.free_surface (); g_surfaced = false;
	g_vidx = -1; g_thumbs->busy = false;
}
// the video vi opened (the music stopped: a video's sound takes its place) -> false: it cannot be
static bool video_start (int vi, bool fromStart)
{
	if (vi < 0 || vi >= VL->n) return false;
	if (g_player.state != PS_STOPPED) { g_player.stop (); g_hasSong = false; g_playing = -1; ui_view_dirty (g_vNow); }
	video_close ();
	Video &x = VL->v[vi];
	int r = g_vp.open (x.path, fromStart || (x.watched && !x.posMs) ? 0 : x.posMs, g_volume, g_muted);
	if (r != 0)
	{
		ui_message ((long) TR ("Media Player"), (long) (r == MK_E_UNSUP ? TR ("This file is not a video Media Player\ncan read.") : TR ("This video cannot be read.")), UM_OK);
		return false;
	}
	g_vidx = vi; g_vpEnded = false;
	x.lastPlayed = now_stamp ();
	VL->save ();
	g_vpSaveT = ticks ();
	g_thumbs->busy = true;
	return true;
}
static void play_video (int vi, bool fromStart)
{
	if (vi < 0 || vi >= VL->n) return;
	const Video &x = VL->v[vi];
	if (!x.playable)
	{
		char m[300], t[28]; scopy (t, x.title, sizeof t);
		snprintf (m, sizeof m, TR ("\xE2\x80\x9C%s\xE2\x80\x9D cannot be played:\nits picture (%s) or its sound (%s)\nis in a format Korin OS cannot decode."), t, codec_label (x.vcodec), x.acodec[0] ? codec_label (x.acodec) : TR ("none"));
		ui_message ((long) TR ("Media Player"), (long) m, UM_OK);
		return;
	}
	if (!video_start (vi, fromStart)) return;
	if (page ().kind == P_WATCH) { page ().arg = vi; refresh_all (); }
	else navigate (P_WATCH, vi);
}
static void video_toggle ()
{
	if (!g_vp.active ()) return;
	if (g_vp.ended) { g_vp.seek (0); g_vp.play (); g_vpEnded = false; }
	else if (g_vp.paused) g_vp.play ();
	else { g_vp.pause (); video_save_pos (); }
}
static void video_seek_by (long long ms) { if (g_vp.active ()) { long long t = g_vp.pos_ms () + ms; g_vp.seek (t < 0 ? 0 : t); g_vpEnded = false; } }
static void video_volume (int v) { g_volume = v < 0 ? 0 : v > 100 ? 100 : v; g_muted = false; apply_volume (); }
// the next episode of the series playing, -1 none
static int next_episode (int vi)
{
	if (vi < 0 || vi >= VL->n || !VL->v[vi].episode) return -1;
	IntList l; VL->list (l, VK_CLIP);
	int k = l.find (vi);
	if (k >= 0 && k + 1 < l.n) { const Video &a = VL->v[vi], &b = VL->v[l.v[k + 1]]; if (!strcmp (a.title, b.title) && b.episode) return l.v[k + 1]; }
	return -1;
}

// What the view shows: the picture is a surface of the window (Elegant puts it over the view: nothing is drawn on it), its
// controls in two bars, above and under it. At the end the surface goes: again, the next episode.
enum { W_BACK = 1, W_PLAY, W_B10, W_F10, W_SEEK, W_MUTE, W_VOL, W_FULL, W_AGAIN, W_NEXTEP };
static const int WATCH_TOP = 52, WATCH_BOT = 84;
static HitList g_wHits; static int g_wHot = -1, g_wDrag; static long long g_wDragMs; static int g_wW, g_wH;
static int w_seek_x () { return 24; }
static void watch_paint (long me, long width, long height)
{
	begin_paint (me);
	int W = (int) width, H = (int) height, cx = W / 2, cy = WATCH_TOP + (H - WATCH_TOP - WATCH_BOT) / 2;
	g_wW = W; g_wH = H;
	HitList &u = g_wHits;
	u.clear ();
	unsigned f = C_BG, dim = col_dim ();
	ui_fill (0, 0, W, H, 0);
	ui_fill (0, 0, W, WATCH_TOP, f); hline (0, WATCH_TOP - 1, W, C_LINE);
	ui_fill (0, H - WATCH_BOT, W, WATCH_BOT, f); hline (0, H - WATCH_BOT, W, C_LINE);
	const Video *x = g_vidx >= 0 && g_vidx < VL->n ? &VL->v[g_vidx] : 0;
	if (x && g_vp.ended)
	{	// (the surface is gone: the frame kept in the library, dimmed)
		int zh = H - WATCH_TOP - WATCH_BOT, tw_ = W, th = tw_ * 9 / 16; if (th > zh) { th = zh; tw_ = th * 16 / 9; }
		g_thumbs->draw (g_vidx, cx - tw_ / 2, cy - th / 2, tw_, th, 0, 0);
		veil (cx - tw_ / 2, cy - th / 2, tw_, th, 0, 150);
		int ne = next_episode (g_vidx);
		int bw = ne >= 0 ? 380 : 200, bx = cx - bw / 2 + 16;
		rbox (cx - bw / 2, cy - 34, bw, 68, 12, 0x1C1C22);
		bool h = u.n == g_wHot;
		rbox (bx, cy - 18, 168, 36, 8, h ? 0xFFFFFF : 0xE8E8EE, h ? 0xF0F0F4 : 0xD8D8E0);
		icon (I_REPEAT, bx + 14, cy - 9, 18, 0x202028); text_v (bx + 42, cy - 18, 36, TR ("Watch again"), 0x202028, F_UI, 2, 122);
		u.add (bx, cy - 18, 168, 36, W_AGAIN);
		if (ne >= 0)
		{
			bx += 180; h = u.n == g_wHot;
			unsigned fill = tone (C_ACCENT, h ? 150 : 128), ink = ink_on (fill);
			rbox (bx, cy - 18, 168, 36, 8, fill);
			char t[48]; snprintf (t, sizeof t, TR ("Next: S%d E%d"), VL->v[ne].season, VL->v[ne].episode);
			icon (I_NEXT, bx + 14, cy - 9, 18, ink); text_v (bx + 42, cy - 18, 36, t, ink, F_UI, 2, 122);
			u.add (bx, cy - 18, 168, 36, W_NEXTEP, ne);
		}
	}
	// the top: back, the title
	{
		bool h = u.n == g_wHot;
		disc (28, 26, 16, h ? tone (C_BG, 140) : C_LINE); disc (28, 26, 15, h ? tone (C_BG, 140) : f);
		icon (I_BACK, 19, 17, 18, C_TEXT);
		u.add (12, 10, 32, 32, W_BACK);
		if (x)
		{
			text (56, 8, x->title, C_TEXT, F_BIG, 2, W - 80);
			char k[64], line[200]; video_kind_line (*x, k, sizeof k);
			if (g_vp.in.width > 0) snprintf (line, sizeof line, "%s  \xC2\xB7  %d \xC3\x97 %d  \xC2\xB7  %s%s%s", k, (int) g_vp.in.width, (int) g_vp.in.height, codec_label (x->vcodec), x->acodec[0] ? " / " : "", x->acodec[0] ? codec_label (x->acodec) : "");
			else snprintf (line, sizeof line, "%s", k);
			text (56, 30, line, dim, F_SMALL, 0, W - 80);
		}
	}
	// the bottom: the position, the transport, the volume, full screen
	long long len = g_vp.len_ms () > 0 ? g_vp.len_ms () : x ? x->durMs : 0;
	long long pos = g_wDrag == W_SEEK ? g_wDragMs : g_vp.pos_ms ();
	int sx = w_seek_x (), sw = W - 48, sy_ = H - 62;
	{
		int fl = len > 0 ? (int) (pos * sw / len) : 0; if (fl > sw) fl = sw; if (fl < 0) fl = 0;
		bool h = u.n == g_wHot || g_wDrag == W_SEEK;
		int th = h ? 5 : 4;
		rbox (sx, sy_ - th / 2, sw, th, 2, C_LINE);
		if (fl > 0) rbox (sx, sy_ - th / 2, fl, th, 2, C_ACCENT);
		disc (sx + fl, sy_, h ? 8 : 6, C_ACCENT);
		u.add (sx - 8, sy_ - 12, sw + 16, 24, W_SEEK);
	}
	int by = H - 44;
	auto btn = [&] (int bx, int ic, int sz, int kind) { bool h = u.n == g_wHot; icon (ic, bx, by + (28 - sz) / 2, sz, h ? C_ACCENT : C_TEXT); u.add (bx - 6, by - 4, sz + 12, 36, kind); };
	btn (sx, g_vp.paused || g_vp.ended ? I_PLAY : I_PAUSE, 26, W_PLAY);
	btn (sx + 46, I_BACK10, 22, W_B10);
	btn (sx + 82, I_FWD10, 22, W_F10);
	char a[16], b[16], t[40]; fmt_time (a, sizeof a, pos); fmt_time (b, sizeof b, len); snprintf (t, sizeof t, "%s / %s", a, b);
	text_v (sx + 122, by, 28, t, C_TEXT, F_UI, 0);
	int rx = W - 24;
	btn (rx - 22, I_FULL, 22, W_FULL);
	int vw = 90, vx = rx - 22 - 22 - vw, vf = g_muted ? 0 : g_volume * vw / 100;
	rbox (vx, by + 12, vw, 4, 2, C_LINE);
	if (vf) rbox (vx, by + 12, vf, 4, 2, C_TEXT);
	disc (vx + vf, by + 14, 6, C_TEXT);
	u.add (vx - 6, by, vw + 12, 28, W_VOL);
	btn (vx - 34, g_muted || !g_volume ? I_MUTE : I_VOLUME, 20, W_MUTE);
}
// a drag on the position or the volume: its value from x
static void watch_drag (int mx, bool up)
{
	if (g_wDrag == W_SEEK)
	{
		long long len = g_vp.len_ms () > 0 ? g_vp.len_ms () : g_vidx >= 0 ? VL->v[g_vidx].durMs : 0;
		int sw = g_wW - 48, p = mx - w_seek_x (); if (p < 0) p = 0; if (p > sw) p = sw;
		g_wDragMs = len * p / sw;
		if (up) { g_vp.seek (g_wDragMs); g_vpEnded = false; g_wDrag = 0; }
	}
	else if (g_wDrag == W_VOL)
	{
		int vw = 90, vx = g_wW - 24 - 22 - 22 - vw, p = mx - vx; if (p < 0) p = 0; if (p > vw) p = vw;
		video_volume (p * 100 / vw);
		if (up) { g_wDrag = 0; save_settings (); }
	}
}
static long watch_mouse (long me, long kind, long x, long y, long info)
{
	if (g_wDrag) { watch_drag ((int) x, kind == 2); ui_view_dirty (me); return 0; }
	const Hit *ht = kind == 3 ? 0 : g_wHits.at ((int) x, (int) y);
	int nh = ht ? (int) (ht - g_wHits.h) : -1;
	if (nh != g_wHot) { g_wHot = nh; ui_view_dirty (me); }
	static int clicks;
	if (kind == 1)
	{
		clicks = (int) (info & 255);
		if (ht && (ht->kind == W_SEEK || ht->kind == W_VOL)) { g_wDrag = ht->kind; watch_drag ((int) x, false); ui_view_dirty (me); }
	}
	if (kind == 2)
	{
		static const Hit pic = { 0, 0, 0, 0, 100, 0, 0 };	// (on the picture: play / pause; twice: full screen)
		if (ht) click_on (FROM_WATCH, ht, (int) x, (int) y, 0, 1, false);
		else if (y > WATCH_TOP && y < g_wH - WATCH_BOT) click_on (FROM_WATCH, &pic, (int) x, (int) y, 0, clicks, false);
	}
	return 0;
}
static long watch_wheel (long me, long n) { video_volume (g_volume + (int) n * 5); ui_view_dirty (me); return 0; }
static void watch_place ()
{
	if (!g_vp.active () || g_vp.ended) { if (g_surfaced) { g_vp.free_surface (); g_surfaced = false; } return; }
	g_surfaced = g_vp.place (0, WATCH_TOP, g_winW, g_winH - WATCH_TOP - WATCH_BOT);
}
static void watch_click (const Hit &ht, int clicks)
{
	switch (ht.kind)
	{
	case 100: video_toggle (); if (clicks >= 2) { video_toggle (); video_full_screen (); } break;
	case W_BACK: go_back_page (); return;
	case W_PLAY: video_toggle (); break;
	case W_B10: video_seek_by (-10000); break;
	case W_F10: video_seek_by (10000); break;
	case W_MUTE: g_muted = !g_muted; apply_volume (); save_settings (); break;
	case W_FULL: video_full_screen (); break;
	case W_AGAIN: g_vp.seek (0); g_vp.play (); g_vpEnded = false; for (int i = 0; i < 100 && g_vp.ended; i++) sleep_ms (2); watch_place (); break;
	case W_NEXTEP: play_video (ht.a, true); return;
	}
	ui_view_dirty (g_vWatch);
}
// the keys of the view (the window's and full screen's): true if taken
enum { HID_ENTER = 0x28, HID_ESC = 0x29, HID_BKSP = 0x2A, HID_SPACE = 0x2C, HID_RIGHT = 0x4F, HID_LEFT = 0x50, HID_DOWN = 0x51, HID_UP = 0x52, HID_HOME = 0x4A, HID_END = 0x4D, HID_PGUP = 0x4B, HID_PGDN = 0x4E };
static bool watch_key (int hid, int ch)
{
	if (!g_vp.active ()) return false;
	switch (hid)
	{
	case HID_SPACE: video_toggle (); return true;
	case HID_LEFT: video_seek_by (-10000); return true;
	case HID_RIGHT: video_seek_by (10000); return true;
	case HID_UP: video_volume (g_volume + 5); return true;
	case HID_DOWN: video_volume (g_volume - 5); return true;
	case HID_HOME: g_vp.seek (0); return true;
	}
	switch (ch)
	{
	case 'k': case 'K': video_toggle (); return true;
	case 'j': video_seek_by (-10000); return true;
	case 'l': video_seek_by (10000); return true;
	case 'm': case 'M': g_muted = !g_muted; apply_volume (); return true;
	}
	if (ch >= '0' && ch <= '9' && g_vp.len_ms () > 0) { g_vp.seek (g_vp.len_ms () * (ch - '0') / 10); return true; }
	return false;
}
// ---- full screen: a frameless window over everything, the surface in it; the keys of the view; a click: play / pause;
// Esc, F, a double click: back ----------------------------------------------------------------------------------------
static void video_full_screen ()
{
	if (!g_vp.active () || g_vp.ended) return;
	long scr[4] = { 0, 0, 0, 0 };
	ui_screen ((long) scr);
	g_vp.free_surface (); g_surfaced = false;
	ui_dialog_at (0, 0);
	long dlg = ui_dialog_begin (scr[0], scr[1], (long) TR ("Media Player"));
	if (dlg)
	{
		long win = ui.win;
		ui_fill (0, 0, ui.w, ui.h, 0); ui_update_all ();
		g_vp.place (0, 0, (int) ui.w, (int) ui.h);
		for (;;)
		{
			long t = ui_event (6);
			g_vp.frame_shown ();
			if (g_vp.ended || !g_vp.active ()) break;
			if (t == WEV_CLOSE) break;
			if (t > 0 && ui_ev_field (&ui, EV_WIN) == win)
			{
				if (t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0)
				{
					int hid = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR);
					if (hid == HID_ESC || ch == 'f' || ch == 'F') break;
					watch_key (hid, ch);
				}
				if (t == WEV_MOUSE && ui_ev_field (&ui, EV_KIND) == 1)
				{
					static unsigned last;
					unsigned now = ticks ();
					video_toggle ();
					if (now - last < 45) { video_toggle (); break; }
					last = now;
				}
			}
			if (ticks () - g_vpSaveT > 1500) { g_vpSaveT = ticks (); if (!g_vp.paused) video_save_pos (); }
		}
		g_vp.free_surface ();
		ui_dialog_end (dlg);
	}
	refresh_all ();
}
static void video_menu (int mx, int my, int vi)
{
	if (vi < 0 || vi >= VL->n) return;
	Video &x = VL->v[vi];
	Popup m;
	m.add (x.posMs > 0 ? TR ("Resume") : TR ("Play"), 1, x.playable);
	m.add (TR ("Play from the Start"), 2, x.playable && x.posMs > 0);
	m.separator ();
	m.add (x.watched || x.posMs ? TR ("Mark as Not Watched") : TR ("Mark as Watched"), 3);
	m.separator ();
	m.add (TR ("Properties..."), 4);
	switch (m.run (mx, my))
	{
	case 1: play_video (vi, false); break;
	case 2: play_video (vi, true); break;
	case 3: if (x.watched || x.posMs) { x.watched = false; x.posMs = 0; } else { x.watched = true; x.posMs = 0; } VL->save (); break;
	case 4: video_props (x); break;
	}
	refresh_all ();
}

// ---- the layout ---------------------------------------------------------------------------------------------------
static void show_part (long w, bool on) { ui_w_show (w, on ? 1 : 0); }
static void layout_parts ()
{
	int W = g_winW = (int) ui.w, H = g_winH = (int) ui.h;
	bool watching = page ().kind == P_WATCH && !g_miniMode;
	bool lib = !watching && !g_miniMode;
	show_part (g_vTop, lib); show_part (g_side, lib); show_part (g_vFoot, lib); show_part (g_vContent, lib); show_part (g_vNow, lib); show_part (g_searchBox, lib);
	show_part (g_vMini, g_miniMode); show_part (g_vWatch, watching);
	if (!lib) ui_w_show (g_scroll, 0);
	if (g_miniMode) { ui_w_move (g_vMini, 0, 0, W, H); ui_view_dirty (g_vMini); return; }
	if (watching) { ui_w_move (g_vWatch, 0, 0, W, H); ui_view_dirty (g_vWatch); return; }
	if (g_surfaced) { g_vp.free_surface (); g_surfaced = false; }
	int sw = side_w (), footH = 64;
	ui_w_move (g_vTop, 0, 0, W, TOP_H);
	ui_w_move (g_side, 0, TOP_H, sw, H - TOP_H - NOW_H - footH);
	ui_w_move (g_vFoot, 0, H - NOW_H - footH, sw, footH);
	side_build ();
	bool grid = page ().kind == P_ALBUMS;
	ui_w_move (g_searchBox, W - search_w () - (grid ? 174 : 116), 12, search_w (), 28);
	ui_w_move (g_vContent, sw, TOP_H, W - sw, H - TOP_H - NOW_H);
	ui_w_move (g_scroll, W - SB_W, TOP_H, SB_W, H - TOP_H - NOW_H);
	ui_w_move (g_vNow, 0, H - NOW_H, W, NOW_H);
	g_barMax = -1; g_barPos = -1;
}
static void refresh_all ()
{
	if (!g_vContent) return;
	layout_parts ();
	ui_fill (0, 0, ui.w, ui.h, C_BG);
	long vs[] = { g_vTop, g_vContent, g_vNow, g_vMini, g_vWatch, g_vFoot };
	for (long v : vs) ui_view_dirty (v);
	ui_w_draw (0); ui_update_all ();
	if (page ().kind == P_WATCH && !g_miniMode) watch_place ();
}
static void on_resize (long w, long h)
{
	(void) w; (void) h;
	refresh_all ();
}
static void enter_mini (bool on)
{
	if (on == g_miniMode) return;
	if (on && page ().kind == P_WATCH) go_back_page ();		// (the mini player is the music's)
	if (on)
	{
		g_restore[0] = (int) ui.w; g_restore[1] = (int) ui.h;
		ui_min_size (330, 92);
		g_miniMode = true;
		ui_resize (330, 92);
	}
	else { g_miniMode = false; ui_resize (g_restore[0], g_restore[1]); ui_min_size (620, 420); }
	refresh_all ();
}

// ---- the library: loaded, scanned, swapped ------------------------------------------------------------------------
static void remap_page (Page &p, Library *o, Library *n)
{
	const char *name = 0;
	switch (p.kind)
	{
	case P_ALBUM: if (p.arg >= 0 && p.arg < o->nal) { const Album &a = o->al[p.arg]; p.arg = -1; for (int i = 0; i < n->nal; i++) if (!strcmp (n->al[i].title, a.title) && !strcmp (n->al[i].artist, a.artist)) p.arg = i; } break;
	case P_ARTIST: name = p.arg >= 0 && p.arg < o->nar ? o->ar[p.arg].name : 0; p.arg = -1; for (int i = 0; name && i < n->nar; i++) if (!strcmp (n->ar[i].name, name)) p.arg = i; break;
	case P_GENRE: name = p.arg >= 0 && p.arg < o->nge ? o->ge[p.arg].name : 0; p.arg = -1; for (int i = 0; name && i < n->nge; i++) if (!strcmp (n->ge[i].name, name)) p.arg = i; break;
	case P_FOLDER: name = p.arg >= 0 && p.arg < o->nfo ? o->fo[p.arg].name : 0; p.arg = -1; for (int i = 0; name && i < n->nfo; i++) if (!strcmp (n->fo[i].name, name)) p.arg = i; break;
	default: return;
	}
	if (p.arg < 0) { p.kind = P_HOME; p.arg = 0; }
}
static void scan_done ()
{
	if (!g_scan) return;
	pthread_join (g_scanTid, 0);
	Library *n = g_scan->fresh, *o = L;
	n->build (); n->index_paths ();
	// what the user did meanwhile (plays, favourites): from the library shown
	for (int i = 0; i < n->n; i++) { int k = o->find (n->s[i].path); if (k >= 0) { n->s[i].plays = o->s[k].plays; n->s[i].fav = o->s[k].fav; n->s[i].lastPlayed = o->s[k].lastPlayed; } }
	if (!o->n) n->load_stats ();				// (a first scan: the plays kept from before, if any)
	auto remap = [&] (int id) { if (id < 0) return id; return n->find (o->s[id].path); };
	for (int i = 0; i < g_queue.n; i++) g_queue[i] = remap (g_queue[i]);
	for (int i = 0; i < g_qorig.n; i++) g_qorig[i] = remap (g_qorig[i]);
	if (PLAYING_LIB) { g_playing = remap (g_playing); if (g_playing < 0) g_hasSong = false; }
	for (int i = 0; i < g_nhist; i++) remap_page (g_hist[i], o, n);
	L = n;
	// the videos: what was done meanwhile kept (where one was left), those opened from outside kept
	{
		VideoLib *vn = g_scan->vfresh, *vo = VL;
		vn->index_paths ();
		for (int i = 0; i < vn->n; i++) { int k = vo->find (vn->v[i].path); if (k >= 0) { vn->v[i].posMs = vo->v[k].posMs; vn->v[i].watched = vo->v[k].watched; vn->v[i].lastPlayed = vo->v[k].lastPlayed; } }
		for (int i = 0; i < vo->n; i++) if (vo->v[i].ext && vn->find (vo->v[i].path) < 0) { Video &x = vn->add (); vn->copy_from (x, vo->v[i]); }
		vn->index_paths ();
		auto vremap = [&] (int id) { return id >= 0 && id < vo->n ? vn->find (vo->v[id].path) : -1; };
		if (g_vidx >= 0) g_vidx = vremap (g_vidx);
		for (int i = 0; i < g_nhist; i++) if (g_hist[i].kind == P_WATCH) { g_hist[i].arg = vremap (g_hist[i].arg); if (g_hist[i].arg < 0) g_hist[i] = Page { P_HOME, 0, 0, 0, false }; }
		if (g_vidx < 0 && g_vp.active ()) video_close ();
		VL = vn; g_thumbs->set_library (vn);
		vn->save ();
		delete vo; delete g_scan->vold;
	}
	g_covers->set_library (n);
	g_rollFor = -999;
	n->save ();
	delete o;
	delete g_scan; g_scan = 0;
	if ((L->n || VL->n) && page ().kind == P_WELCOME && g_nhist == 1) g_hist[0] = Page { P_HOME, 0, 0, 0, false };
	g_rowsValid = false; reset_selection (); g_selCap = 0; free (g_sel); g_sel = 0;
	refresh_all ();
}
static void *scan_main (void *p) { scan_thread (p); return 0; }
static void start_scan ()
{
	if (g_scan) { g_scan->cancel = true; pthread_join (g_scanTid, 0); delete g_scan->fresh; delete g_scan->vfresh; delete g_scan->vold; delete g_scan; g_scan = 0; }
	g_scan = new ScanState;
	memset (g_scan, 0, sizeof *g_scan);
	bool videos = false;
	for (int i = 0; i < g_nfolders; i++) { scopy (g_scan->roots[g_scan->nroots++], g_folders[i], 200); if (!strcasecmp (g_folders[i], VIDEO_DIR)) videos = true; }
	if (!videos && g_scan->nroots < 8) scopy (g_scan->roots[g_scan->nroots++], VIDEO_DIR, 200);	// (always looked at)
	g_scan->old = L; g_scan->fresh = new Library;
	g_scan->vold = VL->clone (); g_scan->vfresh = new VideoLib;
	L->index_paths (); VL->index_paths ();
	if (pthread_create (&g_scanTid, 0, scan_main, g_scan) != 0) { delete g_scan->fresh; delete g_scan->vfresh; delete g_scan->vold; delete g_scan; g_scan = 0; }
}

// ---- the menus ------------------------------------------------------------------------------------------------------
enum { C_OPEN = 1, C_OPENV, C_FOLDERS, C_RESCAN, C_NEWPL, C_PLAY, C_NEXT, C_PREV, C_SHUFFLE, C_REPEAT, C_FULL, C_HOME, C_ALBUMS, C_ARTISTS, C_SONGS, C_FILMS, C_CLIPS, C_NOW, C_SEARCH, C_BACK, C_MINI };
static void menus ()
{
	static char b[1500]; int n = 0;
	auto M = [&] (const char *t) { n += snprintf (b + n, sizeof b - n, "M%s\n", t); };
	auto I = [&] (int id, const char *t, const char *key) { n += snprintf (b + n, sizeof b - n, "I%d\t%s%s%s\n", id, t, key[0] ? "\t" : "", key); };
	auto S = [&] () { n += snprintf (b + n, sizeof b - n, "-\n"); };
	M (TR ("File"));
	I (C_OPEN, TR ("Open a File..."), "Ctrl+O"); I (C_OPENV, TR ("Open a Video..."), ""); S ();
	I (C_FOLDERS, TR ("Folders to Watch..."), ""); I (C_RESCAN, TR ("Look for New Songs"), ""); S ();
	I (C_NEWPL, TR ("New Playlist..."), "");
	M (TR ("Play"));
	I (C_PLAY, TR ("Play / Pause"), "Ctrl+P"); I (C_NEXT, TR ("Next"), "Ctrl+F"); I (C_PREV, TR ("Previous"), "Ctrl+B"); S ();
	I (C_SHUFFLE, TR ("Shuffle"), "Ctrl+S"); I (C_REPEAT, TR ("Repeat (all, one, off)"), "Ctrl+T"); S ();
	I (C_FULL, TR ("Full Screen (a video)"), "");
	M (TR ("View"));
	I (C_HOME, TR ("Home"), ""); I (C_ALBUMS, TR ("Albums"), ""); I (C_ARTISTS, TR ("Artists"), ""); I (C_SONGS, TR ("Songs"), "");
	I (C_FILMS, TR ("Films"), ""); I (C_CLIPS, TR ("Clips and Series"), ""); I (C_NOW, TR ("Now Playing"), "Ctrl+L"); S ();
	I (C_SEARCH, TR ("Search"), "Ctrl+E"); I (C_BACK, TR ("Back"), ""); S ();
	I (C_MINI, TR ("Mini Player"), "Ctrl+K");
	ui_menu ((long) b);
}
static void open_dialog (const char *dir, const char *filter)
{
	char p[300];
	ui_file_filter ((long) filter);
	long ok = ui_file_open ((long) dir, (long) p, sizeof p);
	ui_file_filter (0);
	if (ok) open_file (p);
}
static void m_open_video () { open_dialog (VIDEO_DIR, VIDEO_FILTER); }
static void command (int id)
{
	switch (id)
	{
	case C_OPEN: open_dialog (g_nfolders ? g_folders[0] : "SD0:/home", MEDIA_FILTER); break;
	case C_OPENV: m_open_video (); break;
	case C_FOLDERS: navigate (P_WELCOME); break;
	case C_RESCAN: start_scan (); refresh_all (); break;
	case C_NEWPL: new_playlist_dialog (0); break;
	case C_PLAY: toggle_play (); break;
	case C_NEXT: play_next (true); break;
	case C_PREV: play_prev (); break;
	case C_SHUFFLE: set_shuffle (!g_shuffle); refresh_all (); break;
	case C_REPEAT: g_repeat = (g_repeat + 1) % 3; save_settings (); refresh_all (); break;
	case C_FULL: if (page ().kind == P_WATCH) video_full_screen (); break;
	case C_HOME: navigate (P_HOME); break;
	case C_ALBUMS: navigate (P_ALBUMS); break;
	case C_ARTISTS: navigate (P_ARTISTS); break;
	case C_SONGS: navigate (P_SONGS); break;
	case C_FILMS: navigate (P_VIDEOS, VK_FILM); break;
	case C_CLIPS: navigate (P_VIDEOS, VK_CLIP); break;
	case C_NOW: navigate (P_NOW); break;
	case C_SEARCH: if (g_miniMode) enter_mini (false); ui_w_focus (g_searchBox); break;
	case C_BACK: go_back (); break;
	case C_MINI: enter_mini (!g_miniMode); break;
	}
}
static void open_file (const char *p)
{
	if (is_video_path (p))
	{	// a video: from the library, else read now (kept for this run)
		int vi = VL->find (p);
		if (vi < 0)
		{
			Video &x = VL->add ();
			if (!g_kitsOk || !probe_video (p, &x)) { VL->n--; ui_message ((long) TR ("Media Player"), (long) TR ("This file is not a video Media Player\ncan read."), UM_OK); return; }
			char t[200]; video_names (p, t, sizeof t, &x.kind, &x.season, &x.episode);
			if (x.kind < 0) x.kind = x.durMs >= 40 * 60000 ? VK_FILM : VK_CLIP;
			x.path = sdup (p); x.title = sdup (t); x.ext = true; x.added = now_stamp ();
			vi = VL->n - 1;
			VL->index_paths ();
		}
		play_video (vi, false);
		return;
	}
	if (!strcasecmp (ext_of (p), "m3u") || !strcasecmp (ext_of (p), "m3u8"))
	{	// a playlist: its songs that are in the library, in its order
		Plist pl; scopy (pl.path, p, sizeof pl.path); playlist_read (pl);
		IntList l; for (int i = 0; i < pl.n; i++) { int id = L->find (pl.paths[i]); if (id >= 0) l.push (id); free (pl.paths[i]); }
		free (pl.paths);
		if (l.n) play_list (l, 0, false);
		else ui_message ((long) TR ("Media Player"), (long) TR ("None of this playlist's songs is in the library (Folders to Watch...)."), UM_OK);
		return;
	}
	int id = L ? L->find (p) : -1;
	if (id >= 0)
	{
		const Song &s = L->s[id]; IntList l; int st = 0;
		for (int i = 0; s.alb >= 0 && i < L->al[s.alb].n; i++) { if (L->al[s.alb].songs[i] == id) st = i; l.push (L->al[s.alb].songs[i]); }
		play_list (l, st, false);
		return;
	}
	Tags t;
	if (!read_tags (p, &t)) { ui_message ((long) TR ("Media Player"), (long) TR ("This file cannot be played (MP3, OGG,\nOpus, FLAC, WAV, MIDI and FM songs are;\nWebM, MKV, MP4 videos)."), UM_OK); return; }
	int k = g_next < 16 ? g_next++ : 15;
	Song &x = g_ext[k];
	if (x.path) { free (x.path); free (x.title); free (x.artist); free (x.albumArtist); free (x.album); free (x.genre); free (x.folderCover); }
	memset (&x, 0, sizeof x);
	x.path = sdup (p); x.title = sdup (t.title); x.artist = sdup (t.artist); x.albumArtist = sdup (t.albumArtist); x.album = sdup (t.album);
	x.genre = sdup (t.genre); x.folderCover = sdup (""); x.durMs = t.durMs; x.fmt = t.fmt; x.alb = x.art = x.gen = x.fold = -1;
	IntList l; l.push (-k - 1);
	play_list (l, 0, false);
}

// ---- the keys ---------------------------------------------------------------------------------------------------
static void key (int hid, int ch, int mods)
{
	if (hid >= 0xE0) return;				// (a modifier alone)
	if (page ().kind == P_WATCH && !g_miniMode)
	{
		if (hid == HID_ESC || hid == HID_BKSP) { go_back_page (); return; }
		if (ch == 'f' || ch == 'F') { video_full_screen (); return; }
		if (watch_key (hid, ch)) ui_view_dirty (g_vWatch);
		return;
	}
	if (hid == HID_ESC)
	{
		if (page ().kind == P_NOW) go_back ();
		else if (((const char *) ui_w_get_text (g_searchBox))[0]) { ui_w_set_text (g_searchBox, (long) ""); ui_view_dirty (g_vTop); }
		return;
	}
	if (ui_w_get_focus () == g_searchBox) return;
	if (hid == HID_SPACE) { toggle_play (); return; }
	if ((mods & UI_MOD_CTRL) && (ch == 'a' || ch == 'A' || ch == 1)) { ensure_sel (); for (int i = 0; i < g_rows.n; i++) g_sel[g_rows.v[i]] = 1; ui_view_dirty (g_vContent); return; }
	if (hid == HID_ENTER) { IntList ids; selected_ids (ids); if (ids.n == 1) { int r = g_rows.find (ids.v[0]); play_list (g_rows, r, false); } else if (ids.n) play_list (ids, 0, false); return; }
	if (hid == HID_PGDN || hid == HID_PGUP || hid == HID_HOME || hid == HID_END)
	{
		int maxS = g_contentH - g_ch; if (maxS < 0) maxS = 0;
		scroll_to (hid == HID_HOME ? 0 : hid == HID_END ? maxS : sy () + (hid == HID_PGDN ? g_ch - 60 : -(g_ch - 60)));
	}
}
static void control (long w, long code)
{
	if (w == g_side && (code == UN_CHANGE || code == UN_ACTIVATE)) side_chosen ();
	else if (w == g_scroll && code == UN_CHANGE) { g_barPos = ui_w_get_value (g_scroll); sy () = (int) g_barPos; ui_view_dirty (g_vContent); }
}

// the window's turn: what the threads did
static void turn ()
{
	static int lastSec = -1, lastState = -1, ended, errs; static unsigned lastAnim, lastSide, lastWatch;
	static char lastSearch[128];
	g_tick = ticks ();
	if (g_vp.active ())
	{	// the video: its frame, its status; where it is, saved now and then
		g_vp.frame_shown ();
		if (page ().kind == P_WATCH && !g_miniMode)
		{
			if (g_tick - lastWatch >= 25) { lastWatch = g_tick; ui_view_dirty (g_vWatch); }
			if (g_vp.ended && g_surfaced) { g_vp.free_surface (); g_surfaced = false; ui_view_dirty (g_vWatch); }
		}
		if (g_vp.ended && !g_vpEnded) { g_vpEnded = true; video_save_pos (); }
		if (g_tick - g_vpSaveT > 1500) { g_vpSaveT = g_tick; if (!g_vp.paused) video_save_pos (); }
		g_thumbs->busy = !g_vp.paused;
	}
	g_player.poll ();
	if (g_player.endedGen != ended) { ended = g_player.endedGen; play_next (false); }
	if (g_player.errGen != errs) { errs = g_player.errGen; ui_message ((long) TR ("Media Player"), (long) g_player.err, UM_OK); refresh_all (); }
	int sec = (int) (g_player.posMs / 1000);
	if (sec != lastSec || g_player.state != lastState)
	{
		lastSec = sec; lastState = g_player.state;
		ui_view_dirty (g_miniMode ? g_vMini : g_vNow);
		// a play counted: half the song, or 4 minutes, heard
		if (!g_counted && PLAYING_LIB && g_player.lenMs > 0 && (g_player.posMs * 2 >= g_player.lenMs || g_player.posMs > 240000)) count_play ();
	}
	// what moves: the bars of the song playing (5 a second), the notes of a MIDI file (30 a second)
	bool playing = g_player.state == PS_PLAYING;
	const Song *s = g_hasSong ? song (g_playing) : 0;
	bool roll = s && s->fmt == FMT_MIDI && (page ().kind == P_NOW || (page ().kind == P_ALBUM && page ().arg == s->alb && g_playing >= 0));
	unsigned every = roll ? 3 : 20;
	if (playing && g_anim && g_tick - lastAnim >= every && !g_miniMode) { lastAnim = g_tick; ui_view_dirty (g_vContent); }
	if (roll && playing) ui_view_dirty (g_vContent);
	if (g_scan && g_tick - lastSide >= 50) { lastSide = g_tick; ui_view_dirty (g_vFoot); if (page ().kind == P_WELCOME) ui_view_dirty (g_vContent); }
	if (g_scan && g_scan->done) scan_done ();
	g_covers->pump (); g_thumbs->pump ();
	if (g_covers->loaded || g_thumbs->loaded)
	{
		g_covers->loaded = g_thumbs->loaded = false;
		long vs[] = { g_vContent, g_vNow, g_vMini, g_vWatch };
		for (long v : vs) ui_view_dirty (v);
	}
	// the search
	const char *q = (const char *) ui_w_get_text (g_searchBox);
	if (strcmp (q, lastSearch))
	{
		scopy (lastSearch, q, sizeof lastSearch); scopy (g_search, q, sizeof g_search);
		if (q[0]) { if (page ().kind == P_SEARCH) { g_rowsValid = false; page ().scroll = 0; refresh_all (); } else navigate (P_SEARCH); }
		else if (page ().kind == P_SEARCH) go_back ();
	}
}
static void do_click ()
{
	int from = g_click.from; g_click.from = FROM_NONE;
	switch (from)
	{
	case FROM_TOP:
		if (g_click.h.kind == TB_BACK) go_back ();
		else if (g_click.h.kind == TB_FWD) go_fwd ();
		else if (g_click.h.kind == TB_CRUMB) navigate (g_click.h.a, g_click.h.a == P_VIDEOS ? -1 : 0);
		else if (g_click.h.kind == TB_GRID) { g_albumList = false; save_settings (); refresh_all (); }
		else if (g_click.h.kind == TB_LIST) { g_albumList = true; save_settings (); refresh_all (); }
		break;
	case FROM_FOOT: if (g_click.h.kind == SF_NEWPL) new_playlist_dialog (0); else navigate (P_WELCOME); break;
	case FROM_CONTENT: content_click (g_click.h, g_click.x, g_click.y, g_click.mods, g_click.clicks, g_click.right); break;
	case FROM_NOW: now_click (g_click.h); break;
	case FROM_MINI: mini_click (g_click.h, g_click.x); break;
	case FROM_WATCH: watch_click (g_click.h, g_click.clicks); break;
	}
}

static int run (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	audiokit_kit_open ();
	g_kitsOk = mediakit_kit_open ();
	make_dir ("SD0:/home"); make_dir (LIB_DIR); make_dir (MUSIC_DIR); make_dir (VIDEO_DIR);
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/media.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	load_settings ();

	L = new Library;
	L->load (); L->load_stats (); L->build (); L->index_paths ();
	VL = new VideoLib;
	VL->load (); VL->index_paths ();
	playlists_load ();
	g_hist[0] = Page { L->n || VL->n ? P_HOME : P_WELCOME, 0, 0, 0, false }; g_nhist = 1; g_hpos = 0;

	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int w = scr[0] - 8 < 1000 ? (int) scr[0] - 8 : 1000, h = (int) (scr[1] - scr[3] - (scr[2] > 96 ? scr[2] : 96) - 32); if (h > 640) h = 640;	// (32: the frame; the dock floats, reserved or not)
	ui_window_ex (0, scr[3], w, h, (long) TR ("Media Player"), WF_RESIZABLE);
	ui_min_size (620, 420);
	ui_on_resize ((long) on_resize, 0);
	s_jet = (const unsigned *) ui_jetons ();
	g_winW = (int) ui.w; g_winH = (int) ui.h;

	g_covers = new Covers; g_thumbs = new Thumbs;
	g_vTop = ui_view (0, 0, 10, 10, 0); ui_view_on (g_vTop, (long) top_paint, (long) top_mouse, 0, 0);
	g_searchBox = ui_textbox (10, 12, 200, 28, 0); ui_textbox_hint (g_searchBox, (long) TR ("Search the library")); ui_w_set_parent (g_searchBox, g_vTop);
	g_side = ui_list (0, 0, 10, 10, UL_SIDEBAR); ui_list_column (g_side, (long) "", 200, 0); ui_list_cell (g_side, (long) side_cell);
	g_vFoot = ui_view (0, 0, 10, 10, 0); ui_view_on (g_vFoot, (long) foot_paint, (long) foot_mouse, 0, 0);
	g_vContent = ui_view (0, 0, 10, 10, 0); ui_view_on (g_vContent, (long) content_paint, (long) content_mouse, 0, (long) content_wheel);
	g_scroll = ui_scrollbar (0, 0, SB_W, 10, 0); ui_w_set_parent (g_scroll, g_vContent);
	g_vNow = ui_view (0, 0, 10, 10, 0); ui_view_on (g_vNow, (long) now_paint, (long) now_mouse, 0, (long) now_wheel);
	g_vMini = ui_view (0, 0, 10, 10, 0); ui_view_on (g_vMini, (long) mini_paint, (long) mini_mouse, 0, 0);
	g_vWatch = ui_view (0, 0, 10, 10, 0); ui_view_on (g_vWatch, (long) watch_paint, (long) watch_mouse, 0, (long) watch_wheel);

	g_covers->lib = L; g_covers->start ();
	g_thumbs->lib = VL; g_thumbs->start ();
	g_player.volume = g_muted ? 0 : g_volume; g_player.start ();
	menus ();
	refresh_all ();

	if (argc > 1 && argv[1][0]) open_file (argv[1]);
	start_scan ();

	for (;;)
	{
		long t = ui_event (g_vp.active () && !g_vp.paused ? 6 : 30);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR), mods = ui_ev_field (&ui, EV_MODS);
			long code = 0, taken = 0, wd = uikit_w_event (&code, &taken);
			if (code) control (wd, code);
			if (press && !taken) { long id = ui_menu_key (0); if (id >= 0) command ((int) id); else key (hid, ch, mods); }
		}
		else ui_w_idle ();
		if (g_click.from) do_click ();
		turn ();
		ui_view_flush ();
		if (!g_miniMode && page ().kind != P_WATCH) scroll_sync ();
	}

	count_play ();
	video_close ();
	g_player.quit (); g_covers->quit (); g_thumbs->quit ();
	if (g_scan) { g_scan->cancel = true; pthread_join (g_scanTid, 0); }
	return 0;
}

} // namespace media

int main (int argc, char **argv) { return media::run (argc, argv); }
