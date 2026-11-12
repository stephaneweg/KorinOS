"""testimage : la carte d'essai de Python pour QEMU, build/python/sd.img (docs/PYTHON-PORT.md) :
build/sdroot (fait par build.ps1, Python compris), un mot de passe d'essai pour telnetd et ftpd
(« essai », que tools/armpi_remote.py lit dans ARMPI_REMOTE_PW), Lib/test et les modules d'essai
en .so (build/python/tests, faits par tools/python/build.sh) dans SD0:/python/lib/python3.14, et les
scripts d'essai de tools/python/tests (et le client ACP de tools/vibe) dans SD0:/home/pytest. Ni build/sdroot ni build/sd.img ne changent.
    python -I tools/python/testimage.py          (depuis la racine du worktree, apres build.ps1)
"""
import os
import shutil
import subprocess
import sys

root = os.getcwd()
t = os.path.join(root, "build", "python", "testroot")
shutil.rmtree(t, ignore_errors=True)
shutil.copytree(os.path.join(root, "build", "sdroot"), t)
open(os.path.join(t, "etc", "remote.txt"), "w", encoding="ascii").write("password = essai\n")
bt = os.path.join(root, "build", "python", "tests")
lib = os.path.join(t, "python", "lib", "python3.14")
if os.path.isdir(bt):
    shutil.copytree(os.path.join(bt, "test"), os.path.join(lib, "test"), dirs_exist_ok=True)
    shutil.copytree(os.path.join(bt, "lib-dynload"), os.path.join(lib, "lib-dynload"), dirs_exist_ok=True)
    if os.path.isdir(os.path.join(bt, "site-packages")):    # korin_panic_probe (etape 5)
        shutil.copytree(os.path.join(bt, "site-packages"), os.path.join(lib, "site-packages"), dirs_exist_ok=True)
    if os.path.isdir(os.path.join(bt, "rust-ext")):         # essais pytest de l'etape 5
        shutil.copytree(os.path.join(bt, "rust-ext"), os.path.join(t, "home", "pytest", "rust-ext"))
tests = os.path.join(root, "tools", "python", "tests")
if os.path.isdir(tests):
    shutil.copytree(tests, os.path.join(t, "home", "pytest"), dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("panic"))
    # le client ACP de Vibe et son serveur MCP d'essai (etape 6, tools/python/tests/step6.py)
    for f in ("acp_client.py", "mcp_test_server.py"):
        shutil.copy(os.path.join(root, "tools", "vibe", f), os.path.join(t, "home", "pytest", f))
    # l'essai d'agent, le client terminal de Vibe, dans un pty (etape 8, tools/agent/ptytest.py)
    shutil.copy(os.path.join(root, "tools", "agent", "ptytest.py"), os.path.join(t, "home", "pytest", "ptytest.py"))
subprocess.check_call([sys.executable, "-I", "tools/mkdisk.py", "build/python/sd.img", "--fat32", t,
                       "--exfat", "build/exfat_root"], stdout=subprocess.DEVNULL)
print("build/python/sd.img", os.path.getsize("build/python/sd.img"))
