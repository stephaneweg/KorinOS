//
// replay.h -- recomposer un prereglage (recipe.h) : la sequence est videe, puis le journal rejoue sur un compositeur a
// lui. Le compositeur avance jusqu'a la date de chaque changement, l'applique, recommence, puis finit a la longueur
// gardee ; enfin les retouches d'orchestre faites apres coup.
//
#ifndef ARABESQUE_REPLAY_H
#define ARABESQUE_REPLAY_H

#include "image.h"

// les octets d'un fichier image -> l'image (la fenetre le fournit : UIKit) ; 0 ou false : illisible
static bool (*g_decodeImage) (const unsigned char *data, long size, Image &out);

// l'image numero i du prereglage : calculee, ou relue de ses octets -> false si elle ne se lit pas (un mandala a la place)
static bool recipe_image (const Recipe &r, int i, Image &im)
{
	const ImgRef &f = r.img[i];
	if (!f.data) { image_make (im, f.kind, f.w, f.h, f.seed, &f.set); return true; }
	bool ok = g_decodeImage && g_decodeImage (f.data, f.size, im);
	if (!ok) mandala (im, 512, 512, 1);
	image_source_file (im, f.file, f.data, f.size);
	return ok;
}

// -> false si une image gardee ne se lit pas (le morceau est quand meme recompose) ; endStyle : le style de la fin
static bool recipe_replay (Recipe &r, Song &song, Style *endStyle = 0)
{
	static ChordGrid none;
	bool ok = true;
	song.clear ();
	song.tempo = r.tempo;
	snprintf (song.copyright, sizeof song.copyright, "%s", r.copyright);
	Image *imgs = new Image[r.nimg];
	for (int i = 0; i < r.nimg; i++) if (!recipe_image (r, i, imgs[i])) ok = false;
	Style *st = new Style (*r.ch[0].style);
	Ifs *f = new Ifs (*r.ch[0].ifs);
	ChordGrid *g = new ChordGrid (r.ch[0].chords ? *r.ch[0].chords : none);
	Composer *c = new Composer ();
	c->song = &song; c->style = st; c->ifs = f; c->img = &imgs[r.ch[0].image]; c->chords = g;
	c->mask = r.mask; c->root = r.root;
	c->formIn = r.form;
	c->start (r.seed);
	for (int i = 1; i < r.nch; i++)
	{
		const Change &e = r.ch[i];
		c->advance (e.at);
		switch (e.kind)
		{
		case CH_STATE: *st = *e.style; *f = *e.ifs; c->img = &imgs[e.image]; *g = e.chords ? *e.chords : none; break;
		case CH_SCALE: c->change_scale (e.mask, e.root, e.now); break;
		case CH_STYLE: c->change_style (*e.style, e.now); break;
		case CH_CLEAR: c->clear_track (e.part); break;
		}
	}
	c->advance (r.length);
	if (!r.length) song.clear ();
	for (int i = 0; i < NPART; i++)
	{
		if (r.emptied[i]) { song.tr[i].clear (); continue; }
		if (r.mix[i][0] >= 0) song_retouch (song, i, NK_PROGRAM, r.mix[i][0], 0);
		if (r.mix[i][1] >= 0) song_retouch (song, i, NK_CC, 7, r.mix[i][1]);
		if (r.mix[i][2] >= 0) song_retouch (song, i, NK_CC, 10, r.mix[i][2]);
	}
	if (endStyle) *endStyle = *st;
	free (c->recKey);
	delete c; delete f; delete st; delete g;
	for (int i = 0; i < r.nimg; i++) image_free (imgs[i]);
	delete[] imgs;
	return ok;
}

#endif
