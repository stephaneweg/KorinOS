/* applets/dotnet.cpp -- volet à porter (provisoire : le cadre seul). */
#include "../control.h"

static void open_ (void) { applet_row ("À porter.", R_NOTE); }
static void apply_ (void) {}
const Applet dotnet_applet = { "dotnet", ".NET", "Assemblys .NET", "dotnet", open_, apply_ };
