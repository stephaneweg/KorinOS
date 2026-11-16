/* mediatest - essai de MediaKit (/lib/mediakit.so, docs/MEDIAKIT.md), comme mediatest.asm :
 *	mediatest fichier [images]
 * Ouvre le fichier, montre ses pistes, décode toutes les images (ou les N premières) en
 * XRGB8888 à leur taille, puis tout le son (s16 stéréo 48 kHz) s'il est décodé par MediaKit,
 * cherche au milieu et décode une image. « mediatest : PASS » (code 0) ou « FAIL » (code 1) ;
 * usage : code 2.
 * Comme l'asm : la ligne d'arguments brute du noyau (krt_line) ; le chemin s'arrête au premier
 * octet <= espace (250 octets au plus) et le reste est lu par parse_dec (0 ou illisible : toutes
 * les images) ; le tampon d'image a la taille de la première image annoncée (SYS_SBRK) : une
 * image plus grande arrête le décodage ; sortie sans tampon (puts, print_dec). */
#include "korinrt.h"
#include "mediakit_import.h"

/* src/kits/mediakit/mediakit.inc : champs de 64 bits */
#define MKI_DURATION 0
#define MKI_VTRACK 2
#define MKI_ATRACK 3
#define MKI_WIDTH 4
#define MKI_HEIGHT 5
#define MKI_FPS1000 6
#define MKI_RATE 7
#define MKI_CHANNELS 8
#define MKI_VOK 11
#define MKI_AOK 12
#define MKI_CONTAINER 104 /* octets : 16 o chacun */
#define MKI_VNAME 120
#define MKI_ANAME 136
#define MKI_WORDS 19
#define MKF_PTS 0
#define MKF_WIDTH 2
#define MKF_HEIGHT 3
#define MKF_WORDS 5
#define MKS_VFRAMES 0
#define MKS_VDECODE_US 1
#define MKS_WORDS 6
#define MT_AUDIO_FRAMES 4096

static u64 info[MKI_WORDS];
static u64 frame[MKF_WORDS];
static u64 stats[MKS_WORDS];
static char path[256];
static char hexbuf[16];
static u32 abuf[MT_AUDIO_FRAMES];

static const char m_ok[] = " (décodé par MediaKit)\n";
static const char m_ko[] = " (non décodé : AudioKit ou codec absent)\n";

static NORETURN void fail(void)
{
	ak_puts("mediatest : FAIL\n");
	_exit(1);
}

/* images, ms : « F,f » images/s */
static void print_rate(u64 n, long ms)
{
	u64 v = n * 10000 / (u64)(ms >= 1 ? ms : 1);
	print_dec((long)(v / 10));
	ak_puts(",");
	print_dec((long)(v % 10));
}

/* les pistes (info) : conteneur, vidéo, son, durée */
static void show_info(void)
{
	ak_puts("mediatest : conteneur ");
	ak_puts((const char *)info + MKI_CONTAINER);
	ak_puts(", durée ");
	print_dec((long)(info[MKI_DURATION] / 1000));
	ak_puts(" ms\n");
	if ((long)info[MKI_VTRACK] >= 0) {
		ak_puts("mediatest : vidéo ");
		ak_puts((const char *)info + MKI_VNAME);
		ak_puts(" ");
		print_dec((long)info[MKI_WIDTH]);
		ak_puts("x");
		print_dec((long)info[MKI_HEIGHT]);
		ak_puts(" ");
		print_dec((long)(info[MKI_FPS1000] / 1000));
		ak_puts(" images/s");
		ak_puts(info[MKI_VOK] ? m_ok : m_ko);
	}
	if ((long)info[MKI_ATRACK] >= 0) {
		ak_puts("mediatest : son ");
		ak_puts((const char *)info + MKI_ANAME);
		ak_puts(" ");
		print_dec((long)info[MKI_RATE]);
		ak_puts(" Hz ");
		print_dec((long)info[MKI_CHANNELS]);
		ak_puts(" canaux");
		ak_puts(info[MKI_AOK] ? m_ok : m_ko);
	}
}

/* pixels de la première image : pts et pixel du centre */
static void show_first(const u32 *px)
{
	ak_puts("mediatest : première image, pts ");
	print_dec((long)frame[MKF_PTS]);
	ak_puts(" us, pixel du centre 0x");
	u64 w = frame[MKF_WIDTH], h = frame[MKF_HEIGHT];
	u32 v = px[(h >> 1) * w + (w >> 1)]; /* (h / 2) * l + l / 2 */
	for (int i = 0; i < 8; i++, v <<= 4) {
		u32 d = v >> 28;
		hexbuf[i] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
	}
	hexbuf[8] = 0;
	ak_puts(hexbuf);
	ak_puts("\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const u8 *l = (const u8 *)krt_line;
	if (!l || !*l) {
		ak_puts("usage : mediatest fichier [images]\n");
		_exit(2);
	}
	u64 n = 0; /* chemin : le premier mot */
	u8 c;
	for (;;) {
		c = *l++;
		if (c <= ' ')
			break;
		path[n++] = (char)c;
		if (n >= 250)
			break;
	}
	path[n] = 0;
	u64 max = (u64)-1; /* images au plus (-1 : toutes) */
	if (c) {
		long v = (long)parse_dec((const char *)l);
		max = v > 0 ? (u64)v : (u64)-1;
	}
	long ver = mk_version();
	ak_puts("mediatest : MediaKit ");
	print_dec((long)((u64)ver >> 16));
	ak_puts(".");
	print_dec(ver & 0xFFFF);
	ak_puts("\n");
	long h = mk_open((long)path);
	if (h < 0) {
		ak_puts("mediatest : ouverture impossible : ");
		ak_puts((const char *)mk_error_text(h));
		ak_puts("\n");
		fail();
	}
	mk_info(h, (long)info);
	show_info();

	/* les images */
	if (info[MKI_VOK]) {
		u64 size = (info[MKI_WIDTH] * info[MKI_HEIGHT] * 4 + 4095) & ~4095ull; /* la première image */
		long r = korin_sys1(SYS_SBRK, (long)size);
		if (r < 0) {
			ak_puts("mediatest : mémoire insuffisante\n");
			fail();
		}
		u32 *px = (u32 *)r;
		long t0 = sys_time_ms();
		u64 got = 0;
		while (got < max) {
			if (mk_video_next(h, (long)frame) != 1)
				break;
			u64 fw = frame[MKF_WIDTH], fh = frame[MKF_HEIGHT]; /* (bornée au tampon) */
			if (fw * fh * 4 > size)
				break;
			if (mk_video_convert(h, (long)px, (long)(fw << 2), (long)fw, (long)fh) < 0) {
				ak_puts("mediatest : conversion refusée\n");
				fail();
			}
			if (!got) /* première image : pts, pixel du centre */
				show_first(px);
			got++;
		}
		long dt = sys_time_ms() - t0;
		ak_puts("mediatest : "); /* « N images en T ms : F images/s » */
		print_dec((long)got);
		ak_puts(" images en ");
		print_dec(dt);
		ak_puts(" ms : ");
		print_rate(got, dt);
		ak_puts(" images/s (décodage + conversion XRGB8888)\n");
		ak_puts("mediatest : décodage seul "); /* (mk_stats) */
		mk_stats(h, (long)stats);
		long nf = (long)stats[MKS_VFRAMES];
		print_dec((long)(stats[MKS_VDECODE_US] / (u64)(nf >= 1 ? nf : 1)));
		ak_puts(" us par image\n");
		if (!got)
			fail();
	}

	/* le son */
	if ((long)info[MKI_ATRACK] >= 0) {
		if (mk_audio_open(h, 48000)) {
			ak_puts("mediatest : son muet (codec pour AudioKit)\n");
		} else {
			u64 frames = 0;
			long t0 = sys_time_ms();
			for (;;) {
				long r = mk_audio_read(h, (long)abuf, MT_AUDIO_FRAMES, 0);
				if (r <= 0)
					break;
				frames += (u64)r;
			}
			long dt = sys_time_ms() - t0;
			ak_puts("mediatest : son ");
			print_dec((long)frames);
			ak_puts(" trames s16 stéréo 48 kHz en ");
			print_dec(dt);
			ak_puts(" ms\n");
			if (!frames)
				fail();
		}
	}

	/* recherche au milieu */
	long sk = mk_seek(h, (long)(info[MKI_DURATION] >> 1));
	ak_puts("mediatest : recherche au milieu -> ");
	print_dec(sk < 0 ? sk : (long)((u64)sk / 1000));
	ak_puts(" ms\n");
	if (info[MKI_VOK] && sk >= 0) {
		if (mk_video_next(h, (long)frame) != 1)
			fail();
		ak_puts("mediatest : image suivante, pts ");
		print_dec((long)frame[MKF_PTS] / 1000);
		ak_puts(" ms\n");
	}
	mk_close(h);
	ak_puts("mediatest : PASS\n");
	_exit(0);
}
