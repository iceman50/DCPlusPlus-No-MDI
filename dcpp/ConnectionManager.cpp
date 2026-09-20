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
#include "ConnectionManager.h"

#include "Client.h"
#include "ClientManager.h"
#include "ConnectivityManager.h"
#include "CryptoManager.h"
#include "DownloadManager.h"
#include "Encoder.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "UploadManager.h"
#include "UserConnection.h"

namespace dcpp {

namespace {

bool matchesConnectionHub(const HintedUser& queuedUser, const CID& cid, const string& hubUrl) {
	return queuedUser.user->getCID() == cid &&
		hubHintMatches(queuedUser.hint, hubUrl);
}

} // namespace

ConnectionManager::ConnectionManager() :
	downloads(cqis[CONNECTION_TYPE_DOWNLOAD]),
	shuttingDown(false)
{
	TimerManager::getInstance()->addListener(this);

	features = {
		UserConnection::FEATURE_MINISLOTS,
		UserConnection::FEATURE_XML_BZLIST,
		UserConnection::FEATURE_ADCGET,
		UserConnection::FEATURE_TTHL,
		UserConnection::FEATURE_TTHF
	};

	adcFeatures = {
		"AD" + UserConnection::FEATURE_ADC_BAS0,
		"AD" + UserConnection::FEATURE_ADC_BASE,
		"AD" + UserConnection::FEATURE_ADC_TIGR,
		"AD" + UserConnection::FEATURE_ADC_BZIP,
		"AD" + UserConnection::FEATURE_ADC_MCN1,
		"AD" + UserConnection::FEATURE_ADC_CPMI
	};
}

string ConnectionManager::makeToken() const {
	string token;

	Lock l(cs);
	do {
		token = std::to_string(Util::rand());
	} while(tokens.find(token) != tokens.end() || removedDownloadTokens.find(token) != removedDownloadTokens.end() ||
		std::any_of(std::begin(cqis), std::end(cqis), [&](const auto& items) {
		return std::any_of(items.begin(), items.end(), [&](const ConnectionQueueItem& item) {
			return item.getToken() == token;
		});
	}));

	return token;
}

void ConnectionManager::addToken(const string& token, const OnlineUser& user, ConnectionType type) {
	Lock l(cs);
	auto i = findToken(token, user.getUser()->getCID());
	TokenInfo info { user.getUser()->getCID(), type, user.getClient().getHubUrl() };
	if(i != tokens.end()) {
		i->second = std::move(info);
	} else {
		tokens.emplace(token, std::move(info));
	}
}

ConnectionManager::TokenMap::iterator ConnectionManager::findToken(const string& token, const CID& cid) {
	auto range = tokens.equal_range(token);
	auto i = std::find_if(range.first, range.second, [&](const TokenMap::value_type& item) {
		return item.second.cid == cid;
	});
	return i == range.second ? tokens.end() : i;
}

void ConnectionManager::eraseToken(const string& token, const CID& cid) {
	auto i = findToken(token, cid);
	if(i != tokens.end()) {
		tokens.erase(i);
	}
}

void ConnectionManager::listen() {
	server.reset(new Server(false, Util::toString(CONNSETTING(TCP_PORT)), CONNSETTING(BIND_ADDRESS), CONNSETTING(BIND_ADDRESS6)));

	if(!CryptoManager::getInstance()->TLSOk()) {
		dcdebug("Skipping secure port: %d\n", CONNSETTING(TLS_PORT));
		return;
	}
	if(CONNSETTING(TCP_PORT) != 0 && (CONNSETTING(TCP_PORT) == CONNSETTING(TLS_PORT)))
	{
		LogManager::getInstance()->message(_("The encrypted transfer port cannot be the same as the transfer port, encrypted transfers will be disabled"),
			LogMessage::SEV_ERROR, _("Connectivity"));
		return;
	}
	secureServer.reset(new Server(true, Util::toString(CONNSETTING(TLS_PORT)), CONNSETTING(BIND_ADDRESS), CONNSETTING(BIND_ADDRESS6)));
}

ConnectionQueueItem::ConnectionQueueItem(const HintedUser& user, ConnectionType type, const string& token_) :
	token(token_.empty() ? ConnectionManager::getInstance()->makeToken() : token_),
	lastAttempt(0),
	errors(0),
	state(WAITING),
	type(type),
	downloadType(MCNDownloadType::ANY),
	mcn(false),
	running(false),
	maxRemoteConnections(0),
	user(user)
{
}

bool ConnectionQueueItem::operator==(const ConnectionQueueItem& rhs) const {
	return rhs.getType() == getType() && rhs.getToken() == getToken();
}

bool ConnectionQueueItem::operator==(const UserPtr& user) const {
	return this->user == user;
}

/**
 * Request a connection for downloading.
 * DownloadManager::addConnection will be called as soon as the connection is ready
 * for downloading.
 * @param aUser The user to connect to.
 */
bool ConnectionManager::allowNewMCN(const HintedUser& aUser, bool smallSlot) const {
	bool mcnUser = false;
	int runningNormal = 0;
	int remoteLimit = 0;

	for(const auto& cqi: downloads) {
		if(!(cqi.getUser() == aUser.user)) {
			continue;
		}

		if(!cqi.getMCN()) {
			// Until the first handshake has negotiated MCN1, opening a second
			// connection could duplicate a connection to a legacy peer.
			return false;
		}
		mcnUser = true;
		if(cqi.getMaxRemoteConnections() > 0 &&
			(remoteLimit == 0 || cqi.getMaxRemoteConnections() < remoteLimit))
		{
			remoteLimit = cqi.getMaxRemoteConnections();
		}

		if(smallSlot) {
			if(cqi.getDownloadType() == MCNDownloadType::SMALL) {
				return false;
			}
			continue;
		}

		if(cqi.getDownloadType() == MCNDownloadType::NORMAL) {
			if(!cqi.getRunning()) {
				// Keep at most one non-running regular connection. This includes
				// connecting attempts and reusable idle sockets.
				return false;
			}
			++runningNormal;
		}
	}

	if(!mcnUser || smallSlot) {
		return true;
	}

	return runningNormal < MCN::effectiveDownloadLimit(SETTING(MAX_MCN_DOWNLOADS), remoteLimit);
}

void ConnectionManager::getDownloadConnection(const HintedUser& aUser, bool smallSlot) {
	dcassert((bool)aUser.user);
	// Callers that don't know the queue-item type should still put a lone
	// priority transfer on the dedicated connection.
	if(!smallSlot && QueueManager::getInstance()->hasDownload(aUser.user, MCNDownloadType::NORMAL) == QueueItem::PAUSED &&
		QueueManager::getInstance()->hasDownload(aUser.user, MCNDownloadType::SMALL) != QueueItem::PAUSED)
	{
		smallSlot = true;
	}
	if(DownloadManager::getInstance()->checkIdle(aUser, smallSlot)) {
		return;
	}

	unique_ptr<ConnectionQueueItem> added;
	{
		Lock l(cs);
		if(!allowNewMCN(aUser, smallSlot)) {
			return;
		}

		auto& cqi = getCQI(aUser, CONNECTION_TYPE_DOWNLOAD);
		if(smallSlot) {
			cqi.setDownloadType(MCNDownloadType::SMALL);
		}

		// Once one connection has negotiated MCN1, mark new attempts before
		// their handshake so another queue event can't create duplicates.
		auto established = std::find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem& existing) {
			return &existing != &cqi && existing.getUser() == aUser.user && existing.getMCN();
		});
		if(established != downloads.end()) {
			cqi.setMCN(true);
			cqi.setMaxRemoteConnections(established->getMaxRemoteConnections());
			if(!smallSlot) {
				cqi.setDownloadType(MCNDownloadType::NORMAL);
			}
		}
		added.reset(new ConnectionQueueItem(cqi));
	}
	if(added) {
		fire(ConnectionManagerListener::Added(), added.get());
	}
}

void ConnectionManager::onDownloadStarted(const UserConnection& connection) {
	bool mcn = false;
	bool small = false;
	{
		Lock l(cs);
		auto i = std::find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem& cqi) {
			return cqi.getToken() == connection.getToken();
		});
		if(i == downloads.end()) {
			return;
		}
		i->setRunning(true);
		if(!i->getMCN()) {
			return;
		}
		mcn = true;
		small = i->getDownloadType() == MCNDownloadType::SMALL;
	}

	if(mcn && QueueManager::getInstance()->hasDownload(connection.getUser(), MCNDownloadType::SMALL) != QueueItem::PAUSED) {
		getDownloadConnection(connection.getHintedUser(), true);
	}
	if(small) {
		// A priority connection doesn't consume one of the negotiated regular
		// connections, so it may also bootstrap the regular pool.
		if(QueueManager::getInstance()->hasDownload(connection.getUser(), MCNDownloadType::NORMAL) != QueueItem::PAUSED) {
			getDownloadConnection(connection.getHintedUser(), false);
		}
		return;
	}

	// getNextSegment naturally permits another segment only when segmented
	// downloading is enabled, while still allowing a different queued file.
	if(QueueManager::getInstance()->hasDownload(connection.getUser(), MCNDownloadType::NORMAL) != QueueItem::PAUSED) {
		getDownloadConnection(connection.getHintedUser(), false);
	}
}

void ConnectionManager::removeExtraMCN(ConnectionQueueItem& current, unique_ptr<ConnectionQueueItem>& removedEvent) {
	if(!current.getMCN() || current.getDownloadType() != MCNDownloadType::NORMAL) {
		return;
	}

	auto i = std::find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem& cqi) {
		return &cqi != &current && cqi.getUser() == current.getUser().user && cqi.getMCN() &&
			cqi.getDownloadType() == MCNDownloadType::NORMAL && cqi.getState() != ConnectionQueueItem::ACTIVE;
	});
	if(i != downloads.end()) {
		removedEvent.reset(new ConnectionQueueItem(*i));
		removedDownloadTokens[i->getToken()] = { i->getUser().user->getCID(), GET_TICK() };
		eraseToken(i->getToken(), i->getUser().user->getCID());
		downloads.erase(i);
	}
}

void ConnectionManager::onDownloadIdle(const UserConnection& connection) {
	unique_ptr<ConnectionQueueItem> removed;
	unique_ptr<ConnectionQueueItem> idle;
	{
		Lock l(cs);
		auto i = std::find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem& cqi) {
			return cqi.getToken() == connection.getToken();
		});
		if(i == downloads.end()) {
			return;
		}
		i->setRunning(false);
		idle.reset(new ConnectionQueueItem(*i));
		removeExtraMCN(*i, removed);
	}
	if(idle) {
		fire(ConnectionManagerListener::StatusChanged(), idle.get());
	}
	if(removed) {
		fire(ConnectionManagerListener::Removed(), removed.get());
	}
}

ConnectionQueueItem& ConnectionManager::getCQI(const HintedUser& user, ConnectionType type, const string& token) {
	auto& container = cqis[type];

	container.emplace_back(user, type, token);
	auto& cqi = container.back();

	return cqi;
}

void ConnectionManager::putCQI(ConnectionQueueItem& cqi) {
	auto& container = cqis[cqi.getType()];
	auto i = find_if(container.begin(), container.end(),
		[&cqi](const ConnectionQueueItem& item) { return &item == &cqi; });
	dcassert(i != container.end());
	if(i != container.end()) {
		if(cqi.getType() == CONNECTION_TYPE_DOWNLOAD) {
			removedDownloadTokens[cqi.getToken()] = { cqi.getUser().user->getCID(), GET_TICK() };
			eraseToken(cqi.getToken(), cqi.getUser().user->getCID());
		}
		container.erase(i);
	}
}

bool ConnectionManager::isPMConnectionActive(const UserPtr& user, const string& token) const noexcept {
	Lock l(cs);
	const auto& container = cqis[CONNECTION_TYPE_PM];
	return std::any_of(container.begin(), container.end(), [&](const ConnectionQueueItem& cqi) {
		return cqi.getUser().user == user && cqi.getToken() == token &&
			cqi.getState() == ConnectionQueueItem::ACTIVE;
	});
}

UserConnection* ConnectionManager::getConnection(bool aNmdc, bool secure) noexcept {
	UserConnection* uc = new UserConnection(secure);
	uc->addListener(this);
	{
		Lock l(cs);
		userConnections.push_back(uc);
	}
	if(aNmdc)
		uc->setFlag(UserConnection::FLAG_NMDC);
	return uc;
}

void ConnectionManager::putConnection(UserConnection* aConn) {
	aConn->removeListener(this);
	aConn->disconnect();

	Lock l(cs);
	userConnections.erase(remove(userConnections.begin(), userConnections.end(), aConn), userConnections.end());
}

StringList ConnectionManager::getDownloadRoutes(const HintedUser& user, MCNDownloadType type) const {
	auto routes = ClientManager::getInstance()->getHubUrls(user.user->getCID());
	// A route is useful only if it can serve queued work. In particular, file
	// lists must stay on the hub whose share profile was requested.
	routes.erase(std::remove_if(routes.begin(), routes.end(), [&](const string& hub) {
		return QueueManager::getInstance()->hasDownload(user, type, &hub) == QueueItem::PAUSED;
	}), routes.end());
	std::sort(routes.begin(), routes.end(), [&](const string& lhs, const string& rhs) {
		const bool leftHint = hubHintsEqual(lhs, user.hint);
		const bool rightHint = hubHintsEqual(rhs, user.hint);
		return leftHint != rightHint ? leftHint : Util::stricmp(lhs, rhs) < 0;
	});
	routes.erase(std::unique(routes.begin(), routes.end(), hubHintsEqual), routes.end());
	return routes;
}

void ConnectionManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	UserList passiveUsers;
	StringList removed;
	vector<ConnectionQueueItem> snapshot;
	{
		Lock l(cs);
		snapshot = downloads;
	}

	bool attemptDone = false;
	for(const auto& current: snapshot) {
		if(current.getState() == ConnectionQueueItem::ACTIVE) {
			continue;
		}

		if(!current.getUser().user->isOnline()) {
			removed.push_back(current.getToken());
			continue;
		}

		if(current.getUser().user->isSet(User::PASSIVE) && !ClientManager::getInstance()->isActive()) {
			passiveUsers.push_back(current.getUser());
			removed.push_back(current.getToken());
			continue;
		}

		if(current.getErrors() == -1 && current.getLastAttempt() != 0) {
			continue;
		}

		auto routes = getDownloadRoutes(current.getUser(), current.getDownloadType());
		const auto routeCount = max<size_t>(1, routes.size());
		const auto errorCount = static_cast<size_t>(max(0, current.getErrors()));
		const bool hasUntriedRoute = errorCount > 0 && (errorCount % routeCount) != 0;
		const auto completedRounds = errorCount / routeCount;
		const uint64_t retryDelay = hasUntriedRoute ? 1000 :
			60 * 1000 * max<size_t>(1, completedRounds);
		const bool shouldAttempt = current.getState() != ConnectionQueueItem::CONNECTING &&
			(current.getLastAttempt() == 0 || (!attemptDone &&
			current.getLastAttempt() + retryDelay < aTick));
		if(shouldAttempt) {
			auto downloadType = current.getDownloadType();
			auto prio = QueueManager::getInstance()->hasDownload(current.getUser(), downloadType);
			if(prio == QueueItem::PAUSED && downloadType == MCNDownloadType::SMALL) {
				const bool onlyConnection = std::none_of(snapshot.begin(), snapshot.end(), [&](const ConnectionQueueItem& other) {
					return other.getToken() != current.getToken() && other.getUser() == current.getUser().user;
				});
				if(onlyConnection) {
					prio = QueueManager::getInstance()->hasDownload(current.getUser(), MCNDownloadType::ANY);
					if(prio != QueueItem::PAUSED) {
						downloadType = MCNDownloadType::ANY;
						routes = getDownloadRoutes(current.getUser(), downloadType);
						Lock l(cs);
						auto i = std::find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem& cqi) {
							return cqi.getToken() == current.getToken();
						});
						if(i != downloads.end()) {
							i->setDownloadType(downloadType);
						}
					}
				}
			}
			if(prio == QueueItem::PAUSED) {
				removed.push_back(current.getToken());
				continue;
			}
			if(routes.empty()) {
				continue;
			}

			const bool startDown = DownloadManager::getInstance()->startDownload(prio,
				downloadType == MCNDownloadType::SMALL);
			ConnectionQueueItem eventItem = current;
			bool statusChanged = false;
			bool failedNoSlots = false;
			bool connect = false;
			{
				Lock l(cs);
				auto i = std::find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem& cqi) {
					return cqi.getToken() == current.getToken();
				});
				if(i == downloads.end() || i->getState() == ConnectionQueueItem::ACTIVE ||
					i->getState() == ConnectionQueueItem::CONNECTING) {
					continue;
				}

				i->setLastAttempt(aTick);
				if(i->getState() == ConnectionQueueItem::WAITING) {
					if(startDown) {
						i->setState(ConnectionQueueItem::CONNECTING);
						statusChanged = true;
						connect = true;
						attemptDone = true;
					} else {
						i->setState(ConnectionQueueItem::NO_DOWNLOAD_SLOTS);
						failedNoSlots = true;
					}
				} else if(i->getState() == ConnectionQueueItem::NO_DOWNLOAD_SLOTS && startDown) {
					i->setState(ConnectionQueueItem::WAITING);
					statusChanged = true;
				}
				eventItem = *i;
			}

			if(connect) {
				const auto& hub = routes[static_cast<size_t>(max(0, eventItem.getErrors())) % routes.size()];
				ClientManager::getInstance()->connect(HintedUser(eventItem.getUser().user, hub), eventItem.getToken());
			}
			if(statusChanged) {
				fire(ConnectionManagerListener::StatusChanged(), &eventItem);
			}
			if(failedNoSlots) {
				fire(ConnectionManagerListener::Failed(), &eventItem, _("All download slots taken"));
			}
		} else if(current.getState() == ConnectionQueueItem::CONNECTING && current.getLastAttempt() + 50 * 1000 < aTick) {
			ConnectionQueueItem eventItem = current;
			bool timedOut = false;
			{
				Lock l(cs);
				auto i = std::find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem& cqi) {
					return cqi.getToken() == current.getToken();
				});
				if(i != downloads.end() && i->getState() == ConnectionQueueItem::CONNECTING && i->getLastAttempt() + 50 * 1000 < aTick) {
					i->setErrors(i->getErrors() + 1);
					i->setState(ConnectionQueueItem::WAITING);
					eventItem = *i;
					timedOut = true;
				}
			}
			if(timedOut) {
				fire(ConnectionManagerListener::Failed(), &eventItem, _("Connection timeout"));
			}
		}
	}

	for(const auto& token: removed) {
		unique_ptr<ConnectionQueueItem> eventItem;
		{
			Lock l(cs);
			auto i = std::find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem& cqi) {
				return cqi.getToken() == token;
			});
			if(i != downloads.end()) {
				eventItem.reset(new ConnectionQueueItem(*i));
				removedDownloadTokens[i->getToken()] = { i->getUser().user->getCID(), aTick };
				eraseToken(i->getToken(), i->getUser().user->getCID());
				downloads.erase(i);
			}
		}
		if(eventItem) {
			fire(ConnectionManagerListener::Removed(), eventItem.get());
		}
	}

	for(auto& ui: passiveUsers) {
		QueueManager::getInstance()->removeSource(ui, QueueItem::Source::FLAG_PASSIVE);
	}
}

void ConnectionManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	vector<pair<string, CID>> tokenSnapshot;
	{
		Lock l(cs);
		for(auto i = incomingFlood.begin(); i != incomingFlood.end();) {
			if(i->second.windowStart + 60 * 1000 < aTick) {
				i = incomingFlood.erase(i);
			} else {
				++i;
			}
		}

		for(auto i = removedDownloadTokens.begin(); i != removedDownloadTokens.end();) {
			if(i->second.tick + 90 * 1000 < aTick) {
				i = removedDownloadTokens.erase(i);
			} else {
				++i;
			}
		}

		tokenSnapshot.reserve(tokens.size());
		for(const auto& token: tokens) {
			tokenSnapshot.emplace_back(token.first, token.second.cid);
		}
	}

	for(const auto& token: tokenSnapshot) {
		auto user = ClientManager::getInstance()->findUser(token.second);
		if(!user || !user->isOnline()) {
			Lock l(cs);
			auto i = findToken(token.first, token.second);
			if(i != tokens.end()) {
				tokens.erase(i);
			}
		}
	}

	// disconnect connections that have timed out.
	{
		Lock l(cs);
		for(auto& conn: userConnections) {
			if(!conn->isSet(UserConnection::FLAG_PM) && (conn->getLastActivity() + 180*1000) < aTick) {
				conn->disconnect(true);
			}
		}
	}
}

const string& ConnectionManager::getPort() const {
	return server.get() ? server->getPort() : Util::emptyString;
}

const string& ConnectionManager::getSecurePort() const {
	return secureServer.get() ? secureServer->getPort() : Util::emptyString;
}

ConnectionManager::Server::Server(bool secure, const string& port_, const string& ipv4, const string& ipv6) :
sock(Socket::TYPE_TCP), secure(secure), die(false)
{
	sock.setLocalIp4(ipv4);
	sock.setLocalIp6(ipv6);
	sock.setV4only(false);
	port = sock.listen(port_);

	start();
}

static const uint32_t POLL_TIMEOUT = 250;

int ConnectionManager::Server::run() noexcept {
	while(!die) {
		try {
			while(!die) {
				auto ret = sock.wait(POLL_TIMEOUT, true, false);
				if(ret.first) {
					ConnectionManager::getInstance()->accept(sock, secure);
				}
			}
		} catch(const Exception& e) {
			dcdebug("ConnectionManager::Server::run Error: %s\n", e.getError().c_str());
		}

		bool failed = false;
		while(!die) {
			try {
				sock.disconnect();
				port = sock.listen(port);

				if(failed) {
					LogManager::getInstance()->message(_("Connectivity restored"), LogMessage::SEV_INFO, _("Connectivity"));
					failed = false;
				}
				break;
			} catch(const SocketException& e) {
				dcdebug("ConnectionManager::Server::run Stopped listening: %s\n", e.getError().c_str());

				if(!failed) {
					LogManager::getInstance()->message(str(F_("Connectivity error: %1%") % e.getError()),
						LogMessage::SEV_ERROR, _("Connectivity"));
					failed = true;
				}

				// Spin for 60 seconds
				for(auto i = 0; i < 60 && !die; ++i) {
					Thread::sleep(1000);
				}
			}
		}
	}
	return 0;
}

/**
 * Someone's connecting, accept the connection and wait for identification...
 * It's always the other fellow that starts sending if he made the connection.
 */
void ConnectionManager::accept(const Socket& sock, bool secure) noexcept {
	// Reject new incoming peers once the combined peer-socket population reaches the cap.
	// Hub connections are managed separately.
	const auto maxConcurrentConnections = static_cast<size_t>(std::max(1, SETTING(MAX_CONCURRENT_CONNECTIONS)));
	{
		Lock l(cs);
		if(userConnections.size() >= maxConcurrentConnections) {
			try {
				Socket rejected(Socket::TYPE_TCP);
				rejected.accept(sock);
			} catch(const Exception&) {
			}
			return;
		}
	}

	UserConnection* uc = getConnection(false, secure);
	uc->setFlag(UserConnection::FLAG_INCOMING);
	uc->setState(UserConnection::STATE_SUPNICK);
	uc->setLastActivity(GET_TICK());
	try {
		uc->accept(sock, true);
		if(!allowIncomingConnection(uc->getRemoteIp(), GET_TICK())) {
			dcdebug("Connection flood detected from %s\n", uc->getRemoteIp().c_str());
			putConnection(uc);
			delete uc;
			return;
		}
		uc->completeAccept();
	} catch(const Exception&) {
		putConnection(uc);
		delete uc;
	}
}

bool ConnectionManager::allowIncomingConnection(const string& remoteIp, uint64_t now) {
	// Count attempts independently per source address over a configurable rolling window.
	const auto floodWindow = static_cast<uint64_t>(std::max(1, SETTING(FLOOD_WINDOW)));
	const auto configuredMcnBurst = static_cast<size_t>(std::max(1, SETTING(MAX_MCN_UPLOADS))) * 2 + 8;
	const auto maxAttempts = std::max<size_t>(32, configuredMcnBurst);

	Lock l(cs);
	auto& state = incomingFlood[remoteIp];
	if(state.windowStart == 0 || now - state.windowStart >= floodWindow) {
		state.windowStart = now;
		state.attempts = 1;
		return true;
	}
	return ++state.attempts <= maxAttempts;
}

void ConnectionManager::nmdcConnect(const string& aServer, const string& aPort, const string& aNick, const string& hubUrl, const string& encoding) {
	if(shuttingDown)
		return;

	if (checkHubCCBlock(aServer, aPort, hubUrl))
		return;

	string hubIp;
	{
		auto clientLock = ClientManager::getInstance()->lock();
		for(auto client: ClientManager::getInstance()->getClients()) {
			if(hubHintsEqual(client->getHubUrl(), hubUrl)) {
				hubIp = client->getIp();
				break;
			}
		}
	}
	if(!Util::isSafePeerEndpoint(aServer, aPort, hubIp)) {
		LogManager::getInstance()->message(str(F_("Blocked an unsafe client endpoint '%1%:%2%' requested by '%3%'") % aServer % aPort % hubUrl),
			LogMessage::SEV_WARNING, _("Security"));
		return;
	}

	UserConnection* uc = getConnection(true, false);
	uc->setToken(aNick);
	uc->setHubUrl(hubUrl);
	uc->setEncoding(encoding);
	uc->setState(UserConnection::STATE_CONNECT);
	uc->setFlag(UserConnection::FLAG_NMDC);
	try {
		uc->connect(aServer, aPort, Util::emptyString, BufferedSocket::NAT_NONE);
	} catch(const Exception&) {
		putConnection(uc);
		delete uc;
	}
}

void ConnectionManager::adcConnect(const OnlineUser& aUser, const string& aPort, const string& aToken, bool secure) {
	adcConnect(aUser, aPort, Util::emptyString, BufferedSocket::NAT_NONE, aToken, secure);
}

void ConnectionManager::adcConnect(const OnlineUser& aUser, const string& aPort, const string& localPort, BufferedSocket::NatRoles natRole, const string& aToken, bool secure) {
	if(shuttingDown)
		return;

	const auto remoteIp = CONNSTATE(INCOMING_CONNECTIONS6) ? aUser.getIdentity().getIp() : aUser.getIdentity().getIp4();
	if(!Util::isSafePeerEndpoint(remoteIp, aPort, aUser.getClient().getIp())) {
		LogManager::getInstance()->message(str(F_("Blocked an unsafe client endpoint '%1%:%2%' requested by '%3%'") % remoteIp % aPort % aUser.getClient().getHubUrl()),
			LogMessage::SEV_WARNING, _("Security"));
		return;
	}

	UserConnection* uc = getConnection(false, secure);
	uc->setToken(aToken);
	uc->setEncoding(Text::utf8);
	uc->setState(UserConnection::STATE_CONNECT);
	uc->setHubUrl(aUser.getClient().getHubUrl());
	if(aUser.getIdentity().isOp()) {
		uc->setFlag(UserConnection::FLAG_OP);
	}

	{
		Lock l(cs);
		auto t = findToken(aToken, aUser.getUser()->getCID());
		if(t != tokens.end()) {
			if(t->second.type == CONNECTION_TYPE_PM) {
				uc->setFlag(UserConnection::FLAG_PM);
			}
			tokens.erase(t);
		}
	}

	try {
		uc->connect(remoteIp, aPort, localPort, natRole, aUser.getUser());
	} catch(const Exception&) {
		putConnection(uc);
		delete uc;
	}
}

void ConnectionManager::disconnect() noexcept {
	server.reset();
	secureServer.reset();
}

void ConnectionManager::on(AdcCommand::SUP, UserConnection* aSource, const AdcCommand& cmd) noexcept {
	if(aSource->getState() != UserConnection::STATE_SUPNICK) {
		// Already got this once, ignore...@todo fix support updates
		dcdebug("CM::onSUP %p sent sup twice\n", (void*)aSource);
		return;
	}

	bool baseOk = false;

	for(auto& i: cmd.getParameters()) {
		if(i.compare(0, 2, "AD") == 0) {
			string feat = i.substr(2);
			if(feat == UserConnection::FEATURE_ADC_BASE || feat == UserConnection::FEATURE_ADC_BAS0) {
				baseOk = true;
				// ADC clients must support all these...
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_ADCGET);
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_MINISLOTS);
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_TTHF);
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_TTHL);
				// For compatibility with older clients...
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_XML_BZLIST);
			} else if(feat == UserConnection::FEATURE_ZLIB_GET) {
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_ZLIB_GET);
			} else if(feat == UserConnection::FEATURE_ADC_BZIP) {
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_XML_BZLIST);
			} else if(feat == UserConnection::FEATURE_ADC_CPMI) {
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_CPMI);
			} else if(feat == UserConnection::FEATURE_ADC_MCN1) {
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_MCN1);
			} else if(feat == UserConnection::FEATURE_ADC_RTF0) {
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_RTF0);
			}
		}
	}

	if(!baseOk) {
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_GENERIC, "Invalid SUP"));
		aSource->disconnect();
		return;
	}

	if(aSource->isSet(UserConnection::FLAG_INCOMING)) {
		StringList defFeatures = adcFeatures;
		if(SETTING(COMPRESS_TRANSFERS)) {
			defFeatures.push_back("AD" + UserConnection::FEATURE_ZLIB_GET);
		}
		if(SETTING(ENABLE_RICH_TEXT)) {
			defFeatures.push_back("AD" + UserConnection::FEATURE_ADC_RTF0);
		}
		aSource->sup(defFeatures);
		aSource->setState(UserConnection::STATE_INF);
	} else {
		aSource->setState(UserConnection::STATE_INF);
		aSource->inf(true, aSource->isMCN() ? std::max(1, SETTING(MAX_MCN_UPLOADS)) : 0);
	}
}

void ConnectionManager::on(AdcCommand::STA, UserConnection*, const AdcCommand& cmd) noexcept {

}

void ConnectionManager::on(UserConnectionListener::Connected, UserConnection* aSource) noexcept {
	dcassert(aSource->getState() == UserConnection::STATE_CONNECT);
	if(aSource->isSet(UserConnection::FLAG_NMDC)) {
		aSource->myNick(aSource->getToken());
		aSource->lock(CryptoManager::getInstance()->getLock(), CryptoManager::getInstance()->getPk() + "Ref=" + aSource->getHubUrl());
	} else {
		StringList defFeatures = adcFeatures;
		if(SETTING(COMPRESS_TRANSFERS)) {
			defFeatures.push_back("AD" + UserConnection::FEATURE_ZLIB_GET);
		}
		if(SETTING(ENABLE_RICH_TEXT)) {
			defFeatures.push_back("AD" + UserConnection::FEATURE_ADC_RTF0);
		}
		aSource->sup(defFeatures);
		aSource->send(AdcCommand(AdcCommand::SEV_SUCCESS, AdcCommand::SUCCESS, "Referrer").addParam("RF", aSource->getHubUrl()));
	}
	aSource->setState(UserConnection::STATE_SUPNICK);
}

void ConnectionManager::on(UserConnectionListener::MyNick, UserConnection* aSource, const string& aNick) noexcept {
	if(aSource->getState() != UserConnection::STATE_SUPNICK) {
		// Already got this once, ignore...
		dcdebug("CM::onMyNick %p sent nick twice\n", (void*)aSource);
		return;
	}

	dcassert(!aNick.empty());
	dcdebug("ConnectionManager::onMyNick %p, %s\n", (void*)aSource, aNick.c_str());
	dcassert(!aSource->getUser());

	if(aSource->isSet(UserConnection::FLAG_INCOMING)) {
		// Try to guess where this came from...
		pair<string, string> i = expectedConnections.remove(aNick);
		if(i.second.empty()) {
			dcassert(i.first.empty());
			dcdebug("Unknown incoming connection from %s\n", aNick.c_str());
			putConnection(aSource);
			return;
		}
		aSource->setToken(i.first);
		aSource->setHubUrl(i.second);
		aSource->setEncoding(ClientManager::getInstance()->findHubEncoding(i.second));
	}

	string nick = Text::toUtf8(aNick, aSource->getEncoding());
	CID cid = ClientManager::getInstance()->makeCid(nick, aSource->getHubUrl());

	// First, we try looking in the pending downloads...hopefully it's one of them...
	{
		Lock l(cs);
		for(auto& cqi: downloads) {
			if((cqi.getState() == ConnectionQueueItem::CONNECTING || cqi.getState() == ConnectionQueueItem::WAITING) &&
				matchesConnectionHub(cqi.getUser(), cid, aSource->getHubUrl()))
			{
				cqi.setErrors(0);
				aSource->setUser(cqi.getUser());
				// Indicate that we're interested in this file...
				aSource->setFlag(UserConnection::FLAG_DOWNLOAD);
				break;
			}
		}
	}

	if(!aSource->getUser()) {
		// Make sure we know who it is, i e that he/she is connected...

		aSource->setUser(ClientManager::getInstance()->findUser(cid));
		if(!aSource->getUser() || !ClientManager::getInstance()->isOnline(aSource->getUser())) {
			dcdebug("CM::onMyNick Incoming connection from unknown user %s\n", nick.c_str());
			putConnection(aSource);
			return;
		}
		// We don't need this connection for downloading...make it an upload connection instead...
		aSource->setFlag(UserConnection::FLAG_UPLOAD);
	}

	if(ClientManager::getInstance()->isOp(aSource->getUser(), aSource->getHubUrl()))
		aSource->setFlag(UserConnection::FLAG_OP);

	if( aSource->isSet(UserConnection::FLAG_INCOMING) ) {
		aSource->myNick(aSource->getToken());
		aSource->lock(CryptoManager::getInstance()->getLock(), CryptoManager::getInstance()->getPk());
	}

	aSource->setState(UserConnection::STATE_LOCK);
}

void ConnectionManager::on(UserConnectionListener::CLock, UserConnection* aSource, const string& aLock, const string& aPk) noexcept {
	if(aSource->getState() != UserConnection::STATE_LOCK) {
		dcdebug("CM::onLock %p received lock twice, ignoring\n", (void*)aSource);
		return;
	}

	if( CryptoManager::getInstance()->isExtended(aLock) ) {
		StringList defFeatures = features;
		if(SETTING(COMPRESS_TRANSFERS)) {
			defFeatures.push_back(UserConnection::FEATURE_ZLIB_GET);
		}

		aSource->supports(defFeatures);
	}

	aSource->setState(UserConnection::STATE_DIRECTION);
	aSource->direction(aSource->getDirectionString(), aSource->getNumber());
	aSource->key(CryptoManager::getInstance()->makeKey(aLock));
}

void ConnectionManager::on(UserConnectionListener::Direction, UserConnection* aSource, const string& dir, const string& num) noexcept {
	if(aSource->getState() != UserConnection::STATE_DIRECTION) {
		dcdebug("CM::onDirection %p received direction twice, ignoring\n", (void*)aSource);
		return;
	}

	dcassert(aSource->isSet(UserConnection::FLAG_DOWNLOAD) ^ aSource->isSet(UserConnection::FLAG_UPLOAD));
	if(dir == "Upload") {
		// Fine, the other fellow want's to send us data...make sure we really want that...
		if(aSource->isSet(UserConnection::FLAG_UPLOAD)) {
			// Huh? Strange...disconnect...
			putConnection(aSource);
			return;
		}
	} else {
		if(aSource->isSet(UserConnection::FLAG_DOWNLOAD)) {
			int number = Util::toInt(num);
			// Damn, both want to download...the one with the highest number wins...
			if(aSource->getNumber() < number) {
				// Damn! We lost!
				aSource->unsetFlag(UserConnection::FLAG_DOWNLOAD);
				aSource->setFlag(UserConnection::FLAG_UPLOAD);
			} else if(aSource->getNumber() == number) {
				putConnection(aSource);
				return;
			}
		}
	}

	dcassert(aSource->isSet(UserConnection::FLAG_DOWNLOAD) ^ aSource->isSet(UserConnection::FLAG_UPLOAD));

	aSource->setState(UserConnection::STATE_KEY);
}

void ConnectionManager::addDownloadConnection(UserConnection* uc) {
	bool addConn = false;
	unique_ptr<ConnectionQueueItem> connected;
	{
		Lock l(cs);

		auto i = find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem& cqi) {
			return cqi.getUser() == uc->getUser() && cqi.getToken() == uc->getToken();
		});
		if(uc->isSet(UserConnection::FLAG_NMDC)) {
			i = find(downloads.begin(), downloads.end(), uc->getUser());
		}
		if(i != downloads.end()) {
			auto& cqi = *i;

			if(cqi.getState() == ConnectionQueueItem::WAITING || cqi.getState() == ConnectionQueueItem::CONNECTING) {
				if(uc->isMCN()) {
					cqi.setMCN(true);
					cqi.setMaxRemoteConnections(uc->getMaxRemoteConnections());
					if(cqi.getDownloadType() == MCNDownloadType::SMALL) {
						uc->setFlag(UserConnection::FLAG_MCN_SMALL);
					} else {
						cqi.setDownloadType(MCNDownloadType::NORMAL);
						uc->setFlag(UserConnection::FLAG_MCN_NORMAL);
					}
				} else {
					cqi.setDownloadType(MCNDownloadType::ANY);
				}
				cqi.setState(ConnectionQueueItem::ACTIVE);
				cqi.setErrors(0);
				// NMDC used our nick during the handshake. From now on every
				// transfer event must identify the same queue entry.
				uc->setToken(cqi.getToken());
				uc->setFlag(UserConnection::FLAG_ASSOCIATED);

				dcdebug("ConnectionManager::addDownloadConnection, leaving to downloadmanager\n");
				addConn = true;
				connected.reset(new ConnectionQueueItem(cqi));
			}
		}
	}

	if(addConn) {
		fire(ConnectionManagerListener::Connected(), connected.get(), uc);
		DownloadManager::getInstance()->addConnection(uc);
	} else {
		putConnection(uc);
	}
}

void ConnectionManager::addNewConnection(UserConnection* uc, ConnectionType type) {
	bool addConn = false;
	unique_ptr<ConnectionQueueItem> added;
	unique_ptr<ConnectionQueueItem> connected;
	if(type != CONNECTION_TYPE_PM || SETTING(ENABLE_CCPM)) {
		Lock l(cs);

		auto& container = cqis[type];
		const bool multiple = type == CONNECTION_TYPE_UPLOAD && uc->isMCN();
		auto i = multiple ? find_if(container.begin(), container.end(), [&](const ConnectionQueueItem& cqi) {
			return cqi.getUser() == uc->getUser() && cqi.getProtocolToken() == uc->getToken();
		}) : find(container.begin(), container.end(), uc->getUser());
		const auto userConnections = multiple ? std::count_if(container.begin(), container.end(), [&](const ConnectionQueueItem& cqi) {
			return cqi.getUser() == uc->getUser();
		}) : 0;
		if(i == container.end() && (!multiple || userConnections < std::max(1, SETTING(MAX_MCN_UPLOADS)) + 1)) {
			const bool syncQueueToken = type == CONNECTION_TYPE_UPLOAD || type == CONNECTION_TYPE_PM;
			const auto queueToken = type == CONNECTION_TYPE_PM ? "CCPM-" + std::to_string(++nextPMConnectionId) :
				Util::emptyString;
			auto& cqi = getCQI(uc->getHintedUser(), type,
				queueToken);
			cqi.setProtocolToken(uc->getToken());
			uc->setProtocolToken(uc->getToken());
			added.reset(new ConnectionQueueItem(cqi));
			if(syncQueueToken) {
				// Handshakes are complete. Use a local identifier for transfer events;
				// NMDC nicks and ADC tokens can be identical across different peers.
				uc->setToken(cqi.getToken());
			}

			cqi.setState(ConnectionQueueItem::ACTIVE);
			uc->setFlag(UserConnection::FLAG_ASSOCIATED);

			if(type == CONNECTION_TYPE_PM) {
				uc->setState(UserConnection::STATE_CMD);
			}

			dcdebug("ConnectionManager::addNewConnection, leaving to uploadmanager or PM handler\n");
			addConn = true;
			connected.reset(new ConnectionQueueItem(cqi));
		}
	}

	if(addConn) {
		fire(ConnectionManagerListener::Added(), added.get());
		fire(ConnectionManagerListener::Connected(), connected.get(), uc);
		if(type == CONNECTION_TYPE_UPLOAD) {
			UploadManager::getInstance()->addConnection(uc);
		}
	} else {
		putConnection(uc);
	}
}

void ConnectionManager::on(UserConnectionListener::Key, UserConnection* aSource, const string&/* aKey*/) noexcept {
	if(aSource->getState() != UserConnection::STATE_KEY) {
		dcdebug("CM::onKey Bad state, ignoring");
		return;
	}

	dcassert(aSource->getUser());

	if(aSource->isSet(UserConnection::FLAG_DOWNLOAD)) {
		addDownloadConnection(aSource);
	} else {
		addNewConnection(aSource, CONNECTION_TYPE_UPLOAD);
	}
}

void ConnectionManager::on(AdcCommand::INF, UserConnection* aSource, const AdcCommand& cmd) noexcept {
	if(aSource->getState() != UserConnection::STATE_INF) {
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_GENERIC, "Expecting INF"));
		aSource->disconnect();
		return;
	}

	// Leaks CSUPs, other client's CINF, and ADC connection's presence. Allows removing
	// user from queue by waiting long enough for aSource->getUser() to function.
	if(SETTING(REQUIRE_TLS) && !aSource->isSet(UserConnection::FLAG_NMDC) && !aSource->isSecure()) {
		auto user = aSource->getUser();
		putConnection(aSource);
		// Incoming connections haven't supplied their CID yet. Only outgoing
		// download connections have a source that should be removed here.
		if(user) {
			QueueManager::getInstance()->removeSource(user, QueueItem::Source::FLAG_UNENCRYPTED);
		}
		return;
	}

	string cid;
	if(!cmd.getParam("ID", 0, cid)) {
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_INF_FIELD, "INF ID missing").addParam("FM", "ID"));
		dcdebug("CM::onINF missing ID\n");
		aSource->disconnect();
		return;
	}
	if(cid.size() != 39 || !Encoder::isBase32(cid)) {
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_INF_FIELD, "INF ID invalid").addParam("FB", "ID"));
		putConnection(aSource);
		return;
	}

	if(!aSource->getUser()) {
		aSource->setUser(ClientManager::getInstance()->findUser(CID(cid)));

		if(!aSource->getUser()) {
			aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_INF_FIELD, "INF ID: user not found").addParam("FB", "ID"));
			putConnection(aSource);
			return;
		}
	}

	if(aSource->isMCN()) {
		string connections;
		if(cmd.getParam("CO", 0, connections)) {
			aSource->setMaxRemoteConnections(MCN::normalizeRemoteLimit(Util::toInt(connections)));
		}
	}

	auto type = CONNECTION_TYPE_LAST;

	if(aSource->isSet(UserConnection::FLAG_INCOMING)) {
		string token;
		if(!cmd.getParam("TO", 0, token)) {
			aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_INF_FIELD, "INF TO missing").addParam("FM", "TO"));
			putConnection(aSource);
			return;
		}
		aSource->setToken(token);

		auto tokCheck = checkToken(aSource);
		if(!tokCheck.first) {
			aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_INF_FIELD, "INF TO: invalid token").addParam("FB", "TO"));
			putConnection(aSource);
			return;
		}
		type = tokCheck.second;
	}

	// without a valid KeyPrint this degrades into normal trust check
	if(!checkKeyprint(aSource)) {
		QueueManager::getInstance()->removeSource(aSource->getUser(), QueueItem::Source::FLAG_UNTRUSTED);
		putConnection(aSource);
		return;
	}

	const bool pmRequested = type == CONNECTION_TYPE_PM || aSource->isSet(UserConnection::FLAG_PM) || cmd.hasFlag("PM", 0);

	if(pmRequested && !aSource->isSecure()) {
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_GENERIC, "Unencrypted CCPM connections aren't allowed"));
		putConnection(aSource);
		return;
	}

	const bool downloadRequested = type == CONNECTION_TYPE_DOWNLOAD || checkDownload(aSource);
	if(!downloadRequested && wasRemovedDownload(aSource->getToken(), aSource->getUser()->getCID())) {
		// A late outgoing handshake must not be reclassified as an unsolicited
		// upload after its download queue item was removed.
		aSource->disconnect(true);
		return;
	}

	if(downloadRequested && cmd.hasFlag("PM", 0)) {
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_INF_FIELD, "INF PM: invalid token type").addParam("FB", "PM"));
		putConnection(aSource);
		return;
	}

	if(aSource->isSet(UserConnection::FLAG_INCOMING)) {
		// Set the PM flag before replying so CCPM handshakes include PM1.
		if(pmRequested && !aSource->isSet(UserConnection::FLAG_PM)) {
			aSource->setFlag(UserConnection::FLAG_PM);
		}

		aSource->inf(false, aSource->isMCN() ? std::max(1, SETTING(MAX_MCN_UPLOADS)) : 0);
	}

	if(!pmRequested && downloadRequested) {
		if(!aSource->isSet(UserConnection::FLAG_DOWNLOAD)) { aSource->setFlag(UserConnection::FLAG_DOWNLOAD); }
		addDownloadConnection(aSource);

	} else if(pmRequested) {
		if(!aSource->isSet(UserConnection::FLAG_PM)) { aSource->setFlag(UserConnection::FLAG_PM); }
		addNewConnection(aSource, CONNECTION_TYPE_PM);

	} else {
		if(!aSource->isSet(UserConnection::FLAG_UPLOAD)) { aSource->setFlag(UserConnection::FLAG_UPLOAD); }
		addNewConnection(aSource, CONNECTION_TYPE_UPLOAD);
	}
}

void ConnectionManager::force(const UserPtr& aUser) {
	Lock l(cs);

	for(auto& cqi: downloads) {
		if(cqi.getUser() == aUser) {
			cqi.setLastAttempt(0);
		}
	}
}

bool ConnectionManager::checkKeyprint(UserConnection* aSource) {
	dcassert(aSource->getUser());

	if(!aSource->isSecure() || aSource->isTrusted())
		return true;

	string kp = ClientManager::getInstance()->getField(aSource->getUser()->getCID(), aSource->getHubUrl(), "KP");
	return aSource->verifyKeyprint(kp, SETTING(ALLOW_UNTRUSTED_CLIENTS));
}

pair<bool, ConnectionType> ConnectionManager::checkToken(UserConnection* uc) {
	Lock l(cs);

	auto t = findToken(uc->getToken(), uc->getUser()->getCID());
	if(t != tokens.end()) {
		auto type = t->second.type;
		uc->setHubUrl(t->second.hubUrl);
		tokens.erase(t);
		return make_pair(true, type);
	}

	return make_pair(false, CONNECTION_TYPE_LAST);
}

bool ConnectionManager::checkDownload(const UserConnection* uc) const {
	Lock l(cs);

	auto d = find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem& cqi) {
		return cqi.getUser() == uc->getUser() && cqi.getToken() == uc->getToken();
	});
	if(d != downloads.end()) {
		d->setErrors(0);
		return true;
	}

	return false;
}

bool ConnectionManager::wasRemovedDownload(const string& token, const CID& cid) const {
	Lock l(cs);
	auto i = removedDownloadTokens.find(token);
	return i != removedDownloadTokens.end() && i->second.cid == cid;
}

void ConnectionManager::failed(UserConnection* aSource, const string& aError, bool protocolError) {
	unique_ptr<ConnectionQueueItem> failedEvent;
	unique_ptr<ConnectionQueueItem> removedEvent;
	{
		Lock l(cs);
		if(aSource->isSet(UserConnection::FLAG_ASSOCIATED)) {

			if(aSource->isSet(UserConnection::FLAG_DOWNLOAD)) {
				auto i = find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem& cqi) {
					return cqi.getUser() == aSource->getUser() && cqi.getToken() == aSource->getToken();
				});
				dcassert(i != downloads.end());
				if(i != downloads.end()) {
					auto& cqi = *i;
					cqi.setState(ConnectionQueueItem::WAITING);
					cqi.setRunning(false);
					cqi.setLastAttempt(GET_TICK());
					cqi.setErrors(protocolError ? -1 : (cqi.getErrors() + 1));
					failedEvent.reset(new ConnectionQueueItem(cqi));
					removeExtraMCN(cqi, removedEvent);
				}
			} else {
				auto type = aSource->isSet(UserConnection::FLAG_UPLOAD) ? CONNECTION_TYPE_UPLOAD :
					aSource->isSet(UserConnection::FLAG_PM) ? CONNECTION_TYPE_PM : CONNECTION_TYPE_LAST;
				if(type != CONNECTION_TYPE_LAST) {
					auto& container = cqis[type];
					auto i = find_if(container.begin(), container.end(), [&](const ConnectionQueueItem& cqi) {
						return cqi.getUser() == aSource->getUser() && cqi.getToken() == aSource->getToken();
					});
					dcassert(i != container.end());
					if(i != container.end()) {
						removedEvent.reset(new ConnectionQueueItem(*i));
						putCQI(*i);
					}
				}
			}
		}
	}
	if(failedEvent) {
		fire(ConnectionManagerListener::Failed(), failedEvent.get(), aError);
	}
	if(removedEvent) {
		fire(ConnectionManagerListener::Removed(), removedEvent.get());
	}

	putConnection(aSource);
}

bool ConnectionManager::checkHubCCBlock(const string& aServer, const string& aPort, const string& aHubUrl)
{
	const auto server_lower = Text::toLower(aServer);
	dcassert(server_lower == aServer);

	bool cc_blocked = false;

	{
		Lock l(cs);
		cc_blocked = !hubsBlockingCC.empty() && hubsBlockingCC.find(server_lower) != hubsBlockingCC.end();
	}

    if(cc_blocked)
	{
		LogManager::getInstance()->message(str(F_("Blocked a C-C connection to a hub ('%1%:%2%'; request from '%3%')") % aServer % aPort % aHubUrl),
			LogMessage::SEV_WARNING, _("Security"));
		return true;
	}

	return false;
}

void ConnectionManager::on(UserConnectionListener::Failed, UserConnection* aSource, const string& aError) noexcept {
	failed(aSource, aError, false);
}

void ConnectionManager::on(UserConnectionListener::ProtocolError, UserConnection* aSource, const string& aError) noexcept {
	if(aError.compare(0, 7, "CTM2HUB", 7) == 0) {
		{
			Lock l(cs);
			hubsBlockingCC.insert(Text::toLower(aSource->getRemoteIp()));
		}

		string aServerPort = aSource->getRemoteIp() + ":" + aSource->getPort();
		LogManager::getInstance()->message(str(F_("Blocking '%1%', potential DDoS detected (originating hub '%2%')") % aServerPort % aSource->getHubUrl() ),
			LogMessage::SEV_WARNING, _("Security"));
	}

	failed(aSource, aError, true);
}

void ConnectionManager::disconnect(const UserPtr& user) {
	Lock l(cs);
	for(auto uc: userConnections) {
		if(uc->getUser() == user)
			uc->disconnect(true);
	}
}

void ConnectionManager::disconnect(const UserPtr& user, ConnectionType type) {
	Lock l(cs);
	for(auto uc: userConnections) {
		if(uc->getUser() == user && uc->isSet(type == CONNECTION_TYPE_DOWNLOAD ? UserConnection::FLAG_DOWNLOAD :
			type == CONNECTION_TYPE_UPLOAD ? UserConnection::FLAG_UPLOAD : UserConnection::FLAG_PM))
		{
			uc->disconnect(true);
		}
	}
}

void ConnectionManager::disconnectUploads(const string& hubUrl) {
	Lock l(cs);
	for(auto connection: userConnections) {
		if(connection->isSet(UserConnection::FLAG_UPLOAD) && Util::stricmp(connection->getHubUrl(), hubUrl) == 0) {
			connection->disconnect(true);
		}
	}
}

void ConnectionManager::disconnectExtraMCNUpload(const UserPtr& user, const UserConnection* except) {
	Lock l(cs);
	UserConnection* candidate = nullptr;
	int regularConnections = 0;
	for(auto connection: userConnections) {
		if(connection->getUser() != user ||
			!connection->isSet(UserConnection::FLAG_UPLOAD) || !connection->isMCN() ||
			!connection->isMCNNormal() || !connection->isSet(UserConnection::FLAG_HASSLOT))
		{
			continue;
		}
		++regularConnections;
		if(connection != except && (!candidate || connection->getState() == UserConnection::STATE_GET)) {
			candidate = connection;
		}
	}
	if(candidate && regularConnections > 1) {
		candidate->disconnect(true);
	}
}

void ConnectionManager::disconnectAll() {
	Lock l(cs);
	for(auto j: userConnections) {
		j->disconnect(true);
	}
}

void ConnectionManager::shutdown() {
	TimerManager::getInstance()->removeListener(this);

	shuttingDown = true;
	disconnect();
	disconnectAll();
	// Wait until all connections have died out...
	while(true) {
		{
			Lock l(cs);
			if(userConnections.empty()) {
				break;
			}
		}
		Thread::sleep(50);
	}
}

// UserConnectionListener
void ConnectionManager::on(UserConnectionListener::Supports, UserConnection* conn, const StringList& feat) noexcept {
	for(auto& i: feat) {
		if(i == UserConnection::FEATURE_MINISLOTS) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_MINISLOTS);
		} else if(i == UserConnection::FEATURE_XML_BZLIST) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_XML_BZLIST);
		} else if(i == UserConnection::FEATURE_ADCGET) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_ADCGET);
		} else if(i == UserConnection::FEATURE_ZLIB_GET) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_ZLIB_GET);
		} else if(i == UserConnection::FEATURE_TTHL) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_TTHL);
		} else if(i == UserConnection::FEATURE_TTHF) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_TTHF);
		}
	}
}

} // namespace dcpp
