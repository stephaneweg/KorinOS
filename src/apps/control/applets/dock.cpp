/* applets/dock.cpp -- volet à porter (provisoire : le cadre seul). */
#include "../control.h"

static void open_ (void) { applet_row ("À porter.", R_NOTE); }
static void apply_ (void) {}
const Applet dock_applet = { "dockconf", "Dock", "Dock", "dockconf", open_, apply_ };
