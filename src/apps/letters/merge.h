//
// merge.h -- Letters' mail merge: a letter (the document, its merge fields -- Â«NameÂ» --) filled with the
// records of a Cardfile form (a .card file: its fields' values as Cardfile shows them -- a date
// 29/09/2026, Yes / No, a multi-line text's lines), one document per record: all of them one after the
// other in a new document (each on a page of its own), or each a file of its own in a folder (named
// after a field, or numbered). Tools > Mail Merge: the data chosen, its fields inserted, the records'
// values previewed in the letter (one record at a time), the merge (a new document: another Letters
// shows it, asked as Cardfile asks -- below). The document remembers its data (RTF: a \docvar, Word: a
// document variable, OpenDocument: a user field).
//
// Cardfile asks for one too (Record > Mail Merge: this record, or all of them): "writer --merge JOB",
// JOB a text of "key = value" lines --
//   template = SD0:/home/documents/letter.odt      the letter
//   data     = SD0:/home/cardfile/merge.card
//   records  = all | N                  (N: the record's number, from 1)
//   output   = open | files             (open: one document shown, each letter on a new page; files: written,
//                                        then the first one shown)
//   folder   = SD0:/home/documents/Letters         (files: where; made if needed)
//   name     = Name                     (files: named after this field; empty: letter-1, letter-2...)
//   format   = rtf | docx | odt         (files: theirs; the template's by default)
//   lines    = SD0:/home/ledger/merge-lines.card    (a document's lines: below)
//
// A document's LINES (Ledger's quotes, orders, invoices: "lines = ..." a second form, a record a line): a
// table row of the letter holding merge fields named Line... («LineText», «LineQty», «LineTotal») is
// repeated for each of those records, its fields filled with that record's values (no record: the row
// taken out); the other fields take the data's record, as ever.
//
#ifndef _writer_merge_h
#define _writer_merge_h

#include <spawn.h>

#include "../cardfile/model.h"
#include "docx.h"
#include "odt.h"
#include "dialogs.h"

namespace wr {

// ---- the data ----------------------------------------------------------------------------------------------
static cf::Doc g_mdata; static bool g_mdataOk;
static char g_mdataPath[200];
static int g_mrec;					// the record previewed
static bool g_mpreview;					// the fields show the record's values (else Â«their namesÂ»)

// A field of the data by name (its column, or its label): its index, -1 none.
static int mfield (const char *name)
{
	for (int k = 0; k < g_mdata.nf; k++) if (sicmp (g_mdata.f[k].column, name) == 0) return k;
	for (int k = 0; k < g_mdata.nf; k++) if (sicmp (g_mdata.f[k].label, name) == 0) return k;
	return -1;
}
// Record r's value of a field, as Cardfile shows it (a multi-line one's lines joined by `joiner`).
static bool mvalue (int r, const char *name, char *out, int cap, const char *joiner)
{
	int k = mfield (name);
	if (!g_mdataOk || k < 0 || r < 0 || r >= g_mdata.nr) return false;
	cf::value_show (g_mdata.f[k], g_mdata.r[r][k], out, cap, true, joiner);
	return true;
}
static const char *mpreview_value (const char *name)
{
	static char b[256];
	return mvalue (g_mrec, name, b, sizeof b, ", ") ? b : 0;
}
static void merge_show (bool on)
{
	g_mpreview = on && g_mdataOk;
	g_mergeValue = g_mpreview ? mpreview_value : 0;
	doc_dirty_all (g_doc);
}

// The data read (a .card file): false, and why, when it is not one.
static bool merge_open (const char *path, const char **why)
{
	*why = "The file could not be read.";
	int got = 0; char *b = file_read (path, &got);
	if (!b) { g_mdataOk = false; return false; }
	if (!g_mdataOk) cf::doc_init (g_mdata);
	g_mdataOk = cf::doc_read (g_mdata, b, got, why);
	delete[] b;
	if (g_mdataOk) { scpy (g_mdataPath, path, sizeof g_mdataPath); g_mrec = 0; }
	return g_mdataOk;
}
// ---- a document's lines: its table rows repeated -------------------------------------------------------------
static cf::Doc g_mlines; static bool g_mlinesOk;
static bool merge_lines_open (const char *path)
{
	int got = 0; char *b = file_read (path, &got);
	if (!b) { g_mlinesOk = false; return false; }
	if (!g_mlinesOk) cf::doc_init (g_mlines);
	const char *why;
	g_mlinesOk = cf::doc_read (g_mlines, b, got, &why);
	delete[] b;
	return g_mlinesOk;
}
// A field of a line: its name starts with "Line".
static bool line_field (const char *name) { return (name[0] == 'L' || name[0] == 'l') && (name[1] == 'i' || name[1] == 'I') && (name[2] == 'n' || name[2] == 'N') && (name[3] == 'e' || name[3] == 'E'); }
static bool is_line_field (const Doc &d, const Para *q, int k)
{
	if (q->ch[k] != FIELD_CHAR) return false;
	const CharFmt &f = d.fmt[q->cf[k]];
	return f.fld && d.fld[f.fld - 1].kind == FK_MERGE && line_field (d.fld[f.fld - 1].arg);
}
// A paragraph's line fields made the line r's values.
static void fill_line_fields (Doc &d, Para *q, int r)
{
	for (int k = q->len - 1; k >= 0; k--)
	{
		if (!is_line_field (d, q, k)) continue;
		CharFmt f = d.fmt[q->cf[k]];
		const char *name = d.fld[f.fld - 1].arg;
		char v[1024]; v[0] = 0;
		int fk = -1;
		for (int j = 0; j < g_mlines.nf; j++) if (sicmp (g_mlines.f[j].column, name) == 0 || sicmp (g_mlines.f[j].label, name) == 0) fk = j;
		if (fk >= 0 && r >= 0 && r < g_mlines.nr) cf::value_show (g_mlines.f[fk], g_mlines.r[r][fk], v, sizeof v, true, "\x0B");
		unsigned u[1024]; int m = 0;
		for (const char *t = v; *t && m < 1024; t++) u[m++] = (unsigned char) *t;
		f.fld = 0;
		unsigned short cf = doc_fmt (d, f);
		para_erase (q, k, k + 1);
		para_insert (q, k, u, m, cf);
		q->dirty = true;
	}
}
// The body's table rows holding line fields: repeated for each line (their fields filled), or taken out.
static void merge_lines (Doc &d)
{
	if (!g_mlinesOk) return;
	if (d.cur != SY_BODY) doc_story (d, SY_BODY);
	int N = g_mlines.nr;
	for (int i = 0; i < d.n; i++)
	{
		Para *q = d.p[i];
		if (!q->pf.tbl) continue;
		bool has = false;
		for (int k = 0; k < q->len && !has; k++) has = is_line_field (d, q, k);
		if (!has) continue;
		int t = q->pf.tbl, row = q->pf.row;
		Table *T = t > 0 && t <= d.ntbl ? d.tbl[t - 1] : 0;
		if (!T || row < 0 || row >= T->nrows) continue;
		int a = i; while (a > 0 && d.p[a - 1]->pf.tbl == t && d.p[a - 1]->pf.row == row) a--;
		int b = i; while (b < d.n && d.p[b]->pf.tbl == t && d.p[b]->pf.row == row) b++;
		int reps = N > 0 ? N : (T->nrows > 1 ? 0 : 1);		// (a table of that row alone: kept, emptied)
		// the table: the row repeated `reps` times
		Table *T2 = table_alloc (T->nrows + reps - 1, T->ncols);
		for (int c = 0; c < T->ncols; c++) T2->colW[c] = T->colW[c];
		T2->border = T->border; T2->bw = T->bw; T2->bcolor = T->bcolor; T2->header = T->header; T2->align = T->align; T2->indent = T->indent;
		for (int r2 = 0; r2 < T2->nrows; r2++)
		{
			int src = r2 < row ? r2 : r2 < row + reps ? row : r2 - reps + 1;
			T2->rowH[r2] = T->rowH[src];
			for (int c = 0; c < T->ncols; c++) tcell (T2, r2, c) = tcell (T, src, c);
		}
		table_free (T); d.tbl[t - 1] = T2;
		for (int j = b; j < d.n && d.p[j]->pf.tbl == t; j++) d.p[j]->pf.row = (short) (d.p[j]->pf.row + reps - 1);
		// the row's paragraphs: their copies, a line each
		int cnt = b - a;
		Para **tmpl = new Para *[cnt];
		for (int j = 0; j < cnt; j++) tmpl[j] = doc_take (d, a);
		int at = a;
		for (int k = 0; k < reps; k++)
			for (int j = 0; j < cnt; j++)
			{
				Para *c = para_copy (tmpl[j]);
				c->pf.row = (short) (row + k);
				fill_line_fields (d, c, N > 0 ? k : -1);
				doc_put (d, at++, c);
			}
		for (int j = 0; j < cnt; j++) para_free (tmpl[j]);
		delete[] tmpl;
		i = at - 1;
	}
}

// A document loaded: its data opened (quietly), the preview off.
static void merge_doc_loaded ()
{
	merge_show (false);
	if (g_doc.mergeSrc[0] && sicmp (g_doc.mergeSrc, g_mdataPath) != 0) { const char *why; merge_open (g_doc.mergeSrc, &why); }
}

// ---- the documents made ------------------------------------------------------------------------------------
// A document as a file's bytes (by its name's extension: .docx, .odt, .txt, .html, else RTF); new[].
static bool doc_bytes (Doc &d, const char *path, char **out, unsigned *len)
{
	auto ext = [&] (const char *e) { int n = slen (path), k = slen (e); return n >= k && sicmp (path + n - k, e) == 0; };
	if (ext (".docx") || ext (".odt"))
	{
		unsigned char *z = 0;
		bool ok = ext (".docx") ? docx_save (d, &z, len) : odt_save (d, &z, len);
		*out = (char *) z;
		return ok;
	}
	Out o;
	if (ext (".txt")) txt_save (d, o);
	else if (ext (".html") || ext (".htm")) html_save (d, o, "Document");
	else rtf_save (d, o);
	*out = o.b; *len = (unsigned) o.n;
	return true;
}
static bool doc_from_bytes (Doc &d, const char *b, int n)
{
	// (an archive that is no document, binary bytes under a text's or an RTF's clothes: refused, not shown as text)
	int ctl = 0;
	for (int i = 0; i < n; i++) { unsigned char c = (unsigned char) b[i]; if (c < 9 || (c > 13 && c < 32) || c == 0x7F) ctl++; }
	if (rtf_is (b, n)) return ctl <= n / 64 && rtf_load (d, b, n);
	if (docx_is (b, n)) return docx_load (d, b, n);
	if (odt_is (b, n)) return odt_load (d, b, n);
	if ((n >= 2 && b[0] == 'P' && b[1] == 'K') || ctl > n / 64) return false;
	txt_load (d, b, n);
	return true;
}

// A document's merge fields made record r's text (every story; its lines' rows first).
static void merge_fill (Doc &d, int r)
{
	merge_lines (d);
	for (int s = 0; s < SY_COUNT; s++)
	{
		int n; Para **p = story_p (d, s, &n);
		for (int i = 0; i < n; i++)
		{
			Para *q = p[i];
			for (int k = q->len - 1; k >= 0; k--)
			{
				if (q->ch[k] != FIELD_CHAR) continue;
				CharFmt f = d.fmt[q->cf[k]];
				if (!f.fld || d.fld[f.fld - 1].kind != FK_MERGE) continue;
				char v[1024];
				if (!mvalue (r, d.fld[f.fld - 1].arg, v, sizeof v, "\x0B")) v[0] = 0;
				unsigned u[1024]; int m = 0;
				for (const char *t = v; *t && m < 1024; t++) u[m++] = (unsigned char) *t;
				f.fld = 0;
				unsigned short cf = doc_fmt (d, f);
				para_erase (q, k, k + 1);
				para_insert (q, k, u, m, cf);
				q->dirty = true;
			}
		}
	}
	d.mergeSrc[0] = 0;
}

// s's body put at the end of d's (its formats, images, fields, tables taken along); on a new page.
static void doc_append_body (Doc &d, const Doc &s, bool pageBreak)
{
	unsigned short *map = new unsigned short[s.nfmt > 0 ? s.nfmt : 1];
	for (int i = 0; i < s.nfmt; i++)
	{
		CharFmt f = s.fmt[i];
		f.font = (short) doc_font (d, s.fontName[f.font]);
		if (f.obj) f.obj = doc_image_copy (d, s.img[f.obj - 1]) + 1;
		if (f.fld) f.fld = doc_field (d, s.fld[f.fld - 1].kind, s.fld[f.fld - 1].arg);
		map[i] = doc_fmt (d, f);
	}
	int *tmap = new int[s.ntbl + 1];
	for (int i = 0; i <= s.ntbl; i++) tmap[i] = i == 0 ? 0 : doc_table (d, table_copy (s.tbl[i - 1]));
	int n; Para **p = story_p (s, SY_BODY, &n);
	for (int i = 0; i < n; i++)
	{
		Para *q = para_copy (p[i]);
		for (int k = 0; k < q->len; k++) q->cf[k] = map[q->cf[k]];
		q->endCf = map[q->endCf];
		q->pf.tbl = (short) (q->pf.tbl > 0 && q->pf.tbl <= s.ntbl ? tmap[q->pf.tbl] : 0);
		if (i == 0 && pageBreak) q->pf.pageBreak = true;
		story_append (d, SY_BODY, q);
	}
	delete[] tmap; delete[] map;
}

// The letter (its bytes) made for records [r0, r1]: into one document (out).
static bool merge_combined (const char *tb, int tn, int r0, int r1, Doc &out)
{
	doc_clear (out);
	if (!doc_from_bytes (out, tb, tn)) return false;
	merge_fill (out, r0);
	for (int r = r0 + 1; r <= r1; r++)
	{
		Doc t; doc_init (t);
		if (doc_from_bytes (t, tb, tn)) { merge_fill (t, r); doc_append_body (out, t, true); }
		doc_clear (t);
	}
	doc_fix (out);
	return true;
}

// A file's name from a value: its letters, digits, spaces, - _ . kept.
static void safe_name (const char *v, char *out, int cap)
{
	int n = 0;
	for (const char *t = v; *t && n < cap - 1; t++)
	{
		unsigned char c = (unsigned char) *t;
		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0xC0 || c == ' ' || c == '-' || c == '_' || c == '.') out[n++] = (char) c;
		else if (c == '\x0B' || c == '/' || c == ',') out[n++] = '-';
	}
	while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.')) n--;
	out[n] = 0;
}
// Records [r0, r1] each a file in folder (named after field `name`, else "base-N"; ext: ".rtf"...): how
// many written; the first one's path in first. A file of an earlier merge is replaced; two records
// named alike: the second one's name numbered.
static int merge_files (const char *tb, int tn, int r0, int r1, const char *folder, const char *name, const char *base, const char *ext, char *first, int fcap)
{
	mkdir (folder, 0777);
	int done = 0;
	char **made = new char *[r1 >= r0 ? r1 - r0 + 1 : 1];	// (the paths written by this merge)
	for (int r = r0; r <= r1; r++)
	{
		Doc t; doc_init (t);
		if (!doc_from_bytes (t, tb, tn)) { doc_clear (t); continue; }
		merge_fill (t, r);
		char v[200], nm[120], path[300];
		nm[0] = 0;
		if (name && name[0] && mvalue (r, name, v, sizeof v, " ")) safe_name (v, nm, 80);
		if (!nm[0]) { scpy (nm, base, 80); int k = slen (nm); nm[k++] = '-'; char d[12]; int j = 0, x = r + 1; do { d[j++] = (char) ('0' + x % 10); x /= 10; } while (x); while (j) nm[k++] = d[--j]; nm[k] = 0; }
		scpy (path, folder, 200);
		int k = slen (path);
		if (k && path[k - 1] != '/' && path[k - 1] != ':') path[k++] = '/';
		scpy (path + k, nm, 300 - k); k = slen (path); scpy (path + k, ext, 300 - k);
		bool twin = false;
		for (int i = 0; i < done && !twin; i++) twin = !sicmp (made[i], path);
		if (twin) { k = slen (path) - slen (ext); path[k++] = '-'; char d[12]; int j = 0, x = r + 1; do { d[j++] = (char) ('0' + x % 10); x /= 10; } while (x); while (j) path[k++] = d[--j]; scpy (path + k, ext, 300 - k); }
		char *b = 0; unsigned n = 0;
		if (doc_bytes (t, path, &b, &n) && file_write (path, b, n))
		{
			if (!done) scpy (first, path, fcap);
			int pn = slen (path) + 1; made[done] = new char[pn]; scpy (made[done], path, pn);
			done++;
		}
		delete[] b;
		doc_clear (t);
	}
	for (int i = 0; i < done; i++) delete[] made[i];
	delete[] made;
	return done;
}

// "7 documents written in:" and the folder below (its end when it is long: the message box is narrow).
static void merge_done_msg (int done, const char *folder, char *msg, int cap)
{
	snprintf (msg, (size_t) cap, TR (done == 1 ? "%d document written in:" : "%d documents written in:"), done);
	int m = slen (msg);
	scpy (msg + m, "\n", cap - m); m = slen (msg);
	int n = slen (folder);
	if (n > 36) { scpy (msg + m, "...", cap - m); m = slen (msg); folder += n - 33; }
	scpy (msg + m, folder, cap - m);
}

// ---- Cardfile's request: "--merge JOB" ------------------------------------------------------------------------
// 1: the document made is in g_doc (shown); 2: files written (the first one's path in path); 0: failed
// (said).
static int merge_job (const char *job, char *path, int cap)
{
	int jn = 0; char *jb = file_read (job, &jn);
	if (!jb) { say ("Mail Merge", "The mail merge's request could not be read."); return 0; }
	char tmpl[200] = "", data[200] = "", records[16] = "all", output[16] = "open", folder[200] = "", name[64] = "", format[8] = "", lines[200] = "";
	for (char *p = jb; *p; )
	{
		char *e = p; while (*e && *e != '\n') e++;
		char *eq = p; while (eq < e && *eq != '=') eq++;
		if (eq < e)
		{
			char k[16]; int kn = 0;
			for (char *t = p; t < eq && kn < 15; t++) if (*t != ' ' && *t != '\t') k[kn++] = *t;
			k[kn] = 0;
			char *v = eq + 1; while (v < e && *v == ' ') v++;
			char *ve = e; while (ve > v && (ve[-1] == ' ' || ve[-1] == '\r')) ve--;
			char val[200]; int vn = 0; for (char *t = v; t < ve && vn < 199; t++) val[vn++] = *t; val[vn] = 0;
			if (!sicmp (k, "template")) scpy (tmpl, val, 200);
			else if (!sicmp (k, "data")) scpy (data, val, 200);
			else if (!sicmp (k, "records")) scpy (records, val, 16);
			else if (!sicmp (k, "output")) scpy (output, val, 16);
			else if (!sicmp (k, "folder")) scpy (folder, val, 200);
			else if (!sicmp (k, "name")) scpy (name, val, 64);
			else if (!sicmp (k, "format")) scpy (format, val, 8);
			else if (!sicmp (k, "lines")) scpy (lines, val, 200);
		}
		p = *e ? e + 1 : e;
	}
	delete[] jb;
	const char *why;
	if (!merge_open (data, &why)) { say ("Mail Merge", "The records could not be read."); return 0; }
	if (lines[0]) merge_lines_open (lines); else g_mlinesOk = false;
	int tn = 0; char *tb = file_read (tmpl, &tn);
	if (!tb) { say ("Mail Merge", "The letter could not be read."); return 0; }
	int r0 = 0, r1 = g_mdata.nr - 1;
	if (sicmp (records, "all") != 0) { int v = 0; for (const char *t = records; *t >= '0' && *t <= '9'; t++) v = v * 10 + (*t - '0'); r0 = r1 = wclamp (v - 1, 0, g_mdata.nr - 1); }
	int res = 0;
	if (g_mdata.nr == 0) say ("Mail Merge", "The form has no records.");
	else if (sicmp (output, "files") == 0)
	{
		const char *ext = !sicmp (format, "docx") ? ".docx" : !sicmp (format, "odt") ? ".odt" : !sicmp (format, "rtf") ? ".rtf" : 0;
		if (!ext) { int n = slen (tmpl); ext = n > 5 && !sicmp (tmpl + n - 5, ".docx") ? ".docx" : n > 4 && !sicmp (tmpl + n - 4, ".odt") ? ".odt" : ".rtf"; }
		char base[80]; const char *bn = tmpl; for (const char *t = tmpl; *t; t++) if (*t == '/' || *t == ':') bn = t + 1;
		scpy (base, bn, sizeof base); { int n = slen (base); while (n > 0 && base[n - 1] != '.') n--; if (n > 1) base[n - 1] = 0; }
		int done = merge_files (tb, tn, r0, r1, folder[0] ? folder : DOCS_DIR, name, base, ext, path, cap);
		char msg[120]; merge_done_msg (done, folder[0] ? folder : DOCS_DIR, msg, sizeof msg);
		if (done != 1) say ("Mail Merge", msg);	// (one document: shown at once, its file in the title)
		res = done ? 2 : 0;
	}
	else
	{
		if (merge_combined (tb, tn, r0, r1, g_doc)) res = 1;
		else say ("Mail Merge", "The letter could not be read.");
	}
	delete[] tb;
	return res;
}

// ---- Tools > Mail Merge ------------------------------------------------------------------------------------------
// The dialog closes to act (the page behind it does not repaint), and comes back.
static struct { long fields, preview, which, naming, b2, b3, b4, b5, b6, b7; } MD;
static void (*g_mergeShow) ();				// the app's: the page shown again
static long md_fn (long d, long w, long code)
{
	if (code == UN_ACTIVATE && w == MD.b2) ui_dialog_close (d, 2);
	if (code == UN_ACTIVATE && w == MD.b3) ui_dialog_close (d, 3);
	if (code == UN_ACTIVATE && w == MD.fields) ui_dialog_close (d, 3);
	if (code == UN_ACTIVATE && w == MD.b4) ui_dialog_close (d, 4);
	if (code == UN_ACTIVATE && w == MD.b5) ui_dialog_close (d, 5);
	if (code == UN_ACTIVATE && w == MD.b6) ui_dialog_close (d, 6);
	if (code == UN_ACTIVATE && w == MD.b7) ui_dialog_close (d, 7);
	return 0;
}
// The merge's documents: a request written, and another Letters started on it, as Cardfile does.
static bool merge_spawn (int rec)
{
	Out o; o.init (); rtf_save (g_doc, o);
	char letter[120], jobp[120], job[600];
	snprintf (letter, sizeof letter, "%s/merge-letter.rtf", APP_HOME); snprintf (jobp, sizeof jobp, "%s/merge.job", APP_HOME);
	bool ok = file_write (letter, o.b, o.n);
	o.free ();
	char recs[16]; if (rec >= 0) snprintf (recs, sizeof recs, "%d", rec + 1); else strcpy (recs, "all");
	snprintf (job, sizeof job, "template = %s\ndata = %s\nrecords = %s\noutput = open\n", letter, g_mdataPath, recs);
	if (ok) ok = file_write (jobp, job, strlen (job));
	if (!ok) return false;
	static char a1[] = "--merge"; static char a2[120]; strcpy (a2, jobp);
	char *argv[] = { (char *) "SD0:/apps/letters.app/main", a1, a2, 0 };
	pid_t p;
	return posix_spawn (&p, argv[0], 0, 0, argv, environ) == 0;
}
static void dlg_mail_merge ()
{
	if (g_doc.mergeSrc[0] && !g_mdataOk) { const char *why; merge_open (g_doc.mergeSrc, &why); }
	for (;;)
	{
		enum { W = 560, H = 390 };
		long d = ui_dialog_begin (W, H, (long) "Mail Merge");
		int c = ctl_h () + 2;
		char t[120];
		d_label (16, 12, 70, "Records:");
		d_label (90, 12, W - 220, g_mdataOk ? g_mdataPath : "(none: choose a .card file)", g_mdataOk ? 0 : ULB_DIM);
		MD.b2 = d_button (W - 124, 12, 108, "Choose...");
		if (g_mdataOk) snprintf (t, sizeof t, TR (g_mdata.nr == 1 ? "%s -- %d record" : "%s -- %d records"), g_mdata.title[0] ? g_mdata.title : TR ("(untitled form)"), g_mdata.nr);
		d_label (90, 12 + c, W - 110, g_mdataOk ? t : "", ULB_DIM);
		d_label (16, 12 + 2 * c, 260, "Its fields (double click: inserted):");
		MD.fields = ui_list (16, 12 + 3 * c, 250, H - 3 * c - 100, 0);
		char names[800]; strcpy (names, TR ("Numbered (letter-1, letter-2...)"));
		if (g_mdataOk)
			for (int k = 0; k < g_mdata.nf; k++)
			{
				ui_list_add (MD.fields, (long) (g_mdata.f[k].label[0] ? g_mdata.f[k].label : g_mdata.f[k].column), 0);
				if (strlen (names) + 64 < sizeof names) { strcat (names, "|"); strcat (names, TR ("Named after:")); strcat (names, " "); strncat (names, g_mdata.f[k].label, 40); }
			}
		ui_w_set_value (MD.fields, 0);
		MD.b3 = d_button (16, H - 90, 250, "Insert the Field");
		int y = 12 + 3 * c;
		MD.preview = d_check (290, y, 250, "Preview the values", g_mpreview); y += c;
		MD.b4 = d_button (290, y, 40, "<");
		snprintf (t, sizeof t, TR ("Record %d of %d"), g_mrec + 1, g_mdataOk ? g_mdata.nr : 0);
		d_label (336, y, 158, t, ULB_CENTER);
		MD.b5 = d_button (500, y, 44, ">"); y += c + 4;
		d_label (290, y, 250, "Merge:"); y += c;
		MD.which = d_drop (290, y, 254, "All the records|The record previewed", 0); y += c;
		d_label (290, y, 250, "Files:"); y += c;
		MD.naming = d_drop (290, y, 254, names, 0);
		MD.b6 = d_button (16, H - 46, 210, "Merge to a New Document");
		MD.b7 = d_button (232, H - 46, 150, "Merge to Files...");
		ui_dialog_button (d, 0, (long) "Close", 0, 0, W - 12);
		ui_dialog_cancel (d, 0);
		long r = ui_dialog_run (d, (long) md_fn, 0);
		int fsel = (int) ui_w_get_value (MD.fields), which = (int) ui_w_get_value (MD.which), naming = (int) ui_w_get_value (MD.naming);
		bool prev = ui_w_get_value (MD.preview) != 0;
		ui_dialog_end (d);
		if (prev != g_mpreview) { merge_show (prev); g_relayout = true; }
		if (r == 0) return;
		if (r == 2)
		{
			char path[200] = "";
			ui_file_filter ((long) "*.card");
			long ok = ui_file_open ((long) "SD0:/home/documents", (long) path, sizeof path);
			ui_file_filter (0);
			if (ok)
			{
				const char *why;
				if (merge_open (path, &why)) { scpy (g_doc.mergeSrc, path, sizeof g_doc.mergeSrc); g_doc.changes++; merge_show (prev); g_relayout = true; }
				else say ("Mail Merge", "That file is not a Cardfile form (a .card file).");
			}
		}
		else if (r == 3) { if (g_mdataOk && fsel >= 0 && fsel < g_mdata.nf) ed_insert_field (FK_MERGE, g_mdata.f[fsel].column); }
		else if (r == 4 || r == 5)
		{
			if (g_mdataOk && g_mdata.nr) { g_mrec = wclamp (g_mrec + (r == 4 ? -1 : 1), 0, g_mdata.nr - 1); merge_show (true); g_relayout = true; }
		}
		else if (!g_mdataOk || !g_mdata.nr) say ("Mail Merge", "Choose the records first (a Cardfile form with records).");
		else
		{
			if (g_doc.cur != SY_BODY) ed_story (SY_BODY);
			if (r == 6) { if (!merge_spawn (which == 1 ? g_mrec : -1)) say ("Mail Merge", "Letters could not be started for the documents made."); }
			else
			{
				char path[200] = "";
				if (ui_file_save ((long) DOCS_DIR, (long) "letter.rtf", (long) path, sizeof path))
				{
					Out o; o.init (); rtf_save (g_doc, o);
					int n = slen (path);
					const char *ext = n > 5 && !sicmp (path + n - 5, ".docx") ? ".docx" : n > 4 && !sicmp (path + n - 4, ".odt") ? ".odt" : ".rtf";
					char folder[200], base[80]; scpy (folder, path, sizeof folder);
					int k = slen (folder); while (k > 0 && folder[k - 1] != '/' && folder[k - 1] != ':') k--;
					scpy (base, folder + k, sizeof base); folder[k] = 0;
					if (k > 1 && folder[k - 1] == '/') folder[k - 1] = 0;
					int b = slen (base); while (b > 0 && base[b - 1] != '.') b--; if (b > 1) base[b - 1] = 0;
					const char *field = naming > 0 ? g_mdata.f[naming - 1].column : "";
					char first[200];
					int r0 = 0, r1 = g_mdata.nr - 1;
					if (which == 1) r0 = r1 = g_mrec;
					int done = merge_files (o.b, o.n, r0, r1, folder, field, base, ext, first, sizeof first);
					char msg[120]; merge_done_msg (done, folder, msg, sizeof msg);
					say ("Mail Merge", msg);
					o.free ();
				}
			}
		}
		if (g_mergeShow) g_mergeShow ();
	}
}

} // namespace wr

#endif
