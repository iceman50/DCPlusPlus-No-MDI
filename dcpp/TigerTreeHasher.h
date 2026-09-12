/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_DCPP_TIGER_TREE_HASHER_H
#define DCPLUSPLUS_DCPP_TIGER_TREE_HASHER_H

#include <cstddef>
#include <cstdint>
#include <functional>

#include "MerkleTree.h"

namespace dcpp {

class File;

/**
 * Builds a Tiger Tree Hash with ordered file input and parallel leaf calculation.
 *
 * The caller owns the file handle and all policy decisions. File bytes are observed
 * in file order on the calling thread, while isolated TTH blocks are processed by a
 * bounded worker pool. No listener, settings, or hash-store object is accessed by a
 * worker unless the caller explicitly does so from a progress callback.
 */
class TigerTreeHasher {
public:
	using DataObserver = std::function<bool (const void*, size_t)>;
	using ProgressCallback = std::function<void (size_t)>;

	/** Return a safe worker count bounded by the request, CPU count, leaf count, and memory budget. */
	static size_t getWorkerCount(size_t requested, int64_t fileSize, int64_t blockSize) noexcept;

	/** Hash the current file position through EOF and return false after orderly cancellation or a size mismatch. */
	static bool hash(File& file, int64_t expectedSize, int64_t blockSize, size_t requestedWorkers, TigerTree& tree, const DataObserver& observer, const ProgressCallback& progress);
};

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_TIGER_TREE_HASHER_H
