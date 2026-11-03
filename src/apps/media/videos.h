//
// videos.h -- Media Player's videos (Onyx's Apps/media/videos.h): the films, clips and episodes of the folders it watches
// (and SD0:/home/video), WebM / Matroska and MP4 files read by MediaKit (docs/MEDIAKIT.md). Their facts -- length, size
// in pixels, codecs, whether MediaKit decodes them -- are read from the containers' headers by the scan's thread
// (probe_video) and kept with what the user did (where they stopped, when, watched to the end) in
// SD0:/home/media/videos.tsv.
//
// A video's kind: a film, a clip or an episode of a series -- from its folder (Films, Movies; Series, TV, Shows; Clips),
// else its name ("S01E03", "1x03": an episode), else its length (40 minutes and more: a film).
//
#ifndef _media_videos_h
#define _media_videos_h

#include "tags.h"

namespace media {

#define VIDEO_FILE	"SD0:/home/media/videos.tsv"
#define VIDEO_DIR	"SD0:/home/video"

enum { VK_FILM, VK_CLIP, VK_SERIES };

static inline bool is_video_path (const char *p)
{
	static const char *const E[] = { "webm", "mkv", "mp4", "m4v", "mov", 0 };	// (MediaKit's containers: no FFmpeg)
	const char *e = ext_of (p);
	for (int i = 0; E[i]; i++) if (!strcasecmp (e, E[i])) return true;
	return false;
}

struct Video
{
	char *path, *title;
	int kind, season, episode;			// (an episode: its numbers, else 0)
	int durMs, w, h;
	u64 size;
	char vcodec[20], acodec[20];			// "vp9", "h264", "opus", "ac3" ... ("" none)
	bool playable;					// this build decodes its video (and its sound, if any)
	int posMs;					// where it was left (0: from the start)
	bool watched;					// seen to the end once
	unsigned long long added, lastPlayed;
	bool ext;					// opened from the File Viewer, not in the folders (not saved)
};

// the name shown ("VP9", "H.264"; FFmpeg's names: "mpeg4", "wmv2", "ac3"...)
static inline const char *codec_label (const char *s)
{
	static const char *const M[][2] = { { "vp8", "VP8" }, { "vp9", "VP9" }, { "av1", "AV1" }, { "h264", "H.264" }, { "hevc", "H.265" }, { "mjpeg", "Motion JPEG" },
		{ "i420", "raw" }, { "opus", "Opus" }, { "vorbis", "Vorbis" }, { "aac", "AAC" }, { "mp3", "MP3" }, { "flac", "FLAC" }, { "pcm", "PCM" },
		{ "mpeg4", "MPEG-4" }, { "msmpeg4v3", "DivX 3" }, { "msmpeg4v2", "MS MPEG-4" }, { "mpeg2video", "MPEG-2" }, { "mpeg1video", "MPEG-1" },
		{ "wmv1", "WMV 7" }, { "wmv2", "WMV 8" }, { "wmv3", "WMV 9" }, { "vc1", "VC-1" }, { "theora", "Theora" }, { "flv1", "Sorenson" },
		{ "h263", "H.263" }, { "prores", "ProRes" }, { "rv40", "RealVideo" }, { "rv30", "RealVideo" }, { "ac3", "AC-3" }, { "eac3", "E-AC-3" },
		{ "dts", "DTS" }, { "truehd", "TrueHD" }, { "wmav2", "WMA" }, { "wmav1", "WMA" }, { "wmapro", "WMA Pro" }, { "mp2", "MP2" },
		{ "alac", "ALAC" }, { "cook", "RealAudio" }, { "amr_nb", "AMR" }, { "adpcm_ima_wav", "ADPCM" }, { "adpcm_ms", "ADPCM" } };
	for (auto &m : M) if (!strcmp (s, m[0])) return m[1];
	return s[0] ? s : "?";
}

// the title and kind from the path: the file's name tidied, an episode's numbers found
static void video_names (const char *path, char *title, int cap, int *kind, int *season, int *episode)
{
	const char *sl = strrchr (path, '/'); const char *nm = sl ? sl + 1 : path;
	char b[200]; scopy (b, nm, sizeof b);
	char *dot = strrchr (b, '.'); if (dot) *dot = 0;
	for (char *c = b; *c; c++) if (*c == '_' || (*c == '.' && c[1] != ' ')) *c = ' ';
	*season = *episode = 0;
	// S01E03 / s1e3 / 1x03
	for (char *c = b; *c; c++)
	{
		int s = 0, e = 0; char *q = c;
		if ((*q == 'S' || *q == 's') && q[1] >= '0' && q[1] <= '9')
		{
			q++; while (*q >= '0' && *q <= '9') s = s * 10 + (*q++ - '0');
			if (*q == ' ') q++;
			if ((*q == 'E' || *q == 'e') && q[1] >= '0' && q[1] <= '9') { q++; while (*q >= '0' && *q <= '9') e = e * 10 + (*q++ - '0'); }
			else continue;
		}
		else if (*c >= '0' && *c <= '9' && (c == b || c[-1] == ' '))
		{
			while (*q >= '0' && *q <= '9') s = s * 10 + (*q++ - '0');
			if ((*q != 'x' && *q != 'X') || q[1] < '0' || q[1] > '9') continue;
			q++; while (*q >= '0' && *q <= '9') e = e * 10 + (*q++ - '0');
		}
		else continue;
		if (s > 99 || e <= 0 || e > 999) continue;
		*season = s; *episode = e;
		// the title: what is before the numbers (else after them), without its dashes
		char before[200]; int k = (int) (c - b); memcpy (before, b, (size_t) k); before[k] = 0;
		char *t = before; while (*t == ' ' || *t == '-') t++;
		int l = (int) strlen (t); while (l && (t[l - 1] == ' ' || t[l - 1] == '-')) t[--l] = 0;
		if (!t[0]) { t = q; while (*t == ' ' || *t == '-') t++; }
		memmove (b, t, strlen (t) + 1);
		break;
	}
	int l = (int) strlen (b); while (l && b[l - 1] == ' ') b[--l] = 0;
	scopy (title, b[0] ? b : nm, cap);
	// the kind: the folders, else the name
	*kind = *episode ? VK_SERIES : -1;
	char low[300]; int i = 0; for (const char *c = path; *c && i < 299; c++) low[i++] = (char) (*c >= 'A' && *c <= 'Z' ? *c + 32 : *c); low[i] = 0;
	if (strstr (low, "/films/") || strstr (low, "/movies/") || strstr (low, "/film/")) *kind = VK_FILM;
	else if (strstr (low, "/series/") || strstr (low, "/tv/") || strstr (low, "/shows/")) *kind = VK_SERIES;
	else if (strstr (low, "/clips/") && *kind < 0) *kind = VK_CLIP;
}

// The facts of a video from its container's headers: false if it is not one MediaKit reads (a container it does not
// know, no video track).
static bool probe_video (const char *path, Video *v)
{
	long h = mk_open ((long) path);
	if (h <= 0) return false;
	struct mk_info in;
	bool ok = mk_info ((long) h, (long) &in) == 0 && in.vtrack >= 0;
	if (ok)
	{
		v->w = (int) in.width; v->h = (int) in.height;
		scopy (v->vcodec, in.vname, sizeof v->vcodec);
		if (in.atrack >= 0) scopy (v->acodec, in.aname, sizeof v->acodec); else v->acodec[0] = 0;
		v->playable = in.vok != 0;				// (a sound it cannot read: the video plays mute)
		v->durMs = in.duration > 0 ? (int) (in.duration / 1000) : 0;
	}
	mk_close (h);
	return ok;
}

class VideoLib
{
public:
	Video *v; int n, cap;
	VideoLib () : v (0), n (0), cap (0), m_order (0), m_nidx (0) {}
	~VideoLib () { for (int i = 0; i < n; i++) { free (v[i].path); free (v[i].title); } free (v); free (m_order); }
	Video &add ()
	{
		if (n == cap) { cap = cap ? cap * 2 : 64; v = (Video *) realloc (v, sizeof (Video) * cap); }
		Video &x = v[n++]; memset (&x, 0, sizeof x); return x;
	}
	void copy_from (Video &x, const Video &o) { x = o; x.path = sdup (o.path); x.title = sdup (o.title); }
	// a copy of its own (the scan's thread reads it while the window changes this one)
	VideoLib *clone () const { VideoLib *c = new VideoLib; for (int i = 0; i < n; i++) { Video &x = c->add (); c->copy_from (x, v[i]); } c->index_paths (); return c; }
	void index_paths ()
	{
		free (m_order); m_order = (int *) malloc (sizeof (int) * (n ? n : 1));
		for (int i = 0; i < n; i++) m_order[i] = i;
		sort_idx (m_order, n, [] (const void *c, int a, int b) -> int { const VideoLib *L = (const VideoLib *) c; return strcmp (L->v[a].path, L->v[b].path); }, this);
		m_nidx = n;
	}
	int find (const char *path) const
	{
		if (!m_order || m_nidx != n) { for (int i = 0; i < n; i++) if (!strcmp (v[i].path, path)) return i; return -1; }
		int lo = 0, hi = n - 1;
		while (lo <= hi) { int m = (lo + hi) / 2, c = strcmp (v[m_order[m]].path, path); if (!c) return m_order[m]; if (c < 0) lo = m + 1; else hi = m - 1; }
		return -1;
	}
	// the videos of a kind (-1 all) in an order: by title (an episode: by series, season, episode)
	void list (IntList &out, int kind) const
	{
		out.clear ();
		for (int i = 0; i < n; i++) if (!v[i].ext && (kind < 0 || v[i].kind == kind || (kind == VK_CLIP && v[i].kind == VK_SERIES))) out.push (i);
		sort_idx (out.v, out.n, [] (const void *c, int a, int b) -> int {
			const VideoLib *L = (const VideoLib *) c; const Video &x = L->v[a], &y = L->v[b];
			int r = name_cmp (x.title, y.title); if (r) return r;
			if (x.season != y.season) return x.season - y.season;
			if (x.episode != y.episode) return x.episode - y.episode;
			return strcmp (x.path, y.path); }, this);
	}
	// the one to go on with: left half way, the latest -1 none
	int resume_candidate () const
	{
		int best = -1;
		for (int i = 0; i < n; i++)
			if (v[i].posMs > 0 && v[i].lastPlayed && !v[i].ext && (best < 0 || v[i].lastPlayed > v[best].lastPlayed)) best = i;
		return best;
	}
	// for the home: those played lately first, then the newest
	void recent (IntList &out) const
	{
		out.clear (); for (int i = 0; i < n; i++) if (!v[i].ext) out.push (i);
		sort_idx (out.v, out.n, [] (const void *c, int a, int b) -> int {
			const VideoLib *L = (const VideoLib *) c; const Video &x = L->v[a], &y = L->v[b];
			if (x.lastPlayed != y.lastPlayed) return y.lastPlayed > x.lastPlayed ? 1 : -1;
			if (x.added != y.added) return y.added > x.added ? 1 : -1;
			return name_cmp (x.title, y.title); }, this);
	}

	bool load ()
	{
		char *b = read_all (VIDEO_FILE, 16u << 20);
		if (!b) return false;
		if (strncmp (b, "#onyx-videos 1", 14)) { free (b); return false; }
		for (char *p = b; *p; )
		{
			char *e = strchr (p, '\n'); if (e) *e = 0;
			if (*p && *p != '#')
			{
				char *fl[17]; int k = 0; char *q = p;
				while (k < 17) { fl[k++] = q; char *t = strchr (q, '\t'); if (!t) break; *t = 0; q = t + 1; }
				if (k == 17)
				{
					Video &x = add ();
					x.path = sdup (fl[0]); x.size = strtoull (fl[1], 0, 10); x.durMs = atoi (fl[2]); x.w = atoi (fl[3]); x.h = atoi (fl[4]);
					scopy (x.vcodec, fl[5], sizeof x.vcodec); scopy (x.acodec, fl[6], sizeof x.acodec); x.playable = atoi (fl[7]) != 0;
					x.kind = atoi (fl[8]); x.season = atoi (fl[9]); x.episode = atoi (fl[10]);
					x.posMs = atoi (fl[11]); x.watched = atoi (fl[12]) != 0; x.added = strtoull (fl[13], 0, 10); x.lastPlayed = strtoull (fl[14], 0, 10);
					x.title = sdup (fl[16]);
				}
			}
			if (!e) break;
			p = e + 1;
		}
		free (b);
		return true;
	}
	bool save () const
	{
		make_dir (LIB_DIR);
		size_t cap_ = 160, len = 0;
		for (int i = 0; i < n; i++) cap_ += strlen (v[i].path) + strlen (v[i].title) + 200;
		char *b = (char *) malloc (cap_);
		len += (size_t) snprintf (b, cap_, "#onyx-videos 1\n# path size dur w h vcodec acodec playable kind season episode pos watched added lastPlayed - title\n");
		for (int i = 0; i < n; i++)
		{
			const Video &x = v[i];
			if (x.ext) continue;
			len += (size_t) snprintf (b + len, cap_ - len, "%s\t%llu\t%d\t%d\t%d\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%llu\t%llu\t-\t%s\n", x.path, (unsigned long long) x.size, x.durMs,
						  x.w, x.h, x.vcodec, x.acodec, x.playable ? 1 : 0, x.kind, x.season, x.episode, x.posMs, x.watched ? 1 : 0, x.added, x.lastPlayed, x.title);
		}
		bool ok = save_all (VIDEO_FILE, b, len);
		free (b);
		return ok;
	}
private:
	int *m_order; int m_nidx;
};

// a video found by the scan: known (same size) -> its facts kept, else read
static bool scan_video (const VideoLib *old, VideoLib *fresh, const char *path, unsigned size32)
{
	if (!mediakit_kit_open ()) return false;
	int k = old ? old->find (path) : -1;
	Video &x = fresh->add ();
	if (k >= 0 && (unsigned) old->v[k].size == size32 && !old->v[k].ext)
	{
		fresh->copy_from (x, old->v[k]);
		// (the kind and title again: the rules may have changed, a folder been renamed)
		char t[200]; video_names (path, t, sizeof t, &x.kind, &x.season, &x.episode);
		if (x.kind < 0) x.kind = x.durMs >= 40 * 60000 ? VK_FILM : VK_CLIP;
		free (x.title); x.title = sdup (t);
		return true;
	}
	if (!probe_video (path, &x)) { fresh->n--; return false; }
	char t[200]; video_names (path, t, sizeof t, &x.kind, &x.season, &x.episode);
	if (x.kind < 0) x.kind = x.durMs >= 40 * 60000 ? VK_FILM : VK_CLIP;
	x.path = sdup (path); x.title = sdup (t); x.added = now_stamp (); x.size = size32;
	if (k >= 0) { x.posMs = old->v[k].posMs; x.watched = old->v[k].watched; x.lastPlayed = old->v[k].lastPlayed; x.added = old->v[k].added; }
	return true;
}

// "Film", "Clip", "Series  ·  S1 E3"
static void video_kind_line (const Video &x, char *b, int cap)
{
	if (x.episode) snprintf (b, cap, TR ("Series  \xC2\xB7  S%d E%d"), x.season, x.episode);
	else snprintf (b, cap, "%s", x.kind == VK_FILM ? TR ("Film") : x.kind == VK_SERIES ? TR ("Series") : TR ("Clip"));
}

} // namespace media

#endif
