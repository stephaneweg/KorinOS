/* applets/panel.cpp -- volet à porter (provisoire : le cadre seul). */
#include "../control.h"

static void open_ (void) { applet_row ("À porter.", R_NOTE); }
static void apply_ (void) {}
const Applet panel_applet = { "panelconf", "Panel", "Panel", "panelconf", open_, apply_ };
