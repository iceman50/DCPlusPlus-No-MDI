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

#ifndef DCPLUSPLUS_DCPP_FLAGS_H_
#define DCPLUSPLUS_DCPP_FLAGS_H_

#include <atomic>

namespace dcpp {

class Flags {
public:
	typedef int MaskType;

	Flags() : flags(0) { }
	Flags(MaskType f) : flags(f) { }
	Flags(const Flags& rhs) noexcept : flags(rhs.getFlags()) { }
	Flags& operator=(const Flags& rhs) noexcept { flags.store(rhs.getFlags(), std::memory_order_relaxed); return *this; }
	bool isSet(MaskType aFlag) const { return (flags.load(std::memory_order_relaxed) & aFlag) == aFlag; }
	bool isAnySet(MaskType aFlag) const { return (flags.load(std::memory_order_relaxed) & aFlag) != 0; }
	void setFlag(MaskType aFlag) { flags.fetch_or(aFlag, std::memory_order_relaxed); }
	void unsetFlag(MaskType aFlag) { flags.fetch_and(~aFlag, std::memory_order_relaxed); }
	MaskType getFlags() const { return flags.load(std::memory_order_relaxed); }

private:
	std::atomic<MaskType> flags;
};

} // namespace dcpp

#endif /*FLAGS_H_*/
