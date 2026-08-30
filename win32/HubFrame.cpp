/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2026 iceman50
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

#include "HubFrame.h"

#include <dcpp/AdcHub.h>
#include <dcpp/BBSManager.h>
#include <dcpp/ChatMessage.h>
#include <dcpp/ClientManager.h>
#include <dcpp/ConnectionManager.h>
#include <dcpp/ConnectivityManager.h>
#include <dcpp/FavoriteManager.h>
#include <dcpp/LogManager.h>
#include <dcpp/SearchManager.h>
#include <dcpp/SimpleXML.h>
#include <dcpp/PluginManager.h>
#include <dcpp/CryptoManager.h>
#include <dcpp/RichText.h>
#include <dcpp/User.h>
#include <dcpp/UserMatch.h>
#include <dcpp/version.h>
#include <dcpp/WindowInfo.h>

#include <dwt/util/HoldResize.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/MessageBox.h>
#include <dwt/widgets/SplitterContainer.h>
#include <dwt/widgets/TextBox.h>

#include "BBSFrame.h"
#include "MainWindow.h"
#include "PrivateFrame.h"
#include "HoldRedraw.h"
#include "TypedTable.h"

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using dwt::SplitterContainer;

const string HubFrame::id = WindowManager::hub();
const string& HubFrame::getId() const { return id; }

static const ColumnInfo usersColumns[] = {
	{ N_("Nick"), 100, false },
	{ N_("Shared"), 80, true },
	{ N_("Description"), 75, false },
	{ N_("Tag"), 100, false },
	{ N_("Connection"), 75, false },
	{ N_("IP"), 100, false },
	{ N_("Country"), 100, false },
	{ N_("E-Mail"), 100, false },
	{ N_("CID"), 300, false}
};

decltype(HubFrame::frames) HubFrame::frames;

void HubFrame::openWindow(TabViewPtr parent, string url, bool activate, bool connect) {
	Util::sanitizeUrl(url);

	if(url.empty()) {
		dwt::MessageBox(WinUtil::mainWindow).show(T_("Empty hub address specified"), _T(APPNAME) _T(" ") _T(VERSIONSTRING),
			dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
		return;
	}

	auto i = find_if(frames.begin(), frames.end(), [&url](HubFrame* frame) {
		return hubHintsEqual(frame->url, url);
	});

	if(i == frames.end()) {
		// new hub window
		auto frame = new HubFrame(parent, move(url), connect);
		if(activate)
			frame->activate();

	} else {
		// signal an existing hub window
		auto frame = *i;
		if(activate)
			frame->activate();
		else
			frame->setDirty(SettingsManager::BOLD_HUB);
	}
}

void HubFrame::activateWindow(const string& url) {
	auto i = find_if(frames.begin(), frames.end(), [&url](HubFrame* frame) {
		return hubHintsEqual(frame->url, url);
	});
	if(i != frames.end()) {
		(*i)->activate();
	}
}

void HubFrame::refreshRichTextSettings() {
	for(auto frame: frames) frame->updateRichTextAvailability();
}

void HubFrame::closeAll(ClosePred f) {
	if(!WinUtil::mainWindow->getEnabled())
		return;

	auto toClose = frames;
	if(f) {
		toClose.erase(std::remove_if(toClose.begin(), toClose.end(), f), toClose.end());
	}

	if(!toClose.empty() && (!SETTING(CONFIRM_HUB_CLOSING) || dwt::MessageBox(WinUtil::mainWindow).show(
		str(TF_("Really close %1% hub windows?") % toClose.size()), _T(APPNAME) _T(" ") _T(VERSIONSTRING),
		dwt::MessageBox::BOX_YESNO, dwt::MessageBox::BOX_ICONQUESTION) == IDYES))
	{
		for(auto frame: toClose) {
			frame->confirmClose = false;
			frame->close(true);
		}
	}
}

void HubFrame::closeAll(bool disconnected) {
	closeAll(disconnected ? [](HubFrame* frame) { return frame->client->isConnected(); } : ClosePred());
}

void HubFrame::closeFavGroup(const string& group, bool reversed) {
	closeAll([&](HubFrame* frame) -> bool {
		FavoriteHubEntry* fav = FavoriteManager::getInstance()->getFavoriteHubEntry(frame->url);
		return (fav && fav->getGroup() == group) ^ !reversed;
	});
}

void HubFrame::reconnectDisconnected() {
	for(auto i: frames) {
		if(!i->client->isConnected())
			i->reconnect();
	}
}

void HubFrame::resortUsers() {
	for(auto i: frames)
		i->resortForFavsFirst(true);
}

WindowParams HubFrame::getWindowParams() const {
	WindowParams ret;
	addRecentParams(ret);
	ret["Address"] = WindowParam(url, WindowParam::FLAG_IDENTIFIES);
	return ret;
}

void HubFrame::parseWindowParams(TabViewPtr parent, const WindowParams& params) {
	auto address = params.find("Address");
	if(address != params.end())
		openWindow(parent, address->second, parseActivateParam(params), params.find("NoConnect") == params.end());
}

bool HubFrame::isFavorite(const WindowParams& params) {
	auto i = params.find("Address");
	if(i != params.end())
		return FavoriteManager::getInstance()->isFavoriteHub(i->second);
	return false;
}

HubFrame::HubFrame(TabViewPtr parent, string&& url, bool connect) :
BaseType(parent, Text::toT(url), IDH_HUB, IDI_HUB_OFF, false),
paned(0),
userGrid(0),
users(0),
filter(usersColumns, COLUMN_LAST, [this] { updateUserList(); }),
filterOpts(0),
showUsers(0),
loginOverlay(0),
loginNick(0),
loginPassword(0),
loginSend(0),
client(0),
url(url),
selCount(0),
statusDirty(true),
waitingForPW(false),
resort(false),
confirmClose(true),
updateUsers(false),
currentUser(0),
hubMenu(false),
inTabComplete(false),
tabIcon(IDI_HUB)
{
	auto panedSeed = SplitterContainer::Seed(SETTING(HUB_PANED_POS));
	panedSeed.style |= WS_CLIPSIBLINGS;
	paned = addChild(panedSeed);

	createChat(paned);
	chat->setHelpId(IDH_HUB_CHAT);
	addWidget(chat);
	chat->onLink([this](const tstring& link) { return handleChatLink(link); });
	chat->onContextMenu([this](const dwt::ScreenCoordinate &sc) { return handleChatContextMenu(sc); });

	message->setHelpId(IDH_HUB_MESSAGE);
	addWidget(message, ALWAYS_FOCUS, false);
	addWidget(richTextButton, AUTO_FOCUS, false);
	message->onKeyDown([this](int c) { return handleMessageKeyDown(c); });
	message->onSysKeyDown([this](int c) { return handleMessageKeyDown(c); });
	message->onChar([this] (int c) { return handleMessageChar(c); });

	{
		userGrid = paned->addChild(Grid::Seed(2, 1));
		userGrid->column(0).mode = GridInfo::FILL;
		userGrid->row(0).mode = GridInfo::FILL;
		userGrid->row(0).align = GridInfo::STRETCH;

		users = userGrid->addChild(WidgetUsers::Seed(WinUtil::Seeds::table));
		addWidget(users);

		users->setSmallImageList(WinUtil::userImages);

		WinUtil::makeColumns(users, usersColumns, COLUMN_LAST, SETTING(HUBFRAME_ORDER), SETTING(HUBFRAME_WIDTHS));
		WinUtil::setTableSort(users, COLUMN_LAST, SettingsManager::HUBFRAME_SORT, COLUMN_NICK);

		users->onDblClicked([this] { handleDoubleClickUsers(); });
		users->onKeyDown([this](int c) { return handleUsersKeyDown(c); });
		users->onContextMenu([this](const dwt::ScreenCoordinate &sc) { return handleUsersContextMenu(sc); });

		filter.createTextBox(userGrid);
		filter.text->setHelpId(IDH_HUB_FILTER);
		filter.text->setCue(T_("Filter users"));
		addWidget(filter.text);
	}

	{
		auto seed = Grid::Seed(1, 2);
		seed.exStyle |= WS_EX_TRANSPARENT;
		filterOpts = addChild(seed);
		filterOpts->setHelpId(IDH_HUB_FILTER);

		filter.createColumnBox(filterOpts);
		addWidget(filter.column, AUTO_FOCUS, false);

		filter.createMethodBox(filterOpts);
		addWidget(filter.method, AUTO_FOCUS, false);

		hideFilterOpts(nullptr);

		filter.text->onFocus([this] { showFilterOpts(); });
		filter.text->onKillFocus([this](dwt::Widget* w) { hideFilterOpts(w); });
		filter.column->onKillFocus([this](dwt::Widget* w) { hideFilterOpts(w); });
		filter.method->onKillFocus([this](dwt::Widget* w) { hideFilterOpts(w); });
	}

	{
		auto overlaySeed = Grid::Seed(6, 4);
		overlaySeed.style |= WS_BORDER | WS_CLIPSIBLINGS;
		overlaySeed.style &= ~WS_VISIBLE;
		loginOverlay = addChild(overlaySeed);
		loginOverlay->setSpacing(6);
		loginOverlay->row(0).mode = GridInfo::STATIC;
		loginOverlay->row(0).size = 6;
		loginOverlay->row(5).mode = GridInfo::STATIC;
		loginOverlay->row(5).size = 6;
		loginOverlay->column(0).mode = GridInfo::STATIC;
		loginOverlay->column(0).size = 6;
		loginOverlay->column(1).align = GridInfo::BOTTOM_RIGHT;
		loginOverlay->column(2).mode = GridInfo::STATIC;
		loginOverlay->column(2).size = 220;
		loginOverlay->column(3).mode = GridInfo::STATIC;
		loginOverlay->column(3).size = 6;

		auto title = loginOverlay->addChild(Label::Seed(T_("Login required")));
		loginOverlay->setWidget(title, 1, 2);

		auto nickLabel = loginOverlay->addChild(Label::Seed(T_("Nick")));
		loginOverlay->setWidget(nickLabel, 2, 1);
		loginNick = loginOverlay->addChild(WinUtil::Seeds::textBox);
		loginOverlay->setWidget(loginNick, 2, 2);
		WinUtil::preventSpaces(loginNick);
		addWidget(loginNick, NO_FOCUS);

		auto passwordLabel = loginOverlay->addChild(Label::Seed(T_("Password")));
		loginOverlay->setWidget(passwordLabel, 3, 1);
		loginPassword = loginOverlay->addChild(WinUtil::Seeds::textBox);
		loginOverlay->setWidget(loginPassword, 3, 2);
		loginPassword->setPassword();
		addWidget(loginPassword, NO_FOCUS);

		auto sendSeed = WinUtil::Seeds::button;
		sendSeed.caption = T_("&Send");
		loginSend = loginOverlay->addChild(sendSeed);
		loginOverlay->setWidget(loginSend, 4, 2);
		addWidget(loginSend, NO_FOCUS);

		loginNick->onKeyDown([this](int c) { return handleLoginKeyDown(c); });
		loginPassword->onKeyDown([this](int c) { return handleLoginKeyDown(c); });
		loginSend->onClicked([this] { sendLogin(); });
	}

	initStatus();
	status->onClicked(STATUS_BBS, [this] { openBBS(); });
	status->setToolTip(STATUS_BBS, T_("Bulletin boards"));

	status->onDblClicked(STATUS_STATUS, [this] { openLog(false); });

	status->setIcon(STATUS_USERS, WinUtil::statusIcon(IDI_USERS));

	showUsers = addChild(WinUtil::Seeds::splitCheckBox);
	showUsers->setHelpId(IDH_HUB_SHOW_USERS);
	showUsers->setChecked(SETTING(GET_USER_INFO));
	status->setWidget(STATUS_SHOW_USERS, showUsers);

	status->setHelpId(STATUS_STATUS, IDH_HUB_STATUS);
	status->setHelpId(STATUS_SECURE, IDH_HUB_SECURE_STATUS);
	status->setHelpId(STATUS_USERS, IDH_HUB_USERS_COUNT);
	status->setHelpId(STATUS_SHARED, IDH_HUB_SHARED);
	status->setHelpId(STATUS_AVERAGE_SHARED, IDH_HUB_AVERAGE_SHARED);

	addAccel(FALT, 'G', [this] { handleGetList(getParent()); });
	addAccel(FCONTROL, 'R', [this] { reconnect(); });
	addAccel(FALT, 'P', [this] { handlePrivateMessage(getParent()); });
	addAccel(FALT, 'U', [this] { users->setFocus(); });
	addAccel(FALT, 'I', [this] { filter.text->setFocus(); });
	initAccels();

	layout();

	initTimer();

	client = ClientManager::getInstance()->getClient(url);
	client->addListener(this);
	BBSManager::getInstance()->addListener(this);
	updateRichTextAvailability();
	updateBBSAvailability();
	BBSFrame::attachHub(this);
	if(connect)
		client->connect();

	readLog(getLogPath(), SETTING(HUB_LAST_LOG_LINES));

	frames.push_back(this);

	showUsers->onClicked([this] { handleShowUsersClicked(); });

	FavoriteManager::getInstance()->addListener(this);

	addRecent();
}

HubFrame::~HubFrame() {
	ClientManager::getInstance()->putClient(client);
}

bool HubFrame::preClosing() {
	if(SETTING(CONFIRM_HUB_CLOSING) && confirmClose && !WinUtil::mainWindow->closing() &&
		dwt::MessageBox(this).show(getText() + _T("\n\n") + T_("Really close?"), _T(APPNAME) _T(" ") _T(VERSIONSTRING),
		dwt::MessageBox::BOX_YESNO, dwt::MessageBox::BOX_ICONQUESTION) != IDYES)
	{
		return false;
	}

	FavoriteManager::getInstance()->removeListener(this);
	BBSFrame::detachHub(this);
	BBSManager::getInstance()->removeListener(this);
	client->removeListener(this);
	disconnect(false);

	frames.erase(std::remove(frames.begin(), frames.end(), this), frames.end());
	return true;
}

void HubFrame::postClosing() {
	clearUserList();
	clearTaskList();

	SettingsManager::getInstance()->set(SettingsManager::GET_USER_INFO, showUsers->getChecked());

	SettingsManager::getInstance()->set(SettingsManager::HUB_PANED_POS, paned->getSplitterPos(0));

	SettingsManager::getInstance()->set(SettingsManager::HUBFRAME_ORDER, WinUtil::toString(users->getColumnOrder()));
	SettingsManager::getInstance()->set(SettingsManager::HUBFRAME_WIDTHS, WinUtil::toString(users->getColumnWidths()));
	SettingsManager::getInstance()->set(SettingsManager::HUBFRAME_SORT, WinUtil::getTableSort(users));
}

void HubFrame::layout() {
	const int border = 2;

	dwt::Rectangle r { getClientSize() };

	r.size.y -= status->refresh();

	dwt::util::HoldResize hr(this, 4);
	int ymessage = message->getTextSize(_T("A")).y * messageLines + 10;
	dwt::Rectangle rm(0, r.size.y - ymessage, r.width(), ymessage);
	if(richTextButton->getVisible()) {
		const auto preferred = richTextButton->getPreferredSize();
		const auto buttonWidth = std::min(preferred.x, std::max(0L, rm.width() / 3));
		auto messageRect = rm;
		messageRect.size.x = std::max(0L, rm.width() - buttonWidth - border);
		hr.resize(message, messageRect);
		const auto buttonHeight = std::min(preferred.y, rm.height());
		hr.resize(richTextButton, dwt::Rectangle(messageRect.width() + border,
			rm.y() + (rm.height() - buttonHeight) / 2, buttonWidth, buttonHeight));
	} else {
		hr.resize(message, rm);
	}

	r.size.y -= rm.size.y + border;
	hr.resize(paned, r);

	if(loginOverlay->getVisible()) {
		auto size = loginOverlay->getPreferredSize();
		size.x = std::min(size.x, r.width());
		size.y = std::min(size.y, r.height());
		hr.resize(loginOverlay, dwt::Rectangle(
			r.x() + (r.width() - size.x) / 2,
			r.y() + (r.height() - size.y) / 2,
			size.x,
			size.y));
	}

	if(showUsers->getChecked()) {
		userGrid->setVisible(true);
		paned->maximize(0);

		if(filterOpts->hasStyle(WS_VISIBLE)) {
			filterOpts->setZOrder(HWND_TOP);
			filter.column->setZOrder(HWND_TOP);
			filter.method->setZOrder(HWND_TOP);

			auto r = filter.text->getWindowRect();
			r.pos = dwt::ClientCoordinate(dwt::ScreenCoordinate(r.pos), filterOpts->getParent()).getPoint();
			r.pos.y += r.height();
			r.size = filterOpts->getPreferredSize();
			hr.resize(filterOpts, r);
		}

	} else {
		paned->maximize(chat);
		userGrid->setVisible(false);
	}

	if(loginOverlay->getVisible())
		loginOverlay->setZOrder(HWND_TOP);
}

void HubFrame::updateStatus() {
	auto users = getStatusUsers();
	status->setText(STATUS_USERS, users.second + Text::toT(std::to_string(users.first)));
	status->setToolTip(STATUS_USERS, users.second + str(TFN_("%1% user", "%1% users", static_cast<unsigned long>(users.first)) % users.first));

	auto shared = getStatusShared();
	status->setText(STATUS_SHARED, shared.first);
	status->setText(STATUS_AVERAGE_SHARED, shared.second);
}

void HubFrame::updateSecureStatus() {
	dwt::IconPtr icon;
	tstring text;
	if(client) {
		if(client->isTrusted()) {
			icon = WinUtil::statusIcon(IDI_TRUSTED);
			text = _T("[T] ");
		} else if(client->isSecure()) {
			icon = WinUtil::statusIcon(IDI_SECURE);
			text = _T("[U] ");
		}
		text += Text::toT(client->getCipherName());
	}
	status->setIcon(STATUS_SECURE, icon);
	status->setToolTip(STATUS_SECURE, text);
}

void HubFrame::updateRichTextAvailability() {
	const auto connected = client && client->isConnected();
	setChatAvailability(connected && client->supportsRichText(), connected,
		client ? client->getHubUrl() : url);
}

void HubFrame::updateBBSAvailability() {
	auto adc = dynamic_cast<AdcHub*>(client);
	const auto supported = adc && adc->supportsBBS();
	const auto cached = !BBSManager::getInstance()->getBoards(url).empty();
	const auto available = supported || cached;
	status->setText(STATUS_BBS, available ? _T("BBS") : Util::emptyStringT, true);
	status->setIcon(STATUS_BBS, available ? WinUtil::statusIcon(IDI_CHAT) : dwt::IconPtr(), true);
	status->setToolTip(STATUS_BBS, supported ? T_("Open bulletin boards") : cached ? T_("Open cached bulletin boards") : T_("Bulletin boards are unavailable"));
	BBSFrame::hubStateChanged(this);
}

void HubFrame::openBBS() {
	auto adc = dynamic_cast<AdcHub*>(client);
	if((!adc || !adc->supportsBBS()) && BBSManager::getInstance()->getBoards(url).empty()) {
		addStatus(T_("This hub has no available BBS0 boards."));
		return;
	}
	BBSFrame::openWindow(getParent(), url, this);
}

void HubFrame::initTimer() {
	setTimer([this] { return runTimer(); }, 500);
}

bool HubFrame::runTimer() {
	if(updateUsers) {
		updateUsers = false;
		callAsync([this] { execTasks(); });
	}

	auto prevSelCount = selCount;
	selCount = users->countSelected();
	if(statusDirty || selCount != prevSelCount) {
		statusDirty = false;
		updateStatus();
	}
	return true;
}

void HubFrame::enterImpl(const tstring& s) {
	bool resetText = true;
	bool send = false;

	// Process special commands
	if(s[0] == _T('/')) {
		tstring cmd = s;
		tstring param;
		tstring msg;
		tstring status;
		bool thirdPerson = false;

		if(PluginManager::getInstance()->onChatCommand(client, Text::fromT(s))) {
			// Plugins, chat commands

		} else if(WinUtil::checkCommand(cmd, param, msg, status, thirdPerson)) {
			if(!msg.empty()) {
				if(!client->hubMessage(Text::fromT(msg), thirdPerson)) {
					addStatus(T_("The message could not be sent because the hub connection changed."));
					resetText = false;
				}
			}
			if(!status.empty()) {
				addStatus(status);
			}
		} else if(ChatType::checkCommand(cmd, param, status)) {
			if(!status.empty()) {
				addStatus(status);
			}
		} else if(Util::stricmp(cmd.c_str(), _T("bbs")) == 0) {
			handleBBSCommand(param, resetText);
		} else if(Util::stricmp(cmd.c_str(), _T("rtf")) == 0) {
			if(param.empty()) {
				addStatus(T_("Usage: /rtf <message>"));
			} else if(!client->supportsRichText()) {
				addStatus(T_("RTF0 is disabled or unsupported by this hub. The message was not sent."));
				resetText = false;
			} else {
				auto richMessage = Text::fromT(param);
				if(!RichText::prepareOutgoingMessage(richMessage, true, client->getHubUrl())) {
					addStatus(T_("The RTF0 message has no formatting, exceeds the size limit, contains non-magnet inline media, or references an attachment that is not available in this hub's share. It was not sent."));
					resetText = false;
				} else {
					if(!client->hubMessage(richMessage, false, true)) {
						addStatus(T_("The RTF0 message or one of its attachments became unavailable before it could be sent."));
						resetText = false;
					}
				}
			}
		} else if(Util::stricmp(cmd.c_str(), _T("info")) == 0) {
			map<tstring, string> info;
			info[T_("Hub address")] = url;
			info[T_("Hub IP & port")] = client->getIpPort();
			info[T_("Online users")] = std::to_string(getUserCount());
			info[T_("Shared")] = Util::formatBytes(client->getAvailable());
			info[T_("Nick")] = client->get(HubSettings::Nick);
			info[T_("Description")] = client->get(HubSettings::Description);
			info[T_("Email")] = client->get(HubSettings::Email);
			info[T_("External / WAN IP")] = client->get(HubSettings::UserIp);
			tstring text;
			for(auto& i: info) {
				text += _T("\r\n") + i.first + _T(": ") + Text::toT(i.second);
			}
			addChat(_T("*** ") + text);
		} else if(Util::stricmp(cmd.c_str(), _T("join"))==0) {
			if(!param.empty()) {
				if(SETTING(JOIN_OPEN_NEW_WINDOW)) {
					HubFrame::openWindow(getParent(), Text::fromT(param));
				} else {
					redirect(Text::fromT(param));
				}
			} else {
				addStatus(T_("Specify a server to connect to"));
			}
		} else if( (Util::stricmp(cmd.c_str(), _T("password")) == 0) && waitingForPW ) {
			client->setPassword(Text::fromT(param));
			client->password(Text::fromT(param));
			waitingForPW = false;
			hideLoginOverlay();
		} else if( Util::stricmp(cmd.c_str(), _T("showjoins")) == 0 ) {
			client->get(HubSettings::ShowJoins) = !client->get(HubSettings::ShowJoins);
			if(client->get(HubSettings::ShowJoins)) {
				addStatus(T_("Join/part showing on"));
			} else {
				addStatus(T_("Join/part showing off"));
			}
		} else if( Util::stricmp(cmd.c_str(), _T("favshowjoins")) == 0 ) {
			client->get(HubSettings::FavShowJoins) = !client->get(HubSettings::FavShowJoins);
			if(client->get(HubSettings::FavShowJoins)) {
				addStatus(T_("Join/part of favorite users showing on"));
			} else {
				addStatus(T_("Join/part of favorite users showing off"));
			}
		} else if(Util::stricmp(cmd.c_str(), _T("close")) == 0) {
			close(true);
		} else if(Util::stricmp(cmd.c_str(), _T("userlist")) == 0) {
			showUsers->setChecked(!showUsers->getChecked());
			handleShowUsersClicked();
		} else if(Util::stricmp(cmd.c_str(), _T("conn")) == 0 || Util::stricmp(cmd.c_str(), _T("connection")) == 0) {
			addChat(_T("*** ") + Text::toT(ConnectivityManager::getInstance()->getInformation()));
		} else if((Util::stricmp(cmd.c_str(), _T("favorite")) == 0) || (Util::stricmp(cmd.c_str(), _T("fav")) == 0)) {
			addAsFavorite();
		} else if((Util::stricmp(cmd.c_str(), _T("removefavorite")) == 0) || (Util::stricmp(cmd.c_str(), _T("removefav")) == 0)) {
			removeFavoriteHub();
		} else if(Util::stricmp(cmd.c_str(), _T("getlist")) == 0) {
			if(!param.empty()) {
				auto ui = findUser(param);
				if(ui) {
					ui->getList(getParent());
				}
			}
		} else if(Util::stricmp(cmd.c_str(), _T("ignore")) == 0) {
			if(!param.empty()) {
				auto ui = findUser(param);
				if(ui) {
					ui->ignoreChat(true);
				}
			}
		} else if(Util::stricmp(cmd.c_str(), _T("unignore")) == 0) {
			if(!param.empty()) {
				auto ui = findUser(param);
				if(ui) {
					ui->ignoreChat(false);
				}
			}
		} else if(Util::stricmp(cmd.c_str(), _T("log")) == 0) {
			if(param.empty())
				openLog();
			else if(Util::stricmp(param.c_str(), _T("status")) == 0)
				openLog(true);
		} else if(Util::stricmp(cmd.c_str(), _T("topic")) == 0) {
			addChat(str(TF_("Current hub topic: %1%") % Text::toT(client->getHubDescription())));
		} else if(Util::stricmp(cmd.c_str(), _T("help")) == 0) {
			bool bShowBriefCommands = !param.empty() && (Util::stricmp(param.c_str(), _T("brief")) == 0);

			if(bShowBriefCommands)
			{
				addChat(T_("*** Keyboard commands:") + _T("\r\n") + 
						WinUtil::commands +
						_T(", /join <hub-ip>, /showjoins, /favshowjoins, /close, /userlist, ")
						_T("/conn[ection], /fav[orite], /removefav[orite], /info, ")
						_T("/pm <user> [message], /getlist <user>, /ignore <user>, /unignore <user>, ")
						_T("/log <status, system, downloads, uploads>, /rtf <message>, /bbs, /topic")
					   );
			}
			else
			{
				addChat(T_("*** Keyboard commands:") + _T("\r\n") + 
						WinUtil::getDescriptiveCommands()
						+ _T("\r\n") _T("/join <hub-ip>")
						+ _T("\r\n\t") + T_("Joins <hub-ip>. See also Open new window when using /join.")
						+ _T("\r\n") _T("/showjoins")
						+ _T("\r\n\t") + T_("Toggles the displaying of users joining the hub. This only takes effect for freshly-arriving users.")
						+ _T("\r\n") _T("/favshowjoins")
						+ _T("\r\n\t") + T_("Toggles the displaying of favorite users joining the hub. Does not require /showjoins to be enabled.")
						+ _T("\r\n") _T("/userlist")
						+ _T("\r\n\t") + T_("Toggles visibility of the list of users for the current hub.")
						+ _T("\r\n") _T("/connection")
						+ _T("\r\n") _T("/conn")
						+ _T("\r\n\t") + T_("Displays the connectivity status information, auto detected or manually chosen connection mode, IP and ports that DC++ is currently using for connections with all users.")
						+ _T("\r\n") _T("/favorite")
						+ _T("\r\n") _T("/fav")
						+ _T("\r\n\t") + T_("Adds the current hub (along with your nickname and password, if used) to the list of Favorite Hubs.")
						+ _T("\r\n") _T("/removefavorite")
						+ _T("\r\n") _T("/removefav")
						+ _T("\r\n\t") + T_("Removes the current hub from the list of Favorite Hubs.")
						+ _T("\r\n") _T("/pm <user> [message]")
						+ _T("\r\n\t") + T_("Opens a private message window to the user, and optionally sends the message, if one was specified.")
						+ _T("\r\n") _T("/getlist <user>")
						+ _T("\r\n\t") + T_("Adds the user's list to the Download Queue.")
						+ _T("\r\n") _T("/ignore <user>")
						+ _T("\r\n\t") + T_("Adds a user matching definition (or modifies an existing one, if possible) to ignore chat messages from the specified user.")
						+ _T("\r\n") _T("/unignore <user>")
						+ _T("\r\n\t") + T_("Adds a user matching definition (or modifies an existing one, if possible) to stop ignoring chat messages from the specified user.")
						+ _T("\r\n") _T("/topic")
						+ _T("\r\n\t") + T_("Prints the current hub's topic. Useful if you want to copy the topic or it contains a link you'd like to easily open.")
						+ _T("\r\n") _T("/rtf <message>")
						+ _T("\r\n\t") + T_("Sends CommonMark rich text when this hub has negotiated ADC RTF0. Attachments and inline media must use ADC magnet URIs.")
						+ _T("\r\n") _T("/bbs [help|<board>|read|post|reply|withdraw|subscribe|leave]")
						+ _T("\r\n\t") + T_("Lists and interacts with durable ADC BBS0 bulletin boards. Use /bbs help for syntax.")

					);
			}
			
		} else if(Util::stricmp(cmd.c_str(), _T("pm")) == 0) {
			string::size_type j = param.find(_T(' '));
			if(j != string::npos) {
				tstring nick = param.substr(0, j);
				UserInfo* ui = findUser(nick);

				if(ui) {
					if(param.size() > j + 1)
						PrivateFrame::openWindow(getParent(), HintedUser(ui->getUser(), url), param.substr(j+1));
					else
						PrivateFrame::openWindow(getParent(), HintedUser(ui->getUser(), url), Util::emptyStringT);
				}
			} else if(!param.empty()) {
				UserInfo* ui = findUser(param);
				if(ui) {
					PrivateFrame::openWindow(getParent(), HintedUser(ui->getUser(), url), Util::emptyStringT);
				}
			}

		} else if(SETTING(SEND_UNKNOWN_COMMANDS)) {
			send = true;
		} else {
			addStatus(str(TF_("Unknown command: %1%") % cmd));
		}

	} else if(waitingForPW) {
		addStatus(T_("Don't remove /password before your password"));
		message->setText(_T("/password "));
		message->setFocus();
		message->setSelection(10, 10);
		resetText = false;

	} else {
		send = true;
	}

	if(send) {
		if(client->isConnected()) {
			if(!client->hubMessage(Text::fromT(s))) {
				message->showPopup(T_("Message not sent"),
					T_("The message could not be delivered because the hub connection changed."), TTI_ERROR);
				resetText = false;
			}
		} else {
			message->showPopup(T_("Hub offline"), T_("The message cannot be delivered because the hub is offline."), TTI_ERROR);
			resetText = false;
		}
	}
	if(resetText) {
		message->setText(Util::emptyStringT);
	}
}

void HubFrame::handleBBSCommand(const tstring& parameter, bool& resetText) {
	auto adc = dynamic_cast<AdcHub*>(client);
	if(!adc) {
		addStatus(T_("BBS0 is available only on ADC hubs."));
		return;
	}

	string input = Text::fromT(parameter);
	Util::trim(input);
	auto takeWord = [](string& value) {
		const auto end = value.find_first_of(" \t\r\n");
		string word = end == string::npos ? value : value.substr(0, end);
		value = end == string::npos ? Util::emptyString : value.substr(end + 1);
		Util::trim(value);
		return word;
	};
	auto splitPost = [](const string& value, string& subject, string& body) {
		const auto separator = value.find('|');
		if(separator == string::npos) return false;
		subject = value.substr(0, separator);
		body = value.substr(separator + 1);
		Util::trim(subject);
		while(!body.empty() && (body.front() == ' ' || body.front() == '\t')) body.erase(body.begin());
		return true;
	};

	string action = input.empty() ? "list" : takeWord(input);
	if(action == "help") {
		addChat(T_("*** BBS0 commands:\r\n")
			+ _T("/bbs — list boards\r\n")
			+ _T("/bbs <board> — list the board's cached index\r\n")
			+ _T("/bbs read <board> <TTH> — fetch and verify a post on demand\r\n")
			+ _T("/bbs post <board> <subject> | <body> — start a plain-text thread\r\n")
			+ _T("/bbs rtfpost <board> <subject> | <body> — start an RTF0 thread\r\n")
			+ _T("/bbs reply <board> <parent-TTH> <subject> | <body> — reply\r\n")
			+ _T("/bbs rtfreply <board> <parent-TTH> <subject> | <body> — rich-text reply\r\n")
			+ _T("/bbs withdraw <board> <TTH> — withdraw an indexed post\r\n")
			+ _T("/bbs subscribe <board> [timestamp] — replace the subscription cursor\r\n")
			+ _T("/bbs leave <board> — cancel the live subscription"));
		return;
	}
	if(action == "list") {
		showBBSBoards();
		return;
	}

	const bool knownAction = action == "read" || action == "post" || action == "rtfpost" ||
		action == "reply" || action == "rtfreply" || action == "withdraw" ||
		action == "subscribe" || action == "leave" || action == "entry";
	if(!knownAction) {
		showBBSBoard(action);
		return;
	}
	if(!adc->supportsBBS()) {
		addStatus(T_("This connection has not negotiated BBS0. Cached indexes remain available with /bbs <board>."));
		resetText = false;
		return;
	}

	string error;
	if(action == "read") {
		const auto board = takeWord(input);
		const auto tth = takeWord(input);
		if(board.empty() || tth.empty()) {
			addStatus(T_("Usage: /bbs read <board> <TTH>"));
			return;
		}
		if(adc->fetchBBS(board, tth, error)) {
			bbsChatReads.insert(board + '\n' + tth);
			addStatus(T_("Looking for the post by exact TTH. Reading may be visible to peers that serve it."));
		}
	} else if(action == "entry") {
		const auto board = takeWord(input);
		const auto tth = takeWord(input);
		if(board.empty() || tth.empty()) {
			addStatus(T_("Usage: /bbs entry <board> <TTH>"));
			return;
		}
		adc->requestBBSEntry(board, tth, error);
	} else if(action == "post" || action == "rtfpost") {
		const auto board = takeWord(input);
		string subject, body;
		if(board.empty() || !splitPost(input, subject, body) || subject.empty()) {
			addStatus(T_("Usage: /bbs post <board> <subject> | <body>"));
			return;
		}
		if(adc->postBBS(board, Util::emptyString, subject, body, action == "rtfpost", error)) {
			addStatus(T_("BBS post submitted; it is not published until the hub returns its IBBL entry."));
			if(body.find("magnet:?") != string::npos) addStatus(T_("BBS attachments may be requested long after their original source leaves."));
		}
	} else if(action == "reply" || action == "rtfreply") {
		const auto board = takeWord(input);
		const auto parent = takeWord(input);
		string subject, body;
		if(board.empty() || parent.empty() || !splitPost(input, subject, body)) {
			addStatus(T_("Usage: /bbs reply <board> <parent-TTH> <subject> | <body>"));
			return;
		}
		if(adc->postBBS(board, parent, subject, body, action == "rtfreply", error)) {
			addStatus(T_("BBS reply submitted; it is not published until the hub returns its IBBL entry."));
			if(body.find("magnet:?") != string::npos) addStatus(T_("BBS attachments may be requested long after their original source leaves."));
		}
	} else if(action == "withdraw") {
		const auto board = takeWord(input);
		const auto tth = takeWord(input);
		if(board.empty() || tth.empty()) {
			addStatus(T_("Usage: /bbs withdraw <board> <TTH>"));
			return;
		}
		if(adc->withdrawBBS(board, tth, error)) {
			addStatus(T_("Withdrawal requested. Existing peer copies cannot be deleted."));
		}
	} else if(action == "subscribe") {
		const auto board = takeWord(input);
		const auto timestampText = takeWord(input);
		uint64_t timestamp = 0;
		if(!timestampText.empty()) {
			if(!std::all_of(timestampText.begin(), timestampText.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) {
				addStatus(T_("The BBS subscription timestamp must be a non-negative integer."));
				return;
			}
			timestamp = static_cast<uint64_t>(Util::toInt64(timestampText));
		}
		adc->subscribeBBS(board, timestamp, error);
	} else if(action == "leave") {
		adc->unsubscribeBBS(takeWord(input), error);
	}

	if(!error.empty()) {
		addStatus(Text::toT(error));
		resetText = false;
	}
}

void HubFrame::showBBSBoards() {
	const auto boards = BBSManager::getInstance()->getBoards(url);
	if(boards.empty()) {
		addStatus(T_("No cached BBS0 boards are available for this hub."));
		return;
	}

	tstring output = T_("*** Bulletin boards:");
	for(const auto& board: boards) {
		const auto title = BBSManager::sanitizeDisplayText(board.title.empty() ? board.name : board.title);
		output += _T("\r\n") + Text::toT(board.name) + _T(" — ") + Text::toT(title);
		output += board.subscribed ? T_(" [subscribed]") : T_(" [not subscribed]");
		if(board.gap) output += T_(" [history gap]");
		if(board.postCount >= 0) output += str(TF_(" — %1% posts") % board.postCount);
	}
	output += T_("\r\nUse /bbs <board> to list its cached index or /bbs help for commands.");
	addChat(output);
}

void HubFrame::showBBSBoard(const string& boardName) {
	auto board = BBSManager::getInstance()->getBoard(url, boardName);
	if(!board) {
		addStatus(T_("No such cached BBS board."));
		return;
	}
	auto entries = BBSManager::getInstance()->getEntries(url, boardName);
	tstring output = _T("*** ") + Text::toT(BBSManager::sanitizeDisplayText(
		board->title.empty() ? board->name : board->title));
	if(!board->description.empty()) output += _T(" — ") + Text::toT(BBSManager::sanitizeDisplayText(board->description, 1024));
	if(entries.empty()) {
		output += T_("\r\nNo posts are cached in this board's index.");
		addChat(output);
		return;
	}

	const auto first = entries.size() > 50 ? entries.size() - 50 : 0;
	for(size_t i = first; i < entries.size(); ++i) {
		const auto& entry = entries[i];
		auto document = BBSManager::getInstance()->getDocument(entry.tth);
		const auto subject = BBSManager::sanitizeDisplayText(document ? document->subject : entry.subject);
		const auto nick = BBSManager::sanitizeDisplayText(entry.nick.empty() ? entry.authorId : entry.nick, 128);
		output += _T("\r\n") + Text::toT(entry.parent.empty() ? "• " : "↳ ") + Text::toT(subject.empty() ? _("(no subject)") : subject);
		output += _T(" — ") + Text::toT(nick) + _T(" — ") + Text::toT(entry.tth);
		if(!document) output += T_(" [unverified metadata]");
	}
	if(first != 0) output += str(TF_("\r\n(%1% older entries omitted from this view)") % first);
	output += T_("\r\nUse /bbs read <board> <TTH> to fetch and verify a post.");
	addChat(output);
}

void HubFrame::showBBSDocument(const string& board, const string& tth) {
	auto entry = BBSManager::getInstance()->getEntry(url, board, tth);
	auto document = BBSManager::getInstance()->getDocument(tth);
	if(!entry || !document || entry->withdrawn) return;

	const auto subject = BBSManager::sanitizeDisplayText(document->subject.empty() ? entry->subject : document->subject);
	const auto nick = BBSManager::sanitizeDisplayText(entry->nick.empty() ? entry->authorId : entry->nick, 128);
	string attribution = nick + " [hub CID " + entry->authorId + "]";
	if(document->authorId != entry->authorId) {
		attribution += " — document claims CID " + document->authorId;
	} else {
		attribution += " — document claim matches the hub submitter CID";
	}

	string indexedTime = _("unknown time");
	const auto now = static_cast<uint64_t>(time(nullptr));
	if(entry->timestamp <= now + 24 * 60 * 60) {
		indexedTime = Util::formatTime("%Y-%m-%d %H:%M:%S", static_cast<time_t>(entry->timestamp));
	} else {
		indexedTime = _("unknown time (implausible hub timestamp)");
	}

	string tmp;
	string boardHtml = board;
	SimpleXML::escape(boardHtml, false);
	string subjectHtml = subject;
	SimpleXML::escape(subjectHtml, false);
	string attributionHtml = indexedTime + " — " + attribution;
	SimpleXML::escape(attributionHtml, false);
	string html = "<span id=\"systemMessage\" style=\"white-space: pre-wrap;\"><b>[BBS " +
		boardHtml + "] " + subjectHtml + "</b><br/>" + attributionHtml + "<br/><br/>";
	if(document->richText == 1 && SETTING(ENABLE_RICH_TEXT)) {
		auto rich = RichText::parse(document->body,
			static_cast<size_t>(std::max(1, SETTING(CHAT_LINK_MAX_LENGTH))));
		html += rich.valid ? rich.html : SimpleXML::escape(document->body, tmp, false);
	} else {
		html += SimpleXML::escape(document->body, tmp, false);
	}
	html += "</span>";

	const auto plain = _T("[BBS ") + Text::toT(board) + _T("] ") + Text::toT(subject) + _T("\r\n") +
		Text::toT(indexedTime + " — " + attribution) + _T("\r\n\r\n") + Text::toT(document->body);
	addChatHTML(html, plain, Text::toT(nick), entry->authorId, static_cast<time_t>(entry->timestamp));
}

void HubFrame::clearUserList() {
	users->clear();
	for(auto& i: userMap) {
		delete i.second;
	}
	currentUser = 0;
	userMap.clear();
}

void HubFrame::clearTaskList() {
	tasks.clear();
}

void HubFrame::addedChat(const tstring& message) {
	{
		auto u = url;
		WinUtil::notify(WinUtil::NOTIFICATION_MAIN_CHAT, message, [this, u] { activateWindow(u); });
	}
	setDirty(SettingsManager::BOLD_HUB);

	if(client->get(HubSettings::LogMainChat)) {
		ParamMap params;
		params["message"] = [&message] { return Text::toDOS(Text::fromT(message)); };
		client->getHubIdentity().getParams(params, "hub", false);
		params["hubURL"] = [this] { return client->getHubUrl(); };
		client->getMyIdentity().getParams(params, "my", true);
		LOG(LogManager::CHAT, params);
	}
}

void HubFrame::addStatus(const tstring& text, bool legitimate /* = true */, bool inert /* = false */) {
	status->setText(STATUS_STATUS, Text::toT("[" + Util::getShortTimeString() + "] ") + text);

	if(legitimate) {
		if(SETTING(STATUS_IN_CHAT)) {
			const auto message = _T("*** ") + text;
			if(inert) {
				addChatPlain(message);
				addedChat(message);
			} else {
				addChat(message);
			}
		} else {
			setDirty(SettingsManager::BOLD_HUB);
		}
	}

	if(SETTING(LOG_STATUS_MESSAGES)) {
		ParamMap params;
		client->getHubIdentity().getParams(params, "hub", false);
		params["hubURL"] = [this] { return client->getHubUrl(); };
		client->getMyIdentity().getParams(params, "my", true);
		params["message"] = [&text] { return Text::fromT(text); };
		LOG(LogManager::STATUS, params);
	}
}

void HubFrame::execTasks() {
	updateUsers = false;

	HoldRedraw hold { users };

	for(auto& task: tasks) {
		task.first(*task.second);
	}
	tasks.clear();

	if(resort && showUsers->getChecked()) {
		users->resort();
		resort = false;
	}
}

void HubFrame::onConnected() {
	addStatus(T_("Connected"));
	setIcon(IDI_HUB);
	updateSecureStatus();
	updateRichTextAvailability();
	updateBBSAvailability();
}

void HubFrame::onDisconnected() {
	waitingForPW = false;
	hideLoginOverlay();
	clearUserList();
	clearTaskList();
	setIcon(IDI_HUB_OFF);
	updateSecureStatus();
	updateRichTextAvailability();
	updateBBSAvailability();
}

void HubFrame::onGetPassword() {
	if(!client->getPassword().empty()) {
		waitingForPW = false;
		hideLoginOverlay();
		client->password(client->getPassword());
		addStatus(T_("Stored password sent..."));
	} else if(!waitingForPW) {
		waitingForPW = true;
		if(SETTING(PROMPT_PASSWORD)) {
			showLoginOverlay();
		} else {
			message->setText(_T("/password "));
			message->setFocus();
			message->setSelection(10, 10);
		}
	}
}

void HubFrame::showLoginOverlay() {
	loginNick->setText(Text::toT(client->get(HubSettings::Nick)));
	loginNick->setSelection();
	loginPassword->setText(Util::emptyStringT);
	loginOverlay->setVisible(true);
	loginOverlay->setZOrder(HWND_TOP);
	layout();
	loginOverlay->redrawWindow(RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);

	if(loginNick->getText().empty()) {
		loginNick->setFocus();
	} else {
		loginPassword->setFocus();
	}
}

void HubFrame::hideLoginOverlay() {
	loginPassword->setText(Util::emptyStringT);
	loginOverlay->setVisible(false);
}

void HubFrame::sendLogin() {
	if(!waitingForPW)
		return;

	auto nick = loginNick->getText();
	if(nick.empty()) {
		loginNick->showPopup(T_("Nick required"), T_("Please enter a nick."), TTI_ERROR);
		loginNick->setFocus();
		return;
	}

	auto password = loginPassword->getText();
	if(password.empty()) {
		loginPassword->showPopup(T_("Password required"), T_("Please enter a password."), TTI_ERROR);
		loginPassword->setFocus();
		return;
	}

	client->get(HubSettings::Nick) = Text::fromT(nick);
	client->setPassword(Text::fromT(password));
	client->password(Text::fromT(password));
	waitingForPW = false;
	hideLoginOverlay();
}

bool HubFrame::handleLoginKeyDown(int c) {
	if(c == VK_RETURN && !(WinUtil::isShift() || WinUtil::isCtrl() || WinUtil::isAlt())) {
		sendLogin();
		return true;
	}
	return false;
}

void HubFrame::onPrivateMessage(const ChatMessage& message) {
	bool fromHub = false, fromBot = false;
	{
		auto lock = ClientManager::getInstance()->lock();
		auto ou = ClientManager::getInstance()->findOnlineUserHint(message.replyTo->getCID(), url);
		if(ou && ou->getIdentity().isHub())
			fromHub = true;
		if(ou && ou->getIdentity().isBot())
			fromBot = true;
	}

	bool ignore = false, window = false;
	if(fromHub) {
		if(SETTING(IGNORE_HUB_PMS)) {
			ignore = true;
		} else if(SETTING(POPUP_HUB_PMS) || PrivateFrame::isOpen(message.replyTo)) {
			window = true;
		}
	} else if(fromBot) {
		if(SETTING(IGNORE_BOT_PMS)) {
			ignore = true;
		} else if(SETTING(POPUP_BOT_PMS) || PrivateFrame::isOpen(message.replyTo)) {
			window = true;
		}
	} else if(SETTING(POPUP_PMS) || PrivateFrame::isOpen(message.replyTo) || message.from == client->getMyIdentity().getUser()) {
		window = true;
	}

	if(ignore) {
		addStatus(str(TF_("Ignored message: %1%") % Text::toT(message.message)), false);

	} else {
		if(window && !PrivateFrame::gotMessage(getParent(), message, url, fromBot)) {
			window = false;
			addStatus(T_("Failed to create a new PM window; check the \"Max PM windows\" value in Settings > Experts only"));
		}
		if(!window) {
			/// @todo add formatting here (for PMs in main chat)
			addChat(str(TF_("Private message from %1%: %2%") % getNick(message.from) % Text::toT(message.message)));
		}
		WinUtil::mainWindow->TrayPM();
	}
}

HubFrame::UserInfo* HubFrame::findUser(const tstring& nick) {
	for(auto& i: userMap) {
		if(i.second->getText(COLUMN_NICK) == nick)
			return i.second;
	}
	return nullptr;
}

const tstring& HubFrame::getNick(const UserPtr& aUser) {
	auto i = userMap.find(aUser);
	if(i == userMap.end())
		return Util::emptyStringT;

	UserInfo* ui = i->second;
	return ui->getText(COLUMN_NICK);
}

bool HubFrame::updateUser(const UserTask& u) {
	auto i = userMap.find(u.user);
	if(i == userMap.end()) {
		UserInfo* ui = new UserInfo(u);
		userMap.emplace(u.user, ui);
		if(!ui->isHidden() && showUsers->getChecked())
			users->insert(ui);

		if(!filter.empty())
			updateUserList(ui);
		return true;
	} else {
		UserInfo* ui = i->second;
		if(!ui->isHidden() && u.identity.isHidden() && showUsers->getChecked()) {
			users->erase(ui);
		}

		resort = ui->update(u.identity, users->getSortColumn()) || resort;
		if(showUsers->getChecked()) {
			users->update(ui);
			updateUserList(ui);
		}

		return false;
	}
}

bool HubFrame::UserInfo::update(const Identity& identity, int sortCol) {
	bool needsSort = (getIdentity().isOp() != identity.isOp());
	tstring old;
	if(sortCol != -1)
		old = columns[sortCol];

	columns[COLUMN_NICK] = Text::toT(identity.getNick());
	columns[COLUMN_SHARED] = Text::toT(Util::formatBytes(identity.getBytesShared()));
	columns[COLUMN_DESCRIPTION] = Text::toT(identity.getDescription());
	columns[COLUMN_TAG] = Text::toT(identity.getTag());
	columns[COLUMN_CONNECTION] = Text::toT(identity.getConnection());
	// Show the addy where we actually connect to
	columns[COLUMN_IP] = Text::toT(CONNSTATE(INCOMING_CONNECTIONS6) ? identity.getIp() : identity.getIp4());
	columns[COLUMN_COUNTRY] = Text::toT(identity.getCountry());
	columns[COLUMN_EMAIL] = Text::toT(identity.getEmail());
	columns[COLUMN_CID] = Text::toT(identity.getUser()->getCID().toBase32());

	if(sortCol != -1) {
		needsSort = needsSort || (old != columns[sortCol]);
	}

	setIdentity(identity);
	return needsSort;
}

void HubFrame::removeUser(const UserPtr& aUser) {
	auto i = userMap.find(aUser);
	dcassert(i != userMap.end());

	auto ui = i->second;
	if(!ui->isHidden() && showUsers->getChecked())
		users->erase(ui);

	userMap.erase(i);
	if(ui == currentUser)
		currentUser = 0;
	delete ui;
}

bool HubFrame::handleUsersKeyDown(int c) {
	if(c == VK_RETURN && users->hasSelected()) {
		handleGetList(getParent());
		return true;
	}
	return false;
}

bool HubFrame::handleMessageChar(int c) {
	switch(c) {
	case VK_TAB: return true; break;
	}

	return ChatType::handleMessageChar(c);
}

bool HubFrame::handleMessageKeyDown(int c) {
	if(!complete.empty() && c != VK_TAB)
		complete.clear(), inTabComplete = false;

	switch(c) {
	case VK_TAB:
		if(tab())
			return true;
		break;
	}

	return ChatType::handleMessageKeyDown(c);
}

int HubFrame::UserInfo::getImage(int col) const {
	if(col != 0) {
		return -1;
	}

	int image = identity.isBot() ? WinUtil::USER_ICON_BOT : identity.isAway() ? WinUtil::USER_ICON_AWAY : WinUtil::USER_ICON;
	image *= WinUtil::USER_ICON_MOD_START * WinUtil::USER_ICON_MOD_START;

	if(CONNSETTING(INCOMING_CONNECTIONS) == SettingsManager::INCOMING_PASSIVE &&
		!identity.isBot() && !identity.isTcpActive() && !identity.supports(AdcHub::NAT0_FEATURE))
	{
		// Users we can't connect to
		image += 1 << (WinUtil::USER_ICON_NOCON - WinUtil::USER_ICON_MOD_START);
	}

	string freeSlots = identity.get("FS");
	if(!freeSlots.empty() && Util::toUInt(freeSlots) == 0) {
		image += 1 << (WinUtil::USER_ICON_NOSLOT - WinUtil::USER_ICON_MOD_START);
	}

	if(identity.isOp()) {
		image += 1 << (WinUtil::USER_ICON_OP - WinUtil::USER_ICON_MOD_START);
	}

	return image;
}

int HubFrame::UserInfo::getStyle(HFONT& font, COLORREF& textColor, COLORREF& bgColor, int) const {
	auto style = identity.getStyle();

	if(!style.font.empty()) {
		auto cached = WinUtil::getUserMatchFont(style.font);
		if(cached.get()) {
			font = cached->handle();
		}
	}

	if(style.textColor != -1) {
		textColor = style.textColor;
	}

	if(style.bgColor != -1) {
		bgColor = style.bgColor;
	}

	return CDRF_NEWFONT;
}

HubFrame::UserTask::UserTask(const OnlineUser& ou) :
user(ou),
identity(ou.getIdentity())
{
}

int HubFrame::UserInfo::compareItems(const HubFrame::UserInfo* a, const HubFrame::UserInfo* b, int col) {
	if(col == COLUMN_NICK) {
		bool a_isOp = a->getIdentity().isOp(),
			b_isOp = b->getIdentity().isOp();
		if(a_isOp && !b_isOp)
			return -1;
		if(!a_isOp && b_isOp)
			return 1;
		if(SETTING(SORT_FAVUSERS_FIRST)) {
			bool a_isFav = FavoriteManager::getInstance()->isFavoriteUser(a->getIdentity().getUser()),
				b_isFav = FavoriteManager::getInstance()->isFavoriteUser(b->getIdentity().getUser());
			if(a_isFav && !b_isFav)
				return -1;
			if(!a_isFav && b_isFav)
				return 1;
		}
	}
	if(col == COLUMN_SHARED) {
		return compare(a->identity.getBytesShared(), b->identity.getBytesShared());;
	}
	return compare(a->columns[col], b->columns[col]);
}

void HubFrame::on(Connecting, Client*) noexcept {
	auto hubUrl = client->getConnectionUrl();
	callAsync([this, hubUrl] {
		clearUserList();
		clearTaskList();
		addStatus(str(TF_("Connecting to %1%...") % Text::toT(Util::addBrackets(hubUrl))));
		setText(Text::toT(hubUrl));
	});
}

void HubFrame::on(Connected, Client* aClient) noexcept {
	const auto failover = aClient->isUsingFailover();
	const auto hubUrl = aClient->getConnectionUrl();
	callAsync([this, failover, hubUrl] {
		onConnected();
		if(failover) {
			addStatus(str(TF_("Connected to failover hub address %1%") %
				Text::toT(Util::addBrackets(hubUrl))));
		}
		setTabIcon();
	});
}

void HubFrame::on(ClientListener::UserUpdated, Client*, const OnlineUser& user) noexcept {

	if(user.getIdentity().isSelf())
	{
		callAsync([this] { setTabIcon(); });
	}

	auto task = new UserTask(user);
	callAsync([this, task] {
		tasks.emplace_back([=](const UserTask& u) {
			if(updateUser(u)) {
				if(client->get(HubSettings::ShowJoins) ||
					(client->get(HubSettings::FavShowJoins) && FavoriteManager::getInstance()->isFavoriteUser(u.user)))
				{
					addStatus(str(TF_("Joins: %1%") % Text::toT(u.identity.getNick())));
				}
			}
		}, unique_ptr<UserTask>(task));
		updateUsers = true;
	});
}

void HubFrame::on(UsersUpdated, Client*, const OnlineUserList& aList) noexcept {
	for(auto& i: aList) {
		if(i->getIdentity().isSelf())
		{
			callAsync([this] { setTabIcon(); });
		}

		auto task = new UserTask(*i);
		callAsync([this, task] { tasks.emplace_back([=](const UserTask& u) { updateUser(u); }, unique_ptr<UserTask>(task)); });
	}
	callAsync([this] { updateUsers = true; });
}

void HubFrame::on(ClientListener::UserRemoved, Client*, const OnlineUser& user) noexcept {
	auto task = new UserTask(user);
	callAsync([this, task] {
		tasks.emplace_back([=](const UserTask& u) {
			removeUser(u.user);
			if(client->get(HubSettings::ShowJoins) ||
				(client->get(HubSettings::FavShowJoins) && FavoriteManager::getInstance()->isFavoriteUser(u.user)))
			{
				addStatus(str(TF_("Parts: %1%") % Text::toT(u.identity.getNick())));
			}
		}, unique_ptr<UserTask>(task));
		updateUsers = true;
	});
}

void HubFrame::on(Redirect, Client*, const string& line) noexcept {
	if(ClientManager::getInstance()->isConnected(line)) {
		callAsync([this] { addStatus(T_("Redirect request received to a hub that's already connected"), true); });
		return;
	}
	callAsync([this, line] {
		if(SETTING(AUTO_FOLLOW)) {
			auto copy = line; /// @todo shouldn't the lambda have already created a copy?
			redirect(std::move(copy));
		} else {
			string msg = str(F_("Received a redirect request to %1%, click here to follow it") % Util::addBrackets(line));
			tstring msgT = Text::toT(msg);
			string tmp;
			addStatus(msgT, false);
			/// @todo change to "javascript: external.redirect" when switching to an HTML control
			addChatHTML("<span>*** </span><a href=\"redirect: " + SimpleXML::escape(line, tmp, true) + "\">" +
				SimpleXML::escape(msg, tmp, false) + "</a>");
			addedChat(_T("*** ") + msgT);
		}
	});
}

void HubFrame::on(Failed, Client*, const string& line) noexcept {
	callAsync([this, line] {
		addStatus(Text::toT(line));
		onDisconnected();
	});
}

void HubFrame::on(GetPassword, Client*) noexcept {
	callAsync([this] { onGetPassword(); });
}

void HubFrame::on(HubUpdated, Client*) noexcept {
	string hubName = client->getHubName();
	if(!client->getHubDescription().empty()) {
		hubName += " - " + client->getHubDescription();
	}
	hubName += " (" + client->getHubUrl() + ")";
#ifdef _DEBUG
	auto application = client->getHubIdentity().getApplication();
	if(!application.empty()) {
		hubName += " - " + application;
	}
#endif
	tstring hubNameT = Text::toT(hubName);
	callAsync([this, hubNameT] {
		setText(hubNameT);
		updateRichTextAvailability();
		updateBBSAvailability();
	});
}

void HubFrame::on(Message, Client*, const ChatMessage& message) noexcept {
	callAsync([this, message] {
		if(message.to.get() && message.replyTo.get()) {
			onPrivateMessage(message);
		} else {
			addChat(message);
		}
	});
}

void HubFrame::on(StatusMessage, Client*, const string& line, int statusFlags) noexcept {
	onStatusMessage(line, statusFlags);
}

void HubFrame::on(NickTaken, Client*) noexcept {
	callAsync([this] { addStatus(T_("Your nick was already taken, please change to something else!"), true); });
}

void HubFrame::on(SearchFlood, Client*, const string& line) noexcept {
	callAsync([=] { onStatusMessage(str(F_("Search spam detected from %1%") % line), ClientListener::FLAG_IS_SPAM); });
}

void HubFrame::on(ClientLine, Client*, const string& line, int type) noexcept {
	if(type == MSG_STATUS) {
		callAsync([=] { onStatusMessage(line, ClientListener::FLAG_IS_SPAM); });
	} else if(type == MSG_SYSTEM) {
		callAsync([=] { onStatusMessage(line, ClientListener::FLAG_NORMAL); });
	} else {
		callAsync([=] { addChat(Text::toT(line)); });
	}
}

void HubFrame::on(BBSManagerListener::BoardUpdated, const string& hubUrl, const string&) noexcept {
	if(!hubHintsEqual(hubUrl, url)) return;
	callAsync([this] { setDirty(SettingsManager::BOLD_HUB); updateBBSAvailability(); });
}

void HubFrame::on(BBSManagerListener::EntryUpdated, const string& hubUrl, const string& board, const string& tth) noexcept {
	if(!hubHintsEqual(hubUrl, url)) return;
	callAsync([this, board, tth] {
		setDirty(SettingsManager::BOLD_HUB);
		auto entry = BBSManager::getInstance()->getEntry(url, board, tth);
		auto document = BBSManager::getInstance()->getDocument(tth);
		if(entry && document && !entry->withdrawn && client->getMyIdentity().getUser() &&
			entry->authorId == client->getMyIdentity().getUser()->getCID().toBase32() &&
			entry->timestamp <= static_cast<uint64_t>(time(nullptr) + 120) &&
			entry->timestamp + 120 >= static_cast<uint64_t>(time(nullptr)))
		{
			addStatus(Text::toT(str(F_("BBS post accepted and published: %1%") % tth)));
		}
	});
}

void HubFrame::on(BBSManagerListener::DocumentUpdated, const string& hubUrl, const string& board, const string& tth) noexcept {
	if(!hubHintsEqual(hubUrl, url)) return;
	callAsync([this, board, tth] {
		if(bbsChatReads.erase(board + '\n' + tth) != 0) showBBSDocument(board, tth);
	});
}

void HubFrame::on(BBSManagerListener::Status, const string& hubUrl, const string& line) noexcept {
	if(!hubHintsEqual(hubUrl, url)) return;
	callAsync([this, line] { addStatus(Text::toT(line)); });
}

void HubFrame::on(BBSManagerListener::SupportUpdated, const string& hubUrl, bool) noexcept {
	if(!hubHintsEqual(hubUrl, url)) return;
	callAsync([this] { updateBBSAvailability(); });
}


void HubFrame::onStatusMessage(const string& line, int flags) {
	callAsync([=] { addStatus(Text::toT(line),
		!(flags & ClientListener::FLAG_IS_SPAM) || !SETTING(FILTER_MESSAGES),
		(flags & ClientListener::FLAG_IS_PROTOCOL_SPOOF) != 0); });
}

size_t HubFrame::getUserCount() const {
	size_t userCount = 0;
	for(auto& i: userMap) {
		if(!i.second->isHidden()) {
			++userCount;
		}
	}
	return userCount;
}

pair<size_t, tstring> HubFrame::getStatusUsers() const {
	auto userCount = getUserCount();

	tstring textForUsers;
	if(selCount > 1)
		textForUsers += Text::toT(std::to_string(selCount) + "/");
	auto filteredCount = users->size();
	if(showUsers->getChecked() && filteredCount < userCount)
		textForUsers += Text::toT(std::to_string(filteredCount) + "/");

	return make_pair(userCount, textForUsers);
}

pair<tstring, tstring> HubFrame::getStatusShared() const {
	int64_t available;
	size_t userCount = 0;
	if(selCount > 1) {
		available = users->forEachSelectedT(CountAvailable()).available;
		userCount = selCount;
	} else {
		available = users->forEachT(CountAvailable()).available;
		userCount = users->size();
	}

	return make_pair(Text::toT(Util::formatBytes(available)),
		str(TF_("Average: %1%") % Text::toT(Util::formatBytes(userCount > 0 ? available / userCount : 0))));
}

void HubFrame::on(FavoriteManagerListener::UserAdded, const FavoriteUser& /*aUser*/) noexcept {
	resortForFavsFirst();
}
void HubFrame::on(FavoriteManagerListener::UserRemoved, const FavoriteUser& /*aUser*/) noexcept {
	resortForFavsFirst();
}

void HubFrame::resortForFavsFirst(bool justDoIt /* = false */) {
	if(justDoIt || SETTING(SORT_FAVUSERS_FIRST)) {
		resort = true;
		callAsync([this] { execTasks(); });
	}
}

void HubFrame::addAsFavorite() {
	FavoriteHubEntry* existingHub = FavoriteManager::getInstance()->getFavoriteHubEntry(client->getHubUrl());
	if(!existingHub) {
		FavoriteHubEntry entry;
		entry.setServer(url);
		entry.setName(client->getHubName());
		entry.setHubDescription(client->getHubDescription());
		if(!client->getPassword().empty())  {
			entry.setPassword(client->getPassword());
		}
		FavoriteManager::getInstance()->addFavorite(entry);
		addStatus(T_("Favorite hub added"));
	} else {
		addStatus(T_("Hub already exists as a favorite"));
	}
}

void HubFrame::removeFavoriteHub() {
	FavoriteHubEntry* removeHub = FavoriteManager::getInstance()->getFavoriteHubEntry(client->getHubUrl());
	if(removeHub) {
		FavoriteManager::getInstance()->removeFavorite(removeHub);
		addStatus(T_("Favorite hub removed"));
	} else {
		addStatus(T_("This hub is not a favorite hub"));
	}
}

void HubFrame::updateUserList(UserInfo* ui) {
	auto filterPrep = filter.prepare();
	auto filterInfoF = [this, &ui](int column) { return Text::fromT(ui->getText(column)); };

	//single update?
	//avoid refreshing the whole list and just update the current item
	//instead
	if(ui) {
		if(ui->isHidden()) {
			return;
		}
		if(filter.empty()) {
			if(users->find(ui) == -1) {
				users->insert(ui);
			}
		} else {
			if(filter.match(filterPrep, filterInfoF)) {
				if(users->find(ui) == -1) {
					users->insert(ui);
				}
			} else {
				//erase checks to see that the item exists in the list
				//unnecessary to do it twice.
				users->erase(ui);
			}
		}

	} else {
		HoldRedraw hold { users };
		users->clear();

		if(filter.empty()) {
			for(auto& i: userMap) {
				ui = i.second;
				if(!ui->isHidden())
					users->insert(i.second);
			}
		} else {
			for(auto& i: userMap) {
				ui = i.second;
				if(!ui->isHidden() && filter.match(filterPrep, filterInfoF)) {
					users->insert(ui);
				}
			}
		}
	}

	statusDirty = true;
}

bool HubFrame::userClick(tstring& txt, const dwt::ScreenCoordinate& pt) {
	txt = chat->textUnderCursor(pt);
	if(txt.empty())
		return false;

	// Possible nickname click, let's see if we can find one like it in the name list...
	if(showUsers->getChecked()) {
		int pos = users->find(txt);
		if(pos == -1)
			return false;
		users->clearSelection();
		users->setSelected(pos);
		users->ensureVisible(pos);
	} else if(!(currentUser = findUser(txt))) {
		return false;
	}

	return true;
}

bool HubFrame::handleChatLink(const tstring& link) {
	if(link.size() > 10 && !link.compare(0, 10, _T("redirect: "))) {
		redirect(Text::fromT(link.substr(10)));
		return true;
	}

	return false;
}

bool HubFrame::handleChatContextMenu(dwt::ScreenCoordinate pt) {
	if(pt.x() == -1 || pt.y() == -1) {
		pt = chat->getContextMenuPos();
	}

	tstring searchText;
	if(userClick(searchText, pt) && handleUsersContextMenu(pt))
		return true;

	auto sel = chat->getSelection();
	if (!sel.empty()) {
		searchText = sel;
	}

	auto menu = chat->getMenu(searchText);
	
	WinUtil::addSearchMenu(menu.get(), searchText);
	
	menu->setTitle(escapeMenu(getText()), getParent()->getIcon(this));

	prepareMenu(menu.get(), UserCommand::CONTEXT_HUB, url);

	hubMenu = true;
	menu->open(pt);
	return true;
}

bool HubFrame::handleUsersContextMenu(dwt::ScreenCoordinate pt) {
	auto sel = selectedUsersImpl();
	if(!sel.empty()) {
		if(pt.x() == -1 || pt.y() == -1) {
			pt = users->getContextMenuPos();
		}

		auto menu = addChild(WinUtil::Seeds::menu);

		menu->setTitle((sel.size() == 1) ? escapeMenu(getNick(sel[0]->getUser())) : str(TF_("%1% users") % sel.size()),
			WinUtil::userImages->getIcon(0));

		appendUserItems(getParent(), menu.get());

		WinUtil::addCopyMenu(menu.get(), users);

		prepareMenu(menu.get(), UserCommand::CONTEXT_USER, url);

		hubMenu = false;
		menu->open(pt);
		return true;
	}
	return false;
}

void HubFrame::tabMenuImpl(dwt::Menu* menu) {
	if(!FavoriteManager::getInstance()->isFavoriteHub(url)) {
		menu->appendItem(T_("Add To &Favorites"), [this] { addAsFavorite(); }, WinUtil::menuIcon(IDI_FAVORITE_HUBS));
	}

	menu->appendItem(T_("&Reconnect\tCtrl+R"), [this] { reconnect(); }, WinUtil::menuIcon(IDI_RECONNECT));
	menu->appendItem(T_("Copy &address to clipboard"), [this] { handleCopyHub(false); });
	if (client->isSecure() && Util::isAdcsUrl(client->getHubUrl()) && client->getHubUrl().find("?kp=") == string::npos)
		menu->appendItem(T_("Copy address with &keyprint to clipboard"), [this] { handleCopyHub(true); });
	menu->appendItem(T_("&Search hub"), [this] { handleSearchHub(); }, WinUtil::menuIcon(IDI_SEARCH));
	auto adc = dynamic_cast<AdcHub*>(client);
	const auto hasBBS = (adc && adc->supportsBBS()) || !BBSManager::getInstance()->getBoards(url).empty();
	menu->appendItem(T_("&Bulletin boards"), [this] { openBBS(); }, WinUtil::menuIcon(IDI_CHAT), hasBBS);
	menu->appendItem(T_("&Disconnect"), [this] { disconnect(false); }, WinUtil::menuIcon(IDI_HUB_OFF));

	prepareMenu(menu, UserCommand::CONTEXT_HUB, url);

	menu->appendSeparator();

	hubMenu = true;
}

void HubFrame::handleShowUsersClicked() {
	bool checked = showUsers->getChecked();

	if(checked) {
		updateUserList();
		currentUser = 0;
	} else {
		users->clear();
	}

	SettingsManager::getInstance()->set(SettingsManager::GET_USER_INFO, checked);

	layout();
	statusDirty = true;
}

void HubFrame::handleCopyHub(bool keyprinted) {
	auto address = keyprinted ? url + "/?kp=" + CryptoManager::getInstance()->keyprintToString(client->getKeyprint()) : url;
	WinUtil::setClipboard(Text::toT(address));
}

void HubFrame::handleSearchHub() {
	WinUtil::searchHub(Text::toT(url));
}

void HubFrame::handleDoubleClickUsers() {
	if(users->hasSelected()) {
		users->getSelectedData()->getList(getParent());
	}
}

void HubFrame::runUserCommand(const UserCommand& uc) {
	if(!WinUtil::getUCParams(this, uc, ucLineParams))
		return;

	auto ucParams = ucLineParams;

	// imitate ClientManager::userCommand, except some params are cached for multiple selections

	client->getMyIdentity().getParams(ucParams, "my", true);
	client->getHubIdentity().getParams(ucParams, "hub", false);

	if(hubMenu) {
		client->sendUserCmd(uc, ucParams);
	} else {
		auto sel = selectedUsersImpl();
		for(auto& i: sel) {
			auto tmp = ucParams;
			static_cast<UserInfo*>(i)->getIdentity().getParams(tmp, "user", true);
			client->sendUserCmd(uc, tmp);
		}
	}
}

string HubFrame::getLogPath(bool status) const {
	ParamMap params;
	params["hubNI"] = [this] { return client->getHubName(); };
	params["hubURL"] = [this] { return client->getHubUrl(); };
	params["myNI"] = [this] { return client->getMyNick(); };
	return Util::validateFileName(LogManager::getInstance()->getPath(status ? LogManager::STATUS : LogManager::CHAT, params));
}

void HubFrame::openLog(bool status) {
	WinUtil::openFile(Text::toT(getLogPath(status)));
}

string HubFrame::stripNick(const string& nick) const {
	if (nick.substr(0, 1) != "[") return nick;
	string::size_type x = nick.find(']');
	string ret;
	// Avoid full deleting of [IMCOOL][CUSIHAVENOTHINGELSETHANBRACKETS]-type nicks
	if ((x != string::npos) && (nick.substr(x+1).length() > 0)) {
		ret = nick.substr(x+1);
	} else {
		ret = nick;
	}
	return ret;
}

static bool compareCharsNoCase(string::value_type a, string::value_type b) {
	return Text::toLower(a) == Text::toLower(b);
}

//Has fun side-effects. Otherwise needs reference arguments or multiple-return-values.
tstring HubFrame::scanNickPrefix(const tstring& prefixT) {
	string prefix = Text::fromT(prefixT), maxPrefix;
	tabCompleteNicks.clear();
	for(auto& i: userMap) {
		string prevNick, nick = i.second->getIdentity().getNick(), wholeNick = nick;

		do {
			string::size_type lp = prefix.size(), ln = nick.size();
			if ((ln >= lp) && (!Util::strnicmp(nick, prefix, lp))) {
				if (maxPrefix == Util::emptyString) maxPrefix = nick;	//ugly hack
				tabCompleteNicks.push_back(nick);
				tabCompleteNicks.push_back(wholeNick);
				maxPrefix = maxPrefix.substr(0, mismatch(maxPrefix.begin(),
					maxPrefix.begin()+min(maxPrefix.size(), nick.size()),
					nick.begin(), compareCharsNoCase).first - maxPrefix.begin());
			}

			prevNick = nick;
			nick = stripNick(nick);
		} while (prevNick != nick);
	}

	return Text::toT(maxPrefix);
}

bool HubFrame::tab() {
	if(message->length() == 0) {
		::SetFocus(::GetNextDlgTabItem(handle(), message->handle(), isShiftPressed()));
		return true;
	}

	HWND focus = GetFocus();
	if( (focus == message->handle()) && !isShiftPressed() )
	{
		tstring text = message->getText();
		string::size_type textStart = text.find_last_of(_T(" \n\t"));

		if(complete.empty()) {
			if(textStart != string::npos) {
				complete = text.substr(textStart + 1);
			} else {
				complete = text;
			}
			if(complete.empty()) {
				// Still empty, no text entered...
				return false;
			}
			users->clearSelection();
		}

		if(textStart == string::npos)
			textStart = 0;
		else
			textStart++;

		if (inTabComplete) {
			// Already pressed tab once. Output nick candidate list.
			tstring nicks;
			for (auto i = tabCompleteNicks.begin(); i < tabCompleteNicks.end(); i+=2)
				nicks.append(Text::toT(*i + " "));
			addChat(nicks);
			inTabComplete = false;
		} else {
			// First tab. Maximally extend proposed nick.
			tstring nick = scanNickPrefix(complete);
			if (tabCompleteNicks.empty()) return true;

			// Maybe it found a unique match. If userlist showing, highlight.
			if(showUsers->getChecked() && tabCompleteNicks.size() == 2) {
				int i = users->find(Text::toT(tabCompleteNicks[1]));
				users->setSelected(i);
				users->ensureVisible(i);
			}

			message->setSelection(static_cast<int>(textStart), -1);

			// no shift, use partial nick when appropriate
			if(isShiftPressed()) {
				message->replaceSelection(nick);
			} else {
				message->replaceSelection(Text::toT(stripNick(Text::fromT(nick))));
			}

			inTabComplete = true;
			return true;
		}
	}
	return false;
}

void HubFrame::reconnect() {
	client->reconnect();
}

void HubFrame::disconnect(bool allowReco) {
	client->setAutoReconnect(allowReco);
	client->disconnect(true);
}

void HubFrame::redirect(string&& target) {
	Util::sanitizeUrl(target);

	if(target.empty()) {
		addStatus(T_("Redirect request to an empty hub address"));
		return;
	}

	if(ClientManager::getInstance()->isConnected(target)) {
		addStatus(T_("Redirect request received to a hub that's already connected"));
		return;
	}

	BBSFrame::detachHub(this);
	url = move(target);

	// the client is dead, long live the client!
	client->removeListener(this);
	ClientManager::getInstance()->putClient(client);
	client = 0;
	onDisconnected();
	client = ClientManager::getInstance()->getClient(url);
	client->addListener(this);
	BBSFrame::attachHub(this);
	updateBBSAvailability();
	client->connect();
}

void HubFrame::showFilterOpts() {
	filterOpts->setEnabled(true);
	filterOpts->setVisible(true);

	layout();
}

void HubFrame::hideFilterOpts(dwt::Widget* w) {
	if(w != filter.text && w != filter.column && w != filter.method) {
		filterOpts->setEnabled(false);
		filterOpts->setVisible(false);
	}
}

HubFrame::UserInfoList HubFrame::selectedUsersImpl() const {
	return showUsers->getChecked() ? usersFromTable(users) : (currentUser ? UserInfoList(1, currentUser) : UserInfoList());
}

void HubFrame::setTabIcon()
{
	bool setTabIcon = false;

	auto myIdentity = client->getMyIdentity();
	if(myIdentity.isOp())
	{
		if(tabIcon != IDI_USER_OP)
		{
			tabIcon = IDI_USER_OP;

			setTabIcon = true;
		}
	}
	else if(myIdentity.isRegistered())
	{
		if(tabIcon != IDI_USER_REG)
		{
			tabIcon = IDI_USER_REG;

			setTabIcon = true;
		}
	}
	else
	{
		if(tabIcon != IDI_HUB)
		{
			tabIcon = IDI_HUB;

			setTabIcon = true;
		}
	}

	if(setTabIcon)
	{
		setIcon(WinUtil::mergeIcons({ IDI_HUB, tabIcon }));
	}
}
