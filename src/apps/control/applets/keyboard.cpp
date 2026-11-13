/* applets/keyboard.cpp -- volet à porter (provisoire : le cadre seul). */
#include "../control.h"

static void open_ (void) { applet_row ("À porter.", R_NOTE); }
static void apply_ (void) {}
const Applet keyboard_applet = { "clavier", "Région", "Région et langue", "clavier", open_, apply_ };
