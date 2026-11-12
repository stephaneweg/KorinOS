"""Lance pytest sous Korin pour les essais de l'etape 5 (docs/PYTHON-PORT.md, « Étape 5 ») :

    cd /home/pytest/rust-ext/<paquet> && python3 /home/pytest/run_pytest.py <options de pytest>

os.getcwd () rend « SD0:/home/... » sous Korin, que pathlib ne tient pas pour absolu : pytest
recolle alors le dossier courant devant chaque chemin (SD0:/x/SD0:/x/tests/...) et ses --ignore ne
s'appliquent plus. En attendant l'arbre POSIX des chemins (etape 6), ce lanceur rend la forme
virtuelle « /home/... ». Mais « /x » vise la racine du volume *courant* : apres un chdir dans
/tmp (RAM0:), « /home » serait RAM0:/home ; chdir reprend donc « SD0: » devant un chemin en « / »
quand le dossier courant n'est pas sur SD0:.
"""
import os
import sys

_getcwd = os.getcwd


def getcwd():
    d = _getcwd()
    vol, sep, rest = d.partition(":")
    return (rest or "/") if sep and vol.upper() == "SD0" else d


_chdir = os.chdir


def chdir(p):
    if isinstance(p, os.PathLike):
        p = os.fspath(p)
    if isinstance(p, str) and p.startswith("/") and not _getcwd().upper().startswith("SD0:"):
        p = "SD0:" + p
    return _chdir(p)


os.getcwd = getcwd
os.chdir = chdir
import pytest  # noqa: E402

sys.exit(pytest.main(sys.argv[1:]))
