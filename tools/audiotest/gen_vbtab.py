"""gen_vbtab.py - table du decodeur Vorbis d'AudioKit (src/kits/audiokit/ak_vbtab.inc)

    python -I tools/audiotest/gen_vbtab.py [chemin de ffmpeg]

floor1_inverse_dB_table de la specification Vorbis I (section 10.1), lue dans
ffmpeg (vorbis_data.c, ou elle est recopiee de la specification).
"""
import os
import re
import struct
import sys

FF = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('ONYX', 'build/onyx'), 'third_party/ffmpeg-7.1.2/libavcodec')
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
t = open(os.path.join(FF, 'vorbis_data.c'), encoding='utf-8').read()
m = re.search(r'ff_vorbis_floor1_inverse_db_table\[256\]\s*=\s*\{(.*?)\};', t, re.S)
vals = [float(v) for v in re.findall(r'(\d+\.?\d*(?:[eE][-+]?\d+)?)F', m.group(1))]
assert len(vals) == 256, len(vals)
out = ['; ' + '=' * 76,
       ';  kits/audiokit/ak_vbtab.inc - table du decodeur Vorbis (floor1_inverse_dB_table,',
       ';  specification Vorbis I 10.1) ; ENGENDRE par tools/audiotest/gen_vbtab.py',
       '; ' + '=' * 76, '', '\t\talign\t4', 'vb_invdb:']
for i in range(0, 256, 8):
    out.append('\t\tdw\t' + ', '.join('0x%08X' % struct.unpack('<I', struct.pack('<f', v))[0] for v in vals[i:i + 8]))
out.append('')
open(os.path.join(ROOT, 'src', 'kits', 'audiokit', 'ak_vbtab.inc'), 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
print('256 valeurs', vals[0], vals[-1])
