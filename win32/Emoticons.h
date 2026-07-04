/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_EMOTICONS_H
#define DCPLUSPLUS_WIN32_EMOTICONS_H

#include <string>

#include <dcpp/typedefs.h>

class Emoticons {
public:
	/** Decode a packaged BMP, ICO, or PNG and return it as a self-contained RTF PNG.
	 *
	 * Images are loaded from the active .dcemo package, scaled, converted to a full-color PNG, and
	 * cached by path, display size, and preferred ICO frame bit depth. Full-color PNG is accepted
	 * reliably by RICHEDIT50W and avoids palette quantization while preserving source alpha.
	 */
	static dcpp::tstring rtf(const std::string& name, int pixels, int bitDepth);
};

#endif
