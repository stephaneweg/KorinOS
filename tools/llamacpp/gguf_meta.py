"""gguf_meta : affiche les métadonnées d'un fichier GGUF (sans numpy). Usage : python3 gguf_meta.py FICHIER [MOT…]"""
import struct, sys

def main(path, filters):
    with open(path, 'rb') as f:
        rd = lambda fmt: struct.unpack('<' + fmt, f.read(struct.calcsize(fmt)))[0]
        def s(): return f.read(rd('Q')).decode('utf-8', 'replace')
        SC = {0: 'B', 1: 'b', 2: 'H', 3: 'h', 4: 'I', 5: 'i', 6: 'f', 7: '?', 10: 'Q', 11: 'q', 12: 'd'}
        def val(t):
            if t in SC: return rd(SC[t])
            if t == 8: return s()
            if t == 9:
                et, n = rd('I'), rd('Q')
                items = [val(et) for _ in range(n)]
                return items if n <= 64 else f'[{n} éléments]'
            raise ValueError(t)
        assert f.read(4) == b'GGUF'
        rd('I'); rd('Q'); n = rd('Q')
        for _ in range(n):
            k = s(); v = val(rd('I'))
            if not filters or any(x in k for x in filters):
                print(k, '=', v)

main(sys.argv[1], sys.argv[2:])
