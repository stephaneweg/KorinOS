"""Convertit une police BDF 8xH en include FASMARM (256 glyphes Latin-1, H octets chacun).

Usage : python -I tools/bdf2inc.py ref/spleen/spleen-8x16.bdf src/font8x16.inc
"""
import sys


def main(src, dst):
    glyphs, height = {}, None
    code, rows, in_bitmap = None, [], False
    with open(src, encoding="latin-1") as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            key = parts[0]
            if key == "PIXEL_SIZE":
                height = int(parts[1])
            elif key == "ENCODING":
                code = int(parts[1])
            elif key == "BITMAP":
                in_bitmap, rows = True, []
            elif key == "ENDCHAR":
                in_bitmap = False
                if 0 <= code < 256:
                    glyphs[code] = rows
            elif in_bitmap:
                rows.append(int(parts[0], 16) & 0xFF)

    with open(dst, "w", encoding="ascii", newline="\n") as out:
        out.write("; Police Spleen 8x%d (c) Frederic Cambus, licence BSD-2 (ref/spleen/LICENSE)\n" % height)
        out.write("; Genere par tools/bdf2inc.py : 256 glyphes Latin-1, %d octets/glyphe, bit 7 = gauche\n" % height)
        out.write("FONT_W = 8\nFONT_H = %d\n" % height)
        out.write("font_glyphs:\n")
        for c in range(256):
            rows = (glyphs.get(c) or [0] * height)[:height]
            rows += [0] * (height - len(rows))
            out.write("\tdb " + ",".join("0x%02X" % r for r in rows) + "\t; %d\n" % c)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
