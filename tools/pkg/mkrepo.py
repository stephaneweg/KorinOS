#!/usr/bin/env python3
"""mkrepo.py -- le depot de paquets d'ARMPI depuis build/sdroot/ (docs/PAQUETS.md), port du mkrepo.py d'Onyx.

    python3 tools/pkg/mkrepo.py --out packages --key CLE.pem --boot build/pkgboot [--bump] [--db]
    python3 tools/pkg/mkrepo.py --db-only          (build.ps1 : build/sdroot/var/pkg/db et etc/pkg/abi, rien d'autre)

tools/pkg/packages.ini dit quels fichiers font chaque paquet, tools/pkg/versions.ini leurs versions. Pour chaque
paquet : un .opk (un ZIP de ses fichiers, l'arborescence de la carte, plus PKG/manifest.ini) dans <out>/pkgs/, son
icone dans <out>/icons/ si l'app en a une ; puis <out>/index.txt (version, taille, SHA-256...) signe dans <out>/index.sig
(ECDSA P-256 / SHA-256, DER, en hexa) avec la cle privee.

Un paquet dont les fichiers ont change sans que sa version bouge est une erreur -- sauf --bump, qui monte le dernier
nombre de sa version (versions.ini est reecrit). Un paquet inchange garde son .opk.
--db ecrit aussi build/sdroot/var/pkg/db/<nom>.ini : la carte "installee" (ce que lit pkg).
--boot DIR : ce qui va a la racine de la carte et n'est pas dans build/sdroot (noyaux 800x600, modules, firmware,
config.txt : publish.sh le prepare). La cle boot = de packages.ini (des motifs, comme files =) dit lesquels de ses
fichiers pkg ecrit dans /boot/ (la vraie racine de la partition de demarrage, docs/FICHIERS.md) et non dans / :
le manifeste et la base la portent (boot = ...). --sd DIR : une autre carte ; --versions FICHIER ; --no-sign : pas d'index.sig.
--keep-unbuilt : un paquet deja publie mais pas construit ici (WebKit, .NET, bureautique... faits dans WSL) garde son
entree et son archive telles quelles, au lieu de disparaitre de l'index (publication depuis une machine sans WSL).
--drop NOM[,NOM...] : ces paquets sortent de l'index meme avec --keep-unbuilt (un paquet qui ne doit plus etre publie).

Une archive de plus de 100 Mo (GitHub refuse le fichier) est retiree de l'index et signalee.

Le PC (ARCH=x86_64 ou BOARD=pc dans l'environnement ; docs/PAQUETS.md, « Le PC ») : board = pc, la carte est
build/pc/sysroot, les paquets tools/pkg/packages-pc.ini, les versions de versions.ini ([versions], sauf celles de la
section [pc], ou vont les versions propres au PC), les archives release = 1 dans build/pkg-release-pc et nommees
pc-<nom>-<version>.opk dans la Release. --any INDEX (l'index du Pi, packages/index.txt) : un paquet dont le contenu est
celui d'un paquet publie pour le Pi (meme nom, meme `content` : memes fichiers, memes besoins) n'est pas refait, l'index
du PC cite l'archive du Pi (file = ../pkgs/..., arch = any). Sans la variable : le Pi, rien ne change.
"""
import argparse, configparser, fnmatch, glob, hashlib, os, re, sys, zipfile, datetime

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (os.path.dirname (HERE))
PC = os.environ.get ("ARCH") == "x86_64" or os.environ.get ("BOARD") == "pc"
BOARD = "pc" if PC else "armpi"
SYSTEM = BOARD					# (le paquet du systeme : armpi, pc ; sa version par defaut est datee)
VSECTION = "pc" if PC else None			# (versions.ini : les versions propres au PC)
RELEASE_PREFIX = "pc-" if PC else ""		# (les noms des archives du PC dans la Release, a cote de celles du Pi)
GITHUB_MAX = 100 * 1000 * 1000
RELEASE_URL = "https://github.com/stephaneweg/KorinOS/releases/download/packages"

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
	if PC and (f in ("etc/options.txt", "etc/pointer.ini", "korin/boot.cfg", "korin/installed.txt") or f.startswith (("acpi/", ".vibe/.env", ".vibe/config.toml"))):
		return True	# (PC : les reglages de l'utilisateur, ceux du demarrage, ses tables ACPI, ses comptes de Vibe)
	return f == "var" or f.startswith (("var/", "etc/wifi", "etc/remote", "webkit/")) or f == "etc/lastclock" or f.endswith (".pkgtmp")

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

def plan (sd, boot, ini, known = ()):
	"""-> [paquet], dans l'ordre du ini ; les fichiers sans paquet. known : d'autres noms de paquets à tenir pour
	présents (pub.py : ceux de l'index publié, pour un besoin qui n'est pas construit dans l'arbre)."""
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
			if any (matches (f, p[1:]) for p in pats if p.startswith ("!")): continue	# (!motif : laisse a un autre paquet)
			if any (matches (f, p) for p in pats if not p.startswith ("!")): taken[f] = name; got.append (f)
		return got
	def make (name, sec, pats):
		p = { "name": name, "title": sec.get ("title", name), "category": sec.get ("category", "Autres"),
		      "summary": sec.get ("summary", ""), "author": sec.get ("author", "Korin OS"),
		      "needs": [n.strip () for n in sec.get ("needs", "").split (",") if n.strip ()],
		      "required": sec.get ("required", "0"), "restart": sec.get ("restart", "0"),
		      "publish": sec.get ("publish", "1"), "release": sec.get ("release", "0"),
		      "config_pats": split (sec.get ("config", "")), "merge_pats": split (sec.get ("merge", "")), "boot_pats": split (sec.get ("boot", "")), "part": "",
		      "replaces": [n.strip () for n in sec.get ("replaces", "").split (",") if n.strip ()],
		      "fetch": " ".join (sec.get ("fetch", "").split ()) }
		p["files"] = take (name, pats)
		p["src"] = src
		# root = DOSSIER : les fichiers sont la s'ils ne sont pas sur la carte (build/gcc/sdroot)
		root = sec.get ("root", "").strip ()
		if not p["files"] and root and os.path.isdir (os.path.join (ROOT, root)):
			r = os.path.join (ROOT, root)
			p["files"] = [f for f in card_files (r) if any (matches (f, pt) for pt in pats)]
			p["src"] = { f: r for f in p["files"] }
			p["offcard"] = True
		# aucun de ses programmes nommes (bin/X, apps/X.app/main) dans l'arbre : le paquet n'est pas construit, meme si
		# d'autres de ses fichiers y sont (bash : etc/bash.bashrc et etc/termcap viennent de skel/ ; sans ce test, la
		# carte, la cle d'installation et le systeme installe le croyaient installe sans /bin/bash, et pkg aussi)
		progs = [f for f in pats if not p.get ("offcard") and not any (c in f for c in "*?[") and not f.endswith ("/")
			 and (f.startswith ("bin/") or (f.startswith ("apps/") and f.endswith (".app/main")))]
		absent = [f for f in progs if f not in src]
		if p["files"] and progs and len (absent) == len (progs):	# (aucun : un paquet d'essais partiel reste)
			print ("mkrepo: [%s] : %s absent (pas construit ?), paquet saute" % (name, ", ".join (absent)), file = sys.stderr)
			# (ses autres fichiers restent a lui : pas a [pc], qui prend le reste d'etc/)
			p["files"] = []
		# split = MOTIF MO : ses fichiers qui suivent le motif vont dans des paquets <nom>-data-1.. de moins de MO
		# Mo chacun (GitHub refuse un fichier de plus de 100 Mo), dont il a besoin
		sp = split (sec.get ("split", ""))
		if p["files"] and len (sp) == 2:
			data = [f for f in p["files"] if matches (f, sp[0])]
			p["files"] = [f for f in p["files"] if f not in data]
			limit, chunks, cur, size = int (sp[1]) * 1000 * 1000, [], [], 0
			for f in data:
				n = os.path.getsize (os.path.join (p["src"][f], f))
				if cur and size + n > limit: chunks.append (cur); cur, size = [], 0
				cur.append (f); size += n
			if cur: chunks.append (cur)
			for i, c in enumerate (chunks, 1):
				pn = "%s-data-%d" % (name, i)
				for f in c: taken[f] = pn
				pkgs.append ({ "name": pn, "title": "%s (%d/%d)" % (p["title"], i, len (chunks)), "category": p["category"],
				               "summary": "Les données de %s, partie %d sur %d" % (p["title"], i, len (chunks)),
				               "author": p["author"], "needs": [], "required": "0", "restart": "0", "publish": p["publish"], "release": p["release"],
				               "config_pats": [], "boot_pats": [], "replaces": [], "files": c, "src": p["src"], "part": name })	# (part : jamais seul)
				p["needs"].append (pn)
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
				m = os.path.join (sd, "apps", a + ".app", "main")		# (une app .NET, un PE : le runtime de dotnet)
				if os.path.isfile (m) and open (m, "rb").read (2) == b"MZ" and "dotnet" not in d.get ("needs", ""):
					d["needs"] = ", ".join (n for n in (d.get ("needs", ""), "dotnet") if n.strip ())
				make (a, d, pats)
			continue
		make (s, dict (sec), split (sec.get ("files", "")))
	# les exemples d'une app qui n'est pas construite : pas de paquet (son besoin manquerait au depot)
	for changed in iter (lambda: [p for p in pkgs if any (n.split ()[0] not in [q["name"] for q in pkgs] and n.split ()[0] not in known for n in p["needs"])], []):
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
	if p["boot_pats"]: lines.append ("boot = " + " ".join (p["boot_pats"]))	# (ses fichiers de demarrage : /boot/)
	if p["replaces"]: lines.append ("replaces = " + ", ".join (p["replaces"]))
	# des fichiers téléchargés à l'installation, chez leur éditeur (CHEMIN TAILLE SHA256 URL ; …) : pkglib.h
	if p.get ("fetch"): lines.append ("fetch = " + p["fetch"])
	# les listes de lignes qu'une app change (services.txt) : pkg y ajoute les lignes nouvelles du paquet
	merge = [f for f in cfgfiles if any (matches (f, c) for c in p["merge_pats"])]
	if merge: lines.append ("merge = " + " ".join (merge))
	return "\n".join (lines) + "\n", needs, cfgfiles

# les fichiers dont tools/secrets.py remplit les marques @NOM@ (docs/OAUTH.md) : une archive ne part jamais avec une
# marque non remplie (23/11/2026 : pc 2026.11.94, construit sans ../korin-secrets.ini, avait perdu le client Google)
SECRET_FILES = { "etc/oauth/providers.ini" }
SECRET_MARK = re.compile (rb"@[A-Z][A-Z0-9_]*@")

def write_opk (path, p, manifest):
	"""Un ZIP deterministe : memes fichiers, memes octets."""
	for f in p["files"]:
		if f in SECRET_FILES:
			with open (os.path.join (p["src"][f], f), "rb") as fh:
				m = SECRET_MARK.findall (fh.read ())
			if m:
				raise SystemExit ("mkrepo : %s a des marques non remplies (%s) : ../korin-secrets.ini (ou KORIN_SECRETS) manque "
					"a cette construction ; reprendre le fichier de l'archive publiee (pub.py : ne pas le mettre dans --only), "
					"jamais celui de skel/ (docs/OAUTH.md)" % (f, ", ".join (sorted (set (x.decode () for x in m)))))
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
	"""/var/pkg/db/<nom>.ini : la carte "installee" (ce que lit pkg)."""
	os.makedirs (db, exist_ok = True)
	for f in os.listdir (db):
		if f.endswith (".ini"): os.remove (os.path.join (db, f))
	for p in pkgs:
		L = ["# installe par tools/pkg/mkrepo.py", "[package]", "name = " + p["name"], "title = " + p["title"],
		     "version = " + p["version"], "category = " + p["category"], "author = " + p["author"], "summary = " + p["summary"],
		     "mode = manual", "required = " + p["required"], "restart = " + p["restart"], "needs = " + ", ".join (p["needs_all"]),
		     "config = " + " ".join (p["cfgfiles"])] + (["boot = " + " ".join (p["boot_pats"])] if p["boot_pats"] else []) \
		    + (["part = " + p["part"]] if p["part"] else []) + ["", "[files]"] + ["%s = %s" % h for h in p["hashes"]]
		open (os.path.join (db, p["name"] + ".ini"), "w", encoding = "utf-8", newline = "\n").write ("\n".join (L) + "\n")

def mo (n): return "%.1f Mo" % (n / 1e6)

def main ():
	ap = argparse.ArgumentParser ()
	ap.add_argument ("--out"); ap.add_argument ("--key"); ap.add_argument ("--bump", action = "store_true")
	ap.add_argument ("--db", action = "store_true"); ap.add_argument ("--db-only", action = "store_true")
	ap.add_argument ("--sd", default = os.path.join (ROOT, "build", "pc", "sysroot") if PC else os.path.join (ROOT, "build", "sdroot")); ap.add_argument ("--boot")
	ap.add_argument ("--ini", default = os.path.join (HERE, "packages-pc.ini" if PC else "packages.ini"))
	ap.add_argument ("--any", help = "l'index du Pi (packages/index.txt) : ses paquets de meme contenu sont cites (../pkgs/, arch = any)")
	ap.add_argument ("--release-dir", default = os.path.join (ROOT, "build", "pkg-release-pc" if PC else "pkg-release"),
	                 help = "les archives des paquets release = 1, a envoyer dans la Release « packages » (publish.sh)")
	ap.add_argument ("--versions", default = os.path.join (HERE, "versions.ini"))
	ap.add_argument ("--no-sign", action = "store_true"); ap.add_argument ("--repo-name", default = "korin-packages")
	ap.add_argument ("--keep-unbuilt", action = "store_true", help = "garder tels quels les paquets publies mais pas construits ici")
	ap.add_argument ("--drop", default = "", help = "paquets a retirer de l'index, meme avec --keep-unbuilt (NOM,NOM...)")
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
	if VSECTION:				# (PC : [pc] d'abord, puis [versions] ; [pc] ne garde que ce qui differe)
		if not vcfg.has_section (VSECTION): vcfg.add_section (VSECTION)
		class PcVersions:
			def __init__ (self, base, own): self.base, self.own = base, own
			def get (self, k, d = None): return self.own.get (k, self.base.get (k, d))
			def __setitem__ (self, k, v):
				if self.base.get (k) == v: self.own.pop (k, None)
				else: self.own[k] = v
		V = PcVersions (vcfg["versions"], vcfg[VSECTION])
	anyidx = read_index (a.any) if a.any else None
	old = read_index (os.path.join (a.out, "index.txt")) if a.out else read_index ("")
	if a.out: os.makedirs (os.path.join (a.out, "pkgs"), exist_ok = True)
	errors, changed = [], []
	for p in pkgs:
		hashes = [(f, sha256_file (os.path.join (p["src"][f], f))) for f in p["files"]]
		p["hashes"] = hashes
		p["bytes"] = sum (os.path.getsize (os.path.join (p["src"][f], f)) for f in p["files"])
		# (fetch : CHEMIN TAILLE SHA256 URL ; … : leur taille compte dans ce qu'il prend sur le disque)
		for ent in (e.split () for e in p.get ("fetch", "").split (";") if e.strip ()):
			if len (ent) != 4 or len (ent[2]) != 64 or not ent[3].startswith ("https://") or not ent[1].isdigit ():
				errors.append ("%s : fetch illisible : %s" % (p["name"], " ".join (ent)))
			else:
				p["bytes"] += int (ent[1])
		# le contenu : les fichiers et ce que le manifeste en dit (un nouveau besoin : une nouvelle version)
		desc = "needs %s\nconfig %s\nrequired %s\nrestart %s\n" % (p["needs"], p["config_pats"], p["required"], p["restart"])
		if p["replaces"]: desc += "replaces %s\n" % p["replaces"]
		if p["boot_pats"]: desc += "boot %s\n" % p["boot_pats"]	# (/boot/ : un autre manifeste)
		if p.get ("fetch"): desc += "fetch %s\n" % p["fetch"]		# (une autre version de l'éditeur)
		p["content"] = hashlib.sha256 ((desc + "".join ("%s %s\n" % h for h in hashes)).encode ()).hexdigest ()
		ver = V.get (p["name"], datetime.date.today ().strftime ("%Y.%m.0") if p["name"] == SYSTEM else "1.0.0")
		# (PC : le meme paquet publie pour le Pi, au meme contenu : sa version et son archive, arch = any)
		if anyidx is not None and anyidx.has_section (p["name"]) and anyidx[p["name"]].get ("content") == p["content"] and p["publish"] != "0":
			p["any"] = anyidx[p["name"]]; ver = p["any"]["version"]
		if old.has_section (p["name"]) and old[p["name"]].get ("content") != p["content"] and vkey (ver) <= vkey (old[p["name"]].get ("version", "0")):
			if a.bump: ver = bump (old[p["name"]]["version"]); changed.append ("%s -> %s" % (p["name"], ver))
			else: errors.append ("%s %s : ses fichiers ont change (--bump, ou la monter dans versions.ini)" % (p["name"], ver))
		V[p["name"]] = ver; p["version"] = ver
	if errors: sys.exit ("mkrepo:\n  " + "\n  ".join (errors))
	for p in pkgs:
		man, needs, cfgfiles = manifest_text (p, p["version"], abi)
		p["man"] = man; p["needs_all"] = needs; p["cfgfiles"] = cfgfiles
	# (PC : pas les paquets de home/ seul, que korin.ps1 install ne copie jamais : /media/sd1, les fichiers de l'utilisateur)
	if a.db: write_db (os.path.join (a.sd, "var", "pkg", "db"), [p for p in pkgs if "offcard" not in p and not (PC and all (f.startswith ("home/") for f in p["files"]))])
	if a.db_only:
		print ("mkrepo: base de la carte : %d paquets (abi %d)" % (len ([p for p in pkgs if "offcard" not in p]), abi))
		return
	with open (a.versions, "w", encoding = "utf-8", newline = "\n") as f:
		f.write ("# tools/pkg/versions.ini -- la version de chaque paquet (mkrepo.py --bump les monte)\n")
		vcfg.write (f)
	head = "# index des paquets de Korin OS pour PC" if PC else "# index des paquets d'ARMPI"
	idx = [head + " -- tools/pkg/mkrepo.py (docs/PAQUETS.md)", "[repo]", "name = " + a.repo_name,
	       "date = " + datetime.datetime.now (datetime.timezone.utc).strftime ("%Y-%m-%d %H:%M"), "abi = %d" % abi,
	       "packages = %d", "board = " + BOARD, ""]
	sizes, excluded, total = [], [], 0
	kept = set ()
	if a.keep_unbuilt:				# (les paquets publies mais pas construits ici : l'entree publiee, telle quelle)
		built = set (p["name"] for p in pkgs)
		drop = set (x for x in a.drop.split (",") if x)
		for n in old.sections ():
			if n == "repo" or n in built or n in drop: continue
			o = old[n]
			idx += ["[%s]" % n] + ["%s = %s" % kv for kv in o.items ()] + [""]
			kept.add (os.path.basename (o.get ("file", "")))
			sizes.append ((n, o.get ("version", "?") + " (garde)", int (o.get ("size", "0")), int (o.get ("installed", "0"))))
			total += int (o.get ("size", "0"))
	for p in pkgs:
		if p["publish"] == "0":			# (publish = 0 : sur la carte, jamais sur le depot public)
			p["gone"] = True
			continue
		if "any" in p:				# (l'archive du Pi, publiee une seule fois : citee telle quelle)
			o = p["any"]; f = o["file"]
			if "://" not in f: f = "../" + f
			up = lambda k, v: f if k == "file" else ("../" + v if k == "icon" and "://" not in v else v)	# (relatifs au Pi)
			idx += ["[%s]" % p["name"]] + ["%s = %s" % (k, up (k, v)) for k, v in o.items () if k != "arch"] + ["arch = any", ""]
			sizes.append ((p["name"], p["version"] + " (any)", int (o.get ("size", "0")), int (o.get ("installed", "0"))))
			total += int (o.get ("size", "0"))
			continue
		fn = "pkgs/%s-%s.opk" % (p["name"], p["version"])
		path = os.path.join (a.out, fn)
		if p["release"] == "1":			# (release = 1 : dans la Release « packages », hors de GitHub Pages)
			os.makedirs (a.release_dir, exist_ok = True)
			fn = "%s/%s%s-%s.opk" % (RELEASE_URL, RELEASE_PREFIX, p["name"], p["version"])
			path = os.path.join (a.release_dir, os.path.basename (fn))
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
		    + (["replaces = " + ", ".join (p["replaces"])] if p["replaces"] else []) 		    + (["part = " + p["part"]] if p["part"] else []) + ["content = " + p["content"], ""]
	keep = set ("%s-%s.opk" % (p["name"], p["version"]) for p in pkgs if "gone" not in p) | kept	# les archives des anciennes versions
	keep |= set (RELEASE_PREFIX + k for k in keep)
	for d in (os.path.join (a.out, "pkgs"), a.release_dir):
		for f in os.listdir (d) if os.path.isdir (d) else []:
			if f.endswith (".opk") and f not in keep: os.remove (os.path.join (d, f))
	idx[5] = "packages = %d" % sum (1 for l in idx[7:] if l.startswith ("["))
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
