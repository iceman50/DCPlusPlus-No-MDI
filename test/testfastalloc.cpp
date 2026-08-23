/*
 * Copyright (C) 2001-2026 iceman50
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

#include "testbase.h"

#include <dcpp/DirectoryListing.h>
#include <dcpp/OnlineUser.h>
#include <dcpp/QueueItem.h>
#include <dcpp/SearchResult.h>
#include <dcpp/User.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace dcpp;

namespace {

struct alignas(64) AlignedProbe : FastAlloc<AlignedProbe> {
	explicit AlignedProbe(size_t aValue) : value(aValue) { }

	size_t value;
};

struct ThrowingProbe : FastAlloc<ThrowingProbe> {
	explicit ThrowingProbe(bool fail) {
		lastAddress = this;
		if(fail) {
			throw std::runtime_error("FastAlloc constructor test");
		}
	}

	static void* lastAddress;
};

void* ThrowingProbe::lastAddress = nullptr;

struct ConcurrentProbe : FastAlloc<ConcurrentProbe> {
	ConcurrentProbe(size_t aOwner, size_t aIndex) : owner(aOwner), index(aIndex), marker(MARKER) { ++live; }
	~ConcurrentProbe() { --live; }

	enum { MARKER = 0x5a17c0de };
	static std::atomic<size_t> live;
	size_t owner;
	size_t index;
	uint32_t marker;
};

std::atomic<size_t> ConcurrentProbe::live(0);

struct DerivedBaseProbe : FastAlloc<DerivedBaseProbe> {
	DerivedBaseProbe() { ++live; }
	virtual ~DerivedBaseProbe() { --live; }

	static std::atomic<size_t> live;
};

std::atomic<size_t> DerivedBaseProbe::live(0);

struct DerivedProbe : DerivedBaseProbe {
	DerivedProbe() { ++derivedLive; }
	~DerivedProbe() override { --derivedLive; }

	static std::atomic<size_t> derivedLive;
	std::array<uint8_t, 256> payload;
};

std::atomic<size_t> DerivedProbe::derivedLive(0);

template<class T>
void verifyPointers(const std::vector<T*>& values) {
	std::vector<uintptr_t> addresses;
	addresses.reserve(values.size());
	for(auto value: values) {
		EXPECT_EQ(0U, reinterpret_cast<uintptr_t>(value) % alignof(T));
		addresses.push_back(reinterpret_cast<uintptr_t>(value));
	}
	std::sort(addresses.begin(), addresses.end());
	EXPECT_EQ(addresses.end(), std::adjacent_find(addresses.begin(), addresses.end()));
}

template<class T, class Factory>
void verifyConstructedBatch(size_t count, Factory factory) {
	std::vector<T*> values;
	values.reserve(count);
	for(size_t i = 0; i < count; ++i) {
		values.push_back(factory(i));
	}
	verifyPointers(values);
	for(auto value: values) {
		delete value;
	}
}

#ifndef _DEBUG
template<class T>
void verifyRawBatch(size_t count) {
	std::vector<T*> values;
	values.reserve(count);
	for(size_t i = 0; i < count; ++i) {
		void* memory = T::operator new(sizeof(T));
		memset(memory, static_cast<int>(i), sizeof(T));
		values.push_back(static_cast<T*>(memory));
	}
	verifyPointers(values);
	for(size_t i = 0; i < values.size(); ++i) {
		EXPECT_EQ(static_cast<uint8_t>(i), reinterpret_cast<uint8_t*>(values[i])[sizeof(T) - 1]);
		T::operator delete(values[i], sizeof(T));
	}
}
#endif

}

TEST(FastAllocTest, preserves_overaligned_storage) {
	std::vector<AlignedProbe*> values;
	values.reserve(50000);
	for(size_t i = 0; i < 50000; ++i) {
		values.push_back(new AlignedProbe(i));
	}
	verifyPointers(values);
	for(size_t i = 0; i < values.size(); ++i) {
		EXPECT_EQ(i, values[i]->value);
		delete values[i];
	}
}

TEST(FastAllocTest, returns_storage_when_constructors_throw) {
	for(size_t i = 0; i < 10000; ++i) {
		EXPECT_THROW(new ThrowingProbe(true), std::runtime_error);
		void* failedAddress = ThrowingProbe::lastAddress;
		ThrowingProbe* value = new ThrowingProbe(false);
#ifndef _DEBUG
		EXPECT_EQ(failedAddress, value);
#else
		EXPECT_NE(nullptr, failedAddress);
#endif
		delete value;
	}
}

TEST(FastAllocTest, supports_cross_thread_free_and_thread_cache_exit) {
	const size_t threadCount = 4;
	const size_t count = 25000;
	for(size_t round = 0; round < 3; ++round) {
		std::array<std::vector<ConcurrentProbe*>, threadCount> values;
		std::vector<std::thread> threads;
		for(size_t owner = 0; owner < threadCount; ++owner) {
			threads.emplace_back([&, owner] {
				values[owner].reserve(count);
				for(size_t i = 0; i < count; ++i) {
					values[owner].push_back(new ConcurrentProbe(owner, i));
				}
			});
		}
		for(auto& thread: threads) {
			thread.join();
		}
		EXPECT_EQ(threadCount * count, ConcurrentProbe::live.load());

		std::vector<ConcurrentProbe*> all;
		all.reserve(threadCount * count);
		for(size_t owner = 0; owner < threadCount; ++owner) {
			for(size_t i = 0; i < count; ++i) {
				EXPECT_EQ(owner, values[owner][i]->owner);
				EXPECT_EQ(i, values[owner][i]->index);
				EXPECT_EQ(static_cast<uint32_t>(ConcurrentProbe::MARKER), values[owner][i]->marker);
				all.push_back(values[owner][i]);
			}
		}
		verifyPointers(all);

		threads.clear();
		for(size_t worker = 0; worker < threadCount; ++worker) {
			threads.emplace_back([&, worker] {
				auto& ownedByAnotherThread = values[(worker + 1) % threadCount];
				for(auto value: ownedByAnotherThread) {
					delete value;
				}
			});
		}
		for(auto& thread: threads) {
			thread.join();
		}
		EXPECT_EQ(0U, ConcurrentProbe::live.load());
	}
}

TEST(FastAllocTest, falls_back_for_derived_allocations) {
	std::vector<DerivedBaseProbe*> values;
	values.reserve(25000);
	for(size_t i = 0; i < 25000; ++i) {
		values.push_back(new DerivedProbe);
	}
	EXPECT_EQ(values.size(), DerivedBaseProbe::live.load());
	EXPECT_EQ(values.size(), DerivedProbe::derivedLive.load());
	for(auto value: values) {
		delete value;
	}
	EXPECT_EQ(0U, DerivedBaseProbe::live.load());
	EXPECT_EQ(0U, DerivedProbe::derivedLive.load());

	for(size_t i = 0; i < 10000; ++i) {
		DirectoryListing::Directory* value = new DirectoryListing::AdlDirectory("/ADL/", nullptr, "ADL");
		delete value;
	}
}

TEST(FastAllocTest, constructs_high_volume_dcpp_types) {
	const size_t count = 15000;
	std::array<uint8_t, TTHValue::BYTES> hashData;
	hashData.fill(0x5a);
	const TTHValue tth(hashData.data());
	const CID cid;
	const HintedUser hintedUser;
	const Style style;

	verifyConstructedBatch<User>(count, [&](size_t) { return new User(cid); });
	verifyConstructedBatch<TTHValue>(count, [&](size_t) { return new TTHValue(tth); });
	verifyConstructedBatch<QueueItem>(count, [&](size_t i) { return new QueueItem("/fastalloc/" + Util::toString(static_cast<unsigned int>(i)), 4096, QueueItem::NORMAL, 0, 0, tth); });
	verifyConstructedBatch<SearchResult>(count, [&](size_t i) { return new SearchResult(hintedUser, SearchResult::TYPE_FILE, 3, 2, 4096, "file-" + Util::toString(static_cast<unsigned int>(i)), "hub", "127.0.0.1", tth, "token", style); });
	verifyConstructedBatch<DirectoryListing::File>(count, [&](size_t i) { return new DirectoryListing::File(nullptr, "file-" + Util::toString(static_cast<unsigned int>(i)), 4096, tth); });
	verifyConstructedBatch<DirectoryListing::Directory>(count, [&](size_t i) { return new DirectoryListing::Directory(nullptr, "dir-" + Util::toString(static_cast<unsigned int>(i)), false, true); });
}

#ifndef _DEBUG
TEST(FastAllocTest, allocates_high_volume_raw_dcpp_storage) {
	const size_t count = 25000;
	verifyRawBatch<User>(count);
	verifyRawBatch<OnlineUser>(count);
	verifyRawBatch<SearchResult>(count);
	verifyRawBatch<QueueItem>(count);
	verifyRawBatch<DirectoryListing::File>(count);
	verifyRawBatch<DirectoryListing::Directory>(count);
	verifyRawBatch<TTHValue>(count);
}
#endif
