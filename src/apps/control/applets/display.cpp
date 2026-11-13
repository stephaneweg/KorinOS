/* applets/display.cpp -- volet à porter (provisoire : le cadre seul). */
#include "../control.h"

static void open_ (void) { applet_row ("À porter.", R_NOTE); }
static void apply_ (void) {}
const Applet display_applet = { "ecran", "Écran", "Écran", "ecran", open_, apply_ };
