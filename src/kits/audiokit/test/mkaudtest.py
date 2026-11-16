"""mkaudtest.py - écrit src/kits/audiokit/test/audtest.c : l'en-tête commun des essais de kits
(appels système, sortie, sommes : repris d'artest.c), la liste des fonctions d'audiokit.abi,
puis audtest_body.c (les essais) et main.

    python -I src/kits/audiokit/test/mkaudtest.py     (depuis la racine du dépôt)
"""
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
T = os.path.join(ROOT, 'src', 'kits', 'audiokit', 'test')
NL = '\n'


def main():
    art = open(os.path.join(ROOT, 'src', 'kits', 'archivekit', 'test', 'artest.c'), encoding='utf-8').read()
    lines = art.split(NL)
    a = next(i for i, l in enumerate(lines) if l.startswith('/* ---- appels système'))
    b = next(i for i, l in enumerate(lines) if l.startswith('/* ---- données d'))
    common = NL.join(lines[a:b])
    common = common.replace('static long hres(long h);', '')
    common = common.replace('static long called[AR_COUNT_FN];', 'static long called[X_COUNT];')
    common = common.replace('typedef long (*fn6)(long, long, long, long, long, long);',
                            'typedef long (*fn6)(long, long, long, long, long, long, long);')
    common = common.replace('static long call(int i, long a, long b, long c, long d, long e, long f)',
                            'static long call(int i, long a, long b, long c, long d, long e, long f, long g)')
    common = common.replace('return fn(a, b, c, d, e, f);', 'return fn(a, b, c, d, e, f, g);')
    common = common.replace('return fn(a, b, c, 0, 0, 0);', 'return fn(a, b, c, 0, 0, 0, 0);')
    common = re.sub(r'(#define C\d\(i[^)]*\) call\(i[^\n]*)\)', r'\1, 0)', common)
    assert 'long g)' in common
    abi = open(os.path.join(ROOT, 'src', 'kits', 'audiokit', 'audiokit.abi'), encoding='latin-1').read()
    fns = re.findall(r'^kit_fn\s+(\w+)', abi, re.M)
    out = ['/* audtest : essai différentiel d\'AudioKit (src/kits/audiokit), sans bibliothèque, pour',
           ' * AArch64 (Korin sur le Pi) et x86-64 (Korin sur PC). Écrit par mkaudtest.py (en-tête',
           ' * d\'artest.c) depuis audtest_body.c : modifier audtest_body.c, puis relancer mkaudtest.py.',
           ' *',
           ' * N\'écrit que ce qui ne dépend ni des adresses ni de l\'heure : codes rendus, tailles, sommes',
           ' * FNV-1a des sorties (PCM décodé, octets encodés, rendus des synthés). Le kit en asm et le',
           ' * kit en C doivent donner la même sortie, octet pour octet ; le PC aussi, pour la partie en',
           ' * mémoire (les fichiers d\'essai sont embarqués). audtest -f : en plus, après « === fichiers',
           ' * et son », fichiers de /tmp et /, le service de son, le lecteur, la FM en direct.',
           ' *',
           ' * Construction : src/kits/audiokit/test/build.sh (dans WSL). */',
           '#include <stddef.h>', '#include <stdint.h>', '',
           '#define SYS_EXIT 0x10', '#define SYS_SLEEP 0x17', '#define SYS_PRINT 0x60', '#define SYS_LIB_OPEN 0x64',
           '#define KIT_MAGIC 0x54494B41',
           '/* AppKit (rangs de appkit.abi) */',
           '#define AK_F_OUVRIR 14', '#define AK_F_ECRIRE 16', '#define AK_F_FERMER 18', '#define AK_F_MKDIR 21',
           '#define AK_F_UNLINK 22', '#define AK_F_PRINT 25', '#define AK_F_KIT_LOAD 31', '',
           '/* rangs de audiokit.abi */', 'enum {']
    for f in fns:
        out.append('\tX_' + f[3:].upper() + ',')
    out += ['\tX_COUNT', '};', '', 'static const char *const fn_names[X_COUNT] = {']
    for f in fns:
        out.append('\t"' + f + '",')
    out += ['};', '', common, '',
            'static uint64_t lcg = 88172645463325252ULL;', '',
            'static uint32_t rnd(void)', '{',
            '\tlcg = lcg * 6364136223846793005ULL + 1442695040888963407ULL;',
            '\treturn (uint32_t)(lcg >> 33);', '}', '']
    out.append(open(os.path.join(T, 'audtest_body.c'), encoding='utf-8').read())
    out += ['static int has_flag(const char *line, char f)', '{', '\tif (!line)', '\t\treturn 0;',
            '\tfor (long i = 0; line[i]; i++)', "\t\tif (line[i] == '-' && line[i + 1] == f)", '\t\t\treturn 1;',
            '\treturn 0;', '}', '',
            'static void run(const char *line)', '{', '\tps("audtest : AudioKit\\n");',
            '\tlong a = sys2(SYS_LIB_OPEN, "appkit", 26);', '\tif (a >= 0) {',
            '\t\tconst uint32_t *at = (const uint32_t *)a;', '\t\tf_print = (fn_print)(a + at[2 + AK_F_PRINT]);',
            '\t\tak = a;', '\t}',
            '\tkit = open_kit("audiokit", 1);', '\tif (kit < 0) {', '\t\tps("audiokit absent : ");', '\t\tpd(kit);',
            '\t\tnl();', '\t\tflush();', '\t\tsys6(SYS_EXIT, 127, 0, 0, 0, 0, 0);', '\t}',
            '\tconst uint32_t *t = (const uint32_t *)kit;', '\tkit_count = t[1];', '\tps("table magic=");',
            '\tpx(t[0], 8);', '\tps(" count=");', '\tpd(kit_count);', '\tnl();',
            '\tif (t[0] != KIT_MAGIC || kit_count < X_COUNT) {', '\t\tps("table invalide\\n");', '\t\tflush();',
            '\t\tsys6(SYS_EXIT, 126, 0, 0, 0, 0, 0);', '\t}',
            '\ttest_tools();', '\ttest_decode();', '\ttest_codec();', '\ttest_encode();', '\ttest_fm();',
            '\ttest_synth();',
            '\tint files = has_flag(line, \'f\');', '\tif (files)', '\t\ttest_files();',
            '\tfor (int i = 0; i < X_COUNT; i++)', '\t\tif (!called[i]) {', '\t\t\tps("jamais appelee : ");',
            '\t\t\tps(fn_names[i]);', '\t\t\tnl();', '\t\t}',
            '\tps("fin : ");', '\tpd(calls);', '\tps(" appels, ");', '\tpd(fails);', '\tps(" echecs\\n");',
            '\tflush();', '\tsys6(SYS_EXIT, fails > 125 ? 125 : fails, 0, 0, 0, 0, 0);', '}', '',
            '#if defined(__x86_64__)', '__attribute__((force_align_arg_pointer, noreturn))', '#else',
            '__attribute__((noreturn))', '#endif',
            'void _start(const char *line) /* x0, rdi : la ligne d\'arguments (0 : aucune) */', '{', '\trun(line);',
            '\tfor (;;)', '\t\t;', '}', '']
    with open(os.path.join(T, 'audtest.c'), 'w', encoding='utf-8', newline='') as f:
        f.write(NL.join(out))


if __name__ == '__main__':
    main()
