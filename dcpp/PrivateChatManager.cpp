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
	{
		Lock l(cs);
		shuttingDown = true;
		acceptConnectionF = nullptr;

		// Keep the ConnectionManager listener installed while detaching. A
		// concurrent Removed event will block on cs, keeping each connection
		// alive until its listener has been removed.
		for(const auto& known: knownCCPMs) {
			known.first->removeListener(this);
		}
		ccpms.clear();
		knownCCPMs.clear();
	}

	ConnectionManager::getInstance()->removeListener(this);
}

void PrivateChatManager::setAcceptConnectionF(AcceptConnectionF f) {
	Lock l(cs);
	acceptConnectionF = std::move(f);
}

UserConnection* PrivateChatManager::getPMConn(const UserPtr& user, const string& expectedToken)
{
	Lock l(cs);
	auto i = ccpms.find(user);
	if(i == ccpms.end() || (!expectedToken.empty() && i->second.token != expectedToken)) {
		return nullptr;
	}

	auto pm = i->second;
	// Never dereference a connection unless it is still registered with the
	// token captured when ConnectionManager announced it.
	auto known = knownCCPMs.find(pm.connection);
	if(known == knownCCPMs.end() || known->second.user != user || known->second.token != pm.token) {
		return nullptr;
	}

	// The frame registered as a ConnectionManager listener before asking for
	// this connection. Serialize with queue removal before dereferencing the
	// parked pointer: if removal already took its listener snapshot, the CQI
	// has already been erased and this handoff must be abandoned.
	if(!ConnectionManager::getInstance()->isPMConnectionActive(user, pm.token)) {
		ccpms.erase(i);
		return nullptr;
	}

	ccpms.erase(i);
	if(pm.connection->getState() != UserConnection::STATE_CMD || !pm.connection->isSecure()) {
		pm.connection->disconnect(true);
		return nullptr;
	}
	return pm.connection;
}

bool PrivateChatManager::returnPMConn(const UserPtr& user, const string& token, UserConnection* uc) {
	if(!uc) {
		return false;
	}

	Lock l(cs);

	// The pointer may have been invalidated before a queued UI action reached
	// us. Looking it up without dereferencing it lets Removed be the authority
	// on whether the connection is still alive.
	auto known = knownCCPMs.find(uc);
	if(known == knownCCPMs.end() || known->second.user != user || known->second.token != token) {
		return false;
	}

	if(shuttingDown || uc->getState() != UserConnection::STATE_CMD || !uc->isSecure()) {
		uc->disconnect(true);
		return false;
	}

	auto i = ccpms.find(user);
	if(i != ccpms.end() && i->second.connection != uc) {
		auto& old = i->second;
		auto oldKnown = knownCCPMs.find(old.connection);
		if(oldKnown != knownCCPMs.end() && oldKnown->second.user == user && oldKnown->second.token == old.token) {
			old.connection->disconnect(true);
		}
	}

	ccpms.insert_or_assign(user, PMConnection { uc, token });
	return true;
}

void PrivateChatManager::releasePMConn(const UserPtr& user, const string& token, bool disconnect) {
	Lock l(cs);
	auto i = ccpms.find(user);
	if(i == ccpms.end() || i->second.token != token) {
		return;
	}

	auto pm = i->second;
	auto known = knownCCPMs.find(pm.connection);
	if(known == knownCCPMs.end() || known->second.user != user || known->second.token != pm.token) {
		return;
	}

	ccpms.erase(i);
	if(disconnect) {
		pm.connection->disconnect(true);
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
	dcassert(cqi->getToken() == uc->getToken());
	if(cqi->getToken() != uc->getToken()) {
		uc->disconnect(true);
		return;
	}

	bool frameOpen;
	{
		Lock l(cs);
		if(shuttingDown) {
			uc->disconnect(true);
			return;
		}

		knownCCPMs.insert_or_assign(uc, KnownPMConnection { cqi->getUser().user, cqi->getToken() });
		frameOpen = acceptConnectionF && acceptConnectionF(cqi->getUser());
		if(!SETTING(POPUP_PMS) && !frameOpen) {
			uc->disconnect(true);
			return;
		}

		auto i = ccpms.find(cqi->getUser().user);
		if(i != ccpms.end()) {
			auto old = i->second;
			ccpms.erase(i);

			auto oldKnown = knownCCPMs.find(old.connection);
			if(oldKnown != knownCCPMs.end() && oldKnown->second.user == cqi->getUser().user &&
				oldKnown->second.token == old.token)
			{
				if(old.connection != uc) {
					// Do not leave a superseded parked channel running without
					// an owner. Its Removed event is token-filtered and cannot
					// disturb the replacement.
					old.connection->disconnect(true);
				}
			}
		}

		// Always park first, even when a frame is currently open. The frame's
		// synchronous Connected callback adopts and exact-releases this entry. If
		// that frame closes between listener callbacks, this fallback remains the
		// owner instead of leaving a live CCPM with no listener.
		ccpms.emplace(cqi->getUser().user, PMConnection { uc, cqi->getToken() });
		uc->addListener(this);
	}

	// A frame can register after ConnectionManager took its listener snapshot
	// but before this callback parked the connection. Notify the UI after
	// parking so that frame can complete the exact-token handoff.
	if(frameOpen) {
		fire(PrivateChatManagerListener::PMConnection(), cqi->getUser(), cqi->getToken());
	}
}

void PrivateChatManager::on(ConnectionManagerListener::Removed, ConnectionQueueItem* cqi) noexcept {
	if(cqi->getType() != CONNECTION_TYPE_PM) {
		return;
	}

	Lock l(cs);

	auto parked = ccpms.find(cqi->getUser().user);
	if(parked != ccpms.end() && parked->second.token == cqi->getToken()) {
		ccpms.erase(parked);
	}

	UserConnection* removedConnection = nullptr;
	for(auto known = knownCCPMs.begin(); known != knownCCPMs.end();) {
		if(known->second.user == cqi->getUser().user && known->second.token == cqi->getToken()) {
			removedConnection = known->first;
			known = knownCCPMs.erase(known);
		} else {
			++known;
		}
	}
	if(removedConnection) {
		// This callback pins the connection until it returns. Inbound manager
		// callbacks never acquire cs, so draining them here cannot deadlock.
		removedConnection->removeListener(this);
	}
}

void PrivateChatManager::on(UserConnectionListener::PrivateMessage, UserConnection* uc, const ChatMessage& message) noexcept {
	// Keep one continuous inbound listener for the full CCPM lifetime. This
	// avoids gaps and duplicate delivery while UI ownership is transferred.
	fire(PrivateChatManagerListener::PrivateMessage(), message, uc->getHintedUser(), uc->getToken(), false);
}

void PrivateChatManager::on(AdcCommand::PMI, UserConnection* uc, const AdcCommand& cmd) noexcept {
	fire(PrivateChatManagerListener::PMI(), uc->getHintedUser(), uc->getToken(), cmd);
}

} // namespace dcpp
