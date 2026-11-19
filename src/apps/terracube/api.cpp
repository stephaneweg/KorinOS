// api.cpp -- TerraCube : le protocole des scripts (Python, BASIC, tout programme qui ouvre une connexion TCP).
//
// Une connexion TCP sur le port 4730 ; une commande par ligne, du texte : « nom(arguments) », les arguments séparés
// par des virgules ; un bloc se donne par son numéro ou son nom anglais (« stone »). Les commandes qui demandent
// quelque chose (get…, events…, ping) ont une réponse d'une ligne ; les autres n'en ont pas, et une erreur s'affiche
// dans la conversation du jeu (« script : … »). La liste est dans docs/TERRACUBE.md et le README de l'app.
//
//   world.getBlock(x,y,z)                       -> numéro
//   world.setBlock(x,y,z,bloc)
//   world.setBlocks(x1,y1,z1,x2,y2,z2,bloc)     un pavé plein (1 000 000 de blocs au plus)
//   world.getBlocks(x1,y1,z1,x2,y2,z2)          -> numéros séparés par des virgules (y, puis z, puis x)
//   world.getHeight(x,z)                        -> y du plus haut bloc non vide
//   world.getBlockTypes()                       -> « 0=air,1=stone,… »
//   world.getTime() / world.setTime(heure)      heure de 0 à 24
//   world.save()
//   player.getPos() / player.setPos(x,y,z)      les pieds, en blocs (réels)
//   player.getTile() / player.setTile(x,y,z)    le bloc des pieds (entiers)
//   player.getRotation() / player.setRotation(yaw)    degrés, 0 vers le nord (-z), 90 vers l'est (+x)
//   player.getPitch() / player.setPitch(pitch)        degrés, positif vers le haut
//   player.getDirection()                       -> dx,dy,dz (vecteur unité du regard)
//   player.setFly(0|1)
//   chat.post(texte)
//   events.blocks()                             -> « genre,x,y,z,face,bloc|… » (1 cassé, 2 posé, 3 touché) ; vidés
//   events.clear()
//   game.info() / ping()
//
// Chaque connexion a son thread ; ses commandes vont dans une file que la boucle du jeu vide entre deux images
// (apiPump, dans un temps donné) : un script qui pose des milliers de blocs n'attend pas une image par bloc. Une
// commande à réponse attend la sienne (l'ordre est gardé : la file est vidée dans l'ordre d'arrivée).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "game.h"

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <atomic>
#include <condition_variable>
#include <mutex>

#include "render.h"

namespace tc {

// ---- les opérations -----------------------------------------------------------------------------------------------

std::string blockList ()
{
	std::string s;
	for (int i = 0; i < BLOCK_COUNT; i++) {
		if (i) s += ',';
		s += std::to_string (i) + "=" + BLOCKS[i].name;
	}
	return s;
}

std::string opFill (int x1, int y1, int z1, int x2, int y2, int z2, int block, long *count, bool hollow)
{
	if (block < 0 || block >= BLOCK_COUNT) return "unknown block";
	if (x1 > x2) std::swap (x1, x2);
	if (y1 > y2) std::swap (y1, y2);
	if (z1 > z2) std::swap (z1, z2);
	if (y1 < 0) y1 = 0;
	if (y2 >= CH) y2 = CH - 1;
	if (y1 > y2) return "outside the world (y from 0 to 127)";
	double vol = (double) (x2 - x1 + 1) * (y2 - y1 + 1) * (z2 - z1 + 1);
	if (vol > 1000000) return "too many blocks (1 000 000 at most at once)";
	long n = 0;
	for (int y = y1; y <= y2; y++)
		for (int z = z1; z <= z2; z++)
			for (int x = x1; x <= x2; x++) {
				if (hollow && x != x1 && x != x2 && y != y1 && y != y2 && z != z1 && z != z2) continue;
				if (g.world.set (x, y, z, block)) n++;
			}
	g.edited = true;
	if (count) *count = n;
	return "";
}

namespace {

// « nom(a,b,c) » -> nom, arguments (espaces retirés autour) ; le texte de chat.post est pris tel quel
bool parse (const std::string &line, std::string &name, std::vector<std::string> &args, std::string &raw)
{
	size_t p = line.find ('(');
	std::string l = line;
	while (!l.empty () && (l.back () == '\r' || l.back () == '\n' || l.back () == ' ')) l.pop_back ();
	if (p == std::string::npos) {
		name = l;
		raw.clear ();
	} else {
		name = l.substr (0, p);
		size_t e = l.rfind (')');
		raw = e != std::string::npos && e > p ? l.substr (p + 1, e - p - 1) : l.substr (p + 1);
	}
	while (!name.empty () && name[0] == ' ') name.erase (0, 1);
	while (!name.empty () && name.back () == ' ') name.pop_back ();
	args.clear ();
	if (!raw.empty ()) {
		size_t s = 0;
		for (;;) {
			size_t c = raw.find (',', s);
			std::string a = raw.substr (s, c == std::string::npos ? std::string::npos : c - s);
			while (!a.empty () && (a[0] == ' ' || a[0] == '\t')) a.erase (0, 1);
			while (!a.empty () && (a.back () == ' ' || a.back () == '\t')) a.pop_back ();
			if (a.size () >= 2 && (a[0] == '"' || a[0] == '\'') && a.back () == a[0]) a = a.substr (1, a.size () - 2);
			args.push_back (a);
			if (c == std::string::npos) break;
			s = c + 1;
		}
	}
	return !name.empty ();
}

struct ArgErr {
	std::string msg;
};

double num (const std::vector<std::string> &a, size_t i)
{
	if (i >= a.size ()) throw ArgErr { "missing argument " + std::to_string (i + 1) };
	char *e;
	double v = strtod (a[i].c_str (), &e);
	if (e == a[i].c_str () || *e) throw ArgErr { "not a number: " + a[i] };
	return v;
}
int inum (const std::vector<std::string> &a, size_t i) { return (int) floor (num (a, i)); }
int blk (const std::vector<std::string> &a, size_t i)
{
	if (i >= a.size ()) throw ArgErr { "missing block" };
	int b = blockByName (a[i].c_str ());
	if (b < 0) throw ArgErr { "unknown block: " + a[i] };
	return b;
}

std::string fmt (double v)
{
	char t[32];
	snprintf (t, sizeof t, "%.3f", v);
	// (sans zéros inutiles)
	char *e = t + strlen (t) - 1;
	while (e > t && *e == '0') *e-- = 0;
	if (*e == '.') *e = 0;
	return t;
}

const char *const REPLY[] = { "world.getBlock", "world.getBlocks", "world.getHeight", "world.getBlockTypes", "world.getTime",
			      "player.getPos", "player.getTile", "player.getRotation", "player.getPitch", "player.getDirection",
			      "events.blocks", "game.info", "ping", "world.getBlockWithData", "world.save" };

} // namespace

bool apiWantsReply (const std::string &line)
{
	size_t p = line.find ('(');
	std::string n = line.substr (0, p);
	while (!n.empty () && (n.back () == ' ' || n.back () == '\r' || n.back () == '\n')) n.pop_back ();
	while (!n.empty () && n[0] == ' ') n.erase (0, 1);
	for (const char *r : REPLY)
		if (!strcasecmp (n.c_str (), r)) return true;
	return false;
}

std::string apiExec (const std::string &line, bool &reply)
{
	std::string name, raw;
	std::vector<std::string> a;
	reply = apiWantsReply (line);
	if (!parse (line, name, a, raw)) return "";
	g.apiCommands++;
	const char *n = name.c_str ();
	auto is = [&] (const char *s) { return !strcasecmp (n, s); };
	try {
		if (is ("world.setBlock")) {
			int x = inum (a, 0), y = inum (a, 1), z = inum (a, 2), b = blk (a, 3);
			if (!g.world.set (x, y, z, b)) return "error: outside the world (y from 0 to 127)";
			g.edited = true;
			return "";
		}
		if (is ("world.getBlock") || is ("world.getBlockWithData")) {
			int b = g.world.getLoaded (inum (a, 0), inum (a, 1), inum (a, 2));
			return is ("world.getBlock") ? std::to_string (b) : std::to_string (b) + ",0";
		}
		if (is ("world.setBlocks")) {
			long cnt;
			std::string e = opFill (inum (a, 0), inum (a, 1), inum (a, 2), inum (a, 3), inum (a, 4), inum (a, 5), blk (a, 6), &cnt);
			return e.empty () ? "" : "error: " + e;
		}
		if (is ("world.getBlocks")) {
			int x1 = inum (a, 0), y1 = inum (a, 1), z1 = inum (a, 2), x2 = inum (a, 3), y2 = inum (a, 4), z2 = inum (a, 5);
			if (x1 > x2) std::swap (x1, x2);
			if (y1 > y2) std::swap (y1, y2);
			if (z1 > z2) std::swap (z1, z2);
			double vol = (double) (x2 - x1 + 1) * (y2 - y1 + 1) * (z2 - z1 + 1);
			if (vol > 262144) return "error: too many blocks (262144 at most)";
			std::string s;
			s.reserve ((size_t) vol * 3);
			for (int y = y1; y <= y2; y++)
				for (int z = z1; z <= z2; z++)
					for (int x = x1; x <= x2; x++) {
						if (!s.empty ()) s += ',';
						s += std::to_string (g.world.getLoaded (x, y, z));
					}
			return s;
		}
		if (is ("world.getHeight")) return std::to_string (g.world.height (inum (a, 0), inum (a, 1)));
		if (is ("world.getBlockTypes")) return blockList ();
		if (is ("world.getTime")) return fmt (g.hour);
		if (is ("world.setTime")) {
			double h = fmod (num (a, 0), 24.0);
			g.hour = (float) (h < 0 ? h + 24 : h);
			if (a.size () > 1) g.timeRuns = num (a, 1) != 0;
			return "";
		}
		if (is ("world.save")) {
			std::string err;
			return gameSave (err) ? "ok" : "error: " + err;
		}
		if (is ("player.getPos")) return fmt (g.pl.x) + "," + fmt (g.pl.y) + "," + fmt (g.pl.z);
		if (is ("player.getTile"))
			return std::to_string ((int) floor (g.pl.x)) + "," + std::to_string ((int) floor (g.pl.y)) + "," + std::to_string ((int) floor (g.pl.z));
		if (is ("player.setPos")) {
			gameTeleport (num (a, 0), num (a, 1), num (a, 2));
			return "";
		}
		if (is ("player.setTile")) {
			gameTeleport (inum (a, 0) + 0.5, inum (a, 1), inum (a, 2) + 0.5);
			return "";
		}
		if (is ("player.getRotation")) return fmt (g.pl.yaw);
		if (is ("player.getPitch")) return fmt (g.pl.pitch);
		if (is ("player.setRotation")) {
			double y = fmod (num (a, 0), 360.0);
			g.pl.yaw = (float) (y < 0 ? y + 360 : y);
			return "";
		}
		if (is ("player.setPitch")) {
			double p = num (a, 0);
			g.pl.pitch = (float) (p > 89.9 ? 89.9 : p < -89.9 ? -89.9 : p);
			return "";
		}
		if (is ("player.getDirection")) {
			float d[3];
			viewDir (g.pl.yaw, g.pl.pitch, d);
			return fmt (d[0]) + "," + fmt (d[1]) + "," + fmt (d[2]);
		}
		if (is ("player.setFly")) {
			g.pl.fly = num (a, 0) != 0;
			gameFlyChanged ();
			return "";
		}
		if (is ("chat.post")) {
			gamePost (raw);
			return "";
		}
		if (is ("events.blocks") || is ("events.block.hits")) {
			std::string s;
			for (auto &e : g.events) {
				if (!s.empty ()) s += '|';
				s += std::to_string (e.kind) + "," + std::to_string (e.x) + "," + std::to_string (e.y) + "," + std::to_string (e.z) + "," +
				     std::to_string (e.face) + "," + std::to_string (e.block);
			}
			g.events.clear ();
			return s;
		}
		if (is ("events.clear")) {
			g.events.clear ();
			return "";
		}
		if (is ("game.info"))
			return std::string ("TerraCube ") + VERSION + "," + g.world.name + "," + std::to_string (g.world.seed);
		if (is ("ping")) return "pong";
		return reply ? "error: unknown command: " + name : "";
	} catch (const ArgErr &e) {
		if (!reply) gamePost ("script : " + name + " : " + e.msg);
		return "error: " + e.msg;
	}
}

// ---- le serveur ---------------------------------------------------------------------------------------------------

namespace {

struct Job {
	std::string line;
	std::string out;
	bool reply = false, done = false;
};

std::mutex g_mu;
std::condition_variable g_cv;
std::deque<Job *> g_queue;
const size_t QUEUE_MAX = 200000;
std::atomic<bool> g_run { false }, g_stop { false };
std::atomic<int> g_clients { 0 };
int g_listen = -1;
pthread_t g_thread;

bool sendAll (int fd, const char *p, size_t n)
{
	while (n) {
		ssize_t k = send (fd, p, n, 0);
		if (k <= 0) {
			if (k < 0 && errno == EINTR) continue;
			return false;
		}
		p += k, n -= (size_t) k;
	}
	return true;
}

// une connexion : des lignes, jusqu'à la fermeture
void *client (void *arg)
{
	int fd = (int) (long) arg;
	std::string buf;
	char tmp[8192];
	std::vector<Job *> mine;	// (les commandes sans réponse confiées : libérées par la boucle)
	while (!g_stop) {
		struct pollfd p = { fd, POLLIN, 0 };
		int r = poll (&p, 1, 250);
		if (r < 0 && errno != EINTR) break;
		if (r <= 0) continue;
		ssize_t k = recv (fd, tmp, sizeof tmp, 0);
		if (k <= 0) break;
		buf.append (tmp, (size_t) k);
		if (buf.size () > (1 << 20) && buf.find ('\n') == std::string::npos) break;	// (une ligne sans fin)
		size_t nl;
		bool dead = false;
		while ((nl = buf.find ('\n')) != std::string::npos) {
			std::string line = buf.substr (0, nl);
			buf.erase (0, nl + 1);
			if (line.empty () || line == "\r") continue;
			Job *j = new Job ();
			j->line = line;
			j->reply = apiWantsReply (line);
			std::unique_lock<std::mutex> l (g_mu);
			g_cv.wait (l, [] { return g_queue.size () < QUEUE_MAX || g_stop; });	// (trop d'avance : on attend)
			if (g_stop) {
				delete j;
				dead = true;
				break;
			}
			g_queue.push_back (j);
			if (!j->reply) continue;	// (la boucle la libère)
			g_cv.wait (l, [&] { return j->done || g_stop; });
			std::string out = j->done ? j->out : "error: TerraCube is closing";
			if (j->done) delete j;		// (sinon la boucle ne la touchera plus : laissée)
			l.unlock ();
			out += '\n';
			if (!sendAll (fd, out.data (), out.size ())) {
				dead = true;
				break;
			}
		}
		if (dead) break;
	}
	close (fd);
	g_clients--;
	return nullptr;
}

void *acceptLoop (void *)
{
	while (!g_stop) {
		struct pollfd p = { g_listen, POLLIN, 0 };
		if (poll (&p, 1, 250) <= 0) continue;
		struct sockaddr_in a;
		socklen_t al = sizeof a;
		int fd = accept (g_listen, (struct sockaddr *) &a, &al);
		if (fd < 0) continue;
		if (g_clients >= 16) {
			const char *m = "error: too many scripts connected\n";
			sendAll (fd, m, strlen (m));
			close (fd);
			continue;
		}
		int one = 1;
		setsockopt (fd, IPPROTO_TCP, 1 /* TCP_NODELAY */, &one, sizeof one);
		pthread_t t;
		g_clients++;
		if (pthread_create (&t, nullptr, client, (void *) (long) fd) != 0) {
			g_clients--;
			close (fd);
			continue;
		}
		pthread_detach (t);
	}
	return nullptr;
}

} // namespace

bool apiStart (int port, bool anyAddress, std::string &err)
{
	if (g_run) return true;
	int s = socket (AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		err = "socket : " + std::string (strerror (errno));
		return false;
	}
	int one = 1;
	setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	struct sockaddr_in a;
	memset (&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons ((unsigned short) port);
	a.sin_addr.s_addr = htonl (anyAddress ? INADDR_ANY : INADDR_LOOPBACK);
	if (bind (s, (struct sockaddr *) &a, sizeof a) < 0 || listen (s, 8) < 0) {
		err = "port " + std::to_string (port) + " : " + strerror (errno);
		close (s);
		return false;
	}
	g_listen = s;
	g_stop = false;
	if (pthread_create (&g_thread, nullptr, acceptLoop, nullptr) != 0) {
		err = "thread";
		close (s);
		g_listen = -1;
		return false;
	}
	g_run = true;
	return true;
}

void apiStop ()
{
	if (!g_run) return;
	{
		std::lock_guard<std::mutex> l (g_mu);
		g_stop = true;
		// les commandes encore en file : celles sans réponse sont libérées, les autres rendues à leur thread
		for (Job *j : g_queue)
			if (!j->reply) delete j;
		g_queue.clear ();
	}
	g_cv.notify_all ();
	pthread_join (g_thread, nullptr);
	close (g_listen);
	g_listen = -1;
	g_run = false;
	// (les threads des connexions finissent seuls, au plus 250 ms après)
}

bool apiRunning () { return g_run; }
int apiClients () { return g_clients; }

void apiPump (double budgetMs)
{
	if (!g_run) return;
	double t0 = nowMs ();
	for (int n = 0;; n++) {
		Job *j;
		{
			std::lock_guard<std::mutex> l (g_mu);
			if (g_queue.empty ()) break;
			j = g_queue.front ();
			g_queue.pop_front ();
		}
		bool reply;
		std::string out = apiExec (j->line, reply);
		if (j->reply) {
			std::lock_guard<std::mutex> l (g_mu);
			j->out = out;
			j->done = true;
		} else {
			if (out.rfind ("error:", 0) == 0) gamePost ("script : " + out.substr (7));
			delete j;
		}
		g_cv.notify_all ();
		if ((n & 63) == 63 && nowMs () - t0 > budgetMs) break;
	}
}

} // namespace tc
