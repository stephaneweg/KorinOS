/* applets/remote.cpp -- volet à porter (provisoire : le cadre seul). */
#include "../control.h"

static void open_ (void) { applet_row ("À porter.", R_NOTE); }
static void apply_ (void) {}
const Applet remote_applet = { "remote", "Accès distant", "Accès à distance", "terminal", open_, apply_ };
