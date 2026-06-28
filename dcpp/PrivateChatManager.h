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

#ifndef DCPLUSPLUS_DCPP_PRIVATE_CHAT_MANAGER_H
#define DCPLUSPLUS_DCPP_PRIVATE_CHAT_MANAGER_H

#include <functional>
#include <unordered_map>

#include "ConnectionManagerListener.h"
#include "CriticalSection.h"
#include "PrivateChatManagerListener.h"
#include "Singleton.h"
#include "Speaker.h"
#include "User.h"
#include "UserConnectionListener.h"

namespace dcpp {

class PrivateChatManager :
	public Singleton<PrivateChatManager>,
	public Speaker<PrivateChatManagerListener>,
	private ConnectionManagerListener,
	private UserConnectionListener
{
public:
	using AcceptConnectionF = std::function<bool (const UserPtr&)>;

	void setAcceptConnectionF(AcceptConnectionF f);

	UserConnection* getPMConn(const UserPtr& user, UserConnectionListener* listener);
	void returnPMConn(const UserPtr& user, UserConnection* uc, UserConnectionListener* listener);
	void releasePMConn(const UserPtr& user, bool disconnect);

private:
	friend class Singleton<PrivateChatManager>;

	PrivateChatManager();
	~PrivateChatManager();

	unordered_map<UserPtr, UserConnection*, User::Hash> ccpms;
	CriticalSection cs;
	AcceptConnectionF acceptConnectionF;

	// ConnectionManagerListener
	void on(ConnectionManagerListener::Connected, ConnectionQueueItem* cqi, UserConnection* uc) noexcept;
	void on(ConnectionManagerListener::Removed, ConnectionQueueItem* cqi) noexcept;

	// UserConnectionListener
	void on(UserConnectionListener::PrivateMessage, UserConnection* uc, const ChatMessage& message) noexcept;
	void on(AdcCommand::PMI, UserConnection* uc, const AdcCommand& cmd) noexcept;
};

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_PRIVATE_CHAT_MANAGER_H
