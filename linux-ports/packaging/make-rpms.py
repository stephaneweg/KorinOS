#!/usr/bin/env python3
# make-rpms.py -- fait les paquets rpm de Koton, d'Arabesque, de Budget mensuel, de TerraCube et de Forge (Fedora) sans rpmbuild :
# cargo-generate-rpm écrit le fichier rpm lui-même, comme le fait Tauri.
#
#   python3 -I linux-ports/packaging/make-rpms.py      (après « make » dans linux-ports)
#   -> linux-ports/rpm/korin-<app>-<v>-<RELEASE>.x86_64.rpm (koton, arabesque, budget, terracube, forge ; ou ceux nommés en
#      arguments)
#
# Prérequis : cargo install cargo-generate-rpm --locked
#
# Les binaires trouvent leurs données en remontant de leur dossier (korin_paths.cpp : <bin>/../..). Les paquets gardent
# donc l'arborescence du dépôt sous /opt/korin :
#   /opt/korin/linux-ports/bin/...              programmes et greffons
#   /opt/korin/src/apps/<app>/{data,demos,presets}, /opt/korin/src/apps/fmtracker/ins, /opt/korin/skel/sd0/res/lang
#   (Budget : src/apps/budget/app.txt seul, pour que le binaire reconnaisse la racine ; TerraCube : data/ (les exemples
#   Python et BASIC), Forge : lang/ ; les deux : app.txt et SKILL.md, la compétence pour un agent IA)
#   /usr/bin/<app>                              un lanceur (exec du vrai binaire)
#   /usr/share/applications, /usr/share/icons   l'entrée du menu et l'icône
import os
import shutil
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
PORTS = os.path.dirname(HERE)
ROOT = os.path.dirname(PORTS)
WORK = os.path.join(PORTS, 'build', 'rpm')
OUT = os.path.join(PORTS, 'rpm')
OPT = '/opt/korin'
RELEASE = '6'			# à augmenter quand les binaires changent sans changer de version

COMMON_REQ = {'gtk4': '*'}
SOUND_REQ = {'fluidsynth-libs': '*', 'fluid-soundfont-gm': '*'}

APPS = {
    'koton': {
        'summary': 'Koton : studio de composition de Korin OS (GTK 4, FluidSynth)',
        'desc': "Koton est un studio de composition : on pose des blocs (accords, accompagnements, riffs, batterie, "
                "lignes mélodiques) sur des pistes, des générateurs écrivent les notes et le tout joue sur une SoundFont.",
        'name': 'Koton', 'mime': '',
        'bins': ['linux-ports/bin/koton', 'linux-ports/bin/llm'],
        'trees': ['linux-ports/bin/koton-plugins', 'src/apps/koton/data', 'src/apps/koton/demos',
                  'src/apps/koton/lang', 'skel/sd0/res/lang'],
        'req': dict(SOUND_REQ, **{'openssl-libs': '*'}), 'recommends': {'lame-libs': '*'},
        'categories': 'AudioVideo;Audio;Music;', 'exec': 'koton %f',
    },
    'arabesque': {
        'summary': 'Arabesque : composition algorithmique par fractales, de Korin OS (GTK 4, FluidSynth)',
        'desc': "Arabesque compose de la musique à partir d'une image : un tracé fractal (IFS) parcourt l'image et la "
                "couleur de chaque point devient une note de l'une des dix parties General MIDI.",
        'name': 'Arabesque', 'mime': '',
        'bins': ['linux-ports/bin/arabesque'],
        'trees': ['src/apps/arabesque/data', 'src/apps/arabesque/presets', 'src/apps/fmtracker/ins',
                  'skel/sd0/res/lang'],
        'req': dict(SOUND_REQ), 'recommends': {'lame-libs': '*'},
        'categories': 'AudioVideo;Audio;Music;', 'exec': 'arabesque %f',
    },
    'budget': {
        'summary': 'Budget mensuel : le budget du mois de paie, de Korin OS (GTK 4, banque par Enable Banking)',
        'desc': "Budget mensuel suit le mois de paie : charges fixes, paiements en plusieurs fois, chèques repas, "
                "dépenses courantes et ce qu'il reste à dépenser par jour ; la banque se synchronise par Enable Banking.",
        'name': 'Budget mensuel', 'mime': '',
        'bins': ['linux-ports/bin/budget'],
        'files': ['src/apps/budget/app.txt'],
        'trees': ['skel/sd0/res/lang'],
        'req': {'openssl-libs': '*', 'libsecret': '*'}, 'recommends': {'xdg-utils': '*'},
        'categories': 'Office;Finance;', 'exec': 'budget',
    },
    'terracube': {
        'summary': 'TerraCube : bac à sable de blocs en 3D de Korin OS (OpenGL ES 3, scripts Python, agent IA)',
        'desc': "TerraCube est un monde sans bord fait de blocs, généré d'une graine : on le parcourt, on creuse et on "
                "construit, en créatif ou en survie ; des scripts (Python, port 4730) et un agent IA (MCP) peuvent le piloter.",
        'name': 'TerraCube', 'mime': '',
        'bins': ['linux-ports/bin/terracube'],
        'files': ['src/apps/terracube/app.txt', 'src/apps/terracube/SKILL.md'],
        'trees': ['src/apps/terracube/data', 'skel/sd0/res/lang'],
        'req': {'libglvnd-egl': '*', 'libglvnd-gles': '*', 'fluidsynth-libs': '*', 'libsecret': '*', 'zlib-ng-compat': '*'},
        'recommends': {'mesa-libEGL': '*', 'mesa-dri-drivers': '*', 'python3': '*'},
        'categories': 'Game;Simulation;', 'exec': 'terracube',
    },
    'forge': {
        'summary': 'Forge : la CAO de Korin OS (OpenCASCADE, PlaneGCS, export STEP, agent IA)',
        'desc': "Forge dessine des pièces en 3D : solides exacts d'OpenCASCADE, congés sur n'importe quelle arête, "
                "esquisses avec contraintes (PlaneGCS), historique paramétrique, export STEP, IGES, STL ; pilotable par un agent IA (MCP).",
        'name': 'Forge', 'mime': '',
        'bins': ['linux-ports/bin/forge'],
        'files': ['src/apps/forge/app.txt', 'src/apps/forge/SKILL.md'],
        'trees': ['src/apps/forge/lang', 'skel/sd0/res/lang'],
        'req': {'opencascade-foundation': '*', 'opencascade-modeling': '*', 'opencascade-ocaf': '*',
                'opencascade-visualization': '*', 'libsecret': '*', 'zlib-ng-compat': '*'},
        'recommends': {},
        'categories': 'Graphics;Engineering;3DGraphics;', 'exec': 'forge %f',
        'icon': 'armpi',	# (Forge n'a pas encore d'icône dans tools/icons : celle de Korin)
    },
}


def version(app):
    """La version du paquet PC de Korin (tools/pkg/versions.ini, section [pc]), sinon la première trouvée."""
    v, section, first = None, '', None
    for line in open(os.path.join(ROOT, 'tools', 'pkg', 'versions.ini'), encoding='utf-8'):
        line = line.strip()
        if line.startswith('['):
            section = line.strip('[]')
        elif '=' in line and line.split('=')[0].strip() == app:
            val = line.split('=', 1)[1].strip()
            first = first or val
            if section == 'pc':
                v = val
    return v or first or '1.0.0'


def icon_png(app, size, dest):
    """Une taille de l'icône Korin (tools/icons/rendered/<app>.rgba, format ARIC) en PNG, alpha dé-prémultiplié."""
    d = open(os.path.join(ROOT, 'tools', 'icons', 'rendered', APPS[app].get('icon', app) + '.rgba'), 'rb').read()
    assert d[:4] == b'ARIC'
    n = struct.unpack_from('<H', d, 6)[0]
    sizes = {}
    for i in range(n):
        z, _, off = struct.unpack_from('<HHI', d, 8 + 8 * i)
        sizes[z] = off
    z = size if size in sizes else max(sizes)
    off = sizes[z]
    rows = bytearray()
    for y in range(z):
        rows.append(0)
        for x in range(z):
            b, g, r, a = d[off + 4 * (y * z + x): off + 4 * (y * z + x) + 4]
            if a:
                r, g, b = (min(255, (c * 255 + a // 2) // a) for c in (r, g, b))
            rows += bytes((r, g, b, a))

    def chunk(t, data):
        return struct.pack('>I', len(data)) + t + data + struct.pack('>I', zlib.crc32(t + data) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', z, z, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(rows), 9)) + chunk(b'IEND', b'')
    with open(dest, 'wb') as f:
        f.write(png)
    return z


def toml_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def build(app, info):
    ver = version(app)
    w = os.path.join(WORK, app)
    shutil.rmtree(w, ignore_errors=True)
    os.makedirs(os.path.join(w, 'src'))
    open(os.path.join(w, 'src', 'lib.rs'), 'w').close()
    assets = []

    def add(src, dest, mode):
        assets.append((src, dest, mode))

    for b in info['bins']:
        add(os.path.join(ROOT, b), f'{OPT}/{b}', '755')
    for f in info.get('files', []):
        add(os.path.join(ROOT, f), f'{OPT}/{f}', '644')
    for t in info['trees']:
        top = os.path.join(ROOT, t)
        for dp, ds, fs in os.walk(top):
            ds[:] = [d for d in ds if d != '__pycache__']	# (les .pyc d'un essai local)
            for f in sorted(fs):
                src = os.path.join(dp, f)
                rel = os.path.relpath(src, ROOT)
                add(src, f'{OPT}/{rel}', '755' if os.access(src, os.X_OK) and f == 'main' else '644')
    launcher = os.path.join(w, app)
    with open(launcher, 'w') as f:
        f.write(f'#!/bin/sh\nexec {OPT}/linux-ports/bin/{app} "$@"\n')
    add(launcher, f'/usr/bin/{app}', '755')
    desktop = os.path.join(w, f'org.korin.{app}.desktop')
    with open(desktop, 'w', encoding='utf-8') as f:
        f.write('[Desktop Entry]\nType=Application\n'
                f'Name={info["name"]}\nComment={info["summary"].split(" : ", 1)[1].split(" (")[0]}\n'
                f'Exec={info["exec"]}\nIcon=korin-{app}\nTerminal=false\nCategories={info["categories"]}\n'
                + (f'MimeType={info["mime"]}\n' if info['mime'] else ''))
    add(desktop, f'/usr/share/applications/org.korin.{app}.desktop', '644')
    for z in (48, 64):
        png = os.path.join(w, f'icon{z}.png')
        got = icon_png(app, z, png)
        add(png, f'/usr/share/icons/hicolor/{got}x{got}/apps/korin-{app}.png', '644')

    req = dict(COMMON_REQ, **info['req'])
    lines = ['[package]', f'name = "korin-{app}"', f'version = {toml_str(ver)}', 'edition = "2021"',
             'license = "MIT"', f'description = {toml_str(info["summary"])}',
             'homepage = "https://github.com/stephaneweg/KorinOS"', '',
             '[package.metadata.generate-rpm]', f'release = "{RELEASE}"', f'summary = {toml_str(info["summary"])}',
             f'post_install_script = "gtk-update-icon-cache -q /usr/share/icons/hicolor 2>/dev/null || :"',
             'assets = [']
    for src, dest, mode in assets:
        lines.append(f'  {{ source = {toml_str(src)}, dest = {toml_str(dest)}, mode = "{mode}" }},')
    lines += [']', '', '[package.metadata.generate-rpm.requires]']
    lines += [f'{k} = {toml_str(v)}' for k, v in req.items()]
    lines += ['', '[package.metadata.generate-rpm.recommends]']
    lines += [f'{k} = {toml_str(v)}' for k, v in info.get('recommends', {}).items()]
    lines += ['', '[workspace]', '']
    with open(os.path.join(w, 'Cargo.toml'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    os.makedirs(OUT, exist_ok=True)
    cargo = shutil.which('cargo') or os.path.expanduser('~/.cargo/bin/cargo')
    subprocess.run([cargo, 'generate-rpm', '-o', OUT], cwd=w, check=True)
    print(f'korin-{app} {ver} : {len(assets)} fichiers')


def main():
    for app, info in APPS.items():
        if len(sys.argv) > 1 and app not in sys.argv[1:]:
            continue
        build(app, info)


if __name__ == '__main__':
    main()
