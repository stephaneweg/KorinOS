//
// fonts.h -- the card's fonts, found without opening them whole (fonts.cpp).
//
#ifndef _vector_fonts_h
#define _vector_fonts_h

#include "include/core/SkTypeface.h"
#include <cstdint>
#include <string>
#include <vector>

namespace vr {

std::vector<std::string> font_families ();
sk_sp<SkTypeface> font_match (const char *family, int weight, int width, bool italic);	// nullptr: no such family
sk_sp<SkTypeface> font_for_char (uint32_t cp, int weight, bool italic);
sk_sp<SkTypeface> font_ui ();		// the overlay's face, without waiting for the scan
void fonts_scan_async ();		// the scan on a thread: the first text must not freeze the window

}

#endif
