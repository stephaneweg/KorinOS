#!/usr/bin/env python3
"""mkrepo.py -- le depot de paquets d'ARMPI depuis build/sdroot/ (docs/PAQUETS.md), port du mkrepo.py d'Onyx.

    python3 tools/pkg/mkrepo.py --out ../KorinOS/packages --key CLE.pem --boot build/pkgboot [--bump] [--db]
    python3 tools/pkg/mkrepo.py --db-only          (build.ps1 : build/sdroot/var/pkg/db et etc/pkg/abi, rien d'autre)

tools/pkg/packages.ini dit quels fichiers font chaque paquet, tools/pkg/versions.ini leurs versions. Pour chaque
paquet : un .opk (un ZIP de ses fichiers, l'arborescence de la carte, plus PKG/manifest.ini) dans <out>/pkgs/, son
icone dans <out>/icons/ si l'app en a une ; puis <out>/index.txt (version, taille, SHA-256...) signe dans <out>/index.sig
(ECDSA P-256 / SHA-256, DER, en hexa) avec la cle privee.

Un paquet dont les fichiers ont change sans que sa version bouge est une erreur -- sauf --bump, qui monte le dernier
nombre de sa version (versions.ini est reecrit). Un paquet inchange garde son .opk.
--db ecrit aussi build/sdroot/var/pkg/db/<nom>.ini : la carte "installee" (ce que lit pkg).
--boot DIR : ce qui va a la racine de la carte et n'est pas dans build/sdroot (noyaux 800x600, modules, firmware,
config.txt : publish.sh le prepare). --sd DIR : une autre carte ; --versions FICHIER ; --no-sign : pas d'index.sig.

Une archive de plus de 100 Mo (GitHub refuse le fichier) est retiree de l'index et signalee.
"""
import argparse, configparser, fnmatch, glob, hashlib, os, re, sys, zipfile, datetime

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (os.path.dirname (HERE))
BOARD = "armpi"
GITHUB_MAX = 100 * 1000 * 1000

def sha256_file (p):
	h = hashlib.sha256 ()
	with open (p, "rb") as f:
		for b in iter (lambda: f.read (1 << 20), b""): h.update (b)
	return h.hexdigest ()

def read_app_txt (sd, app):
	d = {}
	p = os.path.join (sd, "apps", app + ".app", "app.txt")
	if os.path.exists (p):
		for line in open (p, encoding = "utf-8", errors = "replace"):
			line = line.split ("#")[0].strip ()
			if "=" in line:
				k, v = line.split ("=", 1); d[k.strip ().lower ()] = v.strip ()
	return d

def abi_level ():
	"""La somme des tables des kits (en ajout seul : elle ne descend jamais) : ce qu'etait kapi sur Onyx."""
	n = 0
	for f in glob.glob (os.path.join (ROOT, "src", "kits", "*", "*.abi")):
		n += sum (1 for l in open (f, encoding = "utf-8", errors = "replace") if l.startswith ("kit_fn"))
	return n

# jamais dans un paquet : l'etat de la carte, les reglages Wi-Fi (pkglib.h refuse aussi ces noms)
def private (f):
	return f == "var" or f.startswith (("var/", "etc/wifi", "webkit/")) or f == "etc/lastclock" or f.endswith (".pkgtmp")

def card_files (sd):
	"""Les fichiers d'une arborescence, separes par /, relatifs."""
	out = []
	for dp, dns, fns in os.walk (sd):
		dns.sort ()
		rel = os.path.relpath (dp, sd).replace (os.sep, "/")
		if rel == ".": rel = ""
		for f in sorted (fns):
			f = (rel + "/" if rel else "") + f
			if not private (f): out.append (f)
	return out

def matches (path, pat):
	if pat.endswith ("/"): return path.startswith (pat)
	if any (ch in pat for ch in "*?["): return fnmatch.fnmatchcase (path, pat)
	return path == pat

def split (v): return v.split () if v else []

def bump (v):
	n = v.split (".")
	n[-1] = str (int (n[-1]) + 1) if n[-1].isdigit () else n[-1] + ".1"
	return ".".join (n)

def vkey (v): return [int (x) if x.isdigit () else 0 for x in re.split (r"[.\-]", v)]

def plan (sd, boot, ini):
	"""-> [paquet], dans l'ordre du ini ; les fichiers sans paquet."""
	files = card_files (sd)
	src = { f: sd for f in files }
	if boot:
		for f in card_files (boot):
			if f not in src: files.append (f)
			src[f] = boot
	taken = {}
	pkgs = []
	cfg = configparser.ConfigParser (interpolation = None, comment_prefixes = ("#", ";"), inline_comment_prefixes = (";",))
	cfg.optionxform = str
	cfg.read (ini, encoding = "utf-8")
	adir = os.path.join (sd, "apps")
	apps = sorted (d[:-4] for d in os.listdir (adir) if d.endswith (".app")) if os.path.isdir (adir) else []
	def take (name, pats):
		got = []
		for f in files:
			if f in taken: continue
			if any (matches (f, p) for p in pats): taken[f] = name; got.append (f)
		return got
	def make (name, sec, pats):
		p = { "name": name, "title": sec.get ("title", name), "category": sec.get ("category", "Autres"),
		      "summary": sec.get ("summary", ""), "author": sec.get ("author", "Korin OS"),
		      "needs": [n.strip () for n in sec.get ("needs", "").split (",") if n.strip ()],
		      "required": sec.get ("required", "0"), "restart": sec.get ("restart", "0"),
		      "publish": sec.get ("publish", "1"),
		      "config_pats": split (sec.get ("config", "")),
		      "replaces": [n.strip () for n in sec.get ("replaces", "").split (",") if n.strip ()] }
		p["files"] = take (name, pats)
		p["src"] = src
		# root = DOSSIER : les fichiers sont la s'ils ne sont pas sur la carte (build/gcc/sdroot)
		root = sec.get ("root", "").strip ()
		if not p["files"] and root and os.path.isdir (os.path.join (ROOT, root)):
			r = os.path.join (ROOT, root)
			p["files"] = [f for f in card_files (r) if any (matches (f, pt) for pt in pats)]
			p["src"] = { f: r for f in p["files"] }
			p["offcard"] = True
		if p["files"]: pkgs.append (p)
		else: print ("mkrepo: [%s] : aucun fichier (pas construit ?), paquet saute" % name, file = sys.stderr)
	for s in cfg.sections ():
		if s.startswith ("app."): continue
		sec = cfg[s]
		if s == "*apps":
			for a in apps:
				if any (f.startswith ("apps/%s.app/" % a) and f in taken for f in files): continue
				at = read_app_txt (sd, a)
				x = cfg["app." + a] if cfg.has_section ("app." + a) else {}
				pats = ["apps/%s.app/" % a] + split (x.get ("files", "") if x else "")
				d = dict (x) if x else {}
				d.setdefault ("title", at.get ("name", a)); d.setdefault ("category", at.get ("category", "Autres"))
				if sec.get ("needs", "").strip ():
					d["needs"] = ", ".join (n for n in (sec.get ("needs"), d.get ("needs", "")) if n.strip ())
				make (a, d, pats)
			continue
		make (s, dict (sec), split (sec.get ("files", "")))
	# les exemples d'une app qui n'est pas construite : pas de paquet (son besoin manquerait au depot)
	for changed in iter (lambda: [p for p in pkgs if any (n.split ()[0] not in [q["name"] for q in pkgs] for n in p["needs"])], []):
		for p in changed:
			print ("mkrepo: [%s] : son besoin n'est pas dans le depot (%s), paquet saute" % (p["name"], ", ".join (p["needs"])), file = sys.stderr)
			pkgs.remove (p)
	return pkgs, [f for f in files if f not in taken]

def manifest_text (p, version, abi):
	# "abi >= N" : les programmes du paquet ont ete construits contre ces tables de kits ; pkg ne l'installe que sur
	# un systeme qui les a. Pas pour un paquet etage (le systeme, le firmware) : il s'installe sur l'ancien.
	needs = p["needs"] + (["abi >= %d" % abi] if p["restart"] != "1" else [])
	cfgfiles = [f for f in p["files"] if any (matches (f, c) for c in p["config_pats"])]
	lines = ["# manifeste de paquet ARMPI (tools/pkg/mkrepo.py)",
		 "name = " + p["name"], "title = " + p["title"], "version = " + version,
		 "category = " + p["category"], "author = " + p["author"], "summary = " + p["summary"],
		 "needs = " + ", ".join (needs), "required = " + p["required"], "restart = " + p["restart"],
		 "installed = %d" % p["bytes"], "config = " + " ".join (cfgfiles)]
	if p["replaces"]: lines.append ("replaces = " + ", ".join (p["replaces"]))
	return "\n".join (lines) + "\n", needs, cfgfiles

def write_opk (path, p, manifest):
	"""Un ZIP deterministe : memes fichiers, memes octets."""
	tmp = path + ".part"
	with zipfile.ZipFile (tmp, "w", zipfile.ZIP_DEFLATED, compresslevel = 9, allowZip64 = True) as z:
		def add (name, data):
			zi = zipfile.ZipInfo (name, (1980, 1, 1, 0, 0, 0)); zi.compress_type = zipfile.ZIP_DEFLATED
			zi.external_attr = 0o644 << 16; z.writestr (zi, data)
		add ("PKG/manifest.ini", manifest.encode ("utf-8"))
		for f in p["files"]:
			with open (os.path.join (p["src"][f], f), "rb") as fh: add (f, fh.read ())
	os.replace (tmp, path)

def read_index (path):
	cfg = configparser.ConfigParser (interpolation = None); cfg.optionxform = str
	if os.path.exists (path): cfg.read (path, encoding = "utf-8")
	return cfg

def sign (data, keyfile):
	from cryptography.hazmat.primitives import hashes, serialization
	from cryptography.hazmat.primitives.asymmetric import ec
	key = serialization.load_pem_private_key (open (keyfile, "rb").read (), password = None)
	return key.sign (data, ec.ECDSA (hashes.SHA256 ())).hex ()

def write_db (db, pkgs):
	"""SD0:/var/pkg/db/<nom>.ini : la carte "installee" (ce que lit pkg)."""
	os.makedirs (db, exist_ok = True)
	for f in os.listdir (db):
		if f.endswith (".ini"): os.remove (os.path.join (db, f))
	for p in pkgs:
		L = ["# installe par tools/pkg/mkrepo.py", "[package]", "name = " + p["name"], "title = " + p["title"],
		     "version = " + p["version"], "category = " + p["category"], "author = " + p["author"], "summary = " + p["summary"],
		     "mode = manual", "required = " + p["required"], "restart = " + p["restart"], "needs = " + ", ".join (p["needs_all"]),
		     "config = " + " ".join (p["cfgfiles"]), "", "[files]"] + ["%s = %s" % h for h in p["hashes"]]
		open (os.path.join (db, p["name"] + ".ini"), "w", encoding = "utf-8", newline = "\n").write ("\n".join (L) + "\n")

def mo (n): return "%.1f Mo" % (n / 1e6)

def main ():
	ap = argparse.ArgumentParser ()
	ap.add_argument ("--out"); ap.add_argument ("--key"); ap.add_argument ("--bump", action = "store_true")
	ap.add_argument ("--db", action = "store_true"); ap.add_argument ("--db-only", action = "store_true")
	ap.add_argument ("--sd", default = os.path.join (ROOT, "build", "sdroot")); ap.add_argument ("--boot")
	ap.add_argument ("--ini", default = os.path.join (HERE, "packages.ini"))
	ap.add_argument ("--versions", default = os.path.join (HERE, "versions.ini"))
	ap.add_argument ("--no-sign", action = "store_true"); ap.add_argument ("--repo-name", default = "korin-packages")
	a = ap.parse_args ()
	if a.db_only: a.db = True
	elif not a.out: sys.exit ("mkrepo: --out DOSSIER (ou --db-only)")
	elif not a.no_sign and not a.key: sys.exit ("mkrepo: --key CLE.pem ou --no-sign")
	abi = abi_level ()
	os.makedirs (os.path.join (a.sd, "etc", "pkg"), exist_ok = True)
	abifile = os.path.join (a.sd, "etc", "pkg", "abi")
	if not os.path.exists (abifile) or open (abifile).read ().strip () != str (abi):
		open (abifile, "w", newline = "\n").write ("%d\n" % abi)
	pkgs, left = plan (a.sd, a.boot, a.ini)
	if left and not a.db_only:
		print ("mkrepo: %d fichier(s) sans paquet :" % len (left), file = sys.stderr)
		for f in left[:20]: print ("   " + f, file = sys.stderr)
	vcfg = configparser.ConfigParser (interpolation = None); vcfg.optionxform = str
	if os.path.exists (a.versions): vcfg.read (a.versions, encoding = "utf-8")
	if not vcfg.has_section ("versions"): vcfg.add_section ("versions")
	V = vcfg["versions"]
	old = read_index (os.path.join (a.out, "index.txt")) if a.out else read_index ("")
	if a.out: os.makedirs (os.path.join (a.out, "pkgs"), exist_ok = True)
	errors, changed = [], []
	for p in pkgs:
		hashes = [(f, sha256_file (os.path.join (p["src"][f], f))) for f in p["files"]]
		p["hashes"] = hashes
		p["bytes"] = sum (os.path.getsize (os.path.join (p["src"][f], f)) for f in p["files"])
		# le contenu : les fichiers et ce que le manifeste en dit (un nouveau besoin : une nouvelle version)
		desc = "needs %s\nconfig %s\nrequired %s\nrestart %s\n" % (p["needs"], p["config_pats"], p["required"], p["restart"])
		if p["replaces"]: desc += "replaces %s\n" % p["replaces"]
		p["content"] = hashlib.sha256 ((desc + "".join ("%s %s\n" % h for h in hashes)).encode ()).hexdigest ()
		ver = V.get (p["name"], datetime.date.today ().strftime ("%Y.%m.0") if p["name"] == "armpi" else "1.0.0")
		if old.has_section (p["name"]) and old[p["name"]].get ("content") != p["content"] and vkey (ver) <= vkey (old[p["name"]].get ("version", "0")):
			if a.bump: ver = bump (old[p["name"]]["version"]); changed.append ("%s -> %s" % (p["name"], ver))
			else: errors.append ("%s %s : ses fichiers ont change (--bump, ou la monter dans versions.ini)" % (p["name"], ver))
		V[p["name"]] = ver; p["version"] = ver
	if errors: sys.exit ("mkrepo:\n  " + "\n  ".join (errors))
	for p in pkgs:
		man, needs, cfgfiles = manifest_text (p, p["version"], abi)
		p["man"] = man; p["needs_all"] = needs; p["cfgfiles"] = cfgfiles
	if a.db: write_db (os.path.join (a.sd, "var", "pkg", "db"), [p for p in pkgs if "offcard" not in p])
	if a.db_only:
		print ("mkrepo: base de la carte : %d paquets (abi %d)" % (len ([p for p in pkgs if "offcard" not in p]), abi))
		return
	with open (a.versions, "w", encoding = "utf-8", newline = "\n") as f:
		f.write ("# tools/pkg/versions.ini -- la version de chaque paquet (mkrepo.py --bump les monte)\n")
		vcfg.write (f)
	idx = ["# index des paquets d'ARMPI -- tools/pkg/mkrepo.py (docs/PAQUETS.md)", "[repo]", "name = " + a.repo_name,
	       "date = " + datetime.datetime.now (datetime.timezone.utc).strftime ("%Y-%m-%d %H:%M"), "abi = %d" % abi,
	       "packages = %d" % len ([p for p in pkgs if p["publish"] != "0"]), "board = " + BOARD, ""]
	sizes, excluded, total = [], [], 0
	for p in pkgs:
		if p["publish"] == "0":			# (publish = 0 : sur la carte, jamais sur le depot public)
			p["gone"] = True
			continue
		fn = "pkgs/%s-%s.opk" % (p["name"], p["version"])
		path = os.path.join (a.out, fn)
		o = old[p["name"]] if old.has_section (p["name"]) else None
		if not (o and o.get ("content") == p["content"] and o.get ("version") == p["version"] and os.path.exists (path)):
			write_opk (path, p, p["man"])
		size = os.path.getsize (path)
		if size >= GITHUB_MAX:				# GitHub refuse un fichier de plus de 100 Mo
			excluded.append ((p["name"], size)); os.remove (path); p["gone"] = True
			continue
		sizes.append ((p["name"], p["version"], size, p["bytes"])); total += size
		icon = os.path.join (p["src"].get ("apps/%s.app/icon.bmp" % p["name"], a.sd), "apps/%s.app/icon.bmp" % p["name"])
		has_icon = os.path.exists (icon)
		if has_icon:
			os.makedirs (os.path.join (a.out, "icons"), exist_ok = True)
			with open (icon, "rb") as s, open (os.path.join (a.out, "icons", p["name"] + ".bmp"), "wb") as d: d.write (s.read ())
		idx += ["[%s]" % p["name"], "title = " + p["title"], "version = " + p["version"], "category = " + p["category"],
			"author = " + p["author"], "summary = " + p["summary"], "size = %d" % size,
			"installed = %d" % p["bytes"], "sha256 = " + sha256_file (path), "file = " + fn] \
		    + (["icon = icons/%s.bmp" % p["name"]] if has_icon else []) \
		    + ["needs = " + ", ".join (p["needs_all"]), "required = " + p["required"], "restart = " + p["restart"]] \
		    + (["replaces = " + ", ".join (p["replaces"])] if p["replaces"] else []) + ["content = " + p["content"], ""]
	keep = set ("%s-%s.opk" % (p["name"], p["version"]) for p in pkgs if "gone" not in p)	# les archives des anciennes versions
	for f in os.listdir (os.path.join (a.out, "pkgs")):
		if f.endswith (".opk") and f not in keep: os.remove (os.path.join (a.out, "pkgs", f))
	data = ("\n".join (idx)).encode ("utf-8")
	ip = os.path.join (a.out, "index.txt")
	oldtext = open (ip, "rb").read () if os.path.exists (ip) else b""
	strip = lambda t: re.sub (rb"\ndate = [^\n]*", b"", t)
	if strip (oldtext) == strip (data) and os.path.exists (os.path.join (a.out, "index.sig")):
		print ("mkrepo: aucun paquet n'a change : index garde")
	else:
		open (ip, "wb").write (data)
		if not a.no_sign:
			open (os.path.join (a.out, "index.sig"), "w", newline = "\n").write (sign (data, a.key) + "\n")
	print ("%-22s %-13s %12s %12s" % ("paquet", "version", "archive", "installe"))
	for n, v, s, b in sorted (sizes, key = lambda x: -x[2]): print ("%-22s %-13s %12s %12s" % (n, v, mo (s), mo (b)))
	print ("mkrepo: %d paquets -> %s, total %s (GitHub : aucun fichier de plus de 100 Mo)%s" % (len (sizes), a.out, mo (total), ("; montes : " + ", ".join (changed)) if changed else ""))
	for n, s in excluded:
		print ("mkrepo: ATTENTION : %s fait %s : plus de 100 Mo, EXCLU de l'index (a decouper ou a alleger)" % (n, mo (s)), file = sys.stderr)

if __name__ == "__main__":
	main ()
