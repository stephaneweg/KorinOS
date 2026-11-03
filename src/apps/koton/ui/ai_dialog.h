//
// ui/ai_dialog.h -- "Compose with AI" on ARMPI: Onyx's dialog runs the helper SD:/bin/llm (a TLS program), which
// ARMPI does not have. The menu and the browser's entries stay; they say it is not available. (engine/ai*.cpp and the
// dialog are not compiled here: see README.md.)
//
#ifndef _koton_ai_dialog_h
#define _koton_ai_dialog_h

#include "ui/dialogs.h"

namespace kui {

enum { AI_COMPOSE, AI_DEVELOP, AI_ADD_TRACK, AI_ADD_DRUMS, AI_POLYRHYTHM };

// the dialog's last request: kept in settings.json as on Onyx
struct AiLast
{
	int kind; Str style, intention; int bars; bool full, drums, voice, pchords, pdrums;
	AiLast () : kind (AI_COMPOSE), style ("bossa nova, warm, acoustic"), intention ("A calm intro, a verse, a brighter chorus, an ending."),
		bars (32), full (false), drums (true), voice (false), pchords (false), pdrums (false) {}
};
struct AiSettings { char *provider; int provCap; char *model; int modelCap; char *key; int keyCap; AiLast *last; void (*save) ();
	bool (*askSave) (); };

static inline bool aiCompose (const AiSettings &, int)
{
	message ("Compose with AI", "Not available on Korin OS (the helper SD:/bin/llm is Onyx's).", UM_OK);
	return false;
}

} // namespace kui

#endif
