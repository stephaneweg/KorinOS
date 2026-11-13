/* applets/wifi.cpp -- volet à porter (provisoire : le cadre seul). */
#include "../control.h"

static void open_ (void) { applet_row ("À porter.", R_NOTE); }
static void apply_ (void) {}
const Applet wifi_applet = { "wifi", "Wi-Fi", "Wi-Fi", "wifi", open_, apply_ };
