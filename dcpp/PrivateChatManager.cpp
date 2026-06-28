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

#include "stdinc.h"
#include "PrivateChatManager.h"

#include "ConnectionManager.h"
#include "ConnectionType.h"
#include "SettingsManager.h"
#include "UserConnection.h"

namespace dcpp {

PrivateChatManager::PrivateChatManager() {
	ConnectionManager::getInstance()->addListener(this);
}

PrivateChatManager::~PrivateChatManager() {
	ConnectionManager::getInstance()->removeListener(this);

	Lock l(cs);
	for(auto& i: ccpms) {
		i.second->removeListener(this);
	}
	ccpms.clear();
}

void PrivateChatManager::setAcceptConnectionF(AcceptConnectionF f) {
	Lock l(cs);
	acceptConnectionF = std::move(f);
}

UserConnection* PrivateChatManager::getPMConn(const UserPtr& user, UserConnectionListener* listener) {
	Lock l(cs);
	auto i = ccpms.find(user);
	if(i == ccpms.end()) {
		return nullptr;
	}

	auto uc = i->second;
	ccpms.erase(i);
	uc->removeListener(this);
	if(!uc->isSecure()) {
		uc->disconnect(true);
		return nullptr;
	}
	uc->addListener(listener);
	return uc;
}

void PrivateChatManager::returnPMConn(const UserPtr& user, UserConnection* uc, UserConnectionListener* listener) {
	if(!uc) {
		return;
	}

	uc->removeListener(listener);
	if(!uc->isSecure()) {
		uc->disconnect(true);
		return;
	}

	Lock l(cs);
	auto i = ccpms.find(user);
	if(i != ccpms.end() && i->second != uc) {
		i->second->removeListener(this);
		i->second = uc;
	} else if(i == ccpms.end()) {
		ccpms.emplace(user, uc);
	}
	uc->addListener(this);
}

void PrivateChatManager::releasePMConn(const UserPtr& user, bool disconnect) {
	UserConnection* uc = nullptr;
	{
		Lock l(cs);
		auto i = ccpms.find(user);
		if(i == ccpms.end()) {
			return;
		}

		uc = i->second;
		ccpms.erase(i);
	}

	uc->removeListener(this);
	if(disconnect) {
		uc->disconnect(true);
	}
}

void PrivateChatManager::on(ConnectionManagerListener::Connected, ConnectionQueueItem* cqi, UserConnection* uc) noexcept {
	if(cqi->getType() != CONNECTION_TYPE_PM) {
		return;
	}

	if(!uc->isSecure()) {
		uc->disconnect(true);
		return;
	}

	AcceptConnectionF f;
	{
		Lock l(cs);
		f = acceptConnectionF;
	}
	const auto frameOpen = f && f(cqi->getUser());

	if(!SETTING(POPUP_PMS) && !frameOpen) {
		uc->disconnect(true);
		return;
	}

	// An open PM frame adopts the connection through its ConnectionManager listener.
	// Parking it here as well would deliver the first direct message twice.
	if(frameOpen) {
		return;
	}

	{
		Lock l(cs);
		auto i = ccpms.find(cqi->getUser());
		if(i != ccpms.end()) {
			if(i->second != uc) {
				i->second->removeListener(this);
				i->second = uc;
			}
		} else {
			ccpms.emplace(cqi->getUser(), uc);
		}
		uc->addListener(this);
	}
}

void PrivateChatManager::on(ConnectionManagerListener::Removed, ConnectionQueueItem* cqi) noexcept {
	if(cqi->getType() != CONNECTION_TYPE_PM) {
		return;
	}

	releasePMConn(cqi->getUser().user, false);
}

void PrivateChatManager::on(UserConnectionListener::PrivateMessage, UserConnection* uc, const ChatMessage& message) noexcept {
	auto user = uc->getHintedUser();
	{
		Lock l(cs);
		if(ccpms.find(user.user) == ccpms.end()) {
			return;
		}
	}

	fire(PrivateChatManagerListener::PrivateMessage(), message, user, false);
}

void PrivateChatManager::on(AdcCommand::PMI, UserConnection* uc, const AdcCommand& cmd) noexcept {
	if(cmd.hasFlag("QU", 0)) {
		Lock l(cs);
		auto i = ccpms.find(uc->getUser());
		if(i != ccpms.end() && i->second == uc) {
			uc->disconnect(false);
		}
	}
}

} // namespace dcpp
