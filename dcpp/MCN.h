/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_DCPP_MCN_H
#define DCPLUSPLUS_DCPP_MCN_H

#include <algorithm>
#include <cstdint>

namespace dcpp {

/** The transfer class permanently assigned to an MCN download connection. */
enum class MCNDownloadType {
	ANY,
	SMALL,
	NORMAL
};

namespace MCN {

/** MCN1 reserves the priority connection for files strictly below 64 KiB. */
static const int64_t SMALL_FILE_LIMIT = 64 * 1024;

/** A missing CO field and CO0 both mean that the peer has no separate limit. */
inline int normalizeRemoteLimit(int value) noexcept {
	return value > 0 ? std::min(value, 100) : 0;
}

inline int effectiveDownloadLimit(int localLimit, int remoteLimit) noexcept {
	localLimit = std::max(1, localLimit);
	return remoteLimit > 0 ? std::min(localLimit, remoteLimit) : localLimit;
}

/** Number of unused regular TCP connections in the global upload allocation. */
inline int freeRegularConnections(int slots, int runningUsers, int mcnConnections, int mcnUsers) noexcept {
	return slots - runningUsers - mcnConnections + mcnUsers;
}

/**
 * Existing MCN users may grow toward the largest peer allocation. When no
 * physical connection is free, the resulting connection replaces one from an
 * overrepresented user.
 */
inline bool allowNewRegularConnection(int currentConnections, int highestOtherConnections,
	int freeConnections, bool hasQueuedUsers, int perUserLimit) noexcept
{
	if(perUserLimit > 0 && currentConnections >= perUserLimit) {
		return false;
	}
	if(hasQueuedUsers) {
		return false;
	}

	return freeConnections > 0 || currentConnections + 1 <= highestOtherConnections;
}

} // namespace MCN
} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_MCN_H
