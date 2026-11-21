#!/usr/bin/env python3
"""pub.py -- publier un seul paquet de Korin OS, a partir de son archive publiee (docs/PAQUETS.md, « Publier un paquet »).

    python tools/pkg/pub.py NOM [--pi] [--tree DOSSIER] [--dry-run] [--yes] [--only MOTIF...] [--add-new] [--drop MOTIF...] [--new] [--abi N]

--new : un paquet absent de l'index publie (pas d'archive de base) : tous ses fichiers sont pris dans l'arbre,
(release = 1 dans packages-pc.ini : son archive va dans la Release),
version de versions.ini ou 1.0.0, section ajoutee a la fin de l'index.

Le chemin sur des publications partielles des 4-20/11 (.agents/memory/armpi-paquets.md), en un outil :
  1. git fetch ; la base est l'index d'origin/main (packages/pc/index.txt, ou packages/index.txt avec --pi) : jamais
     une archive plus ancienne que la derniere publiee ;
  2. l'archive publiee du paquet : packages/pc/pkgs/ (lue dans origin/main) ou, release = 1, la Release « packages »
     (gh release download, copie locale dans build/pkg-release-pc) ; son SHA-256 doit etre celui de l'index ;
  3. son `content` est recalcule (formule de mkrepo.py, dans l'ordre de l'archive, sinon dans celui de mkrepo.plan) et
     doit retomber sur celui de l'index : sinon packages-pc.ini a change depuis (besoins...), arret sauf --force ;
  4. seuls les fichiers de l'archive qui different dans l'arbre construit (build/pc/sysroot ; le demarrage du PC dans
     build/pc/esp) sont remplaces ; res/lang/*.txt est fusionne (les cles publiees sont gardees, celles de l'arbre
     ajoutees ou mises a jour) ; les fichiers que packages-pc.ini donne au paquet mais absents de l'archive ne sont
     ajoutes qu'avec --add-new ; etc/exec.txt et etc/session (qu'une reconstruction brute abime) seulement par --only ;
  5. la liste des changements, puis confirmation (--yes : sans) ;
  6. la version monte (tools/pkg/versions.ini : [pc] d'abord ; une version deja plus haute dans versions.ini est prise),
     l'archive est ecrite par mkrepo.write_opk, son manifeste par mkrepo.manifest_text (meme abi que le publie, sauf
     --abi), la seule section du paquet est reecrite dans l'index, signe (cle : --key, ONYX_PKG_KEY_FILE,
     C:\\Projects\\onyx_pkg-key.pem.txt, ou OnyxPkgKey) ;
  7. checkrepo.py sur une copie du depot (liens) : seules les archives de la Release absentes de la copie locale sont
     tolerees ; --dry-run s'arrete ici, sans rien laisser ;
  8. git merge origin/main, archive envoyee dans la Release (release = 1), puis un commit des seuls fichiers du depot
     (index.txt, index.sig, l'archive, l'ancienne archive retiree, l'icone si elle change) et de la ligne de
     versions.ini (un index git a part : ce que d'autres sessions ont change dans le dossier n'est pas emporte) ;
     git fetch, fusion, push (--no-push : pas pousse).

Python de Windows sans -I (cryptography est dans le site-packages de l'utilisateur).
"""
import argparse, configparser, datetime, hashlib, os, re, shutil, subprocess, sys, tempfile, zipfile

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (os.path.dirname (HERE))
GH_REPO = "stephaneweg/KorinOS"
DEFAULT_KEY = r"C:\Projects\onyx_pkg-key.pem.txt"
GUARDED = { "etc/exec.txt", "etc/session" }	# (PC : une reconstruction brute les abime ; seulement par --only)
TMP = os.path.join (ROOT, "build", "pub-tmp")

def say (*a): print (*a, flush = True)
def die (msg): sys.exit ("pub: " + msg)

def git (*args, check = True, inp = None, env = None):
	r = subprocess.run (["git", "-C", ROOT] + list (args), input = inp, capture_output = True, env = env)
	if check and r.returncode:
		die ("git %s : %s" % (" ".join (args), r.stderr.decode ("utf-8", "replace").strip ()))
	return r

def git_blob (ref, path):
	r = git ("show", "%s:%s" % (ref, path), check = False)
	return r.stdout if r.returncode == 0 else None

def sha256 (data): return hashlib.sha256 (data).hexdigest ()

def read_cfg (text):
	c = configparser.ConfigParser (interpolation = None); c.optionxform = str
	c.read_string (text); return c

# ---- res/lang/*.txt : « cle = valeur », fusionne ----
def lang_merge (old, new):
	"""Les lignes publiees, les valeurs de l'arbre pour les cles connues, les cles nouvelles de l'arbre a la fin."""
	def keyval (l):
		if l.startswith ("#") or " = " not in l: return None
		k, v = l.split (" = ", 1); return k, v
	tree = {}
	order = []
	for l in new.decode ("utf-8").split ("\n"):
		kv = keyval (l)
		if kv and kv[0] not in tree: order.append (kv[0])
		if kv: tree[kv[0]] = kv[1]
	out, seen = [], set ()
	lines = old.decode ("utf-8").split ("\n")
	end = len (lines)
	while end and lines[end - 1] == "": end -= 1
	for l in lines[:end]:
		kv = keyval (l)
		if kv and kv[0] in tree and kv[0] not in seen:
			seen.add (kv[0]); out.append ("%s = %s" % (kv[0], tree[kv[0]]))
		else:
			if kv: seen.add (kv[0])
			out.append (l)
	out += ["%s = %s" % (k, tree[k]) for k in order if k not in seen]
	return ("\n".join (out) + "\n" * max (1, len (lines) - end)).encode ("utf-8")

# ---- versions.ini : une seule ligne changee, le reste du fichier tel quel ----
def versions_edit (text, section, base_section, name, ver):
	lines = text.split ("\n")
	def bounds (sec):
		try: s = lines.index ("[%s]" % sec)
		except ValueError: return None
		e = s + 1
		while e < len (lines) and not lines[e].startswith ("["): e += 1
		return s, e
	def find (sec):
		b = bounds (sec)
		if not b: return None, None
		for i in range (b[0] + 1, b[1]):
			if re.match (r"^%s\s*=" % re.escape (name), lines[i]): return i, lines[i].split ("=", 1)[1].strip ()
		return None, None
	if section != base_section:			# (PC : [pc] ne garde que ce qui differe de [versions])
		_, basev = find (base_section)
		i, _ = find (section)
		if basev == ver:
			if i is not None: del lines[i]
			return "\n".join (lines)
	else:
		i, _ = find (section)
	if i is not None:
		lines[i] = "%s = %s" % (name, ver)
	else:
		b = bounds (section)
		if not b: die ("versions.ini : pas de section [%s]" % section)
		e = b[1]
		while e > b[0] + 1 and lines[e - 1].strip () == "": e -= 1
		lines.insert (e, "%s = %s" % (name, ver))
	return "\n".join (lines)

def versions_get (text, sections, name):
	c = read_cfg (text)
	for s in sections:
		if c.has_section (s) and c[s].get (name): return c[s][name]
	return None

# ---- la cle ----
def key_file (opt):
	for k in (opt, os.environ.get ("ONYX_PKG_KEY_FILE"), DEFAULT_KEY):
		if k and os.path.isfile (k): return k, None
	v = os.environ.get ("OnyxPkgKey") or os.environ.get ("ONYX_PKG_KEY")
	if v:	# (la cle PEM elle-meme, parfois sur une ligne : remise en PEM dans un temporaire, rien n'est affiche)
		import textwrap
		m = re.search (r"-+BEGIN ([A-Z ]+?)-+\s*(.*?)\s*-+END", v, re.S)
		if not m: die ("OnyxPkgKey (ONYX_PKG_KEY) n'est pas une cle PEM")
		h, b = m.group (1), re.sub (r"\s", "", m.group (2))
		fd, path = tempfile.mkstemp (suffix = ".pem")
		with os.fdopen (fd, "w") as f: f.write ("-----BEGIN %s-----\n%s\n-----END %s-----\n" % (h, "\n".join (textwrap.wrap (b, 64)), h))
		return path, path
	return None, None

# ---- la Release « packages » : par gh, ou (--api, le cloud) par l'API REST de GitHub avec curl, la connexion
# GitHub de la session etant appliquee par le proxy (pas de jeton ici)
API = False

def api_json (url):
	r = subprocess.run (["curl", "-sS", "-f", "-H", "Accept: application/vnd.github+json", url], capture_output = True)
	if r.returncode: die ("API GitHub : %s : %s" % (url, r.stderr.decode ("utf-8", "replace").strip ()))
	import json
	return json.loads (r.stdout)

def rel_info ():
	return api_json ("https://api.github.com/repos/%s/releases/tags/packages" % GH_REPO)

def rel_assets ():
	"""-> les noms des archives de la Release"""
	if API: return [x["name"] for x in rel_info ()["assets"]]
	r = subprocess.run (["gh", "release", "view", "packages", "-R", GH_REPO, "--json", "assets", "-q", ".assets[].name"], capture_output = True, text = True)
	if r.returncode: die ("gh release view : " + r.stderr.strip ())
	return r.stdout.split ()

def rel_download (fname, dest_dir):
	os.makedirs (dest_dir, exist_ok = True)
	if not API:
		r = subprocess.run (["gh", "release", "download", "packages", "-R", GH_REPO, "-p", fname, "-D", dest_dir, "--clobber"])
		if r.returncode: die ("gh release download a echoue")
		return
	ids = [x["id"] for x in rel_info ()["assets"] if x["name"] == fname]
	if not ids: die ("%s n'est pas dans la Release" % fname)
	r = subprocess.run (["curl", "-sS", "-f", "-L", "-H", "Accept: application/octet-stream", "-o", os.path.join (dest_dir, fname),
		"https://api.github.com/repos/%s/releases/assets/%d" % (GH_REPO, ids[0])])
	if r.returncode: die ("telechargement de %s par l'API a echoue" % fname)

MANUAL = False

def rel_wait (path):
	"""(--manual-upload) l'archive deposee a la main dans la Release : attendue (la liste lue par l'API toutes les
	20 s), telechargee et comparee octet pour octet avant d'aller plus loin (l'index n'est pousse qu'apres)"""
	import time
	name, want = os.path.basename (path), sha256 (open (path, "rb").read ())
	say ("pub: deposer %s (%d octets, SHA-256 %s) dans la Release « packages » :" % (path, os.path.getsize (path), want))
	say ("pub:   https://github.com/%s/releases/edit/packages  (glisser le fichier, puis « Update release »)" % GH_REPO)
	for _ in range (int (os.environ.get ("PUB_WAIT_MIN", "180")) * 3):
		got = [x for x in rel_info ()["assets"] if x["name"] == name]
		if got and got[0].get ("state", "uploaded") == "uploaded" and got[0]["size"] == os.path.getsize (path):
			d = tempfile.mkdtemp ()
			rel_download (name, d)
			ok = sha256 (open (os.path.join (d, name), "rb").read ()) == want
			shutil.rmtree (d, ignore_errors = True)
			if not ok: die ("%s dans la Release n'a pas le SHA-256 attendu : rien n'est pousse" % name)
			say ("pub: %s est dans la Release, SHA-256 verifie" % name)
			return
		time.sleep (20)
	die ("%s n'est pas apparue dans la Release : rien n'est pousse" % name)

def rel_upload (path):
	if MANUAL: return rel_wait (path)
	if not API:
		if subprocess.run (["gh", "release", "upload", "packages", "-R", GH_REPO, path]).returncode: die ("gh release upload a echoue")
		return
	rid = rel_info ()["id"]
	r = subprocess.run (["curl", "-sS", "-f", "-X", "POST", "-H", "Accept: application/vnd.github+json", "-H", "Content-Type: application/octet-stream",
		"--data-binary", "@" + path, "https://uploads.github.com/repos/%s/releases/%d/assets?name=%s" % (GH_REPO, rid, os.path.basename (path))],
		capture_output = True)
	if r.returncode: die ("envoi de %s par l'API a echoue : %s" % (os.path.basename (path), r.stderr.decode ("utf-8", "replace").strip ()))
	if rel_assets ().count (os.path.basename (path)) != 1: die ("%s n'apparait pas dans la Release apres l'envoi" % os.path.basename (path))

def link_or_copy (src, dst):
	os.makedirs (os.path.dirname (dst), exist_ok = True)
	try: os.link (src, dst)
	except OSError: shutil.copyfile (src, dst)

def main ():
	if hasattr (sys.stdout, "reconfigure"): sys.stdout.reconfigure (encoding = "utf-8", errors = "replace")
	ap = argparse.ArgumentParser (description = "Publier un paquet de Korin OS a partir de son archive publiee (docs/PAQUETS.md).")
	ap.add_argument ("name", help = "le paquet (une app, un service, pc, python...)")
	ap.add_argument ("--pi", action = "store_true", help = "le depot du Pi (packages/, packages.ini, build/sdroot)")
	ap.add_argument ("--tree", help = "l'arbre construit (defaut : build/pc/sysroot ; Pi : build/sdroot)")
	ap.add_argument ("--boot", help = "le demarrage (defaut PC : build/pc/esp ; Pi : build/pkgboot, fait par publish.sh)")
	ap.add_argument ("--dry-run", action = "store_true", help = "tout preparer et verifier, ne rien ecrire ni publier")
	ap.add_argument ("--yes", action = "store_true", help = "sans demander confirmation")
	ap.add_argument ("--only", nargs = "+", default = [], help = "remplacer seulement ces fichiers (motifs de packages.ini)")
	ap.add_argument ("--add-new", action = "store_true", help = "ajouter les fichiers du paquet absents de l'archive")
	ap.add_argument ("--drop", nargs = "+", default = [], help = "retirer de l'archive les fichiers de ces motifs (que packages.ini ne donne plus au paquet)")
	ap.add_argument ("--new", action = "store_true", help = "un paquet absent de l'index publie : tous ses fichiers, pris dans l'arbre")
	ap.add_argument ("--abi", type = int, help = "le besoin abi >= N du manifeste (defaut : celui du publie)")
	ap.add_argument ("--force", action = "store_true", help = "continuer si le content publie ne se recalcule pas")
	ap.add_argument ("--key", help = "la cle privee (defaut : ONYX_PKG_KEY_FILE, %s, OnyxPkgKey)" % DEFAULT_KEY)
	ap.add_argument ("--base", default = "origin/main", help = "la reference git du depot publie (defaut : origin/main)")
	ap.add_argument ("--no-fetch", action = "store_true", help = "ne pas faire git fetch au debut")
	ap.add_argument ("--no-push", action = "store_true", help = "commiter sans pousser")
	ap.add_argument ("--api", action = "store_true", help = "la Release par l'API REST de GitHub (curl) au lieu de gh : le cloud")
	ap.add_argument ("--manual-upload", action = "store_true", help = "avec --api : l'archive de la Release deposee a la main (le proxy du cloud refuse les envois binaires) ; attendue et verifiee avant le push")
	ap.add_argument ("--message", default = "", help = "ce qui change, ajoute au titre du commit")
	a = ap.parse_args ()
	global API, MANUAL
	API = a.api or a.manual_upload
	MANUAL = a.manual_upload

	pc = not a.pi
	if pc: os.environ["ARCH"] = "x86_64"
	else: os.environ.pop ("ARCH", None); os.environ.pop ("BOARD", None)
	sys.path.insert (0, HERE)
	import mkrepo					# (lit ARCH a l'import)
	repo_rel = "packages/pc" if pc else "packages"
	ini = os.path.join (HERE, "packages-pc.ini" if pc else "packages.ini")
	tree = os.path.abspath (a.tree or os.path.join (ROOT, "build", "pc", "sysroot") if pc else a.tree or os.path.join (ROOT, "build", "sdroot"))
	boot = a.boot or os.path.join (ROOT, "build", "pc", "esp") if pc else a.boot or os.path.join (ROOT, "build", "pkgboot")
	release_dir = os.path.join (ROOT, "build", "pkg-release-pc" if pc else "pkg-release")
	vsection, vbase = ("pc", "versions") if pc else ("versions", "versions")
	name = a.name
	if not os.path.isdir (tree): die ("pas d'arbre construit : %s" % tree)

	# ---- 1. la base : l'index publie ----
	if not a.no_fetch:
		say ("pub: git fetch")
		git ("fetch", "-q", "origin")
	idx_bytes = git_blob (a.base, repo_rel + "/index.txt")
	if idx_bytes is None: die ("pas de %s/index.txt dans %s" % (repo_rel, a.base))
	idx_text = idx_bytes.decode ("utf-8")
	idx = read_cfg (idx_text)
	is_new = not idx.has_section (name)
	if is_new and not a.new:
		die ("%s n'est pas dans l'index publie (%s) : --new pour un nouveau paquet (ou publish.sh)" % (name, repo_rel))
	if a.new and not is_new:
		die ("%s est deja dans l'index publie (%s) : sans --new" % (name, repo_rel))
	o = {} if is_new else idx[name]
	if o.get ("arch") == "any": die ("%s est l'archive du Pi citee par le PC (arch = any) : publier avec --pi" % name)
	if o.get ("part"): say ("pub: note : %s est une partie de %s" % (name, o["part"]))
	oldver, oldfile = (None, None) if is_new else (o["version"], o["file"])
	is_release = not is_new and "://" in oldfile

	# ---- 2. l'archive publiee (un nouveau paquet : aucune) ----
	if is_new:
		base_bytes = None
	elif is_release:
		local = os.path.join (release_dir, os.path.basename (oldfile))
		if not (os.path.isfile (local) and mkrepo.sha256_file (local) == o["sha256"]):
			say ("pub: %s release download %s" % ("API" if API else "gh", os.path.basename (oldfile)))
			rel_download (os.path.basename (oldfile), release_dir)
		base_bytes = open (local, "rb").read ()
	else:
		base_bytes = git_blob (a.base, repo_rel + "/" + oldfile)
		if base_bytes is None: die ("%s absente de %s" % (oldfile, a.base))
	if not is_new and sha256 (base_bytes) != o["sha256"]: die ("l'archive publiee n'a pas le SHA-256 de l'index (%s)" % oldfile)

	shutil.rmtree (TMP, ignore_errors = True)
	stage = os.path.join (TMP, "files")
	os.makedirs (stage)
	order, base_data, base_abi = [], {}, None
	if not is_new:
		base_zip = os.path.join (TMP, "base.opk")
		open (base_zip, "wb").write (base_bytes)
		with zipfile.ZipFile (base_zip) as z:
			order = [n for n in z.namelist () if n != "PKG/manifest.ini" and not n.endswith ("/")]
			base_man = z.read ("PKG/manifest.ini").decode ("utf-8")
			base_data = { n: z.read (n) for n in order }
		for n in order:
			p = os.path.join (stage, n); os.makedirs (os.path.dirname (p), exist_ok = True)
			open (p, "wb").write (base_data[n])
		m = re.search (r"^needs = .*?abi >= (\d+)", base_man, re.M)
		base_abi = int (m.group (1)) if m else None

	# ---- 3. le paquet selon packages.ini, et le content publie recalcule ----
	pkgs, _ = mkrepo.plan (tree, None, ini, known = set (idx.sections ()))	# (un besoin publié mais pas construit ici)
	P = next ((p for p in pkgs if p["name"] == name), None)
	if P is None: die ("%s : pas dans %s, ou pas construit dans %s" % (name, os.path.basename (ini), tree))
	if is_new and P.get ("release") == "1":
		is_release = True			# (un nouveau paquet release = 1 : son archive va dans la Release, pas dans pkgs/)
		say ("pub: %s : release = 1, l'archive va dans la Release « packages »" % name)
	def content (files, data):
		desc = "needs %s\nconfig %s\nrequired %s\nrestart %s\n" % (P["needs"], P["config_pats"], P["required"], P["restart"])
		if P["replaces"]: desc += "replaces %s\n" % P["replaces"]
		if P["boot_pats"]: desc += "boot %s\n" % P["boot_pats"]
		if P.get ("fetch"): desc += "fetch %s\n" % P["fetch"]		# (comme mkrepo.py)
		return hashlib.sha256 ((desc + "".join ("%s %s\n" % (f, sha256 (data[f])) for f in files)).encode ()).hexdigest ()
	rank = { f: i for i, f in enumerate (P["files"]) }
	got = content (order, base_data) if not is_new else None
	how = "dans l'ordre de l'archive"
	if is_new: say ("pub: %s : nouveau paquet, %d fichier(s) pris dans l'arbre" % (name, len (P["files"])))
	elif got != o["content"]:
		got = content (sorted (order, key = lambda f: rank.get (f, len (rank))), base_data)
		how = "dans l'ordre de mkrepo.plan"
	if is_new: pass
	elif got == o["content"]: say ("pub: %s %s : content publie recalcule a l'identique (%s)" % (name, oldver, how))
	elif a.force: say ("pub: ATTENTION : le content publie ne se recalcule pas (%s a change ?), --force" % os.path.basename (ini))
	else: die ("le content publie de %s %s ne se recalcule pas : %s a change depuis (besoins, config...) ? (--force)" % (name, oldver, os.path.basename (ini)))

	# ---- 4. ce qui a change dans l'arbre ----
	def tree_path (f):
		if pc and f == "EFI/Korin/korin.efi": return os.path.join (boot, "EFI", "BOOT", "BOOTX64.EFI")
		if pc and f.startswith ("korin/"): return os.path.join (boot, f)
		if not pc and any (mkrepo.matches (f, b) for b in P["boot_pats"]): return os.path.join (boot, f)
		return os.path.join (tree, f)
	def wanted (f, guarded):
		if a.only: return any (mkrepo.matches (f, p) for p in a.only)
		return not guarded
	changed, skipped, missing = [], [], []
	for f in order:
		tp = tree_path (f)
		if not os.path.isfile (tp): missing.append (f); continue
		new = open (tp, "rb").read ()
		merged = re.match (r"^res/lang/[^/]+\.txt$", f) is not None
		if merged: new = lang_merge (base_data[f], new)
		if new == base_data[f]: continue
		guarded = pc and f in GUARDED
		if wanted (f, guarded): changed.append ((f, new, merged, tp))
		else: skipped.append ((f, guarded))
	extra = [f for f in P["files"] if f not in base_data]
	added = []
	for f in extra:
		if (a.add_new or is_new) and wanted (f, pc and f in GUARDED): added.append ((f, open (os.path.join (P["src"][f], f), "rb").read (), False, os.path.join (P["src"][f], f)))
	# (--drop : les fichiers que le paquet n'a plus ; jamais un fichier que packages.ini lui donne encore)
	dropped = [f for f in order if any (mkrepo.matches (f, p) for p in a.drop)]
	still = [f for f in dropped if f in P["files"]]
	if still: die ("--drop : %s encore donne(s) au paquet par %s" % (", ".join (still[:5]), os.path.basename (ini)))
	changed = [c for c in changed if c[0] not in dropped]
	missing = [f for f in missing if f not in dropped]
	def when (tp): return datetime.datetime.fromtimestamp (os.path.getmtime (tp)).strftime ("%d/%m %H:%M")
	say ("")
	say ("%s %s (%s), arbre %s :" % (name, oldver, "Release" if is_release else repo_rel + "/pkgs", os.path.relpath (tree, ROOT)))
	for f, new, merged, tp in changed:
		say ("  remplace  %-60s %10d -> %-10d %s%s" % (f, len (base_data[f]), len (new), when (tp), "  (fusion des cles)" if merged else ""))
	for f, new, merged, tp in added:
		say ("  ajoute    %-60s %10d  %s" % (f, len (new), when (tp)))
	for f in dropped:
		say ("  retire    %-60s %10d" % (f, len (base_data[f])))
	for f, guarded in skipped:
		say ("  garde     %s (differe : %s)" % (f, "une reconstruction brute l'abime, --only pour le prendre" if guarded else "hors de --only"))
	if extra and not a.add_new and not is_new:
		say ("  %d fichier(s) du paquet absents de l'archive (--add-new pour les ajouter) :" % len (extra))
		for f in extra[:15]: say ("            " + f)
		if len (extra) > 15: say ("            ...")
	if missing:
		say ("  %d fichier(s) de l'archive absents de l'arbre (gardes tels quels)%s" % (len (missing), " : " + ", ".join (missing[:5]) if len (missing) <= 5 else ""))
	if not changed and not added and not dropped:
		shutil.rmtree (TMP, ignore_errors = True)
		say ("pub: rien n'a change dans %s : rien a publier" % name)
		return

	# ---- 5. la nouvelle version, l'archive, l'index ----
	vtext = (git_blob ("HEAD", "tools/pkg/versions.ini") or b"").decode ("utf-8")
	vini = versions_get (vtext, [vsection, vbase], name)
	if is_new:
		ver = vini or "1.0.0"
	else:
		ver = mkrepo.bump (oldver)
		if vini and mkrepo.vkey (vini) > mkrepo.vkey (oldver): ver = vini
	abi = a.abi or base_abi or mkrepo.abi_level ()
	src_abi = mkrepo.abi_level ()
	if P["restart"] != "1" and src_abi > abi and not a.abi:
		say ("pub: note : les kits du source sont a abi %d, le paquet garde abi >= %d (--abi %d si ses programmes en ont besoin ; le systeme publie doit les avoir)" % (src_abi, abi, src_abi))
	for f, new, merged, tp in changed + added:
		os.makedirs (os.path.dirname (os.path.join (stage, f)), exist_ok = True)
		open (os.path.join (stage, f), "wb").write (new)
		if f not in order: order.append (f)
	files = [f for f in order if f not in dropped]
	data = { f: open (os.path.join (stage, f), "rb").read () for f in files }
	P2 = dict (P); P2["files"] = files; P2["src"] = { f: stage for f in files }
	P2["bytes"] = sum (len (data[f]) for f in files)
	for ent in (e.split () for e in P.get ("fetch", "").split (";") if e.strip ()):	# (fetch : téléchargé à l'installation)
		if len (ent) == 4 and ent[1].isdigit (): P2["bytes"] += int (ent[1])
	man, needs_all, _ = mkrepo.manifest_text (P2, ver, abi)
	newcontent = content (files, data)
	prefix = mkrepo.RELEASE_PREFIX
	if is_release:
		fn = "%s/%s%s-%s.opk" % (mkrepo.RELEASE_URL, prefix, name, ver)
		opk = os.path.join (release_dir, os.path.basename (fn))
		opk_existed = os.path.exists (opk)
		os.makedirs (release_dir, exist_ok = True)
	else:
		fn = "pkgs/%s-%s.opk" % (name, ver)
		opk = os.path.join (TMP, "out", fn.replace ("/", os.sep))
		os.makedirs (os.path.dirname (opk), exist_ok = True)
	if idx_text.count ("file = %s\n" % fn): die ("%s est deja dans l'index" % fn)
	mkrepo.write_opk (opk, P2, man)
	size = os.path.getsize (opk)
	if size >= mkrepo.GITHUB_MAX and not is_release: die ("%s fait plus de 100 Mo : release = 1 dans %s" % (fn, os.path.basename (ini)))
	icon_rel = "apps/%s.app/icon.bmp" % name
	icon_new = data.get (icon_rel)
	icon_changed = icon_new is not None and git_blob (a.base, "%s/icons/%s.bmp" % (repo_rel, name)) != icon_new
	sec = ["[%s]" % name, "title = " + P["title"], "version = " + ver, "category = " + P["category"],
	       "author = " + P["author"], "summary = " + P["summary"], "size = %d" % size, "installed = %d" % P2["bytes"],
	       "sha256 = " + mkrepo.sha256_file (opk), "file = " + fn] \
	    + (["icon = icons/%s.bmp" % name] if icon_new is not None or o.get ("icon") else []) \
	    + ["needs = " + ", ".join (needs_all), "required = " + P["required"], "restart = " + P["restart"]] \
	    + (["replaces = " + ", ".join (P["replaces"])] if P["replaces"] else []) \
	    + (["part = " + P["part"]] if P["part"] else []) + ["content = " + newcontent]
	lines = idx_text.split ("\n")
	if is_new:
		while lines and lines[-1] == "": lines.pop ()
		lines += [""] + sec + [""]
	else:
		s = lines.index ("[%s]" % name); e = s + 1
		while e < len (lines) and lines[e] != "": e += 1
		lines[s:e] = sec
	r0 = lines.index ("[repo]")
	for i in range (r0 + 1, len (lines)):
		if lines[i].startswith ("["): break
		if lines[i].startswith ("date = "): lines[i] = "date = " + datetime.datetime.now (datetime.timezone.utc).strftime ("%Y-%m-%d %H:%M")
		if lines[i].startswith ("abi = ") and "etc/pkg/abi" in [f for f, *_ in changed]:
			lines[i] = "abi = %d" % int (data["etc/pkg/abi"].decode ().strip ())		# (le systeme apporte d'autres tables de kits)
	new_idx = "\n".join (lines).encode ("utf-8")
	read_cfg (new_idx.decode ("utf-8"))		# (toujours lisible)

	# ---- 6. signature et checkrepo, sur une copie du depot ----
	keyf, keytmp = key_file (a.key)
	shadow = os.path.join (TMP, "repo")
	srepo = os.path.join (shadow, *repo_rel.split ("/"))
	os.makedirs (srepo, exist_ok = True)
	open (os.path.join (srepo, "index.txt"), "wb").write (new_idx)
	sig = None
	try:
		if keyf: sig = mkrepo.sign (new_idx, keyf) + "\n"
	finally:
		if keytmp: os.remove (keytmp)
	if sig: open (os.path.join (srepo, "index.sig"), "w", newline = "\n").write (sig)
	newcfg = read_cfg (new_idx.decode ("utf-8"))
	for n in newcfg.sections ():			# (les archives citees : celles d'origin/main, en liens quand le dossier les a)
		f = newcfg[n].get ("file", "")
		if n == "repo" or "://" in f: continue
		dst = os.path.normpath (os.path.join (srepo, f))
		if n == name: link_or_copy (opk, dst); continue
		rel = os.path.normpath (os.path.join (repo_rel, f)).replace (os.sep, "/")
		here = os.path.join (ROOT, rel)
		if os.path.isfile (here) and mkrepo.sha256_file (here) == newcfg[n]["sha256"]: link_or_copy (here, dst)
		else:
			b = git_blob (a.base, rel)
			if b is not None: os.makedirs (os.path.dirname (dst), exist_ok = True); open (dst, "wb").write (b)
	ok = False
	if sig:
		env = dict (os.environ)
		if not pc: env.pop ("ARCH", None)
		r = subprocess.run ([sys.executable, os.path.join (HERE, "checkrepo.py"), srepo, os.path.join (ROOT, "skel", "sd0", "etc", "pkg", "onyx.pub")],
		                    capture_output = True, text = True, encoding = "utf-8", errors = "replace", env = env)
		out = (r.stdout + r.stderr).strip ().split ("\n")
		errs = [l.strip () for l in out if l.startswith ("  ")]
		absent_ok = [l for l in errs if l.endswith (" : archive absente") and "://" in newcfg[l.split (" : ")[0]].get ("file", "") and l.split (" : ")[0] != name]
		bad = [l for l in errs if l not in absent_ok]
		if r.returncode == 0: say ("pub: checkrepo : " + out[-1]); ok = True
		elif not bad and any ("signature bonne" in l for l in out):
			say ("pub: checkrepo : signature bonne ; %d archive(s) de la Release absentes de %s (tolerees), aucune autre erreur" % (len (absent_ok), os.path.relpath (release_dir, ROOT)))
			ok = True
		else: say ("pub: checkrepo :\n  " + "\n  ".join (bad or out))
	else: say ("pub: pas de cle de signature : index non signe, checkrepo pas fait")
	say ("")
	say ("pub: %s %s -> %s : %d fichier(s), archive %s (%s), content %s" % (name, oldver, ver, len (changed) + len (added), fn, mkrepo.mo (size), newcontent[:16]))
	if a.dry_run:
		if is_release and not opk_existed: os.remove (opk)
		shutil.rmtree (TMP, ignore_errors = True)
		say ("pub: --dry-run : rien n'est ecrit ni publie%s" % ("" if ok else " (la verification n'est PAS bonne)"))
		if not ok: sys.exit (1)
		return
	if not ok: die ("verification pas bonne : rien n'est publie")
	if not a.yes:
		try: rep = input ("Publier %s %s%s ? [o/N] " % (name, ver, " (Release puis push)" if is_release and not a.no_push else "")).strip ().lower ()
		except EOFError: rep = ""
		if rep not in ("o", "oui", "y", "yes"): shutil.rmtree (TMP, ignore_errors = True); die ("abandon, rien n'est publie")

	# ---- 7. le dossier a jour, la Release, le commit ----
	head = git ("rev-parse", "--short", "HEAD").stdout.decode ().strip ()
	if git ("merge-base", "--is-ancestor", a.base, "HEAD", check = False).returncode:
		say ("pub: git merge %s" % a.base)
		r = git ("merge", "--no-edit", "-q", a.base, check = False)
		if r.returncode:
			git ("merge", "--abort", check = False)
			die ("la fusion de %s a echoue (%s) : rien n'est publie" % (a.base, r.stderr.decode ("utf-8", "replace").strip ()))
	if git_blob ("HEAD", repo_rel + "/index.txt") != idx_bytes:
		die ("HEAD porte un %s/index.txt qui n'est pas celui de %s (un commit pas pousse ?) : rien n'est publie" % (repo_rel, a.base))
	if is_release:
		if os.path.basename (opk) in rel_assets () and not MANUAL:	# (--manual-upload : deja deposee, verifiee par rel_wait)
			die ("%s est deja dans la Release : rien n'est publie" % os.path.basename (opk))
		say ("pub: %s release upload %s" % ("a la main" if MANUAL else "API" if API else "gh", os.path.basename (opk)))
		rel_upload (opk)
	vtext = (git_blob ("HEAD", "tools/pkg/versions.ini") or b"").decode ("utf-8")	# (apres la fusion)
	paths = {}					# chemin du depot -> octets (None : retire)
	paths[repo_rel + "/index.txt"] = new_idx
	paths[repo_rel + "/index.sig"] = sig.encode ()
	if not is_release:
		paths[repo_rel + "/" + fn] = open (opk, "rb").read ()
		if oldfile: paths[repo_rel + "/" + oldfile] = None
	if icon_changed: paths["%s/icons/%s.bmp" % (repo_rel, name)] = icon_new
	vnew = versions_edit (vtext, vsection, vbase, name, ver)
	if vnew != vtext: paths["tools/pkg/versions.ini"] = vnew.encode ("utf-8")
	tmp_index = os.path.join (TMP, "git-index")
	env = dict (os.environ, GIT_INDEX_FILE = tmp_index)
	git ("read-tree", "HEAD", env = env)
	for p, b in paths.items ():
		if b is None: git ("update-index", "--force-remove", p, env = env); continue
		blob = git ("hash-object", "-w", "--stdin", inp = b).stdout.decode ().strip ()
		git ("update-index", "--add", "--cacheinfo", "100644,%s,%s" % (blob, p), env = env)
	npk = sum (1 for l in new_idx.decode ("utf-8").split ("\n") if l.startswith ("file = "))
	title = "Paquets%s : %s %s (%d paquets)%s, depuis %s" % (" PC" if pc else "", name, ver, npk, (", " + a.message) if a.message else "", head)
	body = ("Publication par tools/pkg/pub.py, nouveau paquet :\n\n" if is_new else "Publication par tools/pkg/pub.py, sur %s %s publie :\n\n" % (name, oldver)) + "\n".join ("- %s %s" % ("ajoute" if f in [x[0] for x in added] else "remplace", f) for f, *_ in changed + added)
	git ("commit", "-q", "-m", title, "-m", body, env = env)
	git ("reset", "-q", "HEAD", "--", *paths.keys (), check = False)	# (l'index du dossier suit, pour ces chemins seulement)
	for p, b in paths.items ():			# (le dossier : les memes octets ; versions.ini garde ce que d'autres y changent)
		fp = os.path.join (ROOT, *p.split ("/"))
		if b is None:
			if os.path.exists (fp): os.remove (fp)
		elif p == "tools/pkg/versions.ini":
			cur = open (fp, encoding = "utf-8", newline = "").read ()
			open (fp, "w", encoding = "utf-8", newline = "").write (versions_edit (cur, vsection, vbase, name, ver))
		else:
			os.makedirs (os.path.dirname (fp), exist_ok = True); open (fp, "wb").write (b)
	say ("pub: commit %s : %s" % (git ("rev-parse", "--short", "HEAD").stdout.decode ().strip (), title))
	shutil.rmtree (TMP, ignore_errors = True)
	if a.no_push:
		say ("pub: --no-push : commite, PAS pousse"); return
	git ("fetch", "-q", "origin")
	if git ("merge-base", "--is-ancestor", "origin/main", "HEAD", check = False).returncode:
		say ("pub: git merge origin/main")
		r = git ("merge", "--no-edit", "-q", "origin/main", check = False)
		if r.returncode:
			git ("merge", "--abort", check = False)
			die ("la fusion d'origin/main a echoue apres le commit (une autre publication ?) : commite, PAS pousse ; relancer pub.py apres avoir regle")
	git ("push", "-q", "origin", "HEAD:main")
	say ("pub: pousse sur %s (GitHub Pages : une minute ou deux)" % GH_REPO)

if __name__ == "__main__":
	main ()
