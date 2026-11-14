#!/usr/bin/env python3
"""checkrepo.py -- l'essai du depot avant publication (publish.sh, docs/PAQUETS.md) : la signature de l'index avec la
cle publique des cartes, puis chaque archive de l'index : sa taille, son SHA-256, son manifeste, ses noms (rien sous
var/, ni etc/wifi*, ni etc/remote*, ni hors de la carte), moins de 100 Mo, les besoins presents dans le depot, board = armpi.
Le PC (ARCH=x86_64 ou BOARD=pc, comme mkrepo.py) : board = pc, les archives release = 1 dans build/pkg-release-pc,
celles du Pi citees par ../pkgs/ (arch = any) lues a cote du depot, et aussi refuses : etc/options.txt, etc/pointer.ini,
korin/boot.cfg, .vibe/.env, .vibe/config.toml, acpi/ (les reglages et les comptes de l'utilisateur).

    python3 tools/pkg/checkrepo.py DOSSIER_DU_DEPOT CLE_PUBLIQUE.pub
"""
import configparser, hashlib, os, sys, zipfile
PC = os.environ.get ("ARCH") == "x86_64" or os.environ.get ("BOARD") == "pc"
BOARD = "pc" if PC else "armpi"
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

def bad_name (n):
	if PC and (n in ("etc/options.txt", "etc/pointer.ini", "korin/boot.cfg", "korin/installed.txt") or n.startswith (("acpi/", ".vibe/.env", ".vibe/config.toml"))): return True
	return not n or n.startswith ("/") or ":" in n or "\\" in n or ".." in n.split ("/") or n.startswith ("var/") or n.startswith ("etc/wifi") or n.startswith ("etc/remote")

def main ():
	repo, pubfile = sys.argv[1:3]
	data = open (os.path.join (repo, "index.txt"), "rb").read ()
	sig = bytes.fromhex (open (os.path.join (repo, "index.sig")).read ().strip ())
	pub = serialization.load_pem_public_key (open (pubfile, "rb").read ())
	try:
		pub.verify (sig, data, ec.ECDSA (hashes.SHA256 ()))
	except Exception:
		sys.exit ("checkrepo: la signature de l'index n'est pas bonne pour la cle des cartes (%s) : RIEN n'est publie" % pubfile)
	print ("checkrepo: signature bonne pour la cle des cartes")
	cfg = configparser.ConfigParser (interpolation = None); cfg.optionxform = str
	cfg.read_string (data.decode ("utf-8"))
	if cfg["repo"].get ("board") != BOARD: sys.exit ("checkrepo: board = %s" % cfg["repo"].get ("board"))
	names = [s for s in cfg.sections () if s != "repo"]
	errs = []
	for n in names:
		p = cfg[n]; f = os.path.join (repo, p["file"])
		rel = "://" in p["file"]			# (dans la Release « packages » : sa copie locale, build/pkg-release)
		if rel: f = os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "..", "build", "pkg-release-pc" if PC and os.path.basename (p["file"]).startswith ("pc-") else "pkg-release", os.path.basename (p["file"]))
		elif p["file"].startswith ("../") and not (PC and p.get ("arch") == "any" and p["file"].startswith ("../pkgs/") and ".." not in p["file"][3:]):
			errs.append (n + " : hors du depot : " + p["file"]); continue
		if not os.path.exists (f): errs.append (n + " : archive absente"); continue
		size = os.path.getsize (f)
		if size != int (p["size"]): errs.append (n + " : taille")
		if size >= 100 * 1000 * 1000 and not rel: errs.append (n + " : plus de 100 Mo")
		h = hashlib.sha256 ()
		with open (f, "rb") as fh:
			for b in iter (lambda: fh.read (1 << 20), b""): h.update (b)
		if h.hexdigest () != p["sha256"]: errs.append (n + " : SHA-256")
		with zipfile.ZipFile (f) as z:
			if z.testzip (): errs.append (n + " : ZIP abime")
			man = z.read ("PKG/manifest.ini").decode ("utf-8")
			if ("name = %s\n" % n) not in man or ("version = %s\n" % p["version"]) not in man: errs.append (n + " : manifeste")
			for e in z.namelist ():
				if not e.startswith ("PKG/") and bad_name (e): errs.append ("%s : nom refuse : %s" % (n, e))
		for need in p.get ("needs", "").split (","):
			w = need.split ()
			if w and w[0] != "abi" and w[0] not in names: errs.append ("%s : besoin absent du depot : %s" % (n, w[0]))
	if errs: sys.exit ("checkrepo:\n  " + "\n  ".join (errs))
	print ("checkrepo: %d paquets bons" % len (names))

if __name__ == "__main__":
	main ()
