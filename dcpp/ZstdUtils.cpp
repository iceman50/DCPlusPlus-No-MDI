/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdinc.h"
#include "ZstdUtils.h"
#include "Exception.h"

namespace dcpp {
namespace {
void checkZstd(size_t result) {
	if(ZSTD_isError(result)) throw Exception(string("Zstandard: ") + ZSTD_getErrorName(result));
}
}

ZstdFilter::ZstdFilter() : ctx(ZSTD_createCCtx(), ZSTD_freeCCtx) {
	if(!ctx) throw Exception("Unable to allocate Zstandard compressor");
	checkZstd(ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_compressionLevel, 6));
	checkZstd(ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_windowLog, 23));
	checkZstd(ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_checksumFlag, 1));
}

bool ZstdFilter::operator()(const void* in, size_t& insize, void* out, size_t& outsize) {
	if(ended) { insize = outsize = 0; return false; }
	ZSTD_inBuffer input { in, insize, 0 };
	ZSTD_outBuffer output { out, outsize, 0 };
	const bool finish = insize == 0;
	const auto result = ZSTD_compressStream2(ctx.get(), &output, &input, finish ? ZSTD_e_end : ZSTD_e_continue);
	checkZstd(result);
	insize = input.pos;
	outsize = output.pos;
	ended = finish && result == 0;
	return !ended;
}

UnZstdFilter::UnZstdFilter() : ctx(ZSTD_createDCtx(), ZSTD_freeDCtx) {
	if(!ctx) throw Exception("Unable to allocate Zstandard decompressor");
	checkZstd(ZSTD_DCtx_setParameter(ctx.get(), ZSTD_d_windowLogMax, 23));
}

bool UnZstdFilter::operator()(const void* in, size_t& insize, void* out, size_t& outsize) {
	if(ended) {
		if(insize) throw Exception("Garbage data after Zstandard frame");
		outsize = 0;
		return false;
	}
	// ZST1 carries one ordinary frame, never Zstd's skippable metadata frames.
	static const unsigned char magic[] = { 0x28, 0xb5, 0x2f, 0xfd };
	const auto prefix = std::min(insize, sizeof(magic) - headerBytes);
	for(size_t i = 0; i < prefix; ++i) {
		if(static_cast<const unsigned char*>(in)[i] != magic[headerBytes + i])
			throw Exception("Invalid Zstandard frame header");
	}
	ZSTD_inBuffer input { in, insize, 0 };
	ZSTD_outBuffer output { out, outsize, 0 };
	const auto result = ZSTD_decompressStream(ctx.get(), &output, &input);
	checkZstd(result);
	headerBytes += std::min(input.pos, sizeof(magic) - headerBytes);
	if(result == 0 && input.pos != input.size) throw Exception("Garbage data after Zstandard frame");
	if(!insize && result && !output.pos) throw Exception("Truncated Zstandard frame");
	insize = input.pos;
	outsize = output.pos;
	ended = result == 0;
	return !ended;
}
}
