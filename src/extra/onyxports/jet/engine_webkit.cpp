//
// ARMPI (src/extra/onyxports/jet, docs/WEBKIT.md): Onyx's engine, with ARMPI's paths (/...), a drive
// of three letters in what is typed (/x, /tmp/x), and every character of the BMP typed (ARMPI's
// keyboard gives Unicode). WebKit2's IPC runs on ARMPI's kernel channels (src/kernel/ipc.asm).
//
// web/engine_webkit.cpp -- engine.h on WebKit2: its C API and the Onyx view (WKView, WKPagePrivateOnyx,
// WKRunLoop, WKEventOnyx: tools/webkit/patches). One program plays the three roles: started again by
// WebKit with --onyx-webkit-process=web|network, it runs that process (engine_auxiliary_main).
//
// Korin: tabs (engine.h) -- a Tab per page, each with its own WKContext (its own web process); the
// clients' clientInfo is the Tab. The search engine of what is typed is the window's choice (engine_set_search).
//
// What happens is written to the program's standard error -- the kernel log (`kmsg`) for an Onyx app:
// lines "web: ...", and WebKit's own WTFLogAlways lines from the three processes.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
//
#include "config.h"		// (WebKit's: compiled with the command of one of WebKit's sources)

#include "engine.h"
#include <wtf/Language.h>
#include <wtf/text/WTFString.h>

#include <WebKit/WKAuxiliaryProcessOnyx.h>
#include <WebKit/WKContext.h>
#include <WebKit/WKContextConfigurationOnyx.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKDownloadClient.h>
#include <WebKit/WKDownloadRef.h>
#include <WebKit/WKErrorRef.h>
#include <WebKit/WKFindOptions.h>
#include <WebKit/WKFrameInfoRef.h>
#include <WebKit/WKFramePolicyListener.h>
#include <WebKit/WKNavigationResponseRef.h>
#include <WebKit/WKPageFindClient.h>
#include <WebKit/WKURLResponse.h>
#include <WebKit/WKEventOnyx.h>
#include <WebKit/WKGeometry.h>
#include <WebKit/WKHitTestResult.h>
#include <WebKit/WKNavigationActionRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPagePrivate.h>
#include <WebKit/WKPagePrivateOnyx.h>
#include <WebKit/WKPageUIClient.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKPreferencesRefPrivate.h>
#include <WebKit/WKRunLoop.h>
#include <WebKit/WKString.h>
#include <WebKit/WKType.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKURLRequest.h>
#include <WebKit/WKUserContentControllerRef.h>
#include <WebKit/WKUserScriptRef.h>
#include <WebKit/WKView.h>
#include <WebKit/WKWebsiteDataStoreRef.h>
#include <WebKit/WKWebsiteDataStoreConfigurationRef.h>
#include <WebKit/WKHTTPCookieStoreRef.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <time.h>

// (appkit.h's key codes and modifiers; not the header: it is the newlib apps' and declares the kernel table)
#define MOD_CTRL	1
#define MOD_SHIFT	2
#define MOD_ALT		4
#define KEY_BACKSPACE	8
#define KEY_TAB		9
#define KEY_ENTER	13
#define KEY_UP		0x100
#define KEY_DOWN	0x101
#define KEY_LEFT	0x102
#define KEY_RIGHT	0x103
#define KEY_HOME	0x104
#define KEY_END		0x105
#define KEY_PGUP	0x106
#define KEY_PGDN	0x107
#define KEY_DEL		0x108
#define KEY_F1		0x110
#define KEY_F12		0x11B

static const EngineClient *s_client;
static int s_w, s_h;
static double s_start;

// A tab: its page, and what the window was told last (engine_cycle compares and calls back on a change).
struct Tab
{
	int id;
	WKContextRef context;
	WKViewRef view;
	WKPageRef page;
	std::string title, url;
	bool loading, canBack, canForward;
	double progress;
	bool navigating;			// a navigation under way (its progress alone stays part-way when it became a download)
	const char *siteUA;			// what the page sends now (nullptr: WebKit's own)
	double zoom;
	std::string hoverLink, hoverImage;	// (engine_hit: the right click's menu)
	double newTabClick;			// when a middle or Ctrl click was sent (the link it follows: a new tab)
	int pid;				// its web process, as last logged
	bool dead;				// its web process ended: the window closes it
};
static std::vector<Tab *> s_tabs;
static Tab *s_cur;				// the one shown (nullptr: none)
static int s_nextTab = 1;
static WKViewRef s_view;			// s_cur's view and page (nullptr: none)
static WKPageRef s_page;

static Tab *T (const void *clientInfo) { return (Tab *) const_cast<void *> (clientInfo); }
static bool shown (const Tab *t) { return t && t == s_cur; }

static double now ()
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

// One line of the kernel log: one write (the log makes a line of each write).
static void say (const char *fmt, ...)
{
	char m[480], b[560];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (m, sizeof m, fmt, ap);
	va_end (ap);
	int n = snprintf (b, sizeof b, "web: [%.2f s] %s\n", now () - s_start, m);
	if (n > (int) sizeof b - 1) n = (int) sizeof b - 1;
	write (2, b, (size_t) n);
}

static std::string str (WKStringRef s)
{
	if (!s)
		return { };
	size_t cap = WKStringGetMaximumUTF8CStringSize (s);
	std::string r (cap, '\0');
	size_t n = WKStringGetUTF8CString (s, r.data (), cap);
	r.resize (n ? n - 1 : 0);
	return r;
}

// An address for the kernel log: without its query and fragment (account identifiers, searches), at
// most 120 characters; whole with the network trace (WEBKIT_TRACE_NET: Jet --trace-net, /etc/web-trace-net).
static bool trace_net ()
{
	static int on = -1;
	if (on < 0) on = getenv ("WEBKIT_TRACE_NET") || access ("/etc/web-trace-net", F_OK) == 0;
	return on;
}

static std::string brief (const std::string &url)
{
	if (trace_net ()) return url;
	std::string u = url.substr (0, url.find_first_of ("?#"));
	if (u.size () > 120) u = u.substr (0, 117) + "...";
	return u;
}

static std::string url_str (WKURLRef u)
{
	if (!u)
		return { };
	WKStringRef s = WKURLCopyString (u);
	std::string r = str (s);
	if (s)
		WKRelease (s);
	return r;
}

// ---- the program's roles ------------------------------------------------------------------------------

bool engine_is_auxiliary (int argc, char **argv) { return WKIsAuxiliaryProcessOnyx (argc, argv); }

// The language of the system (Settings > Region: lang= and pays= in /etc/options.txt): WebKit's own
// is en-US here, and sites then choose by the address (Google: Dutch from Belgium). navigator.language
// and the network process's Accept-Language come from this list, in each process: fr-BE, fr, en.
static void set_languages ()
{
	char lang[8] = "fr", pays[8] = "", line[128];
	if (FILE *f = fopen ("/etc/options.txt", "rb")) {
		while (fgets (line, sizeof line, f)) {
			char *v = strchr (line, '=');
			if (!v) continue;
			*v++ = 0;
			char *e = line + strcspn (line, " \t");
			*e = 0;
			v += strspn (v, " \t");
			v[strcspn (v, " \t\r\n")] = 0;
			if (!strcmp (line, "lang") && *v) snprintf (lang, sizeof lang, "%s", v);
			else if (!strcmp (line, "pays") && *v) snprintf (pays, sizeof pays, "%s", v);
		}
		fclose (f);
	}
	Vector<String> list;
	if (*pays) {
		char tag[20];
		snprintf (tag, sizeof tag, "%s-%s", lang, pays);
		list.append (String::fromUTF8 (tag));
	}
	list.append (String::fromUTF8 (lang));
	if (strcmp (lang, "en")) list.append ("en"_s);
	overrideUserPreferredLanguages (list);
}

int engine_auxiliary_main (int argc, char **argv)
{
	s_start = now ();
	set_languages ();
	say ("%s process starts", argc > 1 ? argv[1] : "?");
	int r = WKAuxiliaryProcessMainOnyx (argc, argv);
	say ("%s process ends (%d)", argc > 1 ? argv[1] : "?", r);
	return r;
}

// ---- the page's calls ------------------------------------------------------------------------------------

// The pointer's shape the page asks for (a link's hand, the text bar, a resize's arrows): kept, the
// window shows it while the pointer is over the page (main.cpp, PageView::onMouse).
static int s_cursor = ENGINE_CUR_ARROW;
static void setCursor (WKViewRef, WKCursorType type, const void *ci)
{
	if (!shown (T (ci))) return;
	switch (type)
	{
	case kWKCursorTypeHand:				s_cursor = ENGINE_CUR_HAND; break;
	case kWKCursorTypeIBeam:			s_cursor = ENGINE_CUR_TEXT; break;
	case kWKCursorTypeCross:			s_cursor = ENGINE_CUR_CROSSHAIR; break;
	case kWKCursorTypeWait:				s_cursor = ENGINE_CUR_WAIT; break;
	case kWKCursorTypeMove:				s_cursor = ENGINE_CUR_MOVE; break;
	case kWKCursorTypeEastWestResize:
	case kWKCursorTypeColumnResize:			s_cursor = ENGINE_CUR_SIZE_H; break;
	case kWKCursorTypeNorthSouthResize:
	case kWKCursorTypeRowResize:			s_cursor = ENGINE_CUR_SIZE_V; break;
	case kWKCursorTypeNorthEastSouthWestResize:	s_cursor = ENGINE_CUR_SIZE_NESW; break;
	case kWKCursorTypeNorthWestSouthEastResize:	s_cursor = ENGINE_CUR_SIZE_NWSE; break;
	case kWKCursorTypeNotAllowed:			s_cursor = ENGINE_CUR_NO; break;
	default:					s_cursor = ENGINE_CUR_ARROW; break;
	}
}
int engine_cursor () { return s_cursor; }

static void needsDisplay (WKViewRef, WKRect r, const void *ci)
{
	if (shown (T (ci)) && s_client->needsDisplay)
		s_client->needsDisplay ((int) r.origin.x, (int) r.origin.y, (int) r.size.width, (int) r.size.height);
}

// The tab's web process ended (crash, killed): the navigation client's webProcessDidTerminate says it,
// with the reason; the window is told once. (Not the view's webProcessCrashed: it comes too when the page
// moves to another web process -- a navigation to another site, WebKit's process swap -- and the page
// lives on in the new one: "a new web process" follows.)
static void processEnded (Tab *t, const char *why)
{
	say ("tab %d: the web process ended (%s)", t->id, why);
	if (t->dead) return;
	t->dead = true;
	if (s_client->processEnded)
		s_client->processEnded (t->id);
}

static void processCrashed (WKViewRef, const void *ci) { say ("tab %d: the view's web process exited", T (ci)->id); }
static void processRelaunched (WKViewRef, const void *ci) { say ("tab %d: a new web process", T (ci)->id); }

static void keyNotHandled (WKViewRef, WKKeyboardEvent e, const void *);

static void didStartProvisional (WKPageRef, WKNavigationRef, WKTypeRef, const void *ci)
{
	Tab *t = T (ci);
	say ("tab %d: load started", t->id);
	t->navigating = true;
	if (shown (t) && s_client->console) s_client->console (ENGINE_CONSOLE_CLEAR, "", "");	// (a new page: its console starts empty)
}

// A message of the page's console (WebKit's Onyx port sends them all: WKPagePrivateOnyx.h), with its
// place as "file:line:column" -- the file's name alone (the address's last part), the host when none.
static void consoleMessage (WKPageRef, int level, const char *message, const char *sourceURL, unsigned line, unsigned column)
{
	if (!s_client || !s_client->console) return;
	char where[200] = "";
	if (sourceURL && *sourceURL)
	{
		std::string u (sourceURL);
		size_t q = u.find_first_of ("?#");
		if (q != std::string::npos) u.erase (q);
		while (u.size () > 1 && u[u.size () - 1] == '/') u.erase (u.size () - 1);
		size_t slash = u.rfind ('/');
		std::string name = slash == std::string::npos ? u : u.substr (slash + 1);
		if (name.size () > 60) name = name.substr (0, 28) + "..." + name.substr (name.size () - 28);
		if (line) snprintf (where, sizeof where, "%s:%u:%u", name.c_str (), line, column);
		else snprintf (where, sizeof where, "%s", name.c_str ());
	}
	s_client->console (level, message ? message : "", where);
}
static void didCommit (WKPageRef, WKNavigationRef, WKTypeRef, const void *ci) { say ("tab %d: load committed", T (ci)->id); }
static void didFinish (WKPageRef, WKNavigationRef, WKTypeRef, const void *ci) { say ("tab %d: load finished", T (ci)->id); T (ci)->navigating = false; }

static void loadError (Tab *t, const char *when, WKErrorRef e)
{
	WKStringRef d = e ? WKErrorCopyLocalizedDescription (e) : nullptr;
	WKStringRef dom = e ? WKErrorCopyDomain (e) : nullptr;
	WKURLRef u = e ? WKErrorCopyFailingURL (e) : nullptr;
	std::string why = str (d), url = url_str (u);
	int code = e ? WKErrorGetErrorCode (e) : 0;
	say ("tab %d: load failed (%s): %s %d: %s -- %s", t->id, when, str (dom).c_str (), code, why.c_str (), brief (url).c_str ());
	t->navigating = false;
	// A load the user stopped, or a navigation that became a download: no error page.
	if (code != -999 && code != 102 && s_client->loadFailed)
		s_client->loadFailed (t->id, url.c_str (), why.c_str ());
	if (d) WKRelease (d);
	if (dom) WKRelease (dom);
	if (u) WKRelease (u);
}

static void didFailProvisional (WKPageRef, WKNavigationRef, WKErrorRef e, WKTypeRef, const void *ci) { loadError (T (ci), "before it began", e); }
static void didFail (WKPageRef, WKNavigationRef, WKErrorRef e, WKTypeRef, const void *ci) { loadError (T (ci), "while loading", e); }
static void webProcessDidTerminate (WKPageRef, WKProcessTerminationReason reason, const void *ci)
{
	if (reason == kWKProcessTerminationReasonRequestedByClient)	// (the tab closed: engine_tab_close)
		return;
	processEnded (T (ci), reason == kWKProcessTerminationReasonCrash ? "crash or killed"
		: reason == kWKProcessTerminationReasonExceededMemoryLimit ? "memory limit" : "CPU limit");
}

// target=_blank, window.open: a new tab, with its own page and web process (the window opens it: no
// page is made here, the opener's window.opener stays null).
static WKPageRef createNewPage (WKPageRef, WKPageConfigurationRef, WKNavigationActionRef action, WKWindowFeaturesRef, const void *ci)
{
	WKURLRequestRef req = WKNavigationActionCopyRequest (action);
	WKURLRef u = req ? WKURLRequestCopyURL (req) : nullptr;
	std::string url = url_str (u);
	if (u) WKRelease (u);
	if (req) WKRelease (req);
	say ("tab %d: a new tab for %s", T (ci)->id, brief (url).c_str ());
	if (!url.empty () && url != "about:blank" && s_client->openWindow)
		s_client->openWindow (T (ci)->id, url.c_str (), false);
	return nullptr;
}

static void mouseOverElement (WKPageRef, WKHitTestResultRef hit, WKEventModifiers, WKTypeRef, const void *ci)
{
	Tab *t = T (ci);
	WKURLRef u = hit ? WKHitTestResultCopyAbsoluteLinkURL (hit) : nullptr;
	std::string url = url_str (u);
	if (u) WKRelease (u);
	WKURLRef im = hit ? WKHitTestResultCopyAbsoluteImageURL (hit) : nullptr;
	t->hoverImage = url_str (im);
	if (im) WKRelease (im);
	t->hoverLink = url;
	if (shown (t) && s_client->statusText)
		s_client->statusText (url.c_str ());
}

void engine_hit (char *link, int linkCap, char *image, int imageCap)
{
	snprintf (link, (size_t) linkCap, "%s", s_cur ? s_cur->hoverLink.c_str () : "");
	snprintf (image, (size_t) imageCap, "%s", s_cur ? s_cur->hoverImage.c_str () : "");
}

// Korin: in the account's folder (HOME, set by libposix from users.ini: /home/<name>; "/home" without accounts):
// HOME/jet/webkit (the data), HOME/jet/cache, HOME/downloads. Before the accounts, the data was in /home/jet: moved
// to the account's folder the first time (the cache with it).
static std::string home_dir ()
{
	const char *h = getenv ("HOME");
	std::string d = h && h[0] == '/' ? h : "/home";
	while (d.size () > 1 && d.back () == '/')
		d.pop_back ();
	return d;
}

// ---- downloads -------------------------------------------------------------------------------------------
// Each download known by an id; its file chosen here (HOME/downloads/<its name>, made unique) unless the
// window chose one (Save As); its events told to the window (the downloads' window shows them).

struct Download
{
	WKDownloadRef d;
	int id;
	std::string path;			// "" until decided (or the one asked)
	long long done, total;
	double lastTell;
	bool cancelled;
};
static std::vector<Download *> s_downloads;
static int s_nextDownload = 1;

static void tell (Download *dl, int event, const char *text)
{
	if (s_client->download) s_client->download (dl->id, event, text, dl->done, dl->total);
}

static std::string unique_path (const std::string &dir, std::string name)
{
	for (char &c : name)
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
	if (name.empty ()) name = "download";
	std::string path = dir + "/" + name;
	size_t dot = name.rfind ('.');
	std::string stem = dot == std::string::npos || dot == 0 ? name : name.substr (0, dot);
	std::string ext = dot == std::string::npos || dot == 0 ? "" : name.substr (dot);
	for (int i = 1; access (path.c_str (), F_OK) == 0 && i < 1000; i++)
		path = dir + "/" + stem + " (" + std::to_string (i) + ")" + ext;
	return path;
}

static WKStringRef dlDecide (WKDownloadRef, WKURLResponseRef, WKStringRef suggested, const void *info)
{
	Download *dl = (Download *) const_cast<void *> (info);
	if (dl->path.empty ()) {
		std::string dir = home_dir () + "/downloads";
		mkdir (dir.c_str (), 0755);
		dl->path = unique_path (dir, str (suggested));
	} else
		unlink (dl->path.c_str ());		// (a name chosen in Save As: replaced -- WebKit opens it O_EXCL)
	say ("download %d to %s", dl->id, dl->path.c_str ());
	tell (dl, ENGINE_DL_STARTED, dl->path.c_str ());
	return WKStringCreateWithUTF8CString (dl->path.c_str ());
}

static void dlData (WKDownloadRef, long long, long long written, long long expected, const void *info)
{
	Download *dl = (Download *) const_cast<void *> (info);
	dl->done = written;
	dl->total = expected > 0 ? expected : 0;
	double t = now ();
	if (t - dl->lastTell > 0.25) {
		dl->lastTell = t;
		tell (dl, ENGINE_DL_PROGRESS, dl->path.c_str ());
	}
}

static void forget (Download *dl)
{
	for (size_t i = 0; i < s_downloads.size (); i++)
		if (s_downloads[i] == dl) { s_downloads.erase (s_downloads.begin () + (long) i); break; }
	WKRelease (dl->d);
	delete dl;
}

static void dlFinish (WKDownloadRef, const void *info)
{
	Download *dl = (Download *) const_cast<void *> (info);
	if (dl->total < dl->done) dl->total = dl->done;
	say ("download %d done: %lld bytes", dl->id, dl->done);
	tell (dl, ENGINE_DL_FINISHED, dl->path.c_str ());
	forget (dl);
}

static void dlFail (WKDownloadRef, WKErrorRef e, WKDataRef, const void *info)
{
	Download *dl = (Download *) const_cast<void *> (info);
	WKStringRef d = e ? WKErrorCopyLocalizedDescription (e) : nullptr;
	std::string why = str (d);
	if (d) WKRelease (d);
	say ("download %d %s: %s", dl->id, dl->cancelled ? "cancelled" : "failed", why.c_str ());
	tell (dl, dl->cancelled ? ENGINE_DL_CANCELLED : ENGINE_DL_FAILED, dl->cancelled ? dl->path.c_str () : why.c_str ());
	forget (dl);
}

static void attach (WKDownloadRef d, const std::string &path)
{
	Download *dl = new Download { d, s_nextDownload++, path, 0, 0, 0, false };
	WKRetain (d);
	s_downloads.push_back (dl);
	WKDownloadClientV0 c;
	memset (&c, 0, sizeof c);
	c.base.version = 0;
	c.base.clientInfo = dl;
	c.decideDestinationWithResponse = dlDecide;
	c.didWriteData = dlData;
	c.didFinish = dlFinish;
	c.didFailWithError = dlFail;
	WKDownloadSetClient (d, &c.base);
	say ("download %d begins", dl->id);
}

static void responseBecameDownload (WKPageRef, WKNavigationResponseRef, WKDownloadRef d, const void *) { attach (d, ""); }
static void actionBecameDownload (WKPageRef, WKNavigationActionRef, WKDownloadRef d, const void *) { attach (d, ""); }

// A response the page cannot show (a zip, an installer), or one sent as an attachment: downloaded.
static void decideResponse (WKPageRef, WKNavigationResponseRef r, WKFramePolicyListenerRef listener, WKTypeRef, const void *)
{
	WKURLResponseRef resp = WKNavigationResponseCopyResponse (r);
	bool attachment = resp && WKURLResponseIsAttachment (resp);
	if (resp) WKRelease (resp);
	if (attachment || !WKNavigationResponseCanShowMIMEType (r))
		WKFramePolicyListenerDownload (listener);
	else
		WKFramePolicyListenerUse (listener);
}

static void downloadStarted (WKDownloadRef d, const void *context)
{
	std::string *path = (std::string *) const_cast<void *> (context);
	if (d) attach (d, *path);
	delete path;
}

void engine_download_url (const char *url, const char *path)
{
	if (!s_page) return;
	say ("save %s as %s", brief (url).c_str (), path && *path ? path : "(the downloads folder)");
	WKURLRef u = WKURLCreateWithUTF8CString (url);
	WKPageDownloadURLOnyx (s_page, u, new std::string (path ? path : ""), downloadStarted);
	WKRelease (u);
}

// A download cancelled through the API ends here, not in didFailWithError: the part written removed.
static void cancelled (WKDataRef, const void *info)
{
	int id = (int) (long) info;
	for (Download *dl : s_downloads)
		if (dl->id == id) {
			say ("download %d cancelled", dl->id);
			if (!dl->path.empty ()) unlink (dl->path.c_str ());
			tell (dl, ENGINE_DL_CANCELLED, dl->path.c_str ());
			forget (dl);
			return;
		}
}

void engine_download_cancel (int id)
{
	for (Download *dl : s_downloads)
		if (dl->id == id && !dl->cancelled) {
			dl->cancelled = true;
			WKDownloadCancel (dl->d, (const void *) (long) id, cancelled);
			return;
		}
}

// ---- the embedded web view: given HTML, scripts, links -------------------------------------------------

static bool s_scripts = true;
static void (*s_linkHandler) (const char *);

void engine_set_scripts (bool enabled) { s_scripts = enabled; }
void engine_set_link_handler (void (*clicked) (const char *)) { s_linkHandler = clicked; }

void engine_load_html (const char *html, const char *baseUrl)
{
	if (!s_page) return;
	WKStringRef h = WKStringCreateWithUTF8CString (html ? html : "");
	WKURLRef b = baseUrl && *baseUrl ? WKURLCreateWithUTF8CString (baseUrl) : nullptr;
	WKPageLoadHTMLString (s_page, h, b);
	WKRelease (h);
	if (b) WKRelease (b);
}

// ---- a site's own user agent ------------------------------------------------------------------------------
// Sites in their desktop form are heavy for a Pi 4 (YouTube's page: megabytes of script, twenty
// seconds before the first picture) and wide for its small screen; to a phone they serve a lighter
// one. On ARMPI every site is asked for as Android's Chrome -- not as an iPhone, which would be given HLS / H.264, that the media engine
// does not play: Chrome on Android gets Media Source with VP9 / AV1, which it does.
// /etc/web-desktop-ua (a file, empty): WebKit's own user agent, the desktop sites.
#if defined(__x86_64__)
// Korin PC (x86-64): far more powerful than the Pi and with a large screen -- the sites' full desktop form,
// asked for as desktop Safari on macOS (the WebKit of this build is upstream trunk of October 2026, past
// Safari 26: a release version the sites know, that claims nothing this engine lacks).
static const char MOBILE_UA[] = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.2 Safari/605.1.15";
#define SITE_UA_KIND "desktop Safari's"
#else
static const char MOBILE_UA[] = "Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Mobile Safari/537.36";
#define SITE_UA_KIND "a phone's"
#endif
static const char *site_user_agent (const std::string &url)
{
	static int off = -1;
	if (off < 0) off = access ("/etc/web-desktop-ua", F_OK) == 0;
	if (off) return nullptr;
	return url.compare (0, 7, "http://") == 0 || url.compare (0, 8, "https://") == 0 ? MOBILE_UA : nullptr;
}

// The page's user agent made the one of url's site -> true when it changed.
static bool apply_site_user_agent (Tab *t, const std::string &url)
{
	const char *ua = site_user_agent (url);
	if (ua == t->siteUA) return false;
	t->siteUA = ua;
	WKStringRef w = WKStringCreateWithUTF8CString (ua ? ua : "");
	WKPageSetCustomUserAgent (t->page, w);
	WKRelease (w);
	say ("tab %d: user agent: %s (%s)", t->id, ua ? SITE_UA_KIND : "WebKit's own", brief (url).substr (0, 60).c_str ());
	return true;
}

// With a link handler (the embedded view): a link the user clicks is the host's, not followed here.
static void decideAction (WKPageRef, WKNavigationActionRef action, WKFramePolicyListenerRef listener, WKTypeRef, const void *ci)
{
	Tab *t = T (ci);
	WKURLRequestRef req = WKNavigationActionCopyRequest (action);
	WKURLRef u = req ? WKURLRequestCopyURL (req) : nullptr;
	std::string url = url_str (u);
	WKStringRef m = req ? WKURLRequestCopyHTTPMethod (req) : nullptr;
	bool get = !m || str (m) == "GET";
	if (m) WKRelease (m);
	if (u) WKRelease (u);
	if (req) WKRelease (req);
	WKFrameNavigationType type = WKNavigationActionGetNavigationType (action);

	if (s_linkHandler && type == kWKFrameNavigationTypeLinkClicked) {
		WKFramePolicyListenerIgnore (listener);
		s_linkHandler (url.c_str ());
		return;
	}

	// The page's user agent follows the site of its main frame. This request was made with the one
	// before: when it changes, the navigation is started again (a plain GET that is not a move in
	// the history: those go on as they are, and the next request has the right one).
	WKFrameInfoRef frame = WKNavigationActionCopyTargetFrameInfo (action);
	bool mainFrame = frame && WKFrameInfoGetIsMainFrame (frame);
	if (frame) WKRelease (frame);

	// A link clicked with the middle button or with Ctrl (engine_mouse noted when): a new tab behind this
	// one (the C API does not give the navigation's button: the click sent just before says it).
	if (type == kWKFrameNavigationTypeLinkClicked && t->newTabClick > 0 && now () - t->newTabClick < 3 && !s_linkHandler) {
		t->newTabClick = 0;
		WKFramePolicyListenerIgnore (listener);
		say ("tab %d: a link in a new tab: %s", t->id, brief (url).c_str ());
		if (s_client->openWindow) s_client->openWindow (t->id, url.c_str (), true);
		return;
	}
	if (mainFrame && !s_linkHandler) {
		if (apply_site_user_agent (t, url)) {
			if (get && type != kWKFrameNavigationTypeBackForward && type != kWKFrameNavigationTypeReload
			    && type != kWKFrameNavigationTypeFormSubmitted && type != kWKFrameNavigationTypeFormResubmitted) {
				WKFramePolicyListenerIgnore (listener);
				WKURLRef again = WKURLCreateWithUTF8CString (url.c_str ());
				WKPageLoadURL (t->page, again);
				WKRelease (again);
				return;
			}
		}
	}
	WKFramePolicyListenerUse (listener);
}

// ---- the <select> lists, find, the clipboard -----------------------------------------------------------

static WKViewRef s_popupView;			// the view whose <select> list is open
static void showPopupMenu (WKViewRef view, const WKPopupMenuItemOnyx *items, int count, int selected, WKRect r, const void *ci)
{
	if (!shown (T (ci)) || !s_client->showPopup || count <= 0) { WKViewSelectPopupMenuItem (view, -1); return; }
	s_popupView = view;
	std::vector<const char *> texts ((size_t) count);
	std::vector<unsigned char> flags ((size_t) count);
	for (int i = 0; i < count; i++) {
		texts[(size_t) i] = items[i].text ? items[i].text : "";
		flags[(size_t) i] = (unsigned char) ((items[i].enabled ? ENGINE_ITEM_ENABLED : 0) | (items[i].isSeparator ? ENGINE_ITEM_SEPARATOR : 0)
			| (items[i].isLabel ? ENGINE_ITEM_LABEL : 0));
	}
	s_client->showPopup (texts.data (), flags.data (), count, selected, (int) r.origin.x, (int) r.origin.y, (int) r.size.width, (int) r.size.height);
}

static void hidePopupMenu (WKViewRef, const void *ci) { if (shown (T (ci)) && s_client->hidePopup) s_client->hidePopup (); }

void engine_popup_select (int index)
{
	for (Tab *t : s_tabs)				// (the tab may have closed meanwhile)
		if (t->view == s_popupView) { WKViewSelectPopupMenuItem (s_popupView, index); break; }
	s_popupView = nullptr;
}

static void didFind (WKPageRef, WKStringRef, unsigned matches, const void *ci) { if (shown (T (ci)) && s_client->findResult) s_client->findResult ((int) matches); }
static void didNotFind (WKPageRef, WKStringRef, const void *ci) { if (shown (T (ci)) && s_client->findResult) s_client->findResult (0); }

void engine_find (const char *text, bool backwards)
{
	if (!s_page || !text || !*text) return;
	WKStringRef s = WKStringCreateWithUTF8CString (text);
	WKFindOptions o = kWKFindOptionsCaseInsensitive | kWKFindOptionsWrapAround | kWKFindOptionsShowHighlight
		| kWKFindOptionsShowFindIndicator | (backwards ? kWKFindOptionsBackwards : 0);
	WKPageFindString (s_page, s, o, 1000);
	WKRelease (s);
}

void engine_find_done () { if (s_page) WKPageHideFindUI (s_page); }

void engine_set_clipboard (void (*write) (const char *, unsigned long), unsigned long (*read) (char *, unsigned long), unsigned (*serial) ())
{
	WKSetClipboardCallbacksOnyx ((WKClipboardWriteTextOnyx) write, (WKClipboardReadTextOnyx) read, (WKClipboardSerialOnyx) serial);
}

static void runAlert (WKPageRef, WKStringRef text, WKFrameRef, WKSecurityOriginRef, WKPageRunJavaScriptAlertResultListenerRef listener, const void *)
{
	if (s_client->alert)
		s_client->alert (str (text).c_str ());
	WKPageRunJavaScriptAlertResultListenerCall (listener);
}

static void runConfirm (WKPageRef, WKStringRef text, WKFrameRef, WKSecurityOriginRef, WKPageRunJavaScriptConfirmResultListenerRef listener, const void *)
{
	bool ok = s_client->confirm ? s_client->confirm (str (text).c_str ()) : false;
	WKPageRunJavaScriptConfirmResultListenerCall (listener, ok);
}

// A new window: this program started again with the URL (the kernel shares its image: quick).
extern "C" char *program_invocation_name;
extern "C" char **environ;

// This program's file: argv[0] when it is a path (a start from the shell), else the app's place (a
// start from the dock or `run web` may give the name only).
static const char *self_path ()
{
	const char *a = program_invocation_name;
	if (a && (strchr (a, ':') || a[0] == '/')) return a;
	return "/apps/jet.app/main";
}

// The GPU compositor (WebKit's USE(GRAPHICS_LAYER_ONYX): docs/08), asked for before engine_init: the
// page's layers kept in tiles and assembled by the V3D, so that scrolling paints nothing again.
static bool s_compositing;

void engine_set_compositing (bool enabled) { s_compositing = enabled; }

void engine_new_window (const char *url)
{
	const char *self = self_path ();
	char gpu[] = "--gpu", nogpu[] = "--nogpu";		// (a window opened by this one paints as this one)
	char *argv[] = { const_cast<char *> (self), s_compositing ? gpu : nogpu, const_cast<char *> (url), nullptr };
	pid_t pid;
	int r = posix_spawn (&pid, self, nullptr, nullptr, argv, environ);
	say ("new window for %s: %s", brief (url).c_str (), r ? strerror (r) : "started");
}

// This program started again in a helper role (argv[1]), its standard input and output two pipes:
// *toChild written by us, *fromChild read (non-blocking).
int engine_spawn_self (const char *role, int *toChild, int *fromChild)
{
	int a[2], b[2];
	if (pipe (a)) return 0;
	if (pipe (b)) { close (a[0]); close (a[1]); return 0; }
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init (&fa);
	posix_spawn_file_actions_adddup2 (&fa, a[0], 0);
	posix_spawn_file_actions_adddup2 (&fa, b[1], 1);
	posix_spawn_file_actions_addclose (&fa, a[1]);
	posix_spawn_file_actions_addclose (&fa, b[0]);
	const char *self = self_path ();
	char *argv[] = { const_cast<char *> (self), const_cast<char *> (role), nullptr };
	pid_t pid = 0;
	int r = posix_spawn (&pid, self, &fa, nullptr, argv, environ);
	posix_spawn_file_actions_destroy (&fa);
	close (a[0]);
	close (b[1]);
	if (r) {
		say ("%s: %s", role, strerror (r));
		close (a[1]); close (b[0]);
		return 0;
	}
	fcntl (b[0], F_SETFL, fcntl (b[0], F_GETFL) | O_NONBLOCK);
	fcntl (a[1], F_SETFL, fcntl (a[1], F_GETFL) | O_NONBLOCK);	// (a helper that stopped reading never blocks us)
	*toChild = a[1];
	*fromChild = b[0];
	say ("%s started (pid %d)", role, (int) pid);
	return (int) pid ? (int) pid : 1;
}

// ---- init, the tabs, the loop ---------------------------------------------------------------------------

// What every tab's page gets (engine_init reads the files once).
static bool s_console;				// /etc/web-console
static std::string s_probe;			// /etc/web-probe.js

// The sites' data, kept between runs and shared by every tab: one data store (one network process, one
// cookie jar). Its places are rooted paths without a volume name (WebKit's path code takes only those for
// absolute): main.cpp makes / the working directory first, they are then on / whatever the
// directory Jet was started from. The cookies: an SQLite base (CookieJarDB of the curl port; SQLite's
// rollback journal, not WAL: Korin has no writable shared mapping of a file); the session cookies (no
// Expires / Max-Age) are dropped when it opens, like any browser.
static std::string s_data, s_cache;
static WKWebsiteDataStoreRef s_store;

static void set_dir (void (*set) (WKWebsiteDataStoreConfigurationRef, WKStringRef), WKWebsiteDataStoreConfigurationRef c, const char *path)
{
	WKStringRef s = WKStringCreateWithUTF8CString (path);
	set (c, s);
	WKRelease (s);
}

static WKWebsiteDataStoreRef make_data_store ()
{
	std::string h = home_dir (), j = h + "/jet";
	s_data = j + "/webkit";
	s_cache = j + "/cache";
	mkdir (h.c_str (), 0755);
	mkdir (j.c_str (), 0755);
	if (h != "/home" && access (s_data.c_str (), F_OK) != 0 && access ("/home/jet/webkit", F_OK) == 0) {
		bool moved = rename ("/home/jet/webkit", s_data.c_str ()) == 0;
		rename ("/home/jet/cache", s_cache.c_str ());
		say ("the sites' data of /home/jet %s %s", moved ? "moved to" : "NOT moved to", j.c_str ());
	}
	mkdir (s_data.c_str (), 0755);
	mkdir (s_cache.c_str (), 0755);
	const std::string &D = s_data, &C = s_cache;
	WKWebsiteDataStoreConfigurationRef c = WKWebsiteDataStoreConfigurationCreate ();
	set_dir (WKWebsiteDataStoreConfigurationSetCookieStorageFile, c, (D + "/cookies.db").c_str ());
	set_dir (WKWebsiteDataStoreConfigurationSetLocalStorageDirectory, c, (D + "/localstorage").c_str ());
	set_dir (WKWebsiteDataStoreConfigurationSetIndexedDBDatabaseDirectory, c, (D + "/indexeddb").c_str ());
	set_dir (WKWebsiteDataStoreConfigurationSetServiceWorkerRegistrationDirectory, c, (D + "/serviceworkers").c_str ());
	set_dir (WKWebsiteDataStoreConfigurationSetCacheStorageDirectory, c, (D + "/cachestorage").c_str ());
	set_dir (WKWebsiteDataStoreConfigurationSetGeneralStorageDirectory, c, (D + "/storage").c_str ());
	set_dir (WKWebsiteDataStoreConfigurationSetWebSQLDatabaseDirectory, c, (D + "/websql").c_str ());
	set_dir (WKWebsiteDataStoreConfigurationSetMediaKeysStorageDirectory, c, (D + "/mediakeys").c_str ());
	set_dir (WKWebsiteDataStoreConfigurationSetResourceLoadStatisticsDirectory, c, (D + "/itp").c_str ());
	set_dir (WKWebsiteDataStoreConfigurationSetIsolatedSitesDirectory, c, (D + "/isolatedsites").c_str ());
	set_dir (WKWebsiteDataStoreConfigurationSetNetworkCacheDirectory, c, (C + "/network").c_str ());
	WKWebsiteDataStoreRef store = WKWebsiteDataStoreCreateWithConfiguration (c);
	WKRelease (c);
	say ("the sites' data in %s (cookies.db), the cache in %s", D.c_str (), C.c_str ());
	return store;
}

// "Clear cookies and site data": seven removals, done () after the last
static void (*s_clearDone) ();
static int s_clearLeft;
static void cleared (void *)
{
	if (--s_clearLeft == 0) {
		say ("the sites' data cleared");
		if (s_clearDone) s_clearDone ();
	}
}

void engine_clear_site_data (void (*done) ())
{
	if (!s_store || s_clearLeft > 0) return;
	say ("clearing the sites' data");
	s_clearDone = done;
	s_clearLeft = 7;
	WKHTTPCookieStoreDeleteAllCookies (WKWebsiteDataStoreGetHTTPCookieStore (s_store), nullptr, cleared);
	WKWebsiteDataStoreRemoveLocalStorage (s_store, nullptr, cleared);
	WKWebsiteDataStoreRemoveAllIndexedDatabases (s_store, nullptr, cleared);
	WKWebsiteDataStoreRemoveAllServiceWorkerRegistrations (s_store, nullptr, cleared);
	WKWebsiteDataStoreRemoveAllFetchCaches (s_store, nullptr, cleared);
	WKWebsiteDataStoreRemoveNetworkCache (s_store, nullptr, cleared);
	WKWebsiteDataStoreRemoveMemoryCaches (s_store, nullptr, cleared);
}

bool engine_init (const EngineClient *client, int w, int h)
{
	s_start = now ();
	s_client = client;
	s_w = w; s_h = h;
	say ("WebKit starts (%d x %d)", w, h);
	WKRunLoopInitializeMain ();
	say ("program %s (argv[0] %s)", self_path (), program_invocation_name ? program_invocation_name : "-");
	// /etc/web-console: the pages' console (their messages, their script errors) in kmsg
	s_console = access ("/etc/web-console", F_OK) == 0;
	if (s_console)
		say ("the pages' console is logged");
	say ("compositing %s", s_compositing ? "on (the GPU compositor)" : "off (the software path)");
	// /etc/web-probe.js: a script run in every page (all its frames) before the page's own, to
	// find what a site's application does (with web-console, what it logs is in kmsg)
	if (FILE *f = fopen ("/etc/web-probe.js", "rb")) {
		static char src[32768];
		size_t n = fread (src, 1, sizeof src - 1, f);
		fclose (f);
		src[n] = 0;
		s_probe = src;
		say ("the probe script (%u bytes) runs in every page", (unsigned) n);
	}
	WKSetConsoleMessageCallbackOnyx (consoleMessage);
	s_store = make_data_store ();
	say ("WebKit ready");
	return true;
}

static Tab *tab_of (int id)
{
	for (Tab *t : s_tabs)
		if (t->id == id)
			return t;
	return nullptr;
}

// A tab: its own context -- the web process of its pages is its own, never another tab's (WebKit gives
// a context's pages of one site the same process); the network process and the data are shared (the
// default data store).
int engine_tab_new (const char *typed)
{
	Tab *t = new Tab ();
	t->id = s_nextTab++;
	t->zoom = 1;

	WKContextConfigurationRef cc = WKContextConfigurationCreate ();
	// The web and the network process: this same program (its role by an argument).
	WKStringRef self = WKStringCreateWithUTF8CString (self_path ());
	WKContextConfigurationSetWebProcessPath (cc, self);
	WKContextConfigurationSetNetworkProcessPath (cc, self);
	WKRelease (self);
	t->context = WKContextCreateWithConfiguration (cc);
	WKRelease (cc);

	WKPageConfigurationRef pc = WKPageConfigurationCreate ();
	WKPageConfigurationSetContext (pc, t->context);
	// The data (cookies, local storage, the caches): the one data store of every tab (make_data_store).
	WKPageConfigurationSetWebsiteDataStore (pc, s_store ? s_store : WKWebsiteDataStoreGetDefaultDataStore ());
	if (!s_scripts || s_compositing || s_console) {
		WKPreferencesRef prefs = WKPreferencesCreate ();
		if (s_console)
			WKPreferencesSetLogsPageMessagesToSystemConsoleEnabled (prefs, true);
		if (!s_scripts)					// (the embedded view, for a mail's HTML)
			WKPreferencesSetJavaScriptEnabled (prefs, false);
		if (s_compositing)				// (the web process then logs "web: gpu: ..." lines)
			WKPreferencesSetCompositingEnabledOnyx (prefs, true);
		WKPageConfigurationSetPreferences (pc, prefs);
		WKRelease (prefs);
	}
	if (!s_probe.empty ()) {
		WKUserContentControllerRef ucc = WKUserContentControllerCreate ();
		WKStringRef text = WKStringCreateWithUTF8CString (s_probe.c_str ());
		WKUserScriptRef script = WKUserScriptCreateWithSource (text, kWKInjectAtDocumentStart, false);
		WKUserContentControllerAddUserScript (ucc, script);
		WKPageConfigurationSetUserContentController (pc, ucc);
		WKRelease (script); WKRelease (text); WKRelease (ucc);
	}

	t->view = WKViewCreate (pc);
	WKRelease (pc);
	if (!t->view) {
		say ("tab %d: no view", t->id);
		WKRelease (t->context);
		delete t;
		return 0;
	}
	WKViewClientV1 vc;
	memset (&vc, 0, sizeof vc);
	vc.base.version = 1;
	vc.base.clientInfo = t;
	vc.setViewNeedsDisplay = needsDisplay;
	vc.setCursor = setCursor;
	vc.didNotHandleKeyEvent = keyNotHandled;
	vc.webProcessCrashed = processCrashed;
	vc.webProcessDidRelaunch = processRelaunched;
	vc.showPopupMenu = showPopupMenu;
	vc.hidePopupMenu = hidePopupMenu;
	WKViewSetViewClient (t->view, &vc.base);
	WKViewSetSize (t->view, WKSizeMake (s_w, s_h));
	WKViewSetVisible (t->view, false);		// (engine_tab_show)
	WKViewSetActive (t->view, false);

	t->page = WKViewGetPage (t->view);
	WKStringRef app = WKStringCreateWithUTF8CString ("Onyx");
	WKPageSetApplicationNameForUserAgent (t->page, app);
	WKRelease (app);

	WKPageNavigationClientV3 nc;
	memset (&nc, 0, sizeof nc);
	nc.base.version = 3;
	nc.base.clientInfo = t;
	nc.decidePolicyForNavigationAction = decideAction;
	nc.decidePolicyForNavigationResponse = decideResponse;
	nc.navigationActionDidBecomeDownload = actionBecameDownload;
	nc.navigationResponseDidBecomeDownload = responseBecameDownload;
	nc.didStartProvisionalNavigation = didStartProvisional;
	nc.didCommitNavigation = didCommit;
	nc.didFinishNavigation = didFinish;
	nc.didFailProvisionalNavigation = didFailProvisional;
	nc.didFailNavigation = didFail;
	nc.webProcessDidTerminate = webProcessDidTerminate;	// (in place of webProcessDidCrash: with the reason)
	WKPageSetPageNavigationClient (t->page, &nc.base);

	WKPageUIClientV19 uc;
	memset (&uc, 0, sizeof uc);
	uc.base.version = 19;
	uc.base.clientInfo = t;
	uc.createNewPage = createNewPage;
	uc.mouseDidMoveOverElement = mouseOverElement;
	uc.runJavaScriptAlert = runAlert;
	uc.runJavaScriptConfirm = runConfirm;
	WKPageSetPageUIClient (t->page, &uc.base);

	WKPageFindClientV0 fc;
	memset (&fc, 0, sizeof fc);
	fc.base.version = 0;
	fc.base.clientInfo = t;
	fc.didFindString = didFind;
	fc.didFailToFindString = didNotFind;
	WKPageSetPageFindClient (t->page, &fc.base);

	s_tabs.push_back (t);
	say ("tab %d: new (%d tabs)", t->id, (int) s_tabs.size ());
	if (typed && *typed) {
		std::string url = engine_typed_url (typed);
		say ("tab %d: load %s", t->id, brief (url).c_str ());
		if (!s_linkHandler) apply_site_user_agent (t, url);	// (the request is made with it)
		WKURLRef u = WKURLCreateWithUTF8CString (url.c_str ());
		WKPageLoadURL (t->page, u);
		WKRelease (u);
	}
	return t->id;
}

// The shown tab: visible and active (its page paints, takes the input), the others hidden (WebKit
// stops their painting and their timers slow down). With the compositor, its picture is asked again
// (the window shows the picture of the last page that sent one).
void engine_tab_show (int id)
{
	Tab *t = tab_of (id);
	if (!t || t == s_cur) return;
	if (s_cur) {
		WKViewSetVisible (s_cur->view, false);
		WKViewSetActive (s_cur->view, false);
		WKViewSetFocus (s_cur->view, false);
	}
	s_cur = t;
	s_view = t->view;
	s_page = t->page;
	WKViewSetSize (t->view, WKSizeMake (s_w, s_h));
	WKViewSetVisible (t->view, true);
	WKViewSetActive (t->view, true);
	WKViewSetFocus (t->view, true);
	s_cursor = ENGINE_CUR_ARROW;
	if (s_compositing)
		WKPageForceRepaint (t->page, nullptr, [] (WKErrorRef, void *) { });
	say ("tab %d shown", id);
}

int engine_tab_shown () { return s_cur ? s_cur->id : 0; }

int engine_tab_pid (int id)
{
	Tab *t = tab_of (id);
	return t && t->page ? (int) WKPageGetProcessIdentifier (t->page) : 0;
}

// A tab closed: its page closed (its web process ends: WebKit's reason "requested by the client"), its
// view and its context let go. The window shows another one first.
void engine_tab_close (int id)
{
	Tab *t = tab_of (id);
	if (!t) return;
	say ("tab %d: closed (web process %d)", id, t->pid);
	if (t == s_cur) {
		s_cur = nullptr;
		s_view = nullptr;
		s_page = nullptr;
	}
	if (s_popupView == t->view) {
		WKViewSelectPopupMenuItem (t->view, -1);
		s_popupView = nullptr;
	}
	for (size_t i = 0; i < s_tabs.size (); i++)
		if (s_tabs[i] == t) { s_tabs.erase (s_tabs.begin () + (long) i); break; }
	WKPageClose (t->page);
	WKRelease (t->view);
	WKRelease (t->context);
	delete t;
}

void engine_cycle ()
{
	if (s_tabs.empty ())
		return;
	WKRunLoopCycleMain ();

	// The pages' state, polled (the window shows what changed). (Copied: a call back may close a tab.)
	std::vector<Tab *> tabs = s_tabs;
	for (Tab *t : tabs) {
		if (!tab_of (t->id) || t->dead)
			continue;
		WKStringRef ts = WKPageCopyTitle (t->page);
		std::string title = str (ts);
		if (ts) WKRelease (ts);
		if (title != t->title) {
			t->title = title;
			if (s_client->titleChanged) s_client->titleChanged (t->id, title.c_str ());
		}
		WKURLRef u = WKPageCopyActiveURL (t->page);
		std::string url = url_str (u);
		if (u) WKRelease (u);
		if (url != t->url) {
			t->url = url;
			say ("tab %d: address %s", t->id, brief (url).c_str ());
			if (s_client->urlChanged) s_client->urlChanged (t->id, url.c_str ());
		}
		double p = WKPageGetEstimatedProgress (t->page);
		bool loading = t->navigating && p > 0 && p < 1;
		if (loading != t->loading || (loading && p - t->progress > 0.05)) {
			t->loading = loading; t->progress = p;
			if (s_client->loadingChanged) s_client->loadingChanged (t->id, loading, p);
		}
		bool b = WKPageCanGoBack (t->page), f = WKPageCanGoForward (t->page);
		if (b != t->canBack || f != t->canForward) {
			t->canBack = b; t->canForward = f;
			if (s_client->historyChanged) s_client->historyChanged (t->id, b, f);
		}
		// Its web process (one per tab; another after a move to another site): in the log, to find it
		int pid = (int) WKPageGetProcessIdentifier (t->page);
		if (pid > 0 && pid != t->pid) {
			t->pid = pid;
			say ("tab %d: web process pid %d", t->id, pid);
		}
	}
}

void engine_resize (int w, int h)
{
	s_w = w; s_h = h;
	for (Tab *t : s_tabs)
		WKViewSetSize (t->view, WKSizeMake (w, h));
}

extern "C" void onyx_set_frame_client (int (*) (int, int, long, int, int, int, int));

void engine_set_frame_client (int (*show) (int buffer, int pitch, long offset, int x, int y, int w, int h))
{
	onyx_set_frame_client (show);
}

void engine_paint (unsigned *px, int stride, int x, int y, int w, int h)
{
	if (!s_page)
		return;
	WKPagePaint (s_page, reinterpret_cast<unsigned char *> (px), WKSizeMake (s_w, s_h), (uint32_t) stride * 4, WKRectMake (x, y, w, h));
	// The canvas is 0x00RRGGBB: WebKit wrote an alpha byte.
	for (int j = y; j < y + h && j < s_h; j++)
		for (int i = x; i < x + w && i < s_w; i++)
			px[j * stride + i] &= 0x00FFFFFFu;
}

void engine_set_active (bool active)
{
	if (!s_view) return;
	WKViewSetActive (s_view, active);
	WKViewSetFocus (s_view, active);
}

// ---- navigation ----------------------------------------------------------------------------------------

// The search of what is typed that is not an address (engine_set_search; the window's default: Google)
static std::string s_search = "https://www.google.com/search?q=";

void engine_set_search (const char *prefix)
{
	if (prefix && *prefix) s_search = prefix;
}

static bool all_digits (const std::string &s)
{
	if (s.empty ()) return false;
	for (unsigned char c : s)
		if (!isdigit (c)) return false;
	return true;
}

// host[:port] an IPv4 address (four numbers 0..255)?
static bool is_ipv4 (const std::string &host)
{
	int parts = 0;
	size_t i = 0;
	while (i <= host.size ()) {
		size_t dot = host.find ('.', i);
		std::string n = host.substr (i, dot == std::string::npos ? std::string::npos : dot - i);
		if (!all_digits (n) || n.size () > 3 || atoi (n.c_str ()) > 255) return false;
		parts++;
		if (dot == std::string::npos) break;
		i = dot + 1;
	}
	return parts == 4;
}

// A plausible domain: labels of letters, digits, '-' (UTF-8 too: IDN), and a last one (the TLD) of two
// letters or more, not all digits.
static bool is_domain (const std::string &host)
{
	if (host.empty () || host.front () == '.' || host.back () == '.' || host.find ("..") != std::string::npos)
		return false;
	size_t dot = host.rfind ('.');
	if (dot == std::string::npos) return false;
	for (unsigned char c : host)
		if (!(isalnum (c) || c == '-' || c == '.' || c >= 0x80)) return false;
	std::string tld = host.substr (dot + 1);
	if (tld.size () < 2) return false;
	for (unsigned char c : tld)
		if (!(isalpha (c) || c >= 0x80 || (c == '-' && tld.compare (0, 4, "xn--") == 0) || (isdigit (c) && tld.compare (0, 4, "xn--") == 0)))
			return false;
	return true;
}

// What the user typed, made a URL (the address bar's rules):
//   a scheme (http:, https:, file:, about:, data:, javascript:, blob:, view-source:...) -> as it is;
//   a path (/x) or a Korin drive (/x, /tmp) -> a file;
//   no space, a domain with a plausible TLD (example.com, fr.wikipedia.org/x) -> https://;
//   localhost[:port][/...], an IPv4 address[:port][/...] -> http://;
//   anything else (spaces, no dot, a '?' first...) -> a search with the chosen engine.
std::string engine_typed_url (const char *in)
{
	std::string s = in ? in : "";
	while (!s.empty () && (s.back () == ' ' || s.back () == '\t')) s.pop_back ();
	size_t b = s.find_first_not_of (" \t");
	s = b == std::string::npos ? std::string () : s.substr (b);
	if (s.empty ()) return "about:blank";
	bool search = s[0] == '?';
	if (search) {
		s = s.substr (1);
		size_t k = s.find_first_not_of (" \t");
		s = k == std::string::npos ? std::string () : s.substr (k);
	} else if (s.find (' ') == std::string::npos) {
		if (s[0] == '/') return "file://" + s;
		size_t colon = s.find (':');
		if (colon != std::string::npos && colon > 0) {
			std::string scheme = s.substr (0, colon);
			bool alpha = isalpha ((unsigned char) scheme[0]);
			for (unsigned char c : scheme)
				if (!(isalnum (c) || c == '+' || c == '-' || c == '.')) alpha = false;
			// /x -> its path (Korin: a drive of 2-4 letters and digits, uppercase)
			bool drive = colon >= 2 && colon <= 4 && (s.size () == colon + 1 || s[colon + 1] == '/');
			for (unsigned char c : scheme)
				if (!(isupper (c) || isdigit (c))) drive = false;
			if (drive)
				return "file://" + s.substr (colon + 1);
			// a scheme: "x://", or one of those without slashes (about:, data:, mailto:...)
			std::string low;
			for (unsigned char c : scheme) low += (char) tolower (c);
			if (alpha && (s.compare (colon, 3, "://") == 0 || low == "about" || low == "data" || low == "javascript"
				      || low == "blob" || low == "mailto" || low == "view-source" || low == "file"))
				return s;
		}
		// host[:port][/path][?query][#frag]
		size_t end = s.find_first_of ("/?#");
		std::string hostport = s.substr (0, end);
		std::string host = hostport, port;
		size_t pc = hostport.rfind (':');
		if (pc != std::string::npos) {
			host = hostport.substr (0, pc);
			port = hostport.substr (pc + 1);
		}
		bool portOk = pc == std::string::npos || (all_digits (port) && port.size () <= 5);
		std::string lowHost;
		for (unsigned char c : host) lowHost += (char) tolower (c);
		if (portOk && (lowHost == "localhost" || is_ipv4 (host)))
			return "http://" + s;
		if (portOk && is_domain (host))
			return "https://" + s;
	}
	if (s.empty ()) return "about:blank";
	std::string q;
	for (unsigned char c : s) {
		if (isalnum (c) || c == '-' || c == '_' || c == '.' || c == '~') q += (char) c;
		else if (c == ' ') q += '+';
		else { char h[4]; snprintf (h, sizeof h, "%%%02X", c); q += h; }
	}
	return s_search + q;
}

void engine_load (const char *typed)
{
	if (!s_cur) return;
	std::string url = engine_typed_url (typed);
	say ("tab %d: load %s", s_cur->id, brief (url).c_str ());
	if (!s_linkHandler) apply_site_user_agent (s_cur, url);	// (the request is made with it)
	WKURLRef u = WKURLCreateWithUTF8CString (url.c_str ());
	WKPageLoadURL (s_page, u);
	WKRelease (u);
}

void engine_back () { if (s_page && WKPageCanGoBack (s_page)) WKPageGoBack (s_page); }
void engine_forward () { if (s_page && WKPageCanGoForward (s_page)) WKPageGoForward (s_page); }
void engine_reload () { if (s_page) WKPageReload (s_page); }
void engine_stop () { if (s_page) WKPageStopLoading (s_page); }

void engine_zoom (double f)
{
	if (!s_cur) return;
	s_cur->zoom = f < 0.3 ? 0.3 : f > 3 ? 3 : f;
	WKPageSetPageZoomFactor (s_page, s_cur->zoom);
}
double engine_zoom_factor () { return s_cur ? s_cur->zoom : 1; }

void engine_command (const char *name)
{
	if (!s_page) return;
	WKStringRef c = WKStringCreateWithUTF8CString (name);
	WKPageExecuteCommand (s_page, c);
	WKRelease (c);
}

// ---- input ---------------------------------------------------------------------------------------------

static uint32_t uk_mods (unsigned m)
{
	uint32_t r = 0;
	if (m & MOD_SHIFT) r |= kWKEventModifiersShiftKey;
	if (m & MOD_CTRL) r |= kWKEventModifiersControlKey;
	if (m & MOD_ALT) r |= kWKEventModifiersAltKey;
	return r;
}

static int s_buttons;
void engine_mouse (int x, int y, int buttons, int changed, unsigned mods)
{
	if (!s_page) return;
	// a press of the middle button, or of the left one with Ctrl: the link it clicks opens a new tab (decideAction)
	if (changed && (buttons & changed) && ((changed & 4) || ((changed & 1) && (mods & MOD_CTRL))))
		s_cur->newTabClick = now ();
	WKEventType type = kWKEventMouseMove;
	WKEventMouseButton button = kWKEventMouseButtonNoButton;
	if (changed) {
		type = (buttons & changed) ? kWKEventMouseDown : kWKEventMouseUp;
		button = (changed & 1) ? kWKEventMouseButtonLeftButton : (changed & 2) ? kWKEventMouseButtonRightButton : kWKEventMouseButtonMiddleButton;
	}
	uint32_t held = ((buttons & 1) ? kWKEventMouseButtonsLeft : 0) | ((buttons & 2) ? kWKEventMouseButtonsRight : 0)
		| ((buttons & 4) ? kWKEventMouseButtonsMiddle : 0);
	s_buttons = buttons;
	WKPoint p = WKPointMake (x, y);
	WKPageHandleMouseEvent (s_page, WKMouseEventMake (type, button, held, p, p, 0, uk_mods (mods)));
}

void engine_mouse_leave ()
{
	if (s_client && s_client->statusText) s_client->statusText ("");
}

void engine_wheel (int x, int y, int notches, unsigned mods)
{
	if (!s_page || !notches) return;
	WKPoint p = WKPointMake (x, y);
	WKWheelEvent e = WKWheelEventMake (p, p, WKSizeMake (0, notches * 40.0 * 3), WKSizeMake (0, notches), uk_mods (mods));
	WKPageHandleWheelEvent (s_page, e);
}

// A key code (kapi's) -> Windows' virtual key code, the text it types (UTF-8, "" none).
static int vk_of (long k, unsigned mods, char *text)
{
	text[0] = 0;
	if ((mods & MOD_CTRL) && k >= 1 && k <= 26) return 'A' + (int) k - 1;	// Ctrl+letter (^A = 1)
	switch (k) {
	case KEY_BACKSPACE: return 0x08;
	case KEY_TAB: strcpy (text, "\t"); return 0x09;
	case KEY_ENTER: strcpy (text, "\r"); return 0x0D;
	case 27: return 0x1B;
	case KEY_PGUP: return 0x21;
	case KEY_PGDN: return 0x22;
	case KEY_END: return 0x23;
	case KEY_HOME: return 0x24;
	case KEY_LEFT: return 0x25;
	case KEY_UP: return 0x26;
	case KEY_RIGHT: return 0x27;
	case KEY_DOWN: return 0x28;
	case KEY_DEL: return 0x2E;
	}
	if (k >= KEY_F1 && k <= KEY_F12) return 0x70 + (int) (k - KEY_F1);
	// A character: its UTF-8 (Latin-1 0xA0..0xFF, 0x80 the euro sign: the keymaps' codes).
	unsigned cp = k >= 32 && k <= 126 ? (unsigned) k : k >= 0xA0 && k <= 0xFFFF && (k < 0xD800 || k > 0xDFFF) ? (unsigned) k
		: k == 0x80 ? 0x20AC : 0;					// (ARMPI: the whole BMP)
	if (!cp) return 0;
	if (cp < 0x80) { text[0] = (char) cp; text[1] = 0; }
	else if (cp < 0x800) { text[0] = (char) (0xC0 | cp >> 6); text[1] = (char) (0x80 | (cp & 0x3F)); text[2] = 0; }
	else { text[0] = (char) (0xE0 | cp >> 12); text[1] = (char) (0x80 | ((cp >> 6) & 0x3F)); text[2] = (char) (0x80 | (cp & 0x3F)); text[3] = 0; }
	if (cp >= 'a' && cp <= 'z') return 'A' + (int) (cp - 'a');
	if ((cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == ' ') return (int) cp;
	switch (cp) {
	case ';': case ':': return 0xBA;
	case '=': case '+': return 0xBB;
	case ',': case '<': return 0xBC;
	case '-': case '_': return 0xBD;
	case '.': case '>': return 0xBE;
	case '/': case '?': return 0xBF;
	case '`': case '~': return 0xC0;
	case '[': case '{': return 0xDB;
	case '\\': case '|': return 0xDC;
	case ']': case '}': return 0xDD;
	case '\'': case '"': return 0xDE;
	}
	return 0xE5;	// (VK_PROCESSKEY: a character with no key of its own -- its text is what counts)
}

void engine_key (long k, unsigned mods)
{
	if (!s_page) return;
	char text[8];
	int vk = vk_of (k, mods, text);
	if (!vk) return;
	uint32_t m = uk_mods (mods);
	// A character typed with AltGr (@, #, {, the euro sign on a French or Belgian keyboard): the keymap
	// has already made the character, and Alt (with or without Ctrl) is still held -- sent with those,
	// WebKit takes it for a shortcut and types nothing. A character goes as itself.
	if (text[0] && text[0] != '\t' && text[0] != '\r' && (mods & MOD_ALT))
		m &= ~(uint32_t) (kWKEventModifiersAltKey | kWKEventModifiersControlKey);
	WKPageHandleKeyboardEvent (s_page, WKKeyboardEventMake (kWKEventKeyDown, vk, text, m, kWKKeyboardEventFlagsNone));
	WKPageHandleKeyboardEvent (s_page, WKKeyboardEventMake (kWKEventKeyUp, vk, "", m, kWKKeyboardEventFlagsNone));
}

// The page did not use a key: back to the window (its shortcuts), as the code it came as.
static void keyNotHandled (WKViewRef, WKKeyboardEvent e, const void *ci)
{
	if (!shown (T (ci)) || e.type != kWKEventKeyDown || !s_client->keyNotHandled) return;
	unsigned mods = ((e.modifiers & kWKEventModifiersShiftKey) ? MOD_SHIFT : 0) | ((e.modifiers & kWKEventModifiersControlKey) ? MOD_CTRL : 0)
		| ((e.modifiers & kWKEventModifiersAltKey) ? MOD_ALT : 0);
	long k = 0;
	switch (e.virtualKeyCode) {
	case 0x25: k = KEY_LEFT; break;
	case 0x27: k = KEY_RIGHT; break;
	case 0x26: k = KEY_UP; break;
	case 0x28: k = KEY_DOWN; break;
	case 0x24: k = KEY_HOME; break;
	case 0x23: k = KEY_END; break;
	case 0x21: k = KEY_PGUP; break;
	case 0x22: k = KEY_PGDN; break;
	case 0x08: k = KEY_BACKSPACE; break;
	case 0x1B: k = 27; break;
	default:
		if (e.virtualKeyCode >= 0x70 && e.virtualKeyCode <= 0x7B) k = KEY_F1 + (e.virtualKeyCode - 0x70);
		else if ((mods & MOD_CTRL) && e.virtualKeyCode >= 'A' && e.virtualKeyCode <= 'Z') k = e.virtualKeyCode - 'A' + 1;
	}
	if (k) s_client->keyNotHandled (k, mods);
}
