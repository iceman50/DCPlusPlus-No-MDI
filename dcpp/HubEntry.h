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

#ifndef DCPLUSPLUS_DCPP_HUBENTRY_H_
#define DCPLUSPLUS_DCPP_HUBENTRY_H_

#include <set>
#include <string>

#include "GetSet.h"
#include "HubSettings.h"
#include "typedefs.h"
#include "Util.h"

namespace dcpp {

using std::string;

class HubEntry {
public:
	HubEntry(const string& aName, const string& aServer, const string& aDescription, const string& aUsers) :
	name(aName), server(aServer), description(aDescription), country(Util::emptyString),
	rating(Util::emptyString), reliability(0.0), shared(0), minShare(0), users(Util::toInt(aUsers)), minSlots(0), maxHubs(0), maxUsers(0) { }

	HubEntry(const string& aName, const string& aServer, const string& aDescription, const string& aUsers, const string& aCountry,
		const string& aShared, const string& aMinShare, const string& aMinSlots, const string& aMaxHubs, const string& aMaxUsers,
		const string& aReliability, const string& aRating) : name(aName), server(aServer), description(aDescription), country(aCountry),
		rating(aRating), reliability((float)(Util::toFloat(aReliability) / 100.0)), shared(Util::toInt64(aShared)), minShare(Util::toInt64(aMinShare)),
		users(Util::toInt(aUsers)), minSlots(Util::toInt(aMinSlots)), maxHubs(Util::toInt(aMaxHubs)), maxUsers(Util::toInt(aMaxUsers))
	{

	}

	GETSET(string, name, Name);
	GETSET(string, server, Server);
	GETSET(string, description, Description);
	GETSET(string, country, Country);
	GETSET(string, rating, Rating);
	GETSET(float, reliability, Reliability);
	GETSET(int64_t, shared, Shared);
	GETSET(int64_t, minShare, MinShare);
	GETSET(int, users, Users);
	GETSET(int, minSlots, MinSlots);
	GETSET(int, maxHubs, MaxHubs);
	GETSET(int, maxUsers, MaxUsers);
};

class FavoriteHubEntry : public HubSettings {
public:
	FavoriteHubEntry() : encoding(Text::systemCharset), shareProfileSet(false) { }
	FavoriteHubEntry(const HubEntry& rhs) : name(rhs.getName()), server(rhs.getServer()),
		hubDescription(rhs.getDescription()), encoding(Text::systemCharset), shareProfileSet(false) { }

	GETSET(string, name, Name);
	GETSET(string, server, Server);
	GETSET(string, hubDescription, HubDescription);
	GETSET(string, password, Password);
	GETSET(string, encoding, Encoding);
	GETSET(string, group, Group);
	GETSET(StringList, failoverServers, FailoverServers);

	bool hasServer(const string& aUrl) const {
		if(Util::stricmp(getServer(), aUrl) == 0)
			return true;

		for(auto& s: getFailoverServers()) {
			if(Util::stricmp(s, aUrl) == 0)
				return true;
		}
		return false;
	}

	bool hasShareProfile() const { return shareProfileSet; }
	const std::set<string>& getShareDirectories() const { return shareDirectories; }
	void setShareDirectories(const std::set<string>& directories) {
		shareDirectories = directories;
		shareProfileSet = true;
	}
	void clearShareProfile() {
		shareDirectories.clear();
		shareProfileSet = false;
	}

private:
	std::set<string> shareDirectories;
	bool shareProfileSet;
};

}

#endif /*HUBENTRY_H_*/
