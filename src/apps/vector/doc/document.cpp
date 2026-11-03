//
// document.cpp -- see document.h. A removed node stays alive, detached, for as long as the journal may bring it back.
//
#include "document.h"
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace vd {

static const size_t LOG_LIMIT = 64u << 20;	// bytes the journal may hold

Document::Document () {}
Document::~Document () { reset (); }

void Document::reset ()
{
	forget (0, log.size (), true);
	log.clear ();
	pos = savedPos = 0;
	logBytes = 0;
	ids.clear ();
	delete top;
	top = root = nullptr;
}

void Document::index_ids (XNode *n, bool add)
{
	if (!n->element ()) return;
	if (const char *id = n->get ("id"))
	{
		if (add) { if (!ids.count (id)) ids[id] = n; }
		else { auto f = ids.find (id); if (f != ids.end () && f->second == n) ids.erase (f); }
	}
	for (XNode *k : n->kids) index_ids (k, add);
}

bool Document::load (const char *data, size_t n, std::string *err)
{
	XDoc d = x_parse (data, n, err);
	if (!d.root) return false;
	reset ();
	top = d.top; root = d.root;
	index_ids (root, true);
	structure = true;
	revision++;
	return true;
}

void Document::create_blank (double w, double h)
{
	char b[512];
	snprintf (b, sizeof b, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%gmm\" height=\"%gmm\" viewBox=\"0 0 %g %g\" version=\"1.1\"></svg>", w, h, w, h);
	load (b, strlen (b), nullptr);
}

XNode *Document::by_id (const std::string &id) const
{
	auto f = ids.find (id);
	return f == ids.end () ? nullptr : f->second;
}
std::string Document::new_id (const char *prefix)
{
	for (;;)
	{
		std::string s = prefix + std::to_string (idSeq++);
		if (!ids.count (s)) return s;
	}
}

void Document::begin (const char *label, int mergeKey)
{
	if (depth++ == 0) { open = Transaction (); open.label = label; open.mergeKey = mergeKey; }
}
// Transactions leave the log: the detached nodes they alone could bring back have no owner any more. All of a range at
// once: one node appears in several transactions. undone: the changes are not applied (an insert gone with them); else
// they stay applied, and only what they removed goes -- a node they inserted may be another transaction's.
void Document::forget (size_t from, size_t to, bool undone)
{
	std::unordered_set<XNode *> gone;
	for (size_t i = from; i < to; i++)
		for (auto &o : log[i].ops)
			if ((o.kind == OP_REMOVE || (undone && o.kind == OP_INSERT)) && !o.node->parent && o.node != root) gone.insert (o.node);
	for (XNode *n : gone) delete n;
}
void Document::trim ()
{
	while (logBytes > LOG_LIMIT && pos > 1)
	{
		logBytes -= log[0].bytes;
		forget (0, 1, false);
		log.erase (log.begin ());
		pos--;
		savedPos = savedPos > 0 ? savedPos - 1 : -1;	// (past the limit, the saved state cannot come back)
	}
}
void Document::commit ()
{
	if (depth == 0 || --depth > 0) return;
	if (open.ops.empty ()) return;
	for (size_t i = pos; i < log.size (); i++) logBytes -= log[i].bytes;
	forget (pos, log.size (), true);
	log.resize (pos);
	if (savedPos > pos) savedPos = -1;
	for (auto &o : open.ops) open.bytes += sizeof (Op) + o.oldv.size () + o.newv.size () + o.name.size ();
	if (open.mergeKey && pos > 0 && log[pos - 1].mergeKey == open.mergeKey && savedPos != pos)
	{
		Transaction &l = log[pos - 1];
		for (auto &o : open.ops) l.ops.push_back (std::move (o));
		l.bytes += open.bytes;
	}
	else
	{
		log.push_back (std::move (open));
		pos++;
		logBytes += log.back ().bytes;
	}
	open = Transaction ();
	trim ();
}
void Document::cancel ()
{
	if (depth == 0) return;
	depth = 0;
	for (int i = (int) open.ops.size () - 1; i >= 0; i--) apply (open.ops[i], false);
	log.push_back (std::move (open));
	forget (log.size () - 1, log.size (), true);
	log.pop_back ();
	open = Transaction ();
}

void Document::attr_set (XNode *n, const std::string &name, const std::string *v, int at)
{
	bool isId = name == "id";
	if (isId) { if (const char *old = n->get ("id")) { auto f = ids.find (old); if (f != ids.end () && f->second == n) ids.erase (f); } }
	bool found = false;
	for (size_t i = 0; i < n->attrs.size (); i++)
		if (n->attrs[i].name == name)
		{
			found = true;
			if (v) n->attrs[i].value = *v;
			else n->attrs.erase (n->attrs.begin () + i);
			break;
		}
	if (!found && v)
	{
		// (undoing a removal puts it back where it stood: the file stays the same to the byte)
		if (at >= 0 && at < (int) n->attrs.size ()) n->attrs.insert (n->attrs.begin () + at, { name, *v });
		else n->attrs.push_back ({ name, *v });
	}
	if (isId && v && !ids.count (*v)) ids[*v] = n;
	touched.push_back (n);
	revision++;
}

void Document::apply (const Op &o, bool forward)
{
	switch (o.kind)
	{
	case OP_ATTR:
		if (forward) attr_set (o.node, o.name, o.hasNew ? &o.newv : nullptr);
		else attr_set (o.node, o.name, o.hadOld ? &o.oldv : nullptr, o.index);
		return;
	case OP_TEXT:
		o.node->text = forward ? o.newv : o.oldv;
		touched.push_back (o.node->parent ? o.node->parent : o.node);
		revision++;
		return;
	case OP_INSERT:
	case OP_REMOVE:
		if ((o.kind == OP_INSERT) == forward)
		{
			o.parent->kids.insert (o.parent->kids.begin () + o.index, o.node);
			o.node->parent = o.parent;
			index_ids (o.node, true);
		}
		else
		{
			index_ids (o.node, false);
			o.parent->kids.erase (o.parent->kids.begin () + o.index);
			o.node->parent = nullptr;
		}
		break;
	case OP_MOVE:
	{
		XNode *from = forward ? o.parent : o.parent2, *to = forward ? o.parent2 : o.parent;
		int fi = forward ? o.index : o.index2, ti = forward ? o.index2 : o.index;
		from->kids.erase (from->kids.begin () + fi);
		to->kids.insert (to->kids.begin () + ti, o.node);
		o.node->parent = to;
		break;
	}
	}
	structure = true;
	revision++;
}

void Document::set (XNode *n, const char *name, const char *value)
{
	const char *old = n->get (name);
	if ((!old && !value) || (old && value && !strcmp (old, value))) return;
	Op o;
	o.kind = OP_ATTR; o.node = n; o.name = name;
	o.index = -1;
	for (size_t i = 0; i < n->attrs.size (); i++) if (n->attrs[i].name == name) o.index = (int) i;
	if (old) { o.hadOld = true; o.oldv = old; }
	if (value) { o.hasNew = true; o.newv = value; }
	apply (o, true);
	open.ops.push_back (std::move (o));
}
void Document::set_text (XNode *n, const std::string &t)
{
	if (n->text == t) return;
	Op o;
	o.kind = OP_TEXT; o.node = n; o.oldv = n->text; o.newv = t;
	apply (o, true);
	open.ops.push_back (std::move (o));
}
void Document::insert (XNode *parent, int index, XNode *n)
{
	Op o;
	o.kind = OP_INSERT; o.node = n; o.parent = parent;
	o.index = index < 0 || index > (int) parent->kids.size () ? (int) parent->kids.size () : index;
	apply (o, true);
	open.ops.push_back (std::move (o));
}
void Document::remove (XNode *n)
{
	Op o;
	o.kind = OP_REMOVE; o.node = n; o.parent = n->parent; o.index = n->index ();
	apply (o, true);
	open.ops.push_back (std::move (o));
}
void Document::move (XNode *n, XNode *parent, int index)
{
	Op o;
	o.kind = OP_MOVE; o.node = n; o.parent = n->parent; o.index = n->index (); o.parent2 = parent;
	int cap = (int) parent->kids.size () - (parent == n->parent ? 1 : 0);
	o.index2 = index < 0 || index > cap ? cap : index;
	if (o.parent == parent && o.index == o.index2) return;
	apply (o, true);
	open.ops.push_back (std::move (o));
}

bool Document::undo ()
{
	if (!can_undo ()) return false;
	Transaction &t = log[--pos];
	for (int i = (int) t.ops.size () - 1; i >= 0; i--) apply (t.ops[i], false);
	return true;
}
bool Document::redo ()
{
	if (!can_redo ()) return false;
	Transaction &t = log[pos++];
	for (auto &o : t.ops) apply (o, true);
	return true;
}

}
