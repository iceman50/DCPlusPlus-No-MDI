/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include "Emoticons.h"

#include <dcpp/EmoticonManager.h>
#include <dcpp/File.h>
#include <dcpp/Text.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <wincodec.h>

using namespace dcpp;

namespace {
	void appendHex(tstring& out, unsigned char value) {
		static const TCHAR digits[] = _T("0123456789abcdef");
		out += digits[value >> 4];
		out += digits[value & 0x0f];
	}

	template<typename T> void release(T*& value) {
		if(value) {
			value->Release();
			value = nullptr;
		}
	}

	std::vector<unsigned> readIconBitDepths(const string& path) {
		try {
			const auto data = File(path, File::READ, File::OPEN).read();
			if(data.size() < 6 || static_cast<unsigned char>(data[0]) != 0 ||
				static_cast<unsigned char>(data[1]) != 0 || static_cast<unsigned char>(data[2]) != 1 ||
				static_cast<unsigned char>(data[3]) != 0) return {};
			const auto read16 = [&data](size_t offset) {
				return static_cast<unsigned>(static_cast<unsigned char>(data[offset])) |
					(static_cast<unsigned>(static_cast<unsigned char>(data[offset + 1])) << 8);
			};
			const auto count = read16(4);
			if(!count || count > (data.size() - 6) / 16) return {};
			std::vector<unsigned> depths;
			depths.reserve(count);
			for(size_t index = 0; index < count; ++index) {
				auto depth = read16(6 + index * 16 + 6);
				depths.push_back(depth ? depth : 32);
			}
			return depths;
		} catch(const Exception&) {
			return {};
		}
	}

	bool encodePng(const string& path, int targetHeight, int preferredBitDepth,
		std::vector<unsigned char>& png, UINT& width, UINT& height) {
		const auto comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		const bool uninitialize = comResult == S_OK || comResult == S_FALSE;
		if(FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) return false;

		IWICImagingFactory* factory = nullptr;
		IWICBitmapDecoder* decoder = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		IWICBitmapScaler* scaler = nullptr;
		IWICFormatConverter* converter = nullptr;
		IWICBitmapEncoder* encoder = nullptr;
		IWICBitmapFrameEncode* encodedFrame = nullptr;
		IPropertyBag2* properties = nullptr;
		IStream* stream = nullptr;
		bool success = false;

		do {
			if(FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
				IID_IWICImagingFactory, reinterpret_cast<void**>(&factory)))) break;
			if(FAILED(factory->CreateDecoderFromFilename(Text::toT(path).c_str(), nullptr, GENERIC_READ,
				WICDecodeMetadataCacheOnLoad, &decoder))) break;
			UINT frameCount = 0;
			if(FAILED(decoder->GetFrameCount(&frameCount)) || !frameCount) break;
			const auto iconDepths = readIconBitDepths(path);
			uint64_t bestScore = UINT64_MAX;
			for(UINT index = 0; index < frameCount; ++index) {
				IWICBitmapFrameDecode* candidate = nullptr;
				if(FAILED(decoder->GetFrame(index, &candidate))) continue;
				UINT candidateWidth = 0, candidateHeight = 0;
				if(SUCCEEDED(candidate->GetSize(&candidateWidth, &candidateHeight)) && candidateWidth && candidateHeight) {
					const auto sizeDistance = candidateHeight > static_cast<UINT>(targetHeight) ?
						candidateHeight - static_cast<UINT>(targetHeight) : static_cast<UINT>(targetHeight) - candidateHeight;
					const auto frameDepth = index < iconDepths.size() ? iconDepths[index] : static_cast<unsigned>(preferredBitDepth);
					const auto depthDistance = frameDepth > static_cast<unsigned>(preferredBitDepth) ?
						frameDepth - preferredBitDepth : preferredBitDepth - frameDepth;
					const uint64_t score = (frameDepth < 16 ? (uint64_t{1} << 56) : 0) +
						(static_cast<uint64_t>(depthDistance) << 32) +
						(frameDepth < static_cast<unsigned>(preferredBitDepth) ? (uint64_t{1} << 31) : 0) +
						static_cast<uint64_t>(sizeDistance) * 2 + (candidateHeight < static_cast<UINT>(targetHeight) ? 1 : 0);
					// Never choose a sub-16-bpp frame when a suitable frame exists. On other ties prefer
					// higher depth, downscaling over upscaling, then the later frame.
					if(score <= bestScore) {
						release(frame);
						frame = candidate;
						candidate = nullptr;
						bestScore = score;
					}
				}
				release(candidate);
			}
			if(!frame) break;

			UINT sourceWidth = 0, sourceHeight = 0;
			if(FAILED(frame->GetSize(&sourceWidth, &sourceHeight)) || !sourceWidth || !sourceHeight) break;
			height = static_cast<UINT>(std::max(1, targetHeight));
			const auto scaledWidth = (static_cast<uint64_t>(sourceWidth) * height + sourceHeight / 2) / sourceHeight;
			width = static_cast<UINT>(std::clamp<uint64_t>(scaledWidth, 1, static_cast<uint64_t>(height) * 4));

			if(FAILED(factory->CreateBitmapScaler(&scaler))) break;
			if(FAILED(scaler->Initialize(frame, width, height, WICBitmapInterpolationModeFant))) break;

			// RichEdit receives a full-color PNG regardless of the preferred ICO frame depth. Encoding
			// to an indexed PNG here needlessly quantizes BMP/PNG assets and higher-depth ICO frames.
			WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
			if(FAILED(factory->CreateFormatConverter(&converter))) break;
			if(FAILED(converter->Initialize(scaler, format, WICBitmapDitherTypeNone,
				nullptr, 0.0, WICBitmapPaletteTypeMedianCut))) break;

			if(FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) break;
			if(FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) break;
			if(FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) break;
			if(FAILED(encoder->CreateNewFrame(&encodedFrame, &properties))) break;
			if(FAILED(encodedFrame->Initialize(properties))) break;
			if(FAILED(encodedFrame->SetSize(width, height))) break;
			if(FAILED(encodedFrame->SetPixelFormat(&format))) break;
			if(FAILED(encodedFrame->WriteSource(converter, nullptr))) break;
			if(FAILED(encodedFrame->Commit()) || FAILED(encoder->Commit())) break;

			STATSTG stat = {};
			if(FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart <= 0 ||
				stat.cbSize.QuadPart > ULONG_MAX) break;
			png.resize(static_cast<size_t>(stat.cbSize.QuadPart));
			LARGE_INTEGER start = {};
			if(FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr))) break;
			ULONG read = 0;
			if(FAILED(stream->Read(png.data(), static_cast<ULONG>(png.size()), &read)) || read != png.size()) break;
			success = true;
		} while(false);

		release(properties);
		release(encodedFrame);
		release(encoder);
		release(stream);
		release(converter);
		release(scaler);
		release(frame);
		release(decoder);
		release(factory);
		if(uninitialize) CoUninitialize();
		return success;
	}
}

tstring Emoticons::rtf(const std::string& name, int pixels, int bitDepth) {
	const auto path = EmoticonManager::getIconPath(name);
	if(path.empty()) return tstring();
	const auto revision = EmoticonManager::getRevision();
	pixels = std::clamp(pixels, 16, 24);
	if(bitDepth != 24 && bitDepth != 32) bitDepth = 16;
	const int twips = MulDiv(pixels, 1440, 96);

	static std::mutex mutex;
	static std::unordered_map<std::string, tstring> cache;
	static uint64_t cacheRevision = 0;
	const auto key = path + ':' + std::to_string(pixels) + ':' + std::to_string(bitDepth);
	std::lock_guard<std::mutex> lock(mutex);
	if(cacheRevision != revision) {
		cache.clear();
		cacheRevision = revision;
	}
	if(auto i = cache.find(key); i != cache.end()) return i->second;

	UINT width = 0, height = 0;
	std::vector<unsigned char> png;
	if(!encodePng(path, pixels, bitDepth, png, width, height)) return tstring();
	const int goalWidth = std::max(1, MulDiv(twips, static_cast<int>(width), static_cast<int>(height)));

	tstring ret = _T("{\\pict\\pngblip\\picw") + std::to_wstring(width) + _T("\\pich") + std::to_wstring(height) +
		_T("\\picwgoal") + std::to_wstring(goalWidth) + _T("\\pichgoal") + std::to_wstring(twips) + _T("\n");
	ret.reserve(ret.size() + png.size() * 2 + 1);
	for(auto value: png) appendHex(ret, value);
	ret += _T("}");
	if(cache.size() >= 128) cache.erase(cache.begin());
	return cache.emplace(key, std::move(ret)).first->second;
}
