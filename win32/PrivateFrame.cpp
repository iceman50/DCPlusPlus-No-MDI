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

#include "stdafx.h"
#include "PrivateFrame.h"

#include "HoldRedraw.h"
#include "MainWindow.h"
#include "resource.h"

#include <dcpp/AdcHub.h>
#include <dcpp/ChatMessage.h>
#include <dcpp/ClientManager.h>
#include <dcpp/Client.h>
#include <dcpp/ConnectionManager.h>
#include <dcpp/LogManager.h>
#include <dcpp/PluginManager.h>
#include <dcpp/PrivateChatManager.h>
#include <dcpp/User.h>
#include <dcpp/UserConnection.h>
#include <dcpp/WindowInfo.h>

#include <dwt/util/StringUtils.h>

const string PrivateFrame::id = "PM";
const string& PrivateFrame::getId() const { return id; }

PrivateFrame::FrameMap PrivateFrame::frames;
CriticalSection PrivateFrame::framesMutex;

namespace {

bool matchesCurrentHub(const HintedUser& queuedUser, const HintedUser& frameUser) {
	return queuedUser.user == frameUser.user &&
		hubHintMatches(frameUser.hint, queuedUser.hint);
}

} // namespace

void PrivateFrame::openWindow(TabViewPtr parent, const HintedUser& replyTo_, const tstring& msg,
	const string& logPath, bool activate)
{
	PrivateFrame* frame = nullptr;
	{
		Lock l(framesMutex);
		auto i = frames.find(replyTo_);
		if(i != frames.end()) {
			frame = i->second;
		}
	}

	const auto existing = frame != nullptr;
	if(!frame) {
		frame = new PrivateFrame(parent, replyTo_, logPath);
	}
	if(existing && !replyTo_.hint.empty() &&
		!hubHintsEqual(frame->replyTo.getUser().hint, replyTo_.hint))
	{
		frame->changeHub(replyTo_.hint);
	}
	if(activate)
		frame->activate();
	if(!msg.empty())
		frame->sendMessage(msg);
}

bool PrivateFrame::gotMessage(TabViewPtr parent, const ChatMessage& message, const string& hubHint, bool fromBot) {
	auto& user = (message.replyTo == ClientManager::getInstance()->getMe()) ? message.to : message.replyTo;

	PrivateFrame* frame = nullptr;
	size_t frameCount;
	{
		Lock l(framesMutex);
		auto i = frames.find(user);
		if(i != frames.end()) {
			frame = i->second;
		}
		frameCount = frames.size();
	}

	if(!frame) {
		// creating a new window

		if(static_cast<int>(frameCount) >= SETTING(MAX_PM_WINDOWS)) {
			return false;
		}

		auto hintedUser = HintedUser(user, hubHint);

		auto p = new PrivateFrame(parent, hintedUser);
		if(!SETTING(POPUNDER_PM))
			p->activate();

		p->addStatus(Text::toT(str(F_("Message through hub: %1%") % ClientManager::getInstance()->getHubName(hintedUser))));

		p->addChat(message);
		p->lastMessageTime = message.timestamp;

		if(Util::getAway() && !(SETTING(NO_AWAYMSG_TO_BOTS) && fromBot)) {
			auto awayMessage = Util::getAwayMessage();
			if(!awayMessage.empty()) {
				p->sendMessage(Text::toT(awayMessage));
			}
		}

		WinUtil::notify(WinUtil::NOTIFICATION_PM_WINDOW, Text::toT(message.message), [user] { activateWindow(user); });

	} else {
		// send the message to the existing window
		frame->addChat(message);
		frame->lastMessageTime = message.timestamp;
	}

	WinUtil::notify(WinUtil::NOTIFICATION_PM, Text::toT(message.message), [user] { activateWindow(user); });

	return true;
}

void PrivateFrame::activateWindow(const UserPtr& u) {
	PrivateFrame* frame = nullptr;
	{
		Lock l(framesMutex);
		auto i = frames.find(u);
		if(i != frames.end()) {
			frame = i->second;
		}
	}
	if(frame) {
		frame->activate();
	}
}

bool PrivateFrame::isOpen(const UserPtr& u) {
	Lock l(framesMutex);
	return frames.find(u) != frames.end();
}

void PrivateFrame::handlePMConnection(const HintedUser& user, const string& connectionToken) {
	PrivateFrame* frame = nullptr;
	{
		Lock l(framesMutex);
		auto i = frames.find(user.user);
		if(i != frames.end()) {
			frame = i->second;
		}
	}

	if(frame) {
		frame->adoptPMConnection(connectionToken);
	}
	// With no frame, PrivateChatManager remains the continuous listener and
	// keeps the exact channel safely parked. A delayed availability task must
	// not undo an intentional return performed while the tab was closing.
}

void PrivateFrame::handlePMMessage(const HintedUser& user, const string& connectionToken,
	const ChatMessage& message)
{
	if(message.from == ClientManager::getInstance()->getMe()) {
		return;
	}

	PrivateFrame* frame = nullptr;
	{
		Lock l(framesMutex);
		auto i = frames.find(user.user);
		if(i != frames.end()) {
			frame = i->second;
		}
	}
	if(!frame) {
		return;
	}

	Lock lifetimeLock(frame->connLifetimeMutex);
	{
		Lock l(frame->mutex);
		if(!frame->conn.load() || frame->connToken != connectionToken) {
			return;
		}
	}
	frame->messageSeenPending = true;
	frame->sendSeenIfActive();
}

void PrivateFrame::handlePMI(const HintedUser& user, const string& connectionToken, const AdcCommand& cmd) {
	PrivateFrame* frame = nullptr;
	{
		Lock l(framesMutex);
		auto i = frames.find(user.user);
		if(i != frames.end()) {
			frame = i->second;
		}
	}

	if(!frame) {
		if(cmd.hasFlag("QU", 0)) {
			PrivateChatManager::getInstance()->releasePMConn(user.user, connectionToken, true);
		}
		return;
	}

	uint64_t revision;
	{
		Lock l(frame->mutex);
		if(!frame->conn.load() || frame->connToken != connectionToken) {
			if(cmd.hasFlag("QU", 0)) {
				PrivateChatManager::getInstance()->releasePMConn(user.user, connectionToken, true);
			}
			return;
		}
		revision = frame->connRevision.load();
	}

	PMInfo type;
	string value;
	if(cmd.hasFlag("SN", 0)) {
		type = PM_INFO_SEEN;
	} else if(cmd.getParam("TP", 0, value) && (value == "0" || value == "1")) {
		type = value == "1" ? PM_INFO_TYPING_ON : PM_INFO_TYPING_OFF;
	} else if(cmd.getParam("AC", 0, value) && value == "0") {
		type = PM_INFO_NO_AUTOCONNECT;
	} else if(cmd.hasFlag("QU", 0)) {
		type = PM_INFO_QUIT;
	} else {
		return;
	}

	if(frame->isCurrentConnection(connectionToken, revision)) {
		frame->updatePMInfo(type);
	}
}

void PrivateFrame::closeAll(bool offline) {
	vector<PrivateFrame*> snapshot;
	{
		Lock l(framesMutex);
		snapshot.reserve(frames.size());
		for(const auto& i: frames) {
			snapshot.push_back(i.second);
		}
	}

	for(auto frame: snapshot) {
		if(!offline || !frame->online) {
			frame->close(true);
		}
	}
}

WindowParams PrivateFrame::getWindowParams() const {
	WindowParams ret;
	addRecentParams(ret);
	ret["CID"] = WindowParam(replyTo.getUser().user->getCID().toBase32(), WindowParam::FLAG_IDENTIFIES | WindowParam::FLAG_CID);
	ret["Hub"] = WindowParam(replyTo.getUser().hint);
	ret["LogPath"] = WindowParam(getLogPath());
	return ret;
}

void PrivateFrame::parseWindowParams(TabViewPtr parent, const WindowParams& params) {
	auto cid = params.find("CID");
	auto hub = params.find("Hub");
	if(cid != params.end() && hub != params.end()) {
		auto logPath = params.find("LogPath");
		openWindow(parent, HintedUser(ClientManager::getInstance()->getUser(CID(cid->second)), hub->second), Util::emptyStringT,
			logPath != params.end() ? logPath->second.content : Util::emptyString, parseActivateParam(params));
	}
}

bool PrivateFrame::isFavorite(const WindowParams& params) {
	auto cid = params.find("CID");
	if(cid != params.end()) {
		UserPtr u = ClientManager::getInstance()->getUser(CID(cid->second));
		if(u)
			return FavoriteManager::getInstance()->isFavoriteUser(u);
	}
	return false;
}

PrivateFrame::PrivateFrame(TabViewPtr parent, const HintedUser& replyTo_, const string& logPath) :
BaseType(parent, _T(""), IDH_PM, IDI_PRIVATE_OFF, false),
replyTo(replyTo_),
online(false),
conn(nullptr),
connRevision(0),
acceptCCPMConnections(true),
localTyping(false),
remoteTyping(false),
messageSeenPending(false),
allowAutoCCPM(true),
lastMessageTime(time(NULL))
{
	createChat(this);
	chat->setHelpId(IDH_PM_CHAT);
	addWidget(chat);
	chat->onContextMenu([this](const dwt::ScreenCoordinate &sc) { return handleChatContextMenu(sc); });
	chat->onFocus([this] { sendSeenIfActive(); });

	message->setHelpId(IDH_PM_MESSAGE);
	addWidget(message, ALWAYS_FOCUS);
	message->onKeyDown([this](int c) { return handleMessageKeyDown(c); });
	message->onSysKeyDown([this](int c) { return handleMessageKeyDown(c); });
	message->onChar([this](int c) { return handleMessageChar(c); });
	message->onUpdated([this] { updateTypingState(); });
	message->onFocus([this] {
		updateTypingState();
		sendSeenIfActive();
	});
	message->onKillFocus([this](dwt::Widget*) { updateTypingState(false); });
	onVisibilityChanged([this](bool visible) {
		if(visible) {
			sendSeenIfActive();
			updateTypingState();
		} else {
			updateTypingState(false);
		}
	});

	initStatus();

	status->onDblClicked(STATUS_STATUS, [this] { openLog(); });

	{
		auto f = [this] { handleChannelMenu(); };
		status->onClicked(STATUS_CHANNEL, f);
		status->onRightClicked(STATUS_CHANNEL, f);
	}

	status->setToolTip(STATUS_CHANNEL, T_("Current communication channel - click to change"));

	status->setHelpId(STATUS_STATUS, IDH_PM_STATUS);
	status->setHelpId(STATUS_CHANNEL, IDH_PM_CHANNEL);

	initAccels();

	layout();

	readLog(logPath, SETTING(PM_LAST_LOG_LINES));

	// Publish before registering/adopting so PrivateChatManager can park a
	// connection that overlaps construction. Its Connected callback always
	// parks first, making getPMConn below an atomic handoff under its lock.
	{
		Lock l(framesMutex);
		frames.emplace(replyTo.getUser(), this);
	}

	ConnectionManager::getInstance()->addListener(this);
	{
		Lock lifetimeLock(connLifetimeMutex);
		Lock l(mutex);
		auto activeConn = PrivateChatManager::getInstance()->getPMConn(replyTo.getUser().user);
		if(activeConn && !matchesCurrentHub(activeConn->getHintedUser(), replyTo.getUser())) {
			// Parked channels are hub-scoped just like live Connected events.
			// Do not silently reuse a channel from another hub in this frame.
			activeConn->disconnect(true);
			activeConn = nullptr;
		}
		if(activeConn && replyTo.getUser().hint.empty()) {
			replyTo.getUser().hint = activeConn->getHintedUser().hint;
		}
		conn.store(activeConn);
		if(activeConn) {
			connToken = activeConn->getToken();
			++connRevision;
		}
	}

	ClientManager::getInstance()->addListener(this);
	callAsync([this] {
		updateOnlineStatus(true);
	});

	addRecent();
}

PrivateFrame::~PrivateFrame() {
}

void PrivateFrame::addedChat(const tstring& message) {
	setDirty(SettingsManager::BOLD_PM);

	if (ccReady() && SETTING(DONT_LOG_CCPM)) return;

	if(SETTING(LOG_PRIVATE_CHAT)) {
		ParamMap params;
		params["message"] = [&message] { return Text::toDOS(Text::fromT(message)); };
		fillLogParams(params);
		LOG(LogManager::PM, params);
	}
}

void PrivateFrame::addStatus(const tstring& text) {
	lastStatus = Text::toT("[" + Util::getShortTimeString() + "] ") + text;
	if(!remoteTyping) {
		status->setText(STATUS_STATUS, lastStatus);
	}

	if(SETTING(STATUS_IN_CHAT)) {
		addChat(_T("*** ") + text);
	} else {
		setDirty(SettingsManager::BOLD_PM);
	}
}

bool PrivateFrame::preClosing() {
	updateTypingState(false);
	{
		Lock lifetimeLock(connLifetimeMutex);
		UserConnection* activeConn;
		string activeToken;
		{
			Lock l(mutex);
			acceptCCPMConnections = false;
			allowAutoCCPM = false;
			activeConn = conn.exchange(nullptr);
			activeToken = connToken;
			connToken.clear();
			++connRevision;
		}

		// Stop advertising this frame before removing its ConnectionManager
		// listener. Otherwise PrivateChatManager could leave a new connection
		// for a frame that can no longer adopt it.
		{
			Lock l(framesMutex);
			frames.erase(replyTo.getUser());
		}

		if(activeConn) {
			const auto canPark = activeConn->supportsCPMI() &&
				activeConn->getState() == UserConnection::STATE_CMD;
			if(canPark) {
				activeConn->pmi("QU", "1");
			}
			// A failed connection is already being removed and must not be
			// returned to PrivateChatManager while its removal event is in flight.
			if(canPark) {
				if(!PrivateChatManager::getInstance()->returnPMConn(
					replyTo.getUser().user, activeToken, activeConn) &&
					activeConn->getState() == UserConnection::STATE_CMD)
				{
					activeConn->disconnect(false);
				}
			} else if(activeConn->getState() != UserConnection::STATE_UNCONNECTED) {
				activeConn->disconnect(false);
			}
		}
	}

	ClientManager::getInstance()->removeListener(this);
	ConnectionManager::getInstance()->removeListener(this);

	return true;
}

string PrivateFrame::getLogPath() const {
	ParamMap params;
	fillLogParams(params);
	return Util::validateFileName(LogManager::getInstance()->getPath(LogManager::PM, params));
}

void PrivateFrame::openLog() {
	WinUtil::openFile(Text::toT(getLogPath()));
}

void PrivateFrame::fillLogParams(ParamMap& params) const {
	params["hubNI"] = [this] { return ClientManager::getInstance()->getHubName(replyTo.getUser()); };
	params["hubURL"] = [this] { return replyTo.getUser().hint; };
	params["userCID"] = [this] { return replyTo.getUser().user->getCID().toBase32(); };
	params["userNI"] = [this] { return ClientManager::getInstance()->getNick(replyTo.getUser()); };
	params["myCID"] = [] { return ClientManager::getInstance()->getMe()->getCID().toBase32(); };
}

void PrivateFrame::layout() {
	const int border = 2;

	dwt::Rectangle r { getClientSize() };

	r.size.y -= status->refresh();

	int ymessage = message->getTextSize(_T("A")).y * messageLines + 10;
	dwt::Rectangle rm(0, r.size.y - ymessage, r.width(), ymessage);
	message->resize(rm);

	r.size.y -= rm.size.y + border;
	chat->resize(r);
}

void PrivateFrame::updateOnlineStatus(bool newChannel) {
	auto hubs = ClientManager::getInstance()->getHubUrls(replyTo.getUser());
	auto& user = replyTo.getUser();

	if(user.hint.empty() && !hubs.empty()) {
		Lock l(mutex);
		if(user.hint.empty()) {
			user.hint = hubs.front();
			newChannel = true;
		}
	}

	auto hintOnline = !hubs.empty() && (user.hint.empty() ||
		any_of(hubs.begin(), hubs.end(), [&user](const string& hubUrl) {
			return hubHintsEqual(user.hint, hubUrl);
		}));

	if(newChannel || online != hintOnline) {
		online = hintOnline;

		if(!newChannel) {
			addStatus(online ? T_("User went online") : T_("User went offline"));
		}
		setIcon(online ? IDI_PRIVATE : IDI_PRIVATE_OFF);
		status->setIcon(STATUS_CHANNEL, WinUtil::statusIcon(ccReady() ? IDI_SECURE : online ? IDI_HUB : IDI_HUB_OFF));
		newChannel = true;
	}

	setText(WinUtil::getNick(replyTo.getUser()) + _T(" - ") + WinUtil::getHubName(replyTo.getUser()));

	if(newChannel) {
		updateChannel();

		if(online && allowAutoCCPM && SETTING(ALWAYS_CCPM) && !ccReady()) {
			startCC(true);
		}
	}
}

void PrivateFrame::updateChannel() {
	auto channel = ccReady() ? T_("Direct encrypted channel") : WinUtil::getHubName(replyTo.getUser());
	dwt::util::cutStr(channel, 26);
	status->setText(STATUS_CHANNEL, channel, true);
}

void PrivateFrame::startCC(bool silent) {
	if(ccReady()) {
		if(!silent) { addStatus(T_("A direct encrypted channel is already available")); }
		return;
	}

	{
		auto lock = ClientManager::getInstance()->lock();
		const auto& user = replyTo.getUser();
		auto ou = user.hint.empty() ?
			ClientManager::getInstance()->findOnlineUser(user) :
			ClientManager::getInstance()->findOnlineUserHint(user);
		if(!ou) {
			if(!silent) { addStatus(T_("User offline")); }
			return;
		}

		tstring err = ou->getUser()->isNMDC() ? T_("A secure ADC hub is required; this feature is not supported on NMDC hubs") :
			!ou->getUser()->isSet(User::TLS) ? T_("The user does not support secure encrypted connections") :
			!ou->getIdentity().supports(AdcHub::CCPM_FEATURE) ? T_("The user does not support the CCPM ADC extension") : _T("");
		if(!err.empty()) {
			if(!silent) { addStatus(str(TF_("Cannot start the direct encrypted channel: %1%") % err)); }
			return;
		}
	}

	if(!silent) { addStatus(T_("Establishing a direct encrypted channel...")); }
	if(!silent) { allowAutoCCPM = true; }
	ClientManager::getInstance()->connect(replyTo.getUser(), ConnectionManager::getInstance()->makeToken(), CONNECTION_TYPE_PM);
}

void PrivateFrame::closeCC(bool silent) {
	bool found = false;
	{
		Lock lifetimeLock(connLifetimeMutex);
		UserConnection* activeConn;
		{
			Lock l(mutex);
			activeConn = conn.load();
			if(activeConn && activeConn->getState() == UserConnection::STATE_CMD) {
				allowAutoCCPM = false;
			} else {
				activeConn = nullptr;
			}
		}
		if(activeConn) {
			if(activeConn->supportsCPMI()) {
				activeConn->pmi("AC", "0");
			}
			activeConn->disconnect(false);
			found = true;
		}
	}

	if(found) {
		if(!silent) { addStatus(T_("Disconnecting the direct encrypted channel...")); }
	} else {
		if(!silent) { addStatus(T_("No direct encrypted channel available")); }
	}
}

void PrivateFrame::changeHub(const string& hubHint) {
	{
		// Keep the close request and hint change atomic with respect to a
		// connection event for the old hub.
		Lock lifetimeLock(connLifetimeMutex);
		closeCC(true);
		{
			Lock l(mutex);
			replyTo.getUser().hint = hubHint;
		}
	}
	updateOnlineStatus(true);
}

void PrivateFrame::adoptPMConnection(const string& connectionToken) {
	uint64_t revision = 0;
	bool adopted = false;
	{
		Lock lifetimeLock(connLifetimeMutex);
		Lock l(mutex);
		if(!acceptCCPMConnections) {
			PrivateChatManager::getInstance()->releasePMConn(
				replyTo.getUser().user, connectionToken, true);
			return;
		}

		auto activeConn = conn.load();
		if(activeConn) {
			if(connToken == connectionToken) {
				// ConnectionManager may have called this frame before
				// PrivateChatManager parked its fallback copy.
				PrivateChatManager::getInstance()->releasePMConn(
					replyTo.getUser().user, connectionToken, false);
			}
			return;
		}

		activeConn = PrivateChatManager::getInstance()->getPMConn(
			replyTo.getUser().user, connectionToken);
		if(!activeConn) {
			return;
		}

		if(!matchesCurrentHub(activeConn->getHintedUser(), replyTo.getUser())) {
			activeConn->disconnect(true);
			return;
		}

		if(replyTo.getUser().hint.empty()) {
			replyTo.getUser().hint = activeConn->getHintedUser().hint;
		}
		conn.store(activeConn);
		connToken = connectionToken;
		revision = ++connRevision;
		adopted = true;
	}

	if(adopted) {
		callAsync([this, connectionToken, revision] {
			if(isCurrentConnection(connectionToken, revision)) {
				localTyping = false;
				remoteTyping = false;
				updateOnlineStatus(true);
				addStatus(T_("A direct encrypted channel has been established"));
				updateTypingState();
				sendSeenIfActive();
			}
		});
	}
}

bool PrivateFrame::ccReady() const {
	Lock lifetimeLock(connLifetimeMutex);
	UserConnection* activeConn;
	{
		Lock l(mutex);
		activeConn = conn.load();
	}
	return activeConn && activeConn->getState() == UserConnection::STATE_CMD && activeConn->isSecure();
}

bool PrivateFrame::isCurrentConnection(const string& token, uint64_t revision) const {
	Lock l(mutex);
	return conn.load() && connToken == token && connRevision.load() == revision;
}

bool PrivateFrame::isDisconnectedConnection(uint64_t revision) const {
	Lock l(mutex);
	return !conn.load() && connRevision.load() == revision;
}

void PrivateFrame::updatePMInfo(PMInfo type) {
	switch(type) {
	case PM_INFO_SEEN:
		addStatus(T_("Message seen"));
		break;
	case PM_INFO_TYPING_ON:
		remoteTyping = true;
		status->setText(STATUS_STATUS, T_("User is typing..."));
		break;
	case PM_INFO_TYPING_OFF:
		remoteTyping = false;
		status->setText(STATUS_STATUS, lastStatus);
		break;
	case PM_INFO_NO_AUTOCONNECT:
		allowAutoCCPM = false;
		break;
	case PM_INFO_QUIT:
		remoteTyping = false;
		addStatus(T_("User closed the private message window"));
		break;
	}
}

bool PrivateFrame::sendPMI(const char* name, const string& value) {
	Lock lifetimeLock(connLifetimeMutex);
	UserConnection* activeConn;
	{
		Lock l(mutex);
		activeConn = conn.load();
	}
	if(activeConn && activeConn->getState() == UserConnection::STATE_CMD &&
		activeConn->isSecure() && activeConn->supportsCPMI())
	{
		activeConn->pmi(name, value);
		return true;
	}
	return false;
}

void PrivateFrame::updateTypingState(bool refreshTimeout) {
	bool cpmiReady;
	{
		Lock lifetimeLock(connLifetimeMutex);
		UserConnection* activeConn;
		{
			Lock l(mutex);
			activeConn = conn.load();
		}
		cpmiReady = activeConn && activeConn->getState() == UserConnection::STATE_CMD &&
			activeConn->isSecure() && activeConn->supportsCPMI();
	}
	const auto typing = cpmiReady && refreshTimeout && !message->getText().empty() && message->hasFocus() && isActive();
	if(typing != localTyping) {
		localTyping = typing;
		sendPMI("TP", typing ? "1" : "0");
	}

	if(typing) {
		setTimer([this] {
			updateTypingState(false);
			return false;
		}, 5000, TIMER_CPMI_TYPING);
	} else {
		setTimer(nullptr, 0, TIMER_CPMI_TYPING);
	}
}

void PrivateFrame::sendSeenIfActive() {
	if(messageSeenPending && isActive() && WinUtil::mainWindow->onForeground()) {
		if(sendPMI("SN", "1")) {
			messageSeenPending = false;
		}
	}
}

void PrivateFrame::enterImpl(const tstring& s) {
	bool resetText = true;
	bool send = false;

	// Process special commands
	if(s[0] == '/') {
		tstring cmd = s;
		tstring param;
		tstring message;
		tstring status;
		bool thirdPerson = false;

		if(PluginManager::getInstance()->onChatCommandPM(replyTo.getUser(), Text::fromT(s))) {
			// Plugins, chat commands

		} else if(WinUtil::checkCommand(cmd, param, message, status, thirdPerson)) {
			if(!message.empty()) {
				sendMessage(message, thirdPerson);
			}
			if(!status.empty()) {
				addStatus(status);
			}
		} else if(ChatType::checkCommand(cmd, param, status)) {
			if(!status.empty()) {
				addStatus(status);
			}
		} else if(Util::stricmp(cmd.c_str(), _T("grant")) == 0) {
			handleGrantSlot();
			addStatus(T_("Slot granted"));
		} else if(Util::stricmp(cmd.c_str(), _T("close")) == 0) {
			postMessage(WM_CLOSE);
		} else if(Util::stricmp(cmd.c_str(), _T("direct")) == 0 || Util::stricmp(cmd.c_str(), _T("encrypted")) == 0) {
			startCC();
		} else if((Util::stricmp(cmd.c_str(), _T("favorite")) == 0) || (Util::stricmp(cmd.c_str(), _T("fav")) == 0)) {
			handleAddFavorite();
			addStatus(T_("Favorite user added"));
		} else if(Util::stricmp(cmd.c_str(), _T("getlist")) == 0) {
			handleGetList(getParent());
		} else if(Util::stricmp(cmd.c_str(), _T("ignore")) == 0) {
			handleIgnoreChat(true);
		} else if(Util::stricmp(cmd.c_str(), _T("unignore")) == 0) {
			handleIgnoreChat(false);
		} else if(Util::stricmp(cmd.c_str(), _T("log")) == 0) {
			openLog();
		} else if(Util::stricmp(cmd.c_str(), _T("lastmessage")) == 0) {
			addStatus(Text::toT(str(F_("Last message occured %1%") % Util::getTimeString(lastMessageTime, "%c"))));
		} else if(Util::stricmp(cmd.c_str(), _T("help")) == 0) {
			bool bShowBriefCommands = !param.empty() && (Util::stricmp(param.c_str(), _T("brief")) == 0);

			if(bShowBriefCommands)
			{
				addChat(T_("*** Keyboard commands:") + _T("\r\n") + 
						WinUtil::commands + 
						_T(", /direct, /encrypted, /getlist, /grant, /close, /favorite, /ignore, /unignore, /log <system, downloads, uploads>, /lastmessage")
						);
			}
			else
			{
				addChat(T_("*** Keyboard commands:") + _T("\r\n") +
						WinUtil::getDescriptiveCommands() +
						+ _T("\r\n") _T("/direct")
						+ _T("\r\n") _T("/encrypted")
						+ _T("\r\n\t") + T_("Starts a direct encrypted communication channel to avoid spying on your private messages.")
						+ _T("\r\n") _T("/getlist")
						+ _T("\r\n\t") + T_("Adds the current user's list to the Download Queue.")
						+ _T("\r\n") _T("/grant")
						+ _T("\r\n\t") + T_("Grants the remote user a slot. Once they connect, or if they don't connect in 10 minutes, the granted slot is removed.")
						+ _T("\r\n") _T("/favorite")
						+ _T("\r\n") _T("/fav")
						+ _T("\r\n\t") + T_("Adds the current user to the list of Favorite Users.")
						+ _T("\r\n") _T("/ignore")
						+ _T("\r\n\t") + T_("Adds a user matching definition (or modifies an existing one, if possible) to ignore chat messages from the current user.")
						+ _T("\r\n") _T("/unignore")
						+ _T("\r\n\t") + T_("Adds a user matching definition (or modifies an existing one, if possible) to stop ignoring chat messages from the current user.")
						+ _T("\r\n") _T("/lastmessage")
						+ _T("\r\n\t") + T_("Lists the date and time when the last message was sent or received.")
						);
			}

		} else if(SETTING(SEND_UNKNOWN_COMMANDS)) {
			send = true;
		} else {
			addStatus(str(TF_("Unknown command: %1%") % cmd));
		}

	} else {
		send = true;
	}

	if(send) {
		if(online || ccReady()) {
			sendMessage(s);
		} else {
			message->showPopup(T_("User offline"), T_("The message cannot be delivered because the user is offline."), TTI_ERROR);
			resetText = false;
		}
	}
	if(resetText) {
		message->setText(Util::emptyStringT);
	}
}

void PrivateFrame::sendMessage(const tstring& msg, bool thirdPerson) {
	auto msg8 = Text::fromT(msg);

	{
		Lock lifetimeLock(connLifetimeMutex);
		UserConnection* activeConn;
		{
			Lock l(mutex);
			activeConn = conn.load();
		}
		if(activeConn && activeConn->getState() == UserConnection::STATE_CMD && activeConn->isSecure()) {
			activeConn->pm(msg8, thirdPerson);
			return;
		}
	}

	ClientManager::getInstance()->privateMessage(replyTo.getUser(), msg8, thirdPerson, replyTo.getUser().user->isNMDC());
}

PrivateFrame::UserInfoList PrivateFrame::selectedUsersImpl() {
	return UserInfoList(1, &replyTo);
}

void PrivateFrame::tabMenuImpl(dwt::Menu* menu) {
	appendUserItems(getParent(), menu, false, false);
	prepareMenu(menu, UserCommand::CONTEXT_USER, replyTo.getUser().hint);
	menu->appendSeparator();
}

bool PrivateFrame::handleChatContextMenu(dwt::ScreenCoordinate pt) {
	if(pt.x() == -1 || pt.y() == -1) {
		pt = chat->getContextMenuPos();
	}

	tstring searchText;
	WinUtil::getChatSelText(chat, searchText, pt);

	auto menu = chat->getMenu(searchText);
	
	WinUtil::addSearchMenu(menu.get(), searchText);

	menu->setTitle(escapeMenu(getText()), getParent()->getIcon(this));

	prepareMenu(menu.get(), UserCommand::CONTEXT_USER, replyTo.getUser().hint);

	menu->open(pt);
	return true;
}

void PrivateFrame::handleChannelMenu() {
	auto menu = addChild(WinUtil::Seeds::menu);

	menu->setTitle(T_("Communication channel"));

	auto hubs = ClientManager::getInstance()->getHubs(replyTo.getUser());

	if(hubs.empty()) {
		menu->appendItem(T_("(User offline)"), nullptr, nullptr, false);

	} else {
		auto cc = ccReady();

		for(auto& hub: hubs) {
			auto url = hub.first;
			auto current = !cc && hubHintsEqual(url, replyTo.getUser().hint);
			auto pos = menu->appendItem(dwt::util::escapeMenu(Text::toT(hub.second)),
				[this, url] { changeHub(url); }, nullptr, !current);
			if(current) {
				menu->checkItem(pos);
			}
		}

		if(SETTING(ENABLE_CCPM)) {
			menu->appendSeparator();

			if(cc) {
				menu->appendItem(T_("Disconnect the direct encrypted channel"), [this] { closeCC(); });
			} else {
				menu->appendItem(T_("Start a direct encrypted channel"), [this] { startCC(); }, WinUtil::menuIcon(IDI_SECURE));
			}
		}
	}

	menu->open();
}

void PrivateFrame::runUserCommand(const UserCommand& uc) {
	if(!WinUtil::getUCParams(this, uc, ucLineParams))
		return;

	auto ucParams = ucLineParams;
	ClientManager::getInstance()->userCommand(replyTo.getUser(), uc, ucParams, true);
}

void PrivateFrame::on(ClientManagerListener::UserConnected, const UserPtr& aUser) noexcept {
	if(replyTo.getUser() == aUser)
		callAsync([this] { updateOnlineStatus(); });
}

void PrivateFrame::on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept {
	if(replyTo.getUser() == aUser.getUser())
		callAsync([this] { updateOnlineStatus(); });
}

void PrivateFrame::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser) noexcept {
	if(replyTo.getUser() == aUser)
		callAsync([this] { updateOnlineStatus(); });
}

void PrivateFrame::on(ConnectionManagerListener::Connected, ConnectionQueueItem* cqi, UserConnection* uc) noexcept {
	if(cqi->getType() != CONNECTION_TYPE_PM || cqi->getUser().user != replyTo.getUser().user) {
		return;
	}

	if(!uc->isSecure()) {
		uc->disconnect(true);
		return;
	}

	const auto token = cqi->getToken();
	const auto connectedHint = cqi->getUser().hint;
	uint64_t revision = 0;
	UserConnection* oldConn = nullptr;
	bool reject = false;
	bool alreadyCurrent = false;
	{
		Lock lifetimeLock(connLifetimeMutex);
		{
			Lock l(mutex);
			if(!acceptCCPMConnections || !matchesCurrentHub(cqi->getUser(), replyTo.getUser())) {
				reject = true;
			} else {
				oldConn = conn.load();
				if(oldConn == uc && connToken == token) {
					alreadyCurrent = true;
				} else {
					conn.store(uc);
					connToken = token;
					revision = ++connRevision;
				}
			}
		}

		if(!alreadyCurrent && oldConn && oldConn != uc) {
			oldConn->disconnect(false);
		}
		if(!reject) {
			// Keep adoption and fallback release atomic with tab closing.
			// Otherwise closing could re-park this channel in between and this
			// callback would subsequently remove its final owner.
			PrivateChatManager::getInstance()->releasePMConn(
				replyTo.getUser().user, token, false);
		}
	}
	if(reject) {
		// PrivateChatManager leaves connections for an open frame to adopt.
		// Reject one that raced with closing or a hub change so it isn't left
		// alive without an owner.
		uc->disconnect(true);
		return;
	}

	if(alreadyCurrent) {
		return;
	}

	callAsync([this, token, connectedHint, revision] {
		bool current = false;
		{
			Lock l(mutex);
			if(conn.load() && connToken == token && connRevision.load() == revision) {
				current = true;
				if(replyTo.getUser().hint.empty()) {
					replyTo.getUser().hint = connectedHint;
				}
			}
		}
		if(current) {
			localTyping = false;
			remoteTyping = false;
			updateOnlineStatus(true);
			addStatus(T_("A direct encrypted channel has been established"));
			updateTypingState();
			sendSeenIfActive();
		}
	});
}

void PrivateFrame::on(ConnectionManagerListener::Removed, ConnectionQueueItem* cqi) noexcept {
	if(cqi->getType() != CONNECTION_TYPE_PM || cqi->getUser().user != replyTo.getUser().user) {
		return;
	}

	const auto token = cqi->getToken();
	uint64_t revision = 0;
	bool removedCurrent = false;
	{
		Lock lifetimeLock(connLifetimeMutex);
		{
			Lock l(mutex);
			if(conn.load() && connToken == token) {
				conn.store(nullptr);
				connToken.clear();
				revision = ++connRevision;
				removedCurrent = true;
			}
		}
	}

	// This is deliberately token-specific. PrivateChatManager may have handled
	// this event just before a closing frame tried to return the same connection.
	PrivateChatManager::getInstance()->releasePMConn(replyTo.getUser().user, token, false);
	if(!removedCurrent) {
		return;
	}

	callAsync([this, revision] {
		if(isDisconnectedConnection(revision)) {
			updateTypingState(false);
			remoteTyping = false;
			updateOnlineStatus(true);
			addStatus(T_("The direct encrypted channel has been disconnected"));
		}
	});
}
