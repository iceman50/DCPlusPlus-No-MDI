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

#ifndef DCPLUSPLUS_DCPP_PRIVATE_CHAT_MANAGER_LISTENER_H
#define DCPLUSPLUS_DCPP_PRIVATE_CHAT_MANAGER_LISTENER_H

#include "forward.h"
#include "HintedUser.h"

namespace dcpp {

class PrivateChatManagerListener {
public:
	virtual ~PrivateChatManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> PrivateMessage;

	virtual void on(PrivateMessage, const ChatMessage&, const HintedUser&, bool /* fromBot */) noexcept { }
};

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_PRIVATE_CHAT_MANAGER_LISTENER_H
