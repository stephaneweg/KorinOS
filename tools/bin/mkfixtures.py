"""mkfixtures.py -- les essais par comparaison des commandes de /bin (docs/BIN-C.md) :

  python -I tools/bin/mkfixtures.py CARTE [commande...]

Écrit dans CARTE/bt (le dossier SD0:/bt de la carte d'essai) :
  - les fichiers d'essai (textes, nombres, champs, lignes sans fin, octets hauts, un arbre) ;
  - run.sh : pour chaque ligne de src/bin/<commande>/test.txt (toutes les commandes qui en ont, ou
    celles nommées), « $ ligne », la sortie (sortie et erreur standard mêlées) et « [code N] ».
    Lancé par bash sur Korin (tools/bin/compare.sh), dans SD0:/bt ; RAM0:/tmp/w est un dossier
    de travail vide au départ.

Une ligne de test.txt est une ligne de bash ; « # » en tête : commentaire. « @lang en » : la suite
du fichier dans cette langue (SD0:/etc/system.ini réécrit), « @lang fr » pour revenir.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def fixtures(bt):
    fruits = ["pomme", "banane", "banane", "cerise", "pomme", "datte", "datte", "datte",
              "figue", "", "", "kiwi", "Abricot", "abricot", "zeste"]
    write(os.path.join(bt, "a.txt"), "\n".join(fruits) + "\n")
    write(os.path.join(bt, "lines.txt"), "".join("ligne %d\n" % i for i in range(1, 26)))
    write(os.path.join(bt, "nonl.txt"), "un\ndeux\ntrois sans fin")
    write(os.path.join(bt, "empty.txt"), "")
    write(os.path.join(bt, "nl1.txt"), "\n")
    write(os.path.join(bt, "nums.txt"), "10\n9\n-3\n  42 x\n100\n007\nabc\n-0\n9 b\n9 a\n 5\n")
    write(os.path.join(bt, "csv.txt"), "a,b,c,d\n1,2,3,4\nsans delimiteur\n,,\nx,,y\nun,deux\n")
    write(os.path.join(bt, "tab.txt"), "a\tb\tc\n1\t2\t3\nseul\n\t\tfin\n")
    write(os.path.join(bt, "words.txt"), "  bonjour   le\tmonde \n\nfin\tde  ligne\r\nder")
    write(os.path.join(bt, "high.txt"), "été à la plage\nnaïve\n".encode("utf-8") + b"\xff\xfe\x01\n")
    write(os.path.join(bt, "dup.txt"), "a\na\nb\nc\nc\nc\nd\na\n")
    long = ("x" * 5000 + "\n") * 3
    write(os.path.join(bt, "long.txt"), long)
    big = "".join("%05d abcdefghijklmnopqrstuvwxyz\n" % ((i * 7919) % 10007) for i in range(3000))
    write(os.path.join(bt, "big.txt"), big)
    write(os.path.join(bt, "tree", "f1.txt"), "un\n")
    write(os.path.join(bt, "tree", "sub", "f2.txt"), "deux\n")
    write(os.path.join(bt, "tree", "sub", "deep", "f3.txt"), "trois\n")
    write(os.path.join(bt, "tree", "vide.txt"), "")
    write(os.path.join(bt, "sp ace.txt"), "espace\n")


def quote(s):
    return "'" + s.replace("'", "'\\''") + "'"


def tests(bt, names):
    out = ["# genere par tools/bin/mkfixtures.py", "cd SD0:/bt", "mkdir RAM0:/tmp",
           "rm -r RAM0:/tmp/w", "mkdir RAM0:/tmp/w",
           "cp SD0:/etc/system.ini RAM0:/tmp/system.ini.orig",
           "lang() { : > SD0:/etc/system.ini; { cat RAM0:/tmp/system.ini.orig; echo \"lang=$1\"; } >> SD0:/etc/system.ini; }"]
    if not names:
        names = sorted(n for n in os.listdir(os.path.join(ROOT, "src", "bin"))
                       if os.path.exists(os.path.join(ROOT, "src", "bin", n, "test.txt")))
    for n in names:
        path = os.path.join(ROOT, "src", "bin", n, "test.txt")
        out.append("echo '=== %s'" % n)
        with open(path, encoding="utf-8") as f:
            for line in f.read().splitlines():
                if not line.strip() or line.startswith("#"):
                    continue
                if line.startswith("@lang "):
                    out.append("lang %s" % line.split()[1])
                    continue
                out.append("echo %s" % quote("$ " + line))
                out.append("cd SD0:/bt")
                out.append(line)
                out.append('echo "[code $?]"')
        out.append("lang fr")
    out.append("echo '=== fin'")
    write(os.path.join(bt, "run.sh"), "\n".join(out) + "\n")
    # boot.sh : lancé au démarrage (SD0:/etc/startup.txt de la carte d'essai, tools/bin/compare.sh) ;
    # sortie en ajout (bash et ses fils ne partagent pas la position d'un fichier ouvert par « > ») ;
    # puis la sortie sur la console du noyau (bash lancé par init2 n'a pas d'autre sortie), entre deux
    # repères : compare.sh la lit dans le journal de l'UART de QEMU
    write(os.path.join(bt, "boot.sh"), "\n".join([
        "# genere par tools/bin/mkfixtures.py",
        ": > RAM0:/bt_out.txt",
        "bash SD0:/bt/run.sh < SD0:/bt/empty.txt >> RAM0:/bt_out.txt 2>&1",
        "echo @@BT_DEBUT", "cat RAM0:/bt_out.txt", "echo @@BT_FIN"]) + "\n")
    print("mkfixtures : %d commandes, %s" % (len(names), os.path.join(bt, "run.sh")))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    bt = os.path.join(sys.argv[1], "bt")
    fixtures(bt)
    tests(bt, sys.argv[2:])


if __name__ == "__main__":
    main()
