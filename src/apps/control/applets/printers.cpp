/* applets/printers.cpp -- volet à porter (provisoire : le cadre seul). */
#include "../control.h"

static void open_ (void) { applet_row ("À porter.", R_NOTE); }
static void apply_ (void) {}
const Applet printers_applet = { "printconf", "Imprimantes", "Imprimantes", "printconf", open_, apply_ };
