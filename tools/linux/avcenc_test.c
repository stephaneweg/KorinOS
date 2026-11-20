/*
 * avcenc_test.c -- essai natif (Linux, WSL) du chiffrement commun (CENC) de la bibliothèque av de Jet
 * (src/extra/onyx-libs/av : av_mp4.c, av_crypt.c ; docs/etude/WIDEVINE.md, étape 4) :
 *   1. AES-128 : le vecteur de FIPS-197 ;
 *   2. un flux EME Clear Key public (« Angel One » de Shaka, clé connue) : démultiplexé par av comme MSE
 *      (av_demux_append), pssh et tenc lus, chaque paquet chiffré déchiffré par av_decrypt ;
 *   3. les paquets déchiffrés décodés par libavcodec (AAC, H.264) : aucune erreur attendue.
 *
 *   sh tools/linux/avcenc_test.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>

#include "av.h"

static int s_fails;

static void check(int ok, const char *what)
{
	printf("  [%s] %s\n", ok ? "ok" : "ÉCHEC", what);
	if (!ok)
		s_fails++;
}

/* (av_crypt.c garde l'AES pour lui : on l'essaie par un paquet « cbc1 » d'un bloc, déchiffré, puis comparé) */
static void aes_vector(void)
{
	/* FIPS-197 C.1 : clé 000102..0f, clair 00112233..ff, chiffré 69c4e0d8 6a7b0430 d8cdb780 70b4c55a ;
	 * un paquet « cbc1 » d'un bloc, IV nul : le déchiffrement doit rendre le clair */
	static const uint8_t key[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	static const uint8_t ct[16] = { 0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a };
	static const uint8_t pt[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
	struct av_packet p;
	memset(&p, 0, sizeof p);
	p.data = malloc(16);
	memcpy(p.data, ct, 16);
	p.size = 16;
	p.crypt = av_crypt_new(0);
	p.crypt->scheme = 0x63626331;	/* 'cbc1' */
	check(av_decrypt(&p, key) == AV_OK && !memcmp(p.data, pt, 16), "AES-128 : vecteur de FIPS-197 (déchiffrement)");
	/* CTR : chiffrer le clair avec le compteur 0 donne le chiffré (le flux de clé = AES (compteur)) */
	memcpy(p.data, pt, 16);
	memset(p.data, 0, 16);
	p.crypt = av_crypt_new(0);
	p.crypt->scheme = 0x63656e63;	/* 'cenc' ; IV = clair FIPS : le flux de clé est le chiffré FIPS */
	memcpy(p.crypt->iv, pt, 16);
	check(av_decrypt(&p, key) == AV_OK && !memcmp(p.data, ct, 16), "AES-128 CTR : flux de clé (chiffrement de l'IV)");
	av_pkt_free(&p);
}

static int read_file(const char *path, uint8_t **buf, size_t *n)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return -1;
	fseek(f, 0, SEEK_END);
	*n = (size_t) ftell(f);
	fseek(f, 0, SEEK_SET);
	*buf = malloc(*n);
	if (fread(*buf, 1, *n, f) != *n) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static void stream(const char *what, const char *path, const uint8_t key[16])
{
	uint8_t *data;
	size_t n;
	struct av_demux *d;
	struct av_packet pk;
	int r, pkts = 0, enc = 0, dec_ok = 0, frames = 0, errors = 0;
	const AVCodec *codec = NULL;
	AVCodecContext *cx = NULL;
	AVPacket *ap = av_packet_alloc();
	AVFrame *fr = av_frame_alloc();

	printf("%s : %s\n", what, path);
	if (read_file(path, &data, &n)) {
		check(0, "fichier lu");
		return;
	}
	d = av_demux_new(AV_FMT_UNKNOWN);
	av_demux_append(d, data, n);
	av_demux_end(d);
	while ((r = av_demux_read(d, &pk)) == AV_OK || r == AV_AGAIN) {
		if (r == AV_AGAIN)
			break;
		if (cx == NULL) {
			const struct av_track *t = av_demux_track(d, pk.track);
			size_t ilen = 0;
			const uint8_t *init = av_demux_init_data(d, &ilen);
			printf("  piste : %s, chiffrée %d, schéma %08x, KID %02x%02x…, pssh %zu octets, extra %zu octets\n",
			       t->codec_id, t->encrypted, t->scheme, t->kid[0], t->kid[1], ilen, t->extra_len);
			check(t->encrypted && t->scheme == 0x63656e63 && init != NULL && ilen > 32, "piste chiffrée, tenc, pssh (init data d'EME)");
			codec = avcodec_find_decoder(t->codec == AV_C_AAC ? AV_CODEC_ID_AAC : AV_CODEC_ID_H264);
			cx = avcodec_alloc_context3(codec);
			cx->extradata = av_mallocz(t->extra_len + AV_INPUT_BUFFER_PADDING_SIZE);
			memcpy(cx->extradata, t->extra, t->extra_len);
			cx->extradata_size = (int) t->extra_len;
			cx->err_recognition = AV_EF_EXPLODE | AV_EF_BITSTREAM;
			avcodec_open2(cx, codec, NULL);
		}
		pkts++;
		if (pk.crypt != NULL) {
			enc++;
			if (av_decrypt(&pk, key) == AV_OK)
				dec_ok++;
		}
		av_new_packet(ap, (int) pk.size);
		memcpy(ap->data, pk.data, pk.size);
		if (avcodec_send_packet(cx, ap) < 0)
			errors++;
		av_packet_unref(ap);
		while (avcodec_receive_frame(cx, fr) == 0)
			frames++;
		av_pkt_free(&pk);
	}
	if (cx != NULL) {
		avcodec_send_packet(cx, NULL);
		while (avcodec_receive_frame(cx, fr) == 0)
			frames++;
	}
	printf("  %d paquets, %d chiffrés, %d déchiffrés ; libavcodec : %d trames, %d erreurs\n", pkts, enc, dec_ok, frames, errors);
	check(enc > 0 && dec_ok == enc, "paquets chiffrés déchiffrés par av_decrypt");
	check(frames > 0 && errors == 0, "décodés sans erreur après déchiffrement (libavcodec)");
	avcodec_free_context(&cx);
	av_demux_free(d);
	free(data);
}

int main(int argc, char **argv)
{
	/* la clé publique du flux Clear Key de Shaka : KID feedf00d…, clé 00112233…ff */
	static const uint8_t key[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
	aes_vector();
	for (int i = 1; i + 1 < argc; i += 2)
		stream(argv[i], argv[i + 1], key);
	printf("avcenc_test : %s (%d échec(s))\n", s_fails ? "ÉCHEC" : "RÉUSSI", s_fails);
	return s_fails ? 1 : 0;
}
