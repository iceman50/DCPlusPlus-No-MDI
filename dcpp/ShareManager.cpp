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
#include "ShareManager.h"

#include "AdcHub.h"
#include "BZUtils.h"
#include "ClientManager.h"
#include "CryptoManager.h"
#include "Download.h"
#include "File.h"
#include "FilteredFile.h"
#include "FavoriteManager.h"
#include "LogManager.h"
#include "HashBloom.h"
#include "HashManager.h"
#include "QueueManager.h"
#include "ScopedFunctor.h"
#include "SearchResult.h"
#include "SimpleXML.h"
#include "SQLiteDB.h"
#include "StringTokenizer.h"
#include "Transfer.h"
#include "UserConnection.h"
#include "version.h"

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fnmatch.h>
#endif

#include <limits>
#include <memory>

// define this to 1 to measure the time taken by searches to complete.
#ifndef DCPP_TIME_SEARCHES
#define DCPP_TIME_SEARCHES 0
#endif

namespace dcpp {

using std::numeric_limits;

namespace {
const int SHARE_CACHE_SCHEMA_VERSION = 2;
const size_t MAX_SHARE_CACHE_NAME = 4096;
const size_t MAX_SHARE_CACHE_PATH = 32768;

// The fingerprint is not a security boundary; it is a compact invalidation key
// for share roots and settings that change what may be advertised or uploaded.
void fnvAppend(uint64_t& hash, const string& value) noexcept {
	for(auto c: value) {
		hash ^= static_cast<uint8_t>(c);
		hash *= 1099511628211ULL;
	}
	hash ^= 0xff;
	hash *= 1099511628211ULL;
}

void fnvAppend(uint64_t& hash, int64_t value) noexcept {
	fnvAppend(hash, Util::toString(value));
}

bool hasPathSeparator(const string& value) noexcept {
	return value.find('\\') != string::npos || value.find('/') != string::npos;
}

bool isValidCachedName(const string& name) noexcept {
	return !name.empty() && name.size() <= MAX_SHARE_CACHE_NAME && name != "." && name != ".." && !hasPathSeparator(name);
}

bool isValidCachedPath(const string& path) noexcept {
	return !path.empty() && path.size() <= MAX_SHARE_CACHE_PATH;
}

bool isUncPath(const string& path) noexcept {
	if(path.size() < 2) {
		return false;
	}
	if(path.compare(0, 4, "\\\\?\\") == 0) {
		return path.compare(0, 8, "\\\\?\\UNC\\") == 0;
	}
	return (path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/');
}

bool isUnderSharedRoot(const string& path, const map<string, string>& shares) noexcept {
	for(const auto& share: shares) {
		if(Util::strnicmp(path, share.first, share.first.size()) == 0) {
			return true;
		}
	}
	return false;
}

uint64_t countRows(SQLiteDB& db, const char* sql) {
	auto stmt = db.prepare(sql);
	return stmt.step() ? static_cast<uint64_t>(stmt.columnInt64(0)) : 0;
}

string getMetadata(SQLiteDB& db, const string& key) {
	auto stmt = db.prepare("SELECT value FROM metadata WHERE key=?1");
	stmt.bind(1, key);
	return stmt.step() ? stmt.columnText(0) : string();
}
}

std::atomic_flag ShareManager::refreshing = ATOMIC_FLAG_INIT;
std::atomic<bool> ShareManager::refreshActive { false };

ShareManager::ShareManager() : hits(0), xmlListLen(0), bzXmlListLen(0),
	xmlDirty(true), forceXmlRefresh(true), refreshDirs(false), update(false), listN(0),
	lastXmlUpdate(0), lastFullUpdate(GET_TICK()), bloom(1<<20)
{
	SettingsManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);
	HashManager::getInstance()->addListener(this);
}

ShareManager::~ShareManager() {
	SettingsManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);
	HashManager::getInstance()->removeListener(this);

	join();

	if(bzXmlRef.get()) {
		bzXmlRef.reset();
		File::deleteFile(getBZXmlFile());
	}
}

ShareManager::Directory::Directory(const string& aName, const ShareManager::Directory::Ptr& aParent) :
	size(0),
	name(aName),
	parent(aParent.get())
{
}

const string& ShareManager::Directory::getRealName() const noexcept {
	return realName ? *realName : name;
}

string ShareManager::Directory::getADCPath() const noexcept {
	if(!getParent())
		return '/' + name + '/';
	return getParent()->getADCPath() + name + '/';
}

string ShareManager::Directory::getFullName() const noexcept {
	if(!getParent())
		return getName() + '\\';
	return getParent()->getFullName() + getName() + '\\';
}

string ShareManager::Directory::getRealPath(const std::string& path) const {
	if(getParent()) {
		return getParent()->getRealPath(getRealName() + PATH_SEPARATOR_STR + path);
	} else {
		return ShareManager::getInstance()->findRealRoot(getRealName(), path);
	}
}

string ShareManager::findRealRoot(const string& virtualRoot, const string& virtualPath) const {
	for(auto& i: shares) {
		if(Util::stricmp(i.second, virtualRoot) == 0) {
			std::string name = i.first + virtualPath;
			dcdebug("Matching %s\n", name.c_str());
			if(FileFindIter(name) != FileFindIter()) {
				return name;
			}
		}
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

int64_t ShareManager::Directory::getSize() const noexcept {
	int64_t tmp = size;
	for(auto& i: directories)
		tmp += i.second->getSize();
	return tmp;
}

string ShareManager::toVirtual(const TTHValue& tth) const {
	if(bzXmlRoot && tth == bzXmlRoot) {
		return Transfer::USER_LIST_NAME_BZ;
	} else if(xmlRoot && tth == xmlRoot) {
		return Transfer::USER_LIST_NAME;
	}

	Lock l(cs);
	auto i = tthIndex.find(tth);
	if(i != tthIndex.end()) {
		return i->second->getADCPath();
	} else {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}
}

ShareManager::ShareAccess ShareManager::getShareAccess(const string& hubUrl) const {
	ShareAccess access;
	if(hubUrl.empty()) {
		return access;
	}

	if(!FavoriteManager::getInstance()->getHubShareDirectories(hubUrl, access.directories)) {
		access.unrestricted = true;
	}
	return access;
}

bool ShareManager::hasCustomShare(const string& hubUrl) const {
	std::set<string> directories;
	return !hubUrl.empty() && FavoriteManager::getInstance()->getHubShareDirectories(hubUrl, directories);
}

bool ShareManager::isVirtualAllowed(const string& virtualName, const ShareAccess& access) const {
	if(access.unrestricted) {
		return true;
	}

	bool found = false;
	for(const auto& share: shares) {
		if(Util::stricmp(share.second, virtualName) != 0) {
			continue;
		}

		found = true;
		auto allowed = find_if(access.directories.begin(), access.directories.end(), [&share](const string& path) {
			return Util::stricmp(path, share.first) == 0;
		});
		if(allowed == access.directories.end()) {
			return false;
		}
	}
	return found;
}

bool ShareManager::isFileAllowed(const Directory::File& file, const ShareAccess& access) const {
	auto directory = file.getParent();
	while(directory->getParent()) {
		directory = directory->getParent();
	}
	return isVirtualAllowed(directory->getName(), access);
}

string ShareManager::toVirtual(const TTHValue& tth, const string& hubUrl) const {
	auto access = getShareAccess(hubUrl);
	Lock l(cs);
	auto i = tthIndex.find(tth);
	if(i != tthIndex.end() && isFileAllowed(*i->second, access)) {
		return i->second->getADCPath();
	}
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

string ShareManager::toReal(const string& virtualFile) {
	return toRealWithSize(virtualFile).first;
}

string ShareManager::toReal(const string& virtualFile, const string& hubUrl) {
	return toRealWithSize(virtualFile, hubUrl).first;
}

pair<string, int64_t> ShareManager::toRealWithSize(const string& virtualFile) {
	Lock l(cs);

	if(virtualFile == "MyList.DcLst") {
		throw ShareException("NMDC-style lists no longer supported, please upgrade your client");
	}
	if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		generateXmlList();
		return make_pair(getBZXmlFile(), 0);
	}

	auto f = findFile(virtualFile);
	return make_pair(f.getRealPath(), f.getSize());
}

pair<string, int64_t> ShareManager::toRealWithSize(const string& virtualFile, const string& hubUrl) {
	auto access = getShareAccess(hubUrl);
	Lock l(cs);

	if(virtualFile == "MyList.DcLst" || virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	auto& f = findFile(virtualFile);
	if(!isFileAllowed(f, access)) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}
	return make_pair(f.getRealPath(), f.getSize());
}

StringList ShareManager::getRealPaths(const string& virtualPath) {
	if(virtualPath.empty())
		throw ShareException("empty virtual path");

	StringList ret;

	Lock l(cs);

	if(*(virtualPath.end() - 1) == '/') {
		// directory
		Directory::Ptr d = splitVirtual(virtualPath).first;

		// imitate Directory::getRealPath
		if(d->getParent()) {
			ret.push_back(d->getParent()->getRealPath(d->getName()));
		} else {
			for(auto& i: shares) {
				if(Util::stricmp(i.second, d->getName()) == 0) {
					// remove the trailing path sep
					if(FileFindIter(i.first.substr(0, i.first.size() - 1)) != FileFindIter()) {
						ret.push_back(i.first);
					}
				}
			}
		}

	} else {
		// file
		ret.push_back(toReal(virtualPath));
	}

	return ret;
}

StringList ShareManager::getRealPaths(const TTHValue& tth, const string& hubUrl) const {
	StringList ret;
	auto access = getShareAccess(hubUrl);

	Lock l(cs);
	// During refresh recovery the original cached path may no longer exist, but
	// the same content can still be shared from another directory. Walk the live
	// tree and keep hub-specific share rules intact before UploadManager retries.
	function<void (const Directory&)> collect = [&](const Directory& dir) {
		for(const auto& file: dir.files) {
			if(file.tth && *file.tth == tth && isFileAllowed(file, access)) {
				ret.push_back(file.getRealPath());
			}
		}

		for(const auto& child: dir.directories) {
			collect(*child.second);
		}
	};

	for(const auto& dir: directories) {
		collect(*dir.second);
	}

	return ret;
}

optional<TTHValue> ShareManager::getTTHFromReal(const string& realPath) noexcept {
	Lock l(cs);
	auto f = getFile(realPath);
	if (f) {
		return f->get().tth;
	}
	return nullopt;
}

optional<TTHValue> ShareManager::getTTH(const string& virtualFile) const {
	Lock l(cs);
	if(virtualFile == Transfer::USER_LIST_NAME_BZ) {
		return bzXmlRoot;
	} else if(virtualFile == Transfer::USER_LIST_NAME) {
		return xmlRoot;
	}

	return findFile(virtualFile).tth;
}

optional<TTHValue> ShareManager::getTTH(const string& virtualFile, const string& hubUrl) const {
	auto access = getShareAccess(hubUrl);
	Lock l(cs);
	auto& file = findFile(virtualFile);
	if(!isFileAllowed(file, access)) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}
	return file.tth;
}

MemoryInputStream* ShareManager::getTree(const string& virtualFile) const {
	TigerTree tree;
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		if(!HashManager::getInstance()->getTree(TTHValue(virtualFile.substr(4)), tree))
			return nullptr;
	} else {
		try {
			auto tth = getTTH(virtualFile);
			if(!tth) { return nullptr; }
			HashManager::getInstance()->getTree(*tth, tree);
		} catch(const Exception&) {
			return nullptr;
		}
	}

	ByteVector buf = tree.getLeafData();
	return new MemoryInputStream(&buf[0], buf.size());
}

MemoryInputStream* ShareManager::getTree(const string& virtualFile, const string& hubUrl) const {
	TigerTree tree;
	try {
		auto tth = getTTH(virtualFile, hubUrl);
		if(!tth || !HashManager::getInstance()->getTree(*tth, tree)) {
			return nullptr;
		}
	} catch(...) {
		return nullptr;
	}

	ByteVector buf = tree.getLeafData();
	return new MemoryInputStream(&buf[0], buf.size());
}

AdcCommand ShareManager::getFileInfo(const string& aFile) {
	if(aFile == Transfer::USER_LIST_NAME) {
		generateXmlList();
		if(!xmlRoot) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(xmlListLen));
		cmd.addParam("TR", xmlRoot->toBase32());
		return cmd;
	}

	if(aFile == Transfer::USER_LIST_NAME_BZ) {
		generateXmlList();
		if(!bzXmlRoot) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(bzXmlListLen));
		cmd.addParam("TR", bzXmlRoot->toBase32());
		return cmd;
	}

	if(aFile.compare(0, 4, "TTH/") != 0)
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);

	TTHValue val(aFile.substr(4));
	Lock l(cs);
	auto i = tthIndex.find(val);
	if(i == tthIndex.end()) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	const Directory::File& f = *i->second;
	AdcCommand cmd(AdcCommand::CMD_RES);
	cmd.addParam("FN", f.getADCPath());
	cmd.addParam("SI", Util::toString(f.getSize()));
	cmd.addParam("TR", f.tth->toBase32());
	return cmd;
}

AdcCommand ShareManager::getFileInfo(const string& aFile, const string& hubUrl) {
	auto access = getShareAccess(hubUrl);
	if(aFile == Transfer::USER_LIST_NAME || aFile == Transfer::USER_LIST_NAME_BZ) {
		if(access.unrestricted) {
			return getFileInfo(aFile);
		}

		auto data = generateFileListData(hubUrl, aFile == Transfer::USER_LIST_NAME_BZ);
		TigerTree tree(1024 * 1024 * 1024);
		tree.update(data.data(), data.size());
		tree.finalize();

		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(static_cast<int64_t>(data.size())));
		cmd.addParam("TR", tree.getRoot().toBase32());
		return cmd;
	}

	if(aFile.compare(0, 4, "TTH/") != 0) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	TTHValue val(aFile.substr(4));
	Lock l(cs);
	auto i = tthIndex.find(val);
	if(i == tthIndex.end() || !isFileAllowed(*i->second, access)) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	const Directory::File& f = *i->second;
	AdcCommand cmd(AdcCommand::CMD_RES);
	cmd.addParam("FN", f.getADCPath());
	cmd.addParam("SI", Util::toString(f.getSize()));
	cmd.addParam("TR", f.tth->toBase32());
	return cmd;
}

pair<ShareManager::Directory::Ptr, string> ShareManager::splitVirtual(const string& virtualPath) const {
	if(virtualPath.empty() || virtualPath[0] != '/') {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	auto i = virtualPath.find('/', 1);
	if(i == string::npos || i == 1) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	auto dmi = directories.find(virtualPath.substr(1, i - 1));
	if(dmi == directories.end()) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	auto d = dmi->second;

	auto j = i + 1;
	while((i = virtualPath.find('/', j)) != string::npos) {
		auto mi = d->directories.find(virtualPath.substr(j, i - j));
		j = i + 1;
		if(mi == d->directories.end())
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		d = mi->second;
	}

	return make_pair(d, virtualPath.substr(j));
}

const ShareManager::Directory::File& ShareManager::findFile(const string& virtualFile) const {
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		auto i = tthIndex.find(TTHValue(virtualFile.substr(4)));
		if(i == tthIndex.end()) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}
		return *i->second;
	}

	auto v = splitVirtual(virtualFile);
	auto it = find_if(v.first->files.begin(), v.first->files.end(),
		Directory::File::StringComp(v.second));
	if(it == v.first->files.end())
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	return *it;
}

string ShareManager::validateVirtual(const string& aVirt) const noexcept {
	string tmp = aVirt;
	string::size_type idx = 0;

	while( (idx = tmp.find_first_of("\\/"), idx) != string::npos) {
		tmp[idx] = '_';
	}
	return tmp;
}

bool ShareManager::hasVirtual(const string& virtualName) const noexcept {
	Lock l(cs);
	return directories.find(virtualName) != directories.end();
}

void ShareManager::load(SimpleXML& aXml) {
	Lock l(cs);

	aXml.resetCurrentChild();
	if(aXml.findChild("Share")) {
		aXml.stepIn();
		while(aXml.findChild("Directory")) {
			string realPath = aXml.getChildData();
			if(realPath.empty()) {
				continue;
			}
			// make sure realPath ends with a PATH_SEPARATOR
			if(realPath[realPath.size() - 1] != PATH_SEPARATOR) {
				realPath += PATH_SEPARATOR;
			}

			const string& virtualName = aXml.getChildAttrib("Virtual");
			string vName = validateVirtual(virtualName.empty() ? Util::getLastDir(realPath) : virtualName);
			shares[move(realPath)] = vName;
			if(directories.find(vName) == directories.end()) {
				directories[vName] = Directory::create(vName);
			}
		}
		aXml.stepOut();
	}
}

void ShareManager::save(SimpleXML& aXml) {
	Lock l(cs);

	aXml.addTag("Share");
	aXml.stepIn();
	for(auto& i: shares) {
		aXml.addTag("Directory", i.first);
		aXml.addChildAttrib("Virtual", i.second);
	}
	aXml.stepOut();
}

void ShareManager::addDirectory(const string& realPath, const string& virtualName) {
	if(realPath.empty() || virtualName.empty()) {
		throw ShareException(_("No directory specified"));
	}

	if (!checkHidden(realPath)) {
		throw ShareException(_("Directory is hidden"));
	}

	if(Util::stricmp(SETTING(TEMP_DOWNLOAD_DIRECTORY), realPath) == 0) {
		throw ShareException(_("The temporary download directory cannot be shared"));
	}

	{
		Lock l(cs);

		for(auto& i: shares) {
			if(Util::strnicmp(realPath, i.first, i.first.length()) == 0) {
				// Trying to share an already shared directory
				throw ShareException(_("Directory already shared"));
			} else if(Util::strnicmp(realPath, i.first, realPath.length()) == 0) {
				// Trying to share a parent directory
				throw ShareException(_("Remove all subdirectories before adding this one"));
			}
		}
	}

	HashManager::HashPauser pauser;

	auto dp = buildTree(realPath);

	string vName = validateVirtual(virtualName);
	dp->setName(vName);

	Lock l(cs);

	shares[realPath] = move(vName);

	merge(dp, realPath);

	rebuildIndices();
	setDirty();
}

void ShareManager::merge(const Directory::Ptr& directory, const string& realPath) {
	auto i = directories.find(directory->getName());
	if(i != directories.end()) {
		dcdebug("Merging directory <%s> into %s\n", realPath.c_str(), directory->getName().c_str());
		i->second->merge(directory, realPath);

	} else {
		dcdebug("Adding new directory %s\n", directory->getName().c_str());
		directories[directory->getName()] = directory;
	}
}

void ShareManager::Directory::merge(const Directory::Ptr& source, const string& realPath) {
	// merge directories
	for(auto& i: source->directories) {
		auto subSource = i.second;

		auto ti = directories.find(subSource->getName());
		if(ti == directories.end()) {
			// the directory doesn't exist; create it.
			directories.emplace(subSource->getName(), subSource);
			subSource->parent = this;

			auto f = findFile(subSource->getName());
			if(f != files.end()) {
				// we have a file that has the same name as the dir being merged; rename it.
				const_cast<File&>(*f).validateName(Util::getFilePath(f->getRealPath()));
			}

		} else {
			// the directory was already existing; merge into it.
			auto subTarget = ti->second;
			subTarget->merge(subSource, realPath + subSource->getName() + PATH_SEPARATOR);
		}
	}

	// merge files
	for(auto& i: source->files) {
		auto& file = const_cast<File&>(i);

		file.setParent(this);
		file.validateName(realPath);

		files.insert(move(file));
	}
}

bool ShareManager::Directory::nameInUse(const string& name) const {
	return findFile(name) != files.end() || directories.find(name) != directories.end();
}

void ShareManager::Directory::File::validateName(const string& sourcePath) {
	auto diskPath = sourcePath + name;
	if(parent->nameInUse(name)) {
		uint32_t num = 0;
		string base = name, ext, vname;
		auto dot = base.rfind('.');
		if(dot != string::npos) {
			ext = base.substr(dot);
			base.erase(dot);
		}
		do {
			++num;
			vname = base + " (" + Util::toString(num) + ")" + ext;
		} while(parent->nameInUse(vname));
		dcdebug("Renaming duplicate <%s> to <%s>\n", name.c_str(), vname.c_str());
		name = move(vname);
	}
	realPath = move(diskPath);
}

void ShareManager::removeDirectory(const string& realPath) {
	if(realPath.empty())
		return;

	HashManager::getInstance()->stopHashing(realPath);

	Lock l(cs);

	auto i = shares.find(realPath);
	if(i == shares.end()) {
		return;
	}

	auto vName = i->second;
	directories.erase(vName);

	shares.erase(i);

	HashManager::HashPauser pauser;

	// Readd all directories with the same vName
	for(i = shares.begin(); i != shares.end(); ++i) {
		if(Util::stricmp(i->second, vName) == 0 && checkHidden(i->first)) {
			auto dp = buildTree(i->first);
			dp->setName(i->second);
			merge(dp, i->first);
		}
	}

	rebuildIndices();
	setDirty();
}

void ShareManager::renameDirectory(const string& realPath, const string& virtualName) {
	removeDirectory(realPath);
	addDirectory(realPath, virtualName);
}

int64_t ShareManager::getShareSize(const string& realPath) const noexcept {
	Lock l(cs);
 	dcassert(realPath.size()>0);
	auto i = shares.find(realPath);

	if(i != shares.end()) {
		auto j = directories.find(i->second);
		if(j != directories.end()) {
			// Check whether this is a merged share
			int vNames = 0;
			for(auto& s: shares) {
				if(Util::stricmp(s.second, i->second) == 0) { 
					vNames++;
					if (vNames > 1) break;
				}
			}

			if(vNames == 1) {
				// Only one root dir has been found, go simple...
				return j->second->getSize();
			} else {
				// This path is part of multiple merged root dirs, count only what's belong to this virtual share
				int64_t tmp = 0;
				// Subdirectories
				for(auto& d: j->second->directories) {
					if(FileFindIter(realPath + d.second->getRealName()) != FileFindIter()) {
						tmp += d.second->getSize();
					} 
				}

				// Files in the root dir
				for(auto& f: j->second->files) {
					if(FileFindIter(realPath + PATH_SEPARATOR_STR + f.getName()) != FileFindIter()) {
						tmp += f.getSize();
					} 
				}
				return tmp; 
			}
		}
	}
	return -1;
}

int64_t ShareManager::getShareSize() const noexcept {
	Lock l(cs);
	int64_t tmp = 0;
	for(auto& i: tthIndex) {
		tmp += i.second->getSize();
	}
	return tmp;
}

int64_t ShareManager::getShareSizeForHub(const string& hubUrl) const {
	auto access = getShareAccess(hubUrl);
	Lock l(cs);
	int64_t size = 0;
	for(const auto& item: tthIndex) {
		if(isFileAllowed(*item.second, access)) {
			size += item.second->getSize();
		}
	}
	return size;
}

size_t ShareManager::getSharedFiles() const noexcept {
	Lock l(cs);
	return tthIndex.size();
}

size_t ShareManager::getSharedFiles(const string& hubUrl) const {
	auto access = getShareAccess(hubUrl);
	Lock l(cs);
	return count_if(tthIndex.begin(), tthIndex.end(), [this, &access](const auto& item) {
		return isFileAllowed(*item.second, access);
	});
}

ShareManager::Directory::Ptr ShareManager::buildTree(const string& realPath, optional<std::reference_wrapper<const string>> dirName, const Directory::Ptr& parent) {
	auto dir = Directory::create(dirName ? dirName->get() : Util::getLastDir(realPath), parent);

	auto lastFileIter = dir->files.begin();

#ifdef _WIN32
	for(FileFindIter i(realPath + "*"), end; i != end; ++i) {
#else
	//the fileiter just searches directorys for now, not sure if more
	//will be needed later
	for(FileFindIter i(realPath), end; i != end; ++i) {
#endif
		auto name = i->getFileName();

		if(name.empty()) {
			LogManager::getInstance()->message(str(F_("Invalid file name found while hashing folder %1%") % Util::addBrackets(realPath)));
			continue;
		}

		if(name == "." || name == "..")
			continue;
		if(!SETTING(SHARE_HIDDEN) && i->isHidden())
			continue;
		if(!SETTING(FOLLOW_LINKS) && i->isLink())
 			continue;

		if(i->isDirectory()) {
			auto newRealPath = realPath + name + PATH_SEPARATOR;

			if(!checkInvalidPaths(newRealPath))
				continue;

			// don't share unfinished downloads
			if(newRealPath == SETTING(TEMP_DOWNLOAD_DIRECTORY)) { continue; }

			auto virtualName = name;
			if(dir->nameInUse(virtualName)) {
				uint32_t num = 0;
				do {
					++num;
					virtualName = name + " (" + Util::toString(num) + ")";
				} while(dir->nameInUse(virtualName));
			}

			dir->directories[virtualName] = buildTree(newRealPath, std::cref(virtualName), dir);

			if(virtualName != name) {
				dir->directories[virtualName]->setRealName(move(name));
			}

		} else {
			// Not a directory, assume it's a file...make sure we're not sharing the settings file...
			if(name == "DCPlusPlus.xml" || name == "Favorites.xml") { continue; }

			auto size = i->getSize();
			auto fileName = realPath + name;
			
			if(!checkInvalidFileName(name))
				continue;

			if(!checkInvalidFileSize(size))
				continue;

			// don't share the private key file
			if(fileName == SETTING(TLS_PRIVATE_KEY_FILE)) { continue; }

			Directory::File f(name, size, dir,
				HashManager::getInstance()->getTTH(fileName, size, i->getLastWriteTime()));
			f.validateName(realPath);
			lastFileIter = dir->files.insert(lastFileIter, move(f));
		}
	}

	return dir;
}

bool ShareManager::checkHidden(const string& realPath) const {
	FileFindIter ff = FileFindIter(realPath.substr(0, realPath.size() - 1));

	if (ff != FileFindIter()) {
		return (SETTING(SHARE_HIDDEN) || !ff->isHidden());
	}

	return true;
}

bool ShareManager::checkInvalidFileName(const string& name) const
{
	for(auto& f: cachedFilterSkiplistRegEx)
	{
		if(checkRegEx(f, name))
		{
			return false;
		}
	}

	for(auto& f: cachedFilterSkiplistFileExtensions)
	{
		if(checkRegEx(f, name))
		{
			dcdebug("Filtering away the file '%s' with the pattern matching '%s'\n", name.c_str(), f.pattern.c_str());

			return false;
		}
	}

	return true;
}

bool ShareManager::checkInvalidPaths(const string& name) const
{
	for(auto& f: cachedFilterSkiplistPaths)
	{
		if(checkRegEx(f, name))
		{
			return false;
		}
	}

	return true;
}

bool ShareManager::checkInvalidFileSize(uint64_t size) const
{
	uint64_t minimumSize = SETTING(SHARING_SKIPLIST_MINSIZE);
	if(minimumSize != 0)
	{
		if( size < minimumSize )
		{
			return false;
		}
	}

	uint64_t maximumSize = SETTING(SHARING_SKIPLIST_MAXSIZE);
	if(maximumSize != 0)
	{
		if( size > maximumSize )
		{
			return false;
		}
	}

	return true;
}

bool ShareManager::checkRegEx(const StringMatch& matcher, const string& match) const
{
	if(match.empty())
	{
		return false;
	}
	
	return matcher.match(match);
}

void ShareManager::updateFilterCache()
{
	updateFilterCache(SETTING(SHARING_SKIPLIST_REGEX), cachedFilterSkiplistRegEx);
	updateFilterCache(SETTING(SHARING_SKIPLIST_EXTENSIONS), "$", true, cachedFilterSkiplistFileExtensions);
	updateFilterCache(SETTING(SHARING_SKIPLIST_PATHS), cachedFilterSkiplistPaths);
}

void ShareManager::updateFilterCache(const std::string& strSetting, std::list<StringMatch>& lst)
{
	updateFilterCache(strSetting, "", false, lst);
}

void ShareManager::updateFilterCache(const std::string& strSetting, const std::string& strExtraPattern, bool escapeDot, std::list<StringMatch>& lst)
{
	lst.clear();

	auto tokens = StringTokenizer<string>(strSetting, ';').getTokens();
	for(auto& pattern: tokens)
	{
		if(pattern.empty())
		{
			continue;
		}

		if(escapeDot)
		{
			Util::replace(".", "\\.", pattern);
		}

		StringMatch matcher;
		matcher.pattern = pattern + strExtraPattern;
		matcher.setMethod(StringMatch::REGEX);
		matcher.prepare();

		lst.push_back(matcher);
	}
}

void ShareManager::updateIndices(Directory& dir) {
	bloom.add(Text::toLower(dir.getName()));

	for(auto& i: dir.directories) {
		updateIndices(*i.second);
	}

	dir.size = 0;

	for(auto i = dir.files.begin(); i != dir.files.end(); ) {
		updateIndices(dir, i++);
	}
}

void ShareManager::rebuildIndices(size_t expectedFiles) {
	tthIndex.clear();
	if(expectedFiles > 0) {
		tthIndex.reserve(expectedFiles);
	}
	bloom.clear();

	for(auto& i: directories) {
		updateIndices(*i.second);
	}
}

void ShareManager::updateIndices(Directory& dir, const decltype(std::declval<Directory>().files.begin())& i) {
	const Directory::File& f = *i;

	if(!f.tth) {
		return;
	}

	auto j = tthIndex.find(*f.tth);
	if(j == tthIndex.end()) {
		dir.size += f.getSize();

	} else {
		if(!SETTING(LIST_DUPES)) {
			try {
				LogManager::getInstance()->message(str(F_("Duplicate file will not be shared: %1% (Size: %2% B) Dupe matched against: %3%")
					% Util::addBrackets(f.getRealPath()) % Util::toString(f.getSize()) % Util::addBrackets(j->second->getRealPath())));
				dir.files.erase(i);
			} catch (const ShareException&) {
			}
			return;
		}
	}

	tthIndex[*f.tth] = &f;
	bloom.add(Text::toLower(f.getName()));
}

string ShareManager::getShareCacheFile() const {
	return Util::getPath(Util::PATH_USER_CONFIG) + "ShareCache.sqlite3";
}

string ShareManager::getShareCacheFingerprint() const {
	uint64_t hash = 1469598103934665603ULL;
	fnvAppend(hash, string("ShareCache.v2"));

	for(const auto& share: shares) {
		fnvAppend(hash, share.first);
		fnvAppend(hash, share.second);
	}

	fnvAppend(hash, SETTING(SHARE_HIDDEN) ? 1 : 0);
	fnvAppend(hash, SETTING(FOLLOW_LINKS) ? 1 : 0);
	fnvAppend(hash, SETTING(LIST_DUPES) ? 1 : 0);
	fnvAppend(hash, SETTING(SHARING_SKIPLIST_REGEX));
	fnvAppend(hash, SETTING(SHARING_SKIPLIST_EXTENSIONS));
	fnvAppend(hash, SETTING(SHARING_SKIPLIST_PATHS));
	fnvAppend(hash, SETTING(SHARING_SKIPLIST_MINSIZE));
	fnvAppend(hash, SETTING(SHARING_SKIPLIST_MAXSIZE));
	fnvAppend(hash, SETTING(TEMP_DOWNLOAD_DIRECTORY));
	fnvAppend(hash, SETTING(TLS_PRIVATE_KEY_FILE));

	return std::to_string(hash);
}

void ShareManager::createShareCacheSchema(SQLiteDB& db) {
	int version = 0;
	{
		auto versionStmt = db.prepare("PRAGMA user_version");
		version = versionStmt.step() ? versionStmt.columnInt(0) : 0;
	}
	if(version != 0 && version != SHARE_CACHE_SCHEMA_VERSION) {
		db.execute("DROP TABLE IF EXISTS files;DROP TABLE IF EXISTS directories;DROP TABLE IF EXISTS metadata;");
	}

	db.execute(
		"CREATE TABLE IF NOT EXISTS metadata ("
		"key TEXT PRIMARY KEY NOT NULL,"
		"value TEXT NOT NULL"
		") WITHOUT ROWID;"
		"CREATE TABLE IF NOT EXISTS directories ("
		"id INTEGER PRIMARY KEY NOT NULL,"
		"parent_id INTEGER REFERENCES directories(id) ON DELETE CASCADE,"
		"name TEXT NOT NULL CHECK(length(name) > 0),"
		"real_name TEXT"
		");"
		"CREATE INDEX IF NOT EXISTS idx_share_cache_directories_parent ON directories(parent_id);"
		"CREATE TABLE IF NOT EXISTS files ("
		"id INTEGER PRIMARY KEY NOT NULL,"
		"directory_id INTEGER NOT NULL REFERENCES directories(id) ON DELETE CASCADE,"
		"name TEXT NOT NULL CHECK(length(name) > 0),"
		"size INTEGER NOT NULL CHECK(size >= 0),"
		"tth TEXT CHECK(tth IS NULL OR length(tth) = 39),"
		"real_path TEXT NOT NULL"
		");"
		"CREATE INDEX IF NOT EXISTS idx_share_cache_files_directory ON files(directory_id);"
		"CREATE INDEX IF NOT EXISTS idx_share_cache_files_tth ON files(tth);"
		"PRAGMA user_version = 2;"
	);
}

bool ShareManager::loadShareCache() noexcept {
	if(!SETTING(SHARE_CACHE)) {
		return false;
	}
	// Queue duplicate removal must see the freshly scanned filesystem; a stale
	// snapshot could otherwise remove valid queued files during startup.
	if(SETTING(DONT_DL_ALREADY_SHARED)) {
		LogManager::getInstance()->message(_("Share cache skipped because queued duplicate removal requires a fresh share scan"));
		return false;
	}
	{
		Lock l(cs);
		if(any_of(shares.begin(), shares.end(), [](const auto& share) { return isUncPath(share.first); })) {
			LogManager::getInstance()->message(_("Share cache skipped because UNC shares require a fresh share scan"));
			return false;
		}
	}

	const auto cacheFile = getShareCacheFile();
	if(File::getSize(cacheFile) < 0) {
		LogManager::getInstance()->message(_("Share cache not found; running full file list refresh"));
		return false;
	}

	try {
		SQLiteDB db(cacheFile);
		auto versionStmt = db.prepare("PRAGMA user_version");
		const auto version = versionStmt.step() ? versionStmt.columnInt(0) : 0;
		if(version != SHARE_CACHE_SCHEMA_VERSION) {
			LogManager::getInstance()->message(_("Share cache schema version does not match; running full file list refresh"));
			return false;
		}

		string expectedFingerprint;
		std::set<string, noCaseStringLess> expectedRoots;
		{
			Lock l(cs);
			expectedFingerprint = getShareCacheFingerprint();
			for(const auto& share: shares) {
				expectedRoots.insert(share.second);
			}
		}

		if(getMetadata(db, "fingerprint") != expectedFingerprint) {
			LogManager::getInstance()->message(_("Share cache does not match current sharing settings; running full file list refresh"));
			return false;
		}

		const auto directoryRows = countRows(db, "SELECT COUNT(*) FROM directories");
		const auto fileRows = countRows(db, "SELECT COUNT(*) FROM files");
		unordered_map<int64_t, Directory::Ptr> byId;
		if(directoryRows > 0 && directoryRows <= static_cast<uint64_t>(byId.max_size())) {
			byId.reserve(static_cast<size_t>(directoryRows));
		}

		unordered_map<string, Directory::Ptr, noCaseStringHash, noCaseStringEq> newDirectories;
		if(!expectedRoots.empty()) {
			newDirectories.reserve(expectedRoots.size());
		}

		auto dirs = db.prepare("SELECT id, parent_id, name, real_name FROM directories ORDER BY id");
		while(dirs.step()) {
			const auto id = dirs.columnInt64(0);
			if(id <= 0 || byId.find(id) != byId.end()) {
				throw ShareException(_("Invalid directory record in share cache"));
			}

			auto name = dirs.columnText(2);
			if(!isValidCachedName(name)) {
				throw ShareException(_("Invalid directory name in share cache"));
			}

			auto dir = Directory::create(name);
			if(!dirs.columnIsNull(3)) {
				auto realName = dirs.columnText(3);
				if(!isValidCachedName(realName)) {
					throw ShareException(_("Invalid real directory name in share cache"));
				}
				dir->setRealName(std::move(realName));
			}

			if(dirs.columnIsNull(1)) {
				if(expectedRoots.find(name) == expectedRoots.end()) {
					throw ShareException(_("Unexpected root directory in share cache"));
				}
				if(!newDirectories.emplace(name, dir).second) {
					throw ShareException(_("Duplicate root directory in share cache"));
				}
			} else {
				const auto parentId = dirs.columnInt64(1);
				auto parent = byId.find(parentId);
				if(parent == byId.end()) {
					throw ShareException(_("Directory parent missing from share cache"));
				}
				dir->setParent(parent->second.get());
				if(!parent->second->directories.emplace(name, dir).second) {
					throw ShareException(_("Duplicate child directory in share cache"));
				}
			}
			byId.emplace(id, dir);
		}

		if(newDirectories.size() != expectedRoots.size()) {
			throw ShareException(_("Share cache is missing a configured root directory"));
		}

		auto files = db.prepare("SELECT directory_id, name, size, tth, real_path FROM files ORDER BY directory_id, name");
		while(files.step()) {
			auto dir = byId.find(files.columnInt64(0));
			if(dir == byId.end()) {
				throw ShareException(_("File parent missing from share cache"));
			}

			auto name = files.columnText(1);
			const auto size = files.columnInt64(2);
			if(!isValidCachedName(name) || size < 0) {
				throw ShareException(_("Invalid file record in share cache"));
			}

			optional<TTHValue> root;
			if(!files.columnIsNull(3)) {
				auto tth = files.columnText(3);
				if(tth.size() != 39) {
					throw ShareException(_("Invalid TTH value in share cache"));
				}
				root = TTHValue(tth);
			}

			Directory::File file(name, size, dir->second, root);
			if(files.columnIsNull(4)) {
				throw ShareException(_("Missing real file path in share cache"));
			}
			auto realPath = files.columnText(4);
			{
				Lock l(cs);
				if(!isValidCachedPath(realPath) || !isUnderSharedRoot(realPath, shares)) {
					throw ShareException(_("Invalid real file path in share cache"));
				}
			}
			file.realPath = std::move(realPath);

			dir->second->files.insert(std::move(file));
		}

		{
			Lock l(cs);
			directories.swap(newDirectories);
			lastFullUpdate = GET_TICK();
			xmlDirty = true;
			forceXmlRefresh = true;
			rebuildIndices(static_cast<size_t>(std::min<uint64_t>(fileRows, static_cast<uint64_t>(tthIndex.max_size()))));
		}

		LogManager::getInstance()->message(str(F_("Loaded cached share tree from %1% (%2% directories, %3% files)") %
			Util::addBrackets(cacheFile) % std::to_string(directoryRows) % std::to_string(fileRows)));
		return true;
	} catch(const Exception& e) {
		LogManager::getInstance()->message(str(F_("Error loading share cache %1%: %2%; running full file list refresh") %
			Util::addBrackets(cacheFile) % e.getError()));
	} catch(const std::exception& e) {
		LogManager::getInstance()->message(str(F_("Error loading share cache %1%: %2%; running full file list refresh") %
			Util::addBrackets(cacheFile) % e.what()));
	}

	return false;
}

void ShareManager::saveShareCacheDirectory(SQLiteStatement& dirStmt, SQLiteStatement& fileStmt, const Directory& dir,
	optional<int64_t> parentId, int64_t& nextId, uint64_t& directoryCount, uint64_t& fileCount) const
{
	const auto id = nextId++;
	dirStmt.bind(1, id);
	if(parentId) {
		dirStmt.bind(2, *parentId);
	} else {
		dirStmt.bindNull(2);
	}
	dirStmt.bind(3, dir.getName());
	if(dir.getRealNameOverride()) {
		dirStmt.bind(4, *dir.getRealNameOverride());
	} else {
		dirStmt.bindNull(4);
	}
	dirStmt.stepDone();
	dirStmt.reset();
	dirStmt.clearBindings();
	directoryCount++;

	for(const auto& file: dir.files) {
		fileStmt.bind(1, id);
		fileStmt.bind(2, file.getName());
		fileStmt.bind(3, file.getSize());
		if(file.tth) {
			fileStmt.bind(4, file.tth->toBase32());
		} else {
			fileStmt.bindNull(4);
		}
		fileStmt.bind(5, file.getRealPath());
		fileStmt.stepDone();
		fileStmt.reset();
		fileStmt.clearBindings();
		fileCount++;
	}

	for(const auto& child: dir.directories) {
		saveShareCacheDirectory(dirStmt, fileStmt, *child.second, id, nextId, directoryCount, fileCount);
	}
}

void ShareManager::saveShareCache() noexcept {
	if(!SETTING(SHARE_CACHE)) {
		return;
	}

	const auto cacheFile = getShareCacheFile();
	try {
		SQLiteDB db(cacheFile);
		createShareCacheSchema(db);
		// Rebuild the snapshot atomically so interrupted saves never become a
		// valid partial cache on the next startup.
		SQLiteTransaction transaction(db);
		db.execute("DELETE FROM metadata;DELETE FROM files;DELETE FROM directories;");

		uint64_t directoryCount = 0;
		uint64_t fileCount = 0;
		{
			auto metaStmt = db.prepare("INSERT INTO metadata(key, value) VALUES(?1, ?2)");
			auto putMeta = [&metaStmt](const string& key, const string& value) {
				metaStmt.bind(1, key);
				metaStmt.bind(2, value);
				metaStmt.stepDone();
				metaStmt.reset();
				metaStmt.clearBindings();
			};

			{
				Lock l(cs);
				putMeta("schema", Util::toString(SHARE_CACHE_SCHEMA_VERSION));
				putMeta("fingerprint", getShareCacheFingerprint());
				putMeta("app", APPNAME " " VERSIONSTRING);

				auto dirStmt = db.prepare("INSERT INTO directories(id, parent_id, name, real_name) VALUES(?1, ?2, ?3, ?4)");
				auto fileStmt = db.prepare("INSERT INTO files(directory_id, name, size, tth, real_path) VALUES(?1, ?2, ?3, ?4, ?5)");
				int64_t nextId = 1;
				for(const auto& dir: directories) {
					saveShareCacheDirectory(dirStmt, fileStmt, *dir.second, nullopt, nextId, directoryCount, fileCount);
				}
			}

			putMeta("directories", std::to_string(directoryCount));
			putMeta("files", std::to_string(fileCount));
		}
		transaction.commit();
		db.execute("PRAGMA optimize;PRAGMA wal_checkpoint(PASSIVE);");
		LogManager::getInstance()->message(str(F_("Saved share cache %1% (%2% directories, %3% files)") %
			Util::addBrackets(cacheFile) % std::to_string(directoryCount) % std::to_string(fileCount)));
	} catch(const Exception& e) {
		LogManager::getInstance()->message(str(F_("Error saving share cache %1%: %2%") % Util::addBrackets(cacheFile) % e.getError()));
	} catch(const std::exception& e) {
		LogManager::getInstance()->message(str(F_("Error saving share cache %1%: %2%") % Util::addBrackets(cacheFile) % e.what()));
	}
}

void ShareManager::startupRefresh(function<void (float)> progressF) noexcept {
	if(loadShareCache()) {
		// Cached data is immediately usable for share size, search, file lists
		// and upload path resolution; the background refresh reconciles it with
		// the live filesystem and broadcasts updated hub info when it finishes.
		refresh(true, true, false);
		return;
	}

	refresh(true, false, true, progressF);
}

void ShareManager::refresh(bool dirs, bool aUpdate, bool block, function<void (float)> progressF) noexcept {
	if(refreshing.test_and_set()) {
		LogManager::getInstance()->message(_("File list refresh in progress, please wait for it to finish before trying to refresh again"));
		return;
	}
	refreshActive = true;

	update = aUpdate;
	refreshDirs = dirs;

	join();

	if(block) {
		runRefresh(progressF);

	} else {
		try {
			start();
			setThreadPriority(Thread::LOW);
		} catch(const ThreadException& e) {
			LogManager::getInstance()->message(str(F_("File list refresh failed: %1%") % e.getError()));
			refreshActive = false;
			refreshing.clear();
		}
	}
}

StringPairList ShareManager::getDirectories() const noexcept {
	Lock l(cs);
	StringPairList ret;
	for(auto& i: shares) {
		ret.emplace_back(i.second, i.first);
	}
	return ret;
}

int ShareManager::run() {
	runRefresh();
	return 0;
}

void ShareManager::runRefresh(function<void (float)> progressF) {
	auto dirs = getDirectories();
	// Don't need to refresh if no directories are shared
	if(dirs.empty())
		refreshDirs = false;

	std::shared_ptr<HashManager::HashPauser> pauser;
	bool refreshedDirs = false;
		
	if(refreshDirs) {
		pauser = std::make_shared<HashManager::HashPauser>();

		LogManager::getInstance()->message(_("File list refresh initiated"));

		lastFullUpdate = GET_TICK();

		vector<pair<Directory::Ptr, string>> newDirs;

		float progressCounter = 0, dirCount = dirs.size();

		// Make sure that the cache is updated.
		updateFilterCache();

		for(auto& i: dirs) {
			if(checkHidden(i.second)) {
				auto dp = buildTree(i.second);
				dp->setName(i.first);
				newDirs.emplace_back(dp, i.second);
			}

			if(progressF) {
				progressF(++progressCounter / dirCount);
			}
		}

		{
			Lock l(cs);
			directories.clear();

			for(auto& i: newDirs) {
				merge(i.first, i.second);
			}

			rebuildIndices();
		}
		refreshDirs = false;
		refreshedDirs = true;

		LogManager::getInstance()->message(_("File list refresh finished"));
	}

	if(update) {
		/* Hold off hashing until all infos have been sent out 
		   so we properly signal the update to possible bloom requesters. */
		ClientManager::getInstance()->infoUpdated(std::move(pauser));
	}

	refreshing.clear();
	refreshActive = false;
	if(refreshedDirs) {
		saveShareCache();
	}
}

void ShareManager::getBloom(ByteVector& v, size_t k, size_t m, size_t h) const {
	dcdebug("Creating bloom filter, k=%u, m=%u, h=%u\n", k, m, h);
	Lock l(cs);

	HashBloom bloom;
	bloom.reset(k, m, h);
	for(auto& i: tthIndex) {
		bloom.add(i.first);
	}
	bloom.copy_to(v);
}

void ShareManager::getBloom(ByteVector& v, size_t k, size_t m, size_t h, const string& hubUrl) const {
	dcdebug("Creating hub share bloom filter, k=%u, m=%u, h=%u\n", k, m, h);
	auto access = getShareAccess(hubUrl);
	Lock l(cs);

	HashBloom bloom;
	bloom.reset(k, m, h);
	for(const auto& item: tthIndex) {
		if(isFileAllowed(*item.second, access)) {
			bloom.add(item.first);
		}
	}
	bloom.copy_to(v);
}

void ShareManager::generateXmlList() {
	Lock l(cs);
	if(forceXmlRefresh || (xmlDirty && (lastXmlUpdate + 15 * 60 * 1000 < GET_TICK() || lastXmlUpdate < lastFullUpdate))) {
		listN++;

		try {
			string tmp2;
			string indent;

			string newXmlName = Util::getPath(Util::PATH_USER_CONFIG) + "files" + Util::toString(listN) + ".xml.bz2";
			{
				File f(newXmlName, File::WRITE, File::TRUNCATE | File::CREATE);
				// We don't care about the leaves...
				CalcOutputStream<TTFilter<1024*1024*1024>, false> bzTree(&f);
				FilteredOutputStream<BZFilter, false> bzipper(&bzTree);
				CountOutputStream<false> count(&bzipper);
				CalcOutputStream<TTFilter<1024*1024*1024>, false> newXmlFile(&count);

				newXmlFile.write(SimpleXML::utf8Header);
				newXmlFile.write("<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"/\" Generator=\"" APPNAME " " VERSIONSTRING "\">\r\n");
				for(auto& i: directories) {
					i.second->toXml(newXmlFile, indent, tmp2, -1);
				}
				newXmlFile.write("</FileListing>");
				newXmlFile.flush();

				xmlListLen = count.getCount();

				newXmlFile.getFilter().getTree().finalize();
				bzTree.getFilter().getTree().finalize();

				xmlRoot = newXmlFile.getFilter().getTree().getRoot();
				bzXmlRoot = bzTree.getFilter().getTree().getRoot();
			}

			if(bzXmlRef.get()) {
				bzXmlRef.reset();
				File::deleteFile(getBZXmlFile());
			}

			try {
				File::renameFile(newXmlName, Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2");
				newXmlName =Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2";
			} catch(const FileException&) {
				// Ignore, this is for caching only...
			}
			bzXmlRef = unique_ptr<File>(new File(newXmlName, File::READ, File::OPEN));
			setBZXmlFile(newXmlName);
			bzXmlListLen = File::getSize(newXmlName);
			LogManager::getInstance()->message(str(F_("File list %1% generated") % Util::addBrackets(bzXmlFile)));
		} catch(const Exception&) {
			// No new file lists...
		}

		xmlDirty = false;
		forceXmlRefresh = false;
		lastXmlUpdate = GET_TICK();
	}
}

string ShareManager::generateFileListData(const string& hubUrl, bool compressed) const {
	auto access = getShareAccess(hubUrl);
	string xml = SimpleXML::utf8Header;
	xml += "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() +
		"\" Base=\"/\" Generator=\"" APPNAME " " VERSIONSTRING "\">\r\n";

	{
		StringRefOutputStream output(xml);
		string indent;
		string tmp;
		Lock l(cs);
		// An explicit favorite-hub profile with no selected directories is not
		// unrestricted: the loop intentionally writes no directory entries and
		// returns a valid root-only file list.
		for(const auto& item: directories) {
			if(isVirtualAllowed(item.first, access)) {
				item.second->toXml(output, indent, tmp, -1);
			}
		}
	}
	xml += "</FileListing>";

	if(!compressed) {
		return xml;
	}

	StringOutputStream output;
	FilteredOutputStream<BZFilter, false> compressor(&output);
	compressor.write(xml);
	compressor.flush();
	return output.getString();
}

MemoryInputStream* ShareManager::generateFileList(const string& hubUrl, bool compressed) const {
	return new MemoryInputStream(generateFileListData(hubUrl, compressed));
}

MemoryInputStream* ShareManager::generatePartialList(const string& dir, bool recurse) const {
	if(dir.empty() || dir[0] != '/' || dir[dir.size()-1] != '/')
		return 0;

	string xml = SimpleXML::utf8Header;
	string tmp;
	xml += "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"" + SimpleXML::escape(dir, tmp, true) + "\" Generator=\"" APPNAME " " VERSIONSTRING "\">\r\n";
	// Limit generated XML before compression to keep partial-list requests memory-bounded.
	const auto maxPartialListBytes = static_cast<uint64_t>(std::max(1024, SETTING(MAX_PARTIAL_LIST_BYTES)));
	if(xml.size() >= maxPartialListBytes) {
		return 0;
	}
	StringRefOutputStream stringOutput(xml);
	LimitedOutputStream<false> sos(&stringOutput, maxPartialListBytes - xml.size());
	string indent = "\t";

	Lock l(cs);
	if(dir == "/") {
		for(auto& i: directories) {
			tmp.clear();
			i.second->toXml(sos, indent, tmp, recurse ? -1 : 0);
		}
	} else {
		string::size_type i = 1, j = 1;

		Directory::Ptr root;

		bool first = true;
		while( (i = dir.find('/', j)) != string::npos) {
			if(i == j) {
				j++;
				continue;
			}

			if(first) {
				first = false;
				auto it = directories.find(dir.substr(j, i-j));
				if(it == directories.end())
					return 0;
				root = it->second;

			} else {
				auto it2 = root->directories.find(dir.substr(j, i-j));
				if(it2 == root->directories.end()) {
					return 0;
				}
				root = it2->second;
			}
			j = i + 1;
		}

		if(!root)
			return 0;

		for(auto& it2: root->directories) {
			it2.second->toXml(sos, indent, tmp, recurse ? -1 : 0);
		}
		root->filesToXml(sos, indent, tmp);
	}

	xml += "</FileListing>";
	return new MemoryInputStream(xml);
}

MemoryInputStream* ShareManager::generatePartialList(const string& dir, bool recurse, const string& hubUrl) const {
	if(dir.empty() || dir[0] != '/' || dir[dir.size() - 1] != '/') {
		return nullptr;
	}

	auto access = getShareAccess(hubUrl);
	string xml = SimpleXML::utf8Header;
	string tmp;
	xml += "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() +
		"\" Base=\"" + SimpleXML::escape(dir, tmp, true) + "\" Generator=\"" APPNAME " " VERSIONSTRING "\">\r\n";
	// Apply the same uncompressed bound to TTH-based partial-list generation.
	const auto maxPartialListBytes = static_cast<uint64_t>(std::max(1024, SETTING(MAX_PARTIAL_LIST_BYTES)));
	if(xml.size() >= maxPartialListBytes) {
		return 0;
	}
	StringRefOutputStream stringOutput(xml);
	LimitedOutputStream<false> output(&stringOutput, maxPartialListBytes - xml.size());
	string indent = "\t";

	Lock l(cs);
	if(dir == "/") {
		for(const auto& item: directories) {
			if(isVirtualAllowed(item.first, access)) {
				tmp.clear();
				item.second->toXml(output, indent, tmp, recurse ? -1 : 0);
			}
		}
	} else {
		string::size_type i = 1;
		string::size_type j = 1;
		Directory::Ptr root;
		bool first = true;

		while((i = dir.find('/', j)) != string::npos) {
			if(i == j) {
				++j;
				continue;
			}

			if(first) {
				first = false;
				auto item = directories.find(dir.substr(j, i - j));
				if(item == directories.end() || !isVirtualAllowed(item->first, access)) {
					return nullptr;
				}
				root = item->second;
			} else {
				auto item = root->directories.find(dir.substr(j, i - j));
				if(item == root->directories.end()) {
					return nullptr;
				}
				root = item->second;
			}
			j = i + 1;
		}

		if(!root) {
			return nullptr;
		}

		for(const auto& item: root->directories) {
			item.second->toXml(output, indent, tmp, recurse ? -1 : 0);
		}
		root->filesToXml(output, indent, tmp);
	}

	xml += "</FileListing>";
	return new MemoryInputStream(xml);
}

/* params for partial file lists - when any of these params is not satisfied, an incomplete dir is
returned rather than a full dir list. */
const int8_t maxLevel = 2;
const size_t maxItemsPerLevel[maxLevel] = { 16, 4 };

#define LITERAL(n) n, sizeof(n)-1
void ShareManager::Directory::toXml(OutputStream& xmlFile, string& indent, string& tmp2, int8_t level) const {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(name, tmp2, true));

	if(level < 0 || (level < maxLevel && directories.size() + files.size() <= maxItemsPerLevel[level])) {
		xmlFile.write(LITERAL("\">\r\n"));

		indent += '\t';
		if(level >= 0)
			++level;

		for(auto& i: directories) {
			i.second->toXml(xmlFile, indent, tmp2, level);
		}
		filesToXml(xmlFile, indent, tmp2);

		if(level >= 0)
			--level;
		indent.erase(indent.length()-1);

		xmlFile.write(indent);
		xmlFile.write(LITERAL("</Directory>\r\n"));

	} else {
		if(directories.empty() && files.empty()) {
			xmlFile.write(LITERAL("\" />\r\n"));
		} else {
			xmlFile.write(LITERAL("\" Incomplete=\"1\" />\r\n"));
		}
	}
}

void ShareManager::Directory::filesToXml(OutputStream& xmlFile, string& indent, string& tmp2) const {
	for(auto& f: files) {
		if(!f.tth) { continue; }
		xmlFile.write(indent);
		xmlFile.write(LITERAL("<File Name=\""));
		xmlFile.write(SimpleXML::escape(f.getName(), tmp2, true));
		xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(f.getSize()));
		xmlFile.write(LITERAL("\" TTH=\""));
		tmp2.clear();
		xmlFile.write(f.tth->toBase32(tmp2));
		xmlFile.write(LITERAL("\"/>\r\n"));
	}
}

ShareManager::SearchQuery::SearchQuery() :
	include(&includeInit),
	gt(0),
	lt(numeric_limits<int64_t>::max()),
	isDirectory(false)
{
}

namespace {
	inline uint16_t toCode(char a, char b) { return (uint16_t)a | ((uint16_t)b)<<8; }
}
ShareManager::SearchQuery::SearchQuery(const StringList& adcParams) :
	SearchQuery()
{
	for(auto& p: adcParams) {
		if(p.size() <= 2)
			continue;

		auto cmd = toCode(p[0], p[1]);
		if(toCode('T', 'R') == cmd) {
			root = TTHValue(p.substr(2));
			return;
		} else if(toCode('A', 'N') == cmd) {
			includeInit.emplace_back(p.substr(2));
		} else if(toCode('N', 'O') == cmd) {
			exclude.emplace_back(p.substr(2));
		} else if(toCode('E', 'X') == cmd) {
			ext.push_back(Text::toLower(p.substr(2)));
		} else if(toCode('G', 'R') == cmd) {
			auto exts = AdcHub::parseSearchExts(Util::toInt(p.substr(2)));
			ext.insert(ext.begin(), exts.begin(), exts.end());
		} else if(toCode('R', 'X') == cmd) {
			noExt.push_back(Text::toLower(p.substr(2)));
		} else if(toCode('G', 'E') == cmd) {
			gt = Util::toInt64(p.substr(2));
		} else if(toCode('L', 'E') == cmd) {
			lt = Util::toInt64(p.substr(2));
		} else if(toCode('E', 'Q') == cmd) {
			lt = gt = Util::toInt64(p.substr(2));
		} else if(toCode('T', 'Y') == cmd) {
			isDirectory = p[2] == '2';
		}
	}
}

ShareManager::SearchQuery::SearchQuery(const string& nmdcString, int searchType, int64_t size, int fileType) :
	SearchQuery()
{
	if(fileType == SearchManager::TYPE_TTH && nmdcString.compare(0, 4, "TTH:") == 0) {
		root = TTHValue(nmdcString.substr(4));

	} else {
		StringTokenizer<string> tok(Text::toLower(nmdcString), '$');
		for(auto& term: tok.getTokens()) {
			if(!term.empty()) {
				includeInit.emplace_back(term);
			}
		}

		if(searchType == SearchManager::SIZE_ATLEAST) {
			gt = size;
		} else if(searchType == SearchManager::SIZE_ATMOST) {
			lt = size;
		}

		switch(fileType) {
		case SearchManager::TYPE_AUDIO: ext = AdcHub::parseSearchExts(1 << 0); break;
		case SearchManager::TYPE_COMPRESSED: ext = AdcHub::parseSearchExts(1 << 1); break;
		case SearchManager::TYPE_DOCUMENT: ext = AdcHub::parseSearchExts(1 << 2); break;
		case SearchManager::TYPE_EXECUTABLE: ext = AdcHub::parseSearchExts(1 << 3); break;
		case SearchManager::TYPE_PICTURE: ext = AdcHub::parseSearchExts(1 << 4); break;
		case SearchManager::TYPE_VIDEO: ext = AdcHub::parseSearchExts(1 << 5); break;
		case SearchManager::TYPE_DIRECTORY: isDirectory = true; break;
		}
	}
}

bool ShareManager::SearchQuery::isExcluded(const string& str) {
	for(auto& i: exclude) {
		if(i.match(str))
			return true;
	}
	return false;
}

bool ShareManager::SearchQuery::hasExt(const string& name) {
	if(ext.empty())
		return true;
	if(!noExt.empty()) {
		ext = StringList(ext.begin(), set_difference(ext.begin(), ext.end(), noExt.begin(), noExt.end(), ext.begin()));
		noExt.clear();
	}
	auto fileExt = Util::getFileExt(name);
	return !fileExt.empty() && std::find(ext.cbegin(), ext.cend(), Text::toLower(fileExt.substr(1))) != ext.cend();
}

/**
 * Alright, the main point here is that when searching, a search string is most often found in
 * the filename, not directory name, so we want to make that case faster. Also, we want to
 * avoid changing StringLists unless we absolutely have to --> this should only be done if a string
 * has been matched in the directory name. This new stringlist should also be used in all descendants,
 * but not the parents...
 */
void ShareManager::Directory::search(SearchResultList& results, SearchQuery& query, size_t maxResults) const noexcept {
	if(query.isExcluded(name))
		return;

	// Find any matches in the directory name and removed matched terms from the query.
	unique_ptr<StringSearch::List> newTerms;

	for(auto& term: *query.include) {
		if(term.match(name)) {
			if(!newTerms) {
				newTerms.reset(new StringSearch::List(*query.include));
			}
			newTerms->erase(remove(newTerms->begin(), newTerms->end(), term), newTerms->end());
		}
	}

	auto const old = query.include;
	ScopedFunctor(([old, &query] { query.include = old; }));
	if(newTerms) {
		query.include = newTerms.get();
	}

	if(query.include->empty() && query.ext.empty() && query.gt == 0) {
		// We satisfied all the search words! Add the directory...
		/// @todo send the directory hash when we have one
		results.push_back(new SearchResult(SearchResult::TYPE_DIRECTORY, getSize(), getFullName(), TTHValue(string(39, 'A'))));
		ShareManager::getInstance()->addHits(1);
	}

	if(!query.isDirectory) {
		for(auto& i: files) {
			if(!i.tth) { continue; }

			// check the size
			if(!(i.getSize() >= query.gt)) {
				continue;
			} else if(!(i.getSize() <= query.lt)) {
				continue;
			}

			if(query.isExcluded(i.getName()))
				continue;

			// check if the name matches
			auto j = query.include->begin();
			for(; j != query.include->end() && j->match(i.getName()); ++j)
				;	// Empty
			if(j != query.include->end())
				continue;

			// check extensions
			if(!query.hasExt(i.getName()))
				continue;

			results.push_back(new SearchResult(SearchResult::TYPE_FILE, i.getSize(),
				getFullName() + i.getName(), *i.tth));
			ShareManager::getInstance()->addHits(1);

			if(results.size() >= maxResults) { return; }
		}
	}

	for(auto& dir: directories) {
		dir.second->search(results, query, maxResults);

		if(results.size() >= maxResults) { return; }
	}
}

SearchResultList ShareManager::search(SearchQuery&& query, size_t maxResults) noexcept {
	SearchResultList results;

	Lock l(cs);

	if(query.root) {
		auto i = tthIndex.find(*query.root);
		if(i != tthIndex.end()) {
			results.push_back(new SearchResult(SearchResult::TYPE_FILE, i->second->getSize(),
				i->second->getParent()->getFullName() + i->second->getName(), *i->second->tth));
			addHits(1);
		}
		return results;
	}

	for(auto& i: query.includeInit) {
		if(!bloom.match(i.getPattern()))
			return results;
	}

	for(auto& dir: directories) {
		dir.second->search(results, query, maxResults);

		if(results.size() >= maxResults) { return results; }
	}

	return results;
}

SearchResultList ShareManager::search(SearchQuery&& query, size_t maxResults, const ShareAccess& access) noexcept {
	SearchResultList results;
	Lock l(cs);

	if(query.root) {
		auto item = tthIndex.find(*query.root);
		if(item != tthIndex.end() && isFileAllowed(*item->second, access)) {
			results.push_back(new SearchResult(SearchResult::TYPE_FILE, item->second->getSize(),
				item->second->getParent()->getFullName() + item->second->getName(), *item->second->tth));
			addHits(1);
		}
		return results;
	}

	for(const auto& dir: directories) {
		if(!isVirtualAllowed(dir.first, access)) {
			continue;
		}
		dir.second->search(results, query, maxResults);
		if(results.size() >= maxResults) {
			return results;
		}
	}
	return results;
}

SearchResultList ShareManager::search(const StringList& adcParams, size_t maxResults) noexcept {
#if DCPP_TIME_SEARCHES
	auto start = GET_TICK();
	ScopedFunctor(([start] {
		LogManager::getInstance()->message("The ADC search took " + Util::toString(GET_TICK() - start) + " ms");
	}));
#endif

	return search(SearchQuery(adcParams), maxResults);
}

SearchResultList ShareManager::search(const StringList& adcParams, size_t maxResults, const string& hubUrl) noexcept {
	try {
		return search(SearchQuery(adcParams), maxResults, getShareAccess(hubUrl));
	} catch(...) {
		return SearchResultList();
	}
}

SearchResultList ShareManager::search(const string& nmdcString, int searchType, int64_t size, int fileType, size_t maxResults) noexcept {
#if DCPP_TIME_SEARCHES
	auto start = GET_TICK();
	ScopedFunctor(([start] {
		LogManager::getInstance()->message("The NMDC search took " + Util::toString(GET_TICK() - start) + " ms");
	}));
#endif

	return search(SearchQuery(nmdcString, searchType, size, fileType), maxResults);
}

SearchResultList ShareManager::search(const string& nmdcString, int searchType, int64_t size, int fileType,
	size_t maxResults, const string& hubUrl) noexcept
{
	try {
		return search(SearchQuery(nmdcString, searchType, size, fileType), maxResults, getShareAccess(hubUrl));
	} catch(...) {
		return SearchResultList();
	}
}

ShareManager::Directory::Ptr ShareManager::getDirectory(const string& realPath) noexcept {
	for(auto& mi: shares) {
		if(Util::strnicmp(realPath, mi.first, mi.first.length()) == 0) {
			auto di = directories.find(mi.second);
			if(di == directories.end()) {
				return nullptr;
			}
			auto d = di->second;

			string::size_type i;
			string::size_type j = mi.first.length();
			while((i = realPath.find(PATH_SEPARATOR, j)) != string::npos) {
				auto dirName = realPath.substr(j, i - j);

				auto& subDirs = d->directories;
				d.reset();
				for(auto& subDir: subDirs) {
					if(subDir.second->getRealName() == dirName) {
						d = subDir.second;
						break;
					}
				}
				if(!d) {
					return nullptr;
				}

				j = i + 1;
			}
			return d;
		}
	}
	return nullptr;
}

optional<std::reference_wrapper<const ShareManager::Directory::File>> ShareManager::getFile(const string& realPath, Directory::Ptr d) noexcept {
	if(!d) {
		d = getDirectory(realPath);
		if(!d) {
			return nullopt;
		}
	}

	auto i = d->findFile(Util::getFileName(realPath));
	if(i == d->files.end()) {
		/* should never happen, but let's fail gracefully (maybe a synchro issue with a dir being
		removed during hashing...)... */
		dcdebug("ShareManager::getFile: the file <%s> could not be found, strange!\n", realPath.c_str());
		return nullopt;
	}

	if(i->realPath && i->realPath == realPath) {
		return std::cref(*i);
	}

	/* see if the files sorted right before this one have a real path we are looking for. this is
	the most common case for dupes: "x (1).ext" is sorted before "x.ext". */
	auto real = i;
	while(real != d->files.begin()) {
		--real;
		if(!real->realPath) {
			break;
		}
		if(real->realPath == realPath) {
			return std::cref(*real);
		}
	}

	/* couldn't find it before the base file? maybe it's sorted after; could happen with files with
	no ext: "x (1)" is sorted after "x". */
	real = i;
	++real;
	while(real != d->files.end() && real->realPath) {
		if(real->realPath == realPath) {
			return std::cref(*real);
		}
		++real;
	}

	/* most common case: no duplicate; just return the base file. */
	return std::cref(*i);
}

void ShareManager::on(QueueManagerListener::FileMoved, const string& realPath) noexcept {
	if(SETTING(ADD_FINISHED_INSTANTLY)) {
		auto size = File::getSize(realPath);
		if(size == -1) {
			// looks like the file isn't actually there...
			return;
		}

		Lock l(cs);
		// Check if the finished download dir is supposed to be shared
		auto dir = getDirectory(realPath);
		if(dir) {
			Directory::File f(Util::getFileName(realPath), size, dir,
				HashManager::getInstance()->getTTH(realPath, size, 0));
			f.validateName(Util::getFilePath(realPath));
			dir->files.insert(move(f));
		}
	}
}

void ShareManager::on(HashManagerListener::TTHDone, const string& realPath, const TTHValue& root) noexcept {
	Lock l(cs);
	auto f = getFile(realPath);
	if(f) {
		auto& file = const_cast<Directory::File&>(f->get());
		if(file.tth && root != *file.tth)
			tthIndex.erase(*file.tth);
		file.tth = root;
		tthIndex[*file.tth] = &file;

		setDirty();
		forceXmlRefresh = true;

		bloom.add(Text::toLower(Util::getFileName(realPath)));
	}
}

void ShareManager::on(TimerManagerListener::Minute, uint64_t tick) noexcept {
	if(SETTING(AUTO_REFRESH_TIME) > 0) {
		if(lastFullUpdate + SETTING(AUTO_REFRESH_TIME) * 60 * 1000 <= tick) {
			refresh(true, true);
		}
	}
}

} // namespace dcpp
