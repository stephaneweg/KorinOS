/* applets/preload.cpp -- volet à porter (provisoire : le cadre seul). */
#include "../control.h"

static void open_ (void) { applet_row ("À porter.", R_NOTE); }
static void apply_ (void) {}
const Applet preload_applet = { "preload", "Préchargés", "Préchargement", "preload", open_, apply_ };
