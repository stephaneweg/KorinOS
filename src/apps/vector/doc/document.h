//
// document.h -- one open drawing: its tree, its ids, and the journal. Every change goes through set, insert, remove,
// move or set_text, inside a begin / commit: undoing is playing the journal backwards, and the same list of touched
// nodes tells the scene what to rebuild. Nothing else writes to the tree.
//
#ifndef _vector_document_h
#define _vector_document_h

#include "xml.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace vd {

enum OpKind : uint8_t { OP_ATTR, OP_TEXT, OP_INSERT, OP_REMOVE, OP_MOVE };

struct Op
{
	OpKind kind;
	bool hadOld = false, hasNew = false;
	XNode *node = nullptr;
	XNode *parent = nullptr, *parent2 = nullptr;
	int index = 0, index2 = 0;
	std::string name, oldv, newv;
};

struct Transaction
{
	std::string label;
	int mergeKey = 0;			// not 0: a next transaction of the same key melts into this one
	std::vector<Op> ops;
	size_t bytes = 0;
};

class Document
{
public:
	XNode *top = nullptr, *root = nullptr;
	std::string path;			// where it was read (empty: never saved)

	Document ();
	~Document ();
	bool load (const char *data, size_t n, std::string *err);
	void create_blank (double w, double h);
	std::string text () const { return x_write (top); }

	// ---- changes
	void begin (const char *label, int mergeKey = 0);
	void commit ();
	void cancel ();				// undoes what the open transaction did
	void set (XNode *n, const char *name, const char *value);	// nullptr removes the attribute
	void set (XNode *n, const char *name, const std::string &v) { set (n, name, v.c_str ()); }
	void set_text (XNode *n, const std::string &t);
	void insert (XNode *parent, int index, XNode *n);		// index < 0: at the end; n is the journal's from then on
	void remove (XNode *n);
	void move (XNode *n, XNode *parent, int index);

	bool can_undo () const { return pos > 0; }
	bool can_redo () const { return pos < (int) log.size (); }
	const char *undo_label () const { return can_undo () ? log[pos - 1].label.c_str () : ""; }
	const char *redo_label () const { return can_redo () ? log[pos].label.c_str () : ""; }
	bool undo ();
	bool redo ();
	bool modified () const { return pos != savedPos; }
	void mark_saved () { savedPos = pos; }
	int history () const { return (int) log.size (); }

	// ---- ids
	XNode *by_id (const std::string &id) const;
	std::string new_id (const char *prefix);

	// ---- what changed since the scene last looked
	std::vector<XNode *> touched;
	bool structure = false;			// a node came, went or moved
	unsigned revision = 1;

private:
	std::vector<Transaction> log;
	int pos = 0, savedPos = 0;
	Transaction open;
	int depth = 0;
	size_t logBytes = 0;
	std::unordered_map<std::string, XNode *> ids;
	unsigned idSeq = 1;

	void index_ids (XNode *n, bool add);
	void apply (const Op &o, bool forward);
	void attr_set (XNode *n, const std::string &name, const std::string *v, int at = -1);
	void trim ();
	void forget (size_t from, size_t to, bool undone);
	void reset ();
};

}

#endif
