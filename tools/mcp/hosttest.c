/* hosttest.c -- les essais de korin-mcp qui tournent sur la machine de construction (sh tools/mcp/build.sh --host) :
 * le JSON (lecture, écriture, échappements, UTF-8 faux) et le PNG (une image relue par zlib de Python, pixel pour
 * pixel). Sortie : « hosttest : RÉUSSI » ou la première erreur. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "png.h"

static int fails;
#define CHECK(c)                                                                                                   \
	do {                                                                                                       \
		if (!(c)) {                                                                                        \
			printf("ÉCHEC ligne %d : %s\n", __LINE__, #c);                                               \
			fails++;                                                                                   \
		}                                                                                                  \
	} while (0)

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	/* lecture */
	const char *t = "{\"jsonrpc\":\"2.0\",\"id\":\"server-discover-probe-1\",\"method\":\"tools/call\",\"params\":"
			"{\"name\":\"read_file\",\"arguments\":{\"path\":\"/home/\\u00e9t\\u00e9 \\ud83d\\ude00\\n\","
			"\"length\":4096,\"flag\":true,\"x\":null,\"arr\":[1,-2.5e3,\"a\"]}}}";
	struct jv *v = json_parse(t, strlen(t));
	CHECK(v != 0);
	const struct jv *id = json_get(v, "id");
	CHECK(id && id->rawlen == strlen("\"server-discover-probe-1\""));
	const struct jv *args = json_get(json_get(v, "params"), "arguments");
	CHECK(!strcmp(json_str(args, "path", ""), "/home/été 😀\n"));
	CHECK(json_int(args, "length", 0) == 4096);
	CHECK(json_bool(args, "flag", 0) == 1);
	const struct jv *arr = json_get(args, "arr");
	CHECK(arr && arr->t == J_ARR && arr->kid && arr->kid->next && arr->kid->next->num == -2500);
	/* écriture */
	struct jb b;
	jb_init(&b);
	jb_obj(&b);
	jb_key(&b, "id");
	jb_json(&b, id->raw, id->rawlen);
	jb_kstr(&b, "s", json_str(args, "path", ""));
	jb_kstr(&b, "ctl", "a\"b\\c\x01\t");
	jb_key(&b, "bad");
	jb_strn(&b, "x\xff\xc3\xa9y", 5); /* un octet faux : U+FFFD, le reste gardé */
	jb_key(&b, "a");
	jb_arr(&b);
	jb_int(&b, -3);
	jb_num(&b, 1.25, 2);
	jb_bool(&b, 0);
	jb_null(&b);
	jb_arr_end(&b);
	jb_key(&b, "b64");
	jb_base64(&b, (const uint8_t *)"Korin", 5);
	jb_obj_end(&b);
	const char *want = "{\"id\":\"server-discover-probe-1\",\"s\":\"/home/été 😀\\n\",\"ctl\":\"a\\\"b\\\\c\\u0001\\t\","
			   "\"bad\":\"x\xef\xbf\xbd\xc3\xa9y\",\"a\":[-3,1.25,false,null],\"b64\":\"S29yaW4=\"}";
	CHECK(b.p && !strcmp(b.p, want));
	if (b.p && strcmp(b.p, want))
		printf("  rendu : %s\n  voulu : %s\n", b.p, want);
	struct jv *back = json_parse(b.p, b.n); /* relu */
	CHECK(back && !strcmp(json_str(back, "ctl", ""), "a\"b\\c\x01\t"));
	json_free(back);
	jb_free(&b);
	json_free(v);
	CHECK(!json_parse("{\"a\":}", 6) && !json_parse("[1,2", 4) && !json_parse("\"\\x\"", 4));
	CHECK(utf8_valid((const uint8_t *)"é😀", strlen("é😀")) && !utf8_valid((const uint8_t *)"\xed\xa0\x80", 3));

	/* PNG : un « bureau » (aplats, dégradé, bruit, texte imité), écrit avec ses pixels bruts */
	int w = 640, h = 400;
	uint8_t *rgb = malloc((size_t)w * h * 3);
	unsigned seed = 1;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			uint8_t *p = rgb + ((size_t)y * w + x) * 3;
			if (y < 30) { /* barre */
				p[0] = 235, p[1] = 238, p[2] = 242;
			} else if (x < 200) { /* dégradé */
				p[0] = (uint8_t)x, p[1] = (uint8_t)y, p[2] = (uint8_t)(x ^ y);
			} else if (y > 300) { /* bruit */
				seed = seed * 1103515245 + 12345;
				p[0] = (uint8_t)(seed >> 16), p[1] = (uint8_t)(seed >> 8), p[2] = (uint8_t)(seed >> 24);
			} else { /* texte : des traits */
				int on = ((x / 3) % 7 == 0) && ((y / 2) % 9 < 6);
				p[0] = p[1] = p[2] = on ? 20 : 250;
			}
		}
	size_t n = 0;
	uint8_t *png = png_encode(rgb, w, h, &n);
	CHECK(png && n > 100);
	char path[512];
	snprintf(path, sizeof path, "%s/hosttest.png", dir);
	FILE *f = fopen(path, "wb");
	fwrite(png, 1, n, f);
	fclose(f);
	snprintf(path, sizeof path, "%s/hosttest.rgb", dir);
	f = fopen(path, "wb");
	fwrite(rgb, 1, (size_t)w * h * 3, f);
	fclose(f);
	printf("png : %dx%d, %zu octets (brut %d)\n", w, h, n, w * h * 3);
	free(png);
	free(rgb);
	/* (Python relit : zlib, filtres) */
	char cmd[1400];
	snprintf(cmd, sizeof cmd,
		 "python3 -I -c \"\n"
		 "import zlib,struct,sys\n"
		 "d=open('%s/hosttest.png','rb').read(); raw=open('%s/hosttest.rgb','rb').read()\n"
		 "assert d[:8]==b'\\x89PNG\\r\\n\\x1a\\n'\n"
		 "i=8; idat=b''; w=h=0\n"
		 "while i<len(d):\n"
		 " n,=struct.unpack('>I',d[i:i+4]); t=d[i+4:i+8]; c=d[i+8:i+8+n]\n"
		 " assert zlib.crc32(t+c)==struct.unpack('>I',d[i+8+n:i+12+n])[0]\n"
		 " if t==b'IHDR': w,h=struct.unpack('>II',c[:8])\n"
		 " if t==b'IDAT': idat+=c\n"
		 " i+=12+n\n"
		 "z=zlib.decompress(idat); row=w*3; out=bytearray(); prev=bytes(row)\n"
		 "for y in range(h):\n"
		 " f=z[y*(row+1)]; l=bytearray(z[y*(row+1)+1:(y+1)*(row+1)])\n"
		 " for x in range(row):\n"
		 "  a=l[x-3] if x>=3 else 0\n"
		 "  l[x]=(l[x]+(a if f==1 else prev[x] if f==2 else 0))&255\n"
		 " out+=l; prev=bytes(l)\n"
		 "assert bytes(out)==raw, 'pixels'\n"
		 "print('png relu par zlib : pixels identiques')\n"
		 "\"",
		 dir, dir);
	if (system(cmd) != 0) {
		printf("ÉCHEC : le PNG relu par Python\n");
		fails++;
	}
	printf(fails ? "hosttest : ÉCHEC (%d)\n" : "hosttest : RÉUSSI\n", fails);
	return fails != 0;
}
