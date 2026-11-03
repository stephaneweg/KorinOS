//
// xml.cpp -- see xml.h. Namespaces stay as they are spelt in the file: a declaration is an attribute like any other.
//
#include "xml.h"
#include <cstring>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>

namespace vd {

static uint32_t s_uid;

XNode *x_new (XType t, const std::string &name)
{
	XNode *n = new XNode;
	n->type = t; n->name = name; n->uid = ++s_uid;
	return n;
}
XNode::~XNode ()
{
	for (XNode *k : kids) delete k;
}
const char *XNode::get (const char *n) const
{
	for (auto &a : attrs) if (a.name == n) return a.value.c_str ();
	return nullptr;
}
int XNode::index () const
{
	if (!parent) return -1;
	for (size_t i = 0; i < parent->kids.size (); i++) if (parent->kids[i] == this) return (int) i;
	return -1;
}
XNode *XNode::clone () const
{
	XNode *c = x_new (type, name);
	c->text = text; c->attrs = attrs;
	for (XNode *k : kids) { XNode *q = k->clone (); q->parent = c; c->kids.push_back (q); }
	return c;
}
std::string XNode::text_content () const
{
	if (type == X_TEXT || type == X_CDATA) return text;
	std::string o;
	for (XNode *k : kids) o += k->text_content ();
	return o;
}

static std::string qname (const xmlChar *name, xmlNs *ns)
{
	std::string s;
	if (ns && ns->prefix) { s = (const char *) ns->prefix; s += ':'; }
	s += (const char *) name;
	return s;
}
static XNode *convert (xmlNode *n)
{
	XNode *x = nullptr;
	switch (n->type)
	{
	case XML_ELEMENT_NODE:
	{
		x = x_new (X_ELEMENT, qname (n->name, n->ns));
		for (xmlNs *ns = n->nsDef; ns; ns = ns->next)
		{
			std::string a = "xmlns";
			if (ns->prefix) { a += ':'; a += (const char *) ns->prefix; }
			x->attrs.push_back ({ a, ns->href ? (const char *) ns->href : "" });
		}
		for (xmlAttr *a = n->properties; a; a = a->next)
		{
			xmlChar *v = xmlNodeListGetString (n->doc, a->children, 1);
			x->attrs.push_back ({ qname (a->name, a->ns), v ? (const char *) v : "" });
			if (v) xmlFree (v);
		}
		for (xmlNode *k = n->children; k; k = k->next)
		{
			XNode *c = convert (k);
			if (c) { c->parent = x; x->kids.push_back (c); }
		}
		break;
	}
	case XML_TEXT_NODE:
		x = x_new (X_TEXT, ""); x->text = n->content ? (const char *) n->content : "";
		break;
	case XML_CDATA_SECTION_NODE:
		x = x_new (X_CDATA, ""); x->text = n->content ? (const char *) n->content : "";
		break;
	case XML_COMMENT_NODE:
		x = x_new (X_COMMENT, ""); x->text = n->content ? (const char *) n->content : "";
		break;
	case XML_PI_NODE:
		x = x_new (X_PI, (const char *) n->name); x->text = n->content ? (const char *) n->content : "";
		break;
	default:
		break;
	}
	return x;
}

XDoc x_parse (const char *data, size_t n, std::string *err)
{
	XDoc d;
	// (NOENT: Illustrator's files spell their namespaces as entities; HUGE: long paths and embedded images)
	xmlDocPtr doc = xmlReadMemory (data, (int) n, "doc.svg", nullptr,
		XML_PARSE_NOENT | XML_PARSE_NONET | XML_PARSE_HUGE | XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (!doc || !xmlDocGetRootElement (doc))
	{
		if (err) { const xmlError *e = xmlGetLastError (); *err = e && e->message ? e->message : "not an XML file"; }
		if (doc) xmlFreeDoc (doc);
		return d;
	}
	d.top = x_new (X_ELEMENT, "#top");
	for (xmlNode *k = doc->children; k; k = k->next)
	{
		XNode *c = convert (k);
		if (!c) continue;
		c->parent = d.top;
		d.top->kids.push_back (c);
		if (c->type == X_ELEMENT && !d.root) d.root = c;
	}
	xmlFreeDoc (doc);
	return d;
}

static void esc (std::string &o, const std::string &s, bool attr)
{
	for (char c : s)
		switch (c)
		{
		case '&': o += "&amp;"; break;
		case '<': o += "&lt;"; break;
		case '>': o += attr ? ">" : "&gt;"; break;
		case '"': o += attr ? "&quot;" : "\""; break;
		case '\n': o += attr ? "&#10;" : "\n"; break;
		case '\t': o += attr ? "&#9;" : "\t"; break;
		default: o += c;
		}
}
static void out (std::string &o, const XNode *n)
{
	switch (n->type)
	{
	case X_TEXT: esc (o, n->text, false); break;
	case X_CDATA: o += "<![CDATA["; o += n->text; o += "]]>"; break;
	case X_COMMENT: o += "<!--"; o += n->text; o += "-->"; break;
	case X_PI: o += "<?"; o += n->name; if (!n->text.empty ()) { o += ' '; o += n->text; } o += "?>"; break;
	case X_ELEMENT:
		o += '<'; o += n->name;
		for (auto &a : n->attrs) { o += ' '; o += a.name; o += "=\""; esc (o, a.value, true); o += '"'; }
		if (n->kids.empty ()) { o += "/>"; break; }
		o += '>';
		for (XNode *k : n->kids) out (o, k);
		o += "</"; o += n->name; o += '>';
		break;
	}
}
std::string x_write (const XNode *top)
{
	std::string o = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	for (XNode *k : top->kids) { out (o, k); o += '\n'; }
	return o;
}
std::string x_write_node (const XNode *n)
{
	std::string o;
	out (o, n);
	return o;
}

}
