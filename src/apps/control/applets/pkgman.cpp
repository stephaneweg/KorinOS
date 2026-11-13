/* applets/pkgman.cpp -- volet à porter (provisoire : le cadre seul). */
#include "../control.h"

static void open_ (void) { applet_row ("À porter.", R_NOTE); }
static void apply_ (void) {}
const Applet pkgman_applet = { "pkgman", "Logiciels", "Logiciels", "pkgman", open_, apply_ };
