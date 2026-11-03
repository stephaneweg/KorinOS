//
// lpe.h -- path effects and filter presets (lpe.cpp).
//
#ifndef _vector_lpe_h
#define _vector_lpe_h

#include "../geom/path.h"
#include "../doc/xml.h"
#include <vector>

namespace ve {

struct LpeParam { const char *name, *label; double def, min, max, step; };
struct LpeType { const char *id, *label; const LpeParam *params; int nparams; };
extern const LpeType LPE_TYPES[];
extern const int LPE_COUNT;

const LpeType *lpe_type (const char *id);
double lpe_param (const vd::XNode *e, const LpeParam &p);
vg::Path lpe_apply (const vd::XNode *e, const vg::Path &in);
std::vector<vd::XNode *> lpe_stack (const vd::XNode *path);
void lpe_refresh (vd::XNode *path);		// inside a transaction
void lpe_add (vd::XNode *&path, const LpeType *t);
void lpe_remove (vd::XNode *path, int index);
void lpe_set (vd::XNode *path, vd::XNode *e, const LpeParam &p, double v);

int filter_presets ();
const char *filter_preset_label (int i);
void filter_apply (int i);
void filter_remove ();

}

#endif
