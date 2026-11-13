/* applets/theme.cpp -- volet à porter (provisoire : le cadre seul). */
#include "../control.h"

static void open_ (void) { applet_row ("À porter.", R_NOTE); }
static void apply_ (void) {}
const Applet theme_applet = { "theme", "Thème", "Thème", "theme", open_, apply_ };
