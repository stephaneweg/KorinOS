"""Essai de demarrage pour la CI (docs/CI.md) : lance build/kernel8.img dans QEMU raspi4b
sans affichage, avec les modules de demarrage (initramfs) et build/sd.img, et attend sur
l'UART les marqueurs d'un bon demarrage, dans l'ordre. Echec si un marqueur d'erreur
apparait ou si le delai est depasse.

Usage : python -I tools/ci/boot_test.py [--qemu chemin] [--timeout 180] [--uart build/ci-uart.log]
QEMU : --qemu, sinon la variable ARMPI_QEMU, sinon C:\\Program Files\\qemu\\qemu-system-aarch64.exe
Code de sortie : 0 si tous les marqueurs sont vus, 1 sinon.
"""
import os
import subprocess
import sys
import time

# marqueurs d'un bon demarrage, dans l'ordre (src/kernel/kernel.asm, src/sys/elegant,
# src/svc/init2 : son preload est la derniere etape de startup.txt)
MARKERS = [
    ("noyau lance", "Korin OS - jalon"),
    ("init (initramfs)", "Init             : services de demarrage (initramfs)"),
    ("SD0: servi", "Init             : SD0: servi par les services"),
    ("noyau pret", "Noyau            : d\u00e9marrage termin\u00e9"),
    ("Elegant", "elegant : ecran pris, en service"),
    ("fin du demarrage (preload)", "init2 : preload : "),
]
# marqueurs d'echec : arret immediat
FATAL = [
    "*** EXCEPTION NOYAU",
    "] faute, process tué",
    "SD0: non servi apres",
    "pas de module init",
    "Carte SD         : absente",
    "Framebuffer : echec",
    "elegant : memoire insuffisante",
]


def arg(nom, defaut):
    if nom in sys.argv:
        return sys.argv[sys.argv.index(nom) + 1]
    return defaut


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    os.chdir(root)
    qemu = arg("--qemu", os.environ.get("ARMPI_QEMU", r"C:\Program Files\qemu\qemu-system-aarch64.exe"))
    timeout = float(arg("--timeout", "180"))
    uart = arg("--uart", "build/ci-uart.log")

    # modules de demarrage : la ligne initramfs de boot/config.txt (comme tools/qemu_drive.py)
    noms = []
    for l in open("boot/config.txt", encoding="utf-8"):
        if l.startswith("initramfs "):
            noms = ["build/modules/" + n for n in l.split()[1].split(",")]
            break
    ini = "build/ci-initramfs.bin"
    with open(ini, "wb") as f:
        for n in noms:
            f.write(open(n, "rb").read())

    if os.path.exists(uart):
        os.remove(uart)
    cmd = [qemu, "-M", "raspi4b", "-kernel", "build/kernel8.img",
           "-dtb", "firmware/bcm2711-rpi-4-b.dtb", "-serial", "file:" + uart,
           "-display", "none", "-monitor", "none",
           "-drive", "file=build/sd.img,if=sd,format=raw",
           "-device", "usb-kbd", "-device", "usb-tablet",
           "-device", "usb-net,netdev=n0", "-netdev", "user,id=n0",
           "-device", "loader,file=%s,addr=0x02000000,force-raw=on" % ini]
    print("QEMU :", " ".join(cmd), flush=True)
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    debut = time.time()
    vus = 0
    erreur = None
    texte = ""
    try:
        while time.time() - debut < timeout:
            time.sleep(1)
            if p.poll() is not None:
                erreur = "QEMU s'est arrete (code %d) : %s" % (
                    p.returncode, p.stderr.read().decode(errors="replace").strip())
                break
            if os.path.exists(uart):
                with open(uart, "rb") as f:
                    texte = f.read().decode("utf-8", errors="replace")
            while vus < len(MARKERS) and MARKERS[vus][1] in texte:
                print("[%5.1f s] vu : %s" % (time.time() - debut, MARKERS[vus][0]), flush=True)
                vus += 1
            fatal = [m for m in FATAL if m in texte]
            if fatal:
                erreur = "marqueur d'echec : " + fatal[0]
                break
            if vus == len(MARKERS):
                break
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
    if vus < len(MARKERS) and not erreur:
        erreur = "delai de %d s depasse, attendu : %s" % (timeout, MARKERS[vus][0])
    if erreur:
        print("ECHEC :", erreur)
        print("---- fin de l'UART (%s) ----" % uart)
        print("\n".join(texte.splitlines()[-40:]))
        return 1
    print("demarrage correct en %.1f s" % (time.time() - debut))
    return 0


if __name__ == "__main__":
    sys.exit(main())
