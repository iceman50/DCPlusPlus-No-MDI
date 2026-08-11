/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef HASHMANAGERLISTENER_H_
#define HASHMANAGERLISTENER_H_

#include "forward.h"

namespace dcpp {

class HashManagerListener {
public:
	virtual ~HashManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> TTHDone;
	typedef X<1> TTHFailed;

	enum class Failure {
		FILE_IO,
		FILE_CHANGED,
		CRC_MISMATCH,
		INCOMPLETE,
		CANCELLED,
		HASH_STORE
	};

	/** A hash job completed for the exact file snapshot described by size and timestamp. */
	virtual void on(TTHDone, const string& /* fileName */, const TTHValue& /* root */,
		int64_t /* size */, uint32_t /* timestamp */, uint64_t /* jobId */) noexcept = 0;
	/** A queued hash job reached a terminal failure. Defaulted for listeners that only need success. */
	virtual void on(TTHFailed, const string& /* fileName */, int64_t /* size */, uint32_t /* timestamp */,
		uint64_t /* jobId */, Failure /* reason */, const string& /* detail */) noexcept { }
};

}
#endif /*HASHMANAGERLISTENER_H_*/
