//
// xml.h -- the document's tree. It is the truth: everything else derives from it. libxml2 only reads and is never kept;
// what we do not understand stays here and is written back as it came, comments and spacing included.
//
#ifndef _vector_xml_h
#define _vector_xml_h

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vd {

struct XAttr { std::string name, value; };

enum XType : uint8_t { X_ELEMENT, X_TEXT, X_CDATA, X_COMMENT, X_PI };

struct XNode
{
	XType type = X_ELEMENT;
	std::string name;			// element name with its prefix ("sodipodi:namedview"), or the PI's target
	std::string text;			// text, comment, PI content
	std::vector<XAttr> attrs;
	std::vector<XNode *> kids;
	XNode *parent = nullptr;
	uint32_t uid = 0;			// stable for the session: the scene's key

	XNode () {}
	~XNode ();
	const char *get (const char *n) const;	// nullptr when absent
	const char *get2 (const char *n, const char *def) const { const char *v = get (n); return v ? v : def; }
	bool is (const char *n) const { return type == X_ELEMENT && name == n; }
	bool element () const { return type == X_ELEMENT; }
	int index () const;
	XNode *clone () const;			// deep, without a parent; new uids
	std::string text_content () const;
};

XNode *x_new (XType t, const std::string &name);	// a node with a fresh uid

struct XDoc { XNode *top = nullptr; XNode *root = nullptr; };	// top holds the root and the comments around it
XDoc x_parse (const char *data, size_t n, std::string *err);
std::string x_write (const XNode *top);
std::string x_write_node (const XNode *n);	// one element, for the clipboard

}

#endif
