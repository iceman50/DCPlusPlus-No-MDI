/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_DCPP_ZSTD_UTILS_H
#define DCPLUSPLUS_DCPP_ZSTD_UTILS_H

#include <cstddef>
#include <memory>
#include <zstd.h>

namespace dcpp {

// One frame per transfer/chunk; no dictionaries, at most an 8 MiB window.
class ZstdFilter {
public:
	ZstdFilter();
	bool operator()(const void* in, size_t& insize, void* out, size_t& outsize);
private:
	std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> ctx;
	bool ended = false;
};

class UnZstdFilter {
public:
	UnZstdFilter();
	bool operator()(const void* in, size_t& insize, void* out, size_t& outsize);
private:
	std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> ctx;
	bool ended = false;
	size_t headerBytes = 0;
};

}
#endif
