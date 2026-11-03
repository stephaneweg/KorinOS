#!/usr/bin/env python3
# rmask.py -- genere src/kits/uikit/uikit_rmask.inc : la couverture des coins de ui_rbox, rayons 1 a 16.
#
#   python -I tools/kits/rmask.py
import os

import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
S = 16                  # echantillons par pixel, par cote

rows = ["; ============================================================================",
        ";  kits/uikit_rmask.inc - UIKit : couverture des coins arrondis de ui_rbox, rayons 1",
        ";  a 16. Coin haut-gauche, r x r octets : 0 dehors, 255 dedans, sinon couverture.",
        ";  Genere par tools/kits/rmask.py ; ne pas modifier a la main.",
        "; ============================================================================",
        "", "\t\talign 2", "uk_rm_off:\tdh 0"]
off, offs, data = 0, [], []
for r in range(1, 17):
    offs.append(off)
    off += r * r
    y, x = np.mgrid[0:r * S, 0:r * S]
    d = np.hypot((x + .5) / S - r, (y + .5) / S - r)
    m = (d <= r).reshape(r, S, r, S).mean(axis=(1, 3))
    for row in np.round(m * 255).astype(int):
        data.append('\tdb ' + ','.join(str(v) for v in row))
rows.append('\t\tdh ' + ','.join(str(v) for v in offs))
rows.append('uk_rm:')
rows += data
rows.append('\t\talign 4')
with open(os.path.join(ROOT, 'src', 'kits', 'uikit', 'uikit_rmask.inc'), 'w', newline='\n') as f:
    f.write('\n'.join(rows) + '\n')
