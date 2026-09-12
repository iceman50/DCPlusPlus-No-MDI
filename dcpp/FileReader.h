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

#ifndef DCPLUSPLUS_DCPP_FILE_READER_H
#define DCPLUSPLUS_DCPP_FILE_READER_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dcpp {

using std::function;
using std::pair;
using std::string;
using std::vector;

class File;

/** Helper class for reading an entire file */

class FileReader {
public:
	FileReader(const FileReader&) = delete;
	FileReader& operator=(const FileReader&) = delete;

	typedef function<bool(const void*, size_t)> DataCallback;

	/**
	 * Set up file reader
	 * @param direct Bypass system caches - good for reading files which are not in the cache and should not be there (for example when hashing)
	 * @param blockSize Read block size, 0 = use default
	 */
	FileReader(bool direct = false, size_t blockSize = 0) : direct(direct), blockSize(blockSize) { }

	/**
	 * Read the file. Cached reads fill the requested block size across short system reads; only the final block may be short.
	 * The direct Windows path may use alignment-derived chunk sizes.
	 * @param file File name
	 * @param callback Called for each block. The reader owns the memory until the callback returns; false stops all further callbacks.
	 * @return The number of bytes actually read
	 * @throw FileException if the read fails
	 */
	size_t read(const string& file, const DataCallback& callback);

	/** Read from the handle's current position so validation and hashing use the same filesystem object. */
	size_t read(File& file, const DataCallback& callback);

private:
	static const size_t DEFAULT_BLOCK_SIZE = 1024*1024;

	bool direct;
	size_t blockSize;

	vector<uint8_t> buffer;

	/** Return an aligned buffer which is at least twice the size of ret.second */
	size_t getBlockSize(size_t alignment);
	void* align(void* buf, size_t alignment);

	size_t readDirect(const string& file, const DataCallback& callback);
	size_t readCached(const string& file, const DataCallback& callback);
	size_t readCached(File& file, const DataCallback& callback);
};

}

#endif
