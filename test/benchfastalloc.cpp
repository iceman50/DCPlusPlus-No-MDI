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
#include <dcpp/FastAlloc.h>
#include <dcpp/OnlineUser.h>
#include <dcpp/QueueItem.h>
#include <dcpp/SearchResult.h>
#include <dcpp/User.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

using namespace dcpp;

namespace {

using Clock = std::chrono::steady_clock;

const size_t BLOCK_BYTES = 128 * 1024;
const size_t SAMPLE_COUNT = 7;

#ifdef _MSC_VER
#define BENCHMARK_NOINLINE __declspec(noinline)
#else
#define BENCHMARK_NOINLINE __attribute__((noinline))
#endif

#ifdef _WIN32
const char* WINDOWS_ALLOCATOR_NAME = "Windows process heap";
#else
const char* WINDOWS_ALLOCATOR_NAME = "System new/delete fallback";
#endif

std::atomic<uintptr_t> benchmarkSink(0);

std::mutex& legacyMutex() {
	static std::mutex mutex;
	return mutex;
}

template<class T>
struct LegacyPoolStorage {
	union Slot {
		alignas(T) uint8_t storage[sizeof(T)];
		Slot* next;
	};

	struct State {
		Slot* freeList = nullptr;
		std::vector<std::unique_ptr<Slot[]> > slabs;
	};

	static State& getState() {
		static State state;
		return state;
	}

	static void grow(State& state) {
		const size_t items = (BLOCK_BYTES + sizeof(Slot) - 1) / sizeof(Slot);
		std::unique_ptr<Slot[]> slab(new Slot[items]);
		for(size_t i = 0; i + 1 < items; ++i) {
			slab[i].next = &slab[i + 1];
		}
		slab[items - 1].next = nullptr;
		Slot* first = slab.get();
		state.slabs.push_back(std::move(slab));
		state.freeList = first;
	}

	static void* allocate() {
		State& state = getState();
		std::lock_guard<std::mutex> lock(legacyMutex());
		if(!state.freeList) {
			grow(state);
		}
		Slot* slot = state.freeList;
		state.freeList = slot->next;
		return slot;
	}

	static void deallocate(void* value) noexcept {
		State& state = getState();
		std::lock_guard<std::mutex> lock(legacyMutex());
		Slot* slot = static_cast<Slot*>(value);
		slot->next = state.freeList;
		state.freeList = slot;
	}
};

template<class T>
struct LegacyFastAlloc {
	static void* operator new(size_t size) {
		return size == sizeof(T) ? LegacyPoolStorage<T>::allocate() : ::operator new(size);
	}

	static void* operator new(size_t, void* memory) noexcept {
		return memory;
	}

	static void operator delete(void*, void*) noexcept {
	}

	static void operator delete(void* memory, size_t size) noexcept {
		if(!memory) {
			return;
		}
		if(size == sizeof(T)) {
			LegacyPoolStorage<T>::deallocate(memory);
		} else {
			::operator delete(memory);
		}
	}

protected:
	~LegacyFastAlloc() { }
};

template<class T>
struct ThreadCachedPoolStorage {
	union Slot {
		alignas(T) uint8_t storage[sizeof(T)];
		Slot* next;
	};

	struct State {
		std::mutex mutex;
		Slot* freeList = nullptr;
		std::vector<std::unique_ptr<Slot[]> > slabs;
	};

	struct LocalCache {
		~LocalCache() {
			ThreadCachedPoolStorage<T>::release(*this);
		}

		Slot* freeList = nullptr;
		size_t count = 0;
	};

	static State& getState() {
		static State* state = new State;
		return *state;
	}

	static LocalCache& getCache() {
		static thread_local LocalCache cache;
		return cache;
	}

	static void grow(State& state) {
		const size_t items = (BLOCK_BYTES + sizeof(Slot) - 1) / sizeof(Slot);
		std::unique_ptr<Slot[]> slab(new Slot[items]);
		for(size_t i = 0; i + 1 < items; ++i) {
			slab[i].next = &slab[i + 1];
		}
		slab[items - 1].next = nullptr;
		Slot* first = slab.get();
		state.slabs.push_back(std::move(slab));
		state.freeList = first;
	}

	static void refill(LocalCache& cache) {
		const size_t batchSize = 32;
		State& state = getState();
		std::lock_guard<std::mutex> lock(state.mutex);
		if(!state.freeList) {
			grow(state);
		}

		Slot* first = state.freeList;
		Slot* last = first;
		size_t count = 1;
		while(count < batchSize && last->next) {
			last = last->next;
			++count;
		}
		state.freeList = last->next;
		last->next = nullptr;
		cache.freeList = first;
		cache.count = count;
	}

	static void release(LocalCache& cache, size_t count = static_cast<size_t>(-1)) noexcept {
		if(!cache.freeList) {
			return;
		}
		count = std::min(count, cache.count);
		Slot* first = cache.freeList;
		Slot* last = first;
		for(size_t i = 1; i < count; ++i) {
			last = last->next;
		}
		cache.freeList = last->next;
		cache.count -= count;

		State& state = getState();
		std::lock_guard<std::mutex> lock(state.mutex);
		last->next = state.freeList;
		state.freeList = first;
	}

	static void* allocate() {
		LocalCache& cache = getCache();
		if(!cache.freeList) {
			refill(cache);
		}
		Slot* slot = cache.freeList;
		cache.freeList = slot->next;
		--cache.count;
		return slot;
	}

	static void deallocate(void* value) noexcept {
		const size_t cacheLimit = 64;
		const size_t releaseCount = 32;
		LocalCache& cache = getCache();
		Slot* slot = static_cast<Slot*>(value);
		slot->next = cache.freeList;
		cache.freeList = slot;
		++cache.count;
		if(cache.count > cacheLimit) {
			release(cache, releaseCount);
		}
	}
};

template<class T>
struct ThreadCachedFastAlloc {
	static void* operator new(size_t size) {
		return size == sizeof(T) ? ThreadCachedPoolStorage<T>::allocate() : ::operator new(size);
	}

	static void* operator new(size_t, void* memory) noexcept {
		return memory;
	}

	static void operator delete(void*, void*) noexcept {
	}

	static void operator delete(void* memory, size_t size) noexcept {
		if(!memory) {
			return;
		}
		if(size == sizeof(T)) {
			ThreadCachedPoolStorage<T>::deallocate(memory);
		} else {
			::operator delete(memory);
		}
	}

protected:
	~ThreadCachedFastAlloc() { }
};

template<class T>
struct WindowsHeapAlloc {
	static void* operator new(size_t size) {
#ifdef _WIN32
		void* memory = HeapAlloc(GetProcessHeap(), 0, size);
		if(!memory) {
			throw std::bad_alloc();
		}
		return memory;
#else
		return ::operator new(size);
#endif
	}

	static void* operator new(size_t, void* memory) noexcept {
		return memory;
	}

	static void operator delete(void*, void*) noexcept {
	}

	static void operator delete(void* memory) noexcept {
#ifdef _WIN32
		if(memory) {
			HeapFree(GetProcessHeap(), 0, memory);
		}
#else
		::operator delete(memory);
#endif
	}

	static void operator delete(void* memory, size_t) noexcept {
		operator delete(memory);
	}

protected:
	~WindowsHeapAlloc() { }
};

template<class T>
struct DefaultRawStorage {
	static void* allocate() { return ::operator new(sizeof(T)); }
	static void deallocate(void* memory) noexcept { ::operator delete(memory); }
};

template<class T>
struct WindowsRawStorage {
	static void* allocate() {
#ifdef _WIN32
		void* memory = HeapAlloc(GetProcessHeap(), 0, sizeof(T));
		if(!memory) {
			throw std::bad_alloc();
		}
		return memory;
#else
		return ::operator new(sizeof(T));
#endif
	}

	static void deallocate(void* memory) noexcept {
#ifdef _WIN32
		HeapFree(GetProcessHeap(), 0, memory);
#else
		::operator delete(memory);
#endif
	}
};

template<class T>
struct LegacyRawStorage {
	static void* allocate() { return LegacyPoolStorage<T>::allocate(); }
	static void deallocate(void* memory) noexcept { LegacyPoolStorage<T>::deallocate(memory); }
};

template<class T>
struct ProductionRawStorage {
	static void* allocate() { return T::operator new(sizeof(T)); }
	static void deallocate(void* memory) noexcept { T::operator delete(memory, sizeof(T)); }
};

template<size_t Size>
struct BenchmarkPayload {
	static_assert(Size >= sizeof(uintptr_t), "The benchmark payload is too small");

	explicit BenchmarkPayload(uintptr_t value_) : value(value_) { }

	uintptr_t value;
	std::array<uint8_t, Size - sizeof(uintptr_t)> padding;
};

template<size_t Size, size_t Tag>
struct DefaultNode {
	explicit DefaultNode(uintptr_t value) : payload(value) { }
	BenchmarkPayload<Size> payload;
};

template<size_t Size, size_t Tag>
struct WindowsNode : WindowsHeapAlloc<WindowsNode<Size, Tag> > {
	explicit WindowsNode(uintptr_t value) : payload(value) { }
	BenchmarkPayload<Size> payload;
};

template<size_t Size, size_t Tag>
struct CurrentNode : FastAlloc<CurrentNode<Size, Tag> > {
	explicit CurrentNode(uintptr_t value) : payload(value) { }
	BenchmarkPayload<Size> payload;
};

template<size_t Size, size_t Tag>
struct LegacyNode : LegacyFastAlloc<LegacyNode<Size, Tag> > {
	explicit LegacyNode(uintptr_t value) : payload(value) { }
	BenchmarkPayload<Size> payload;
};

template<size_t Size, size_t Tag>
struct ThreadCachedNode : ThreadCachedFastAlloc<ThreadCachedNode<Size, Tag> > {
	explicit ThreadCachedNode(uintptr_t value) : payload(value) { }
	BenchmarkPayload<Size> payload;
};

template<class T>
BENCHMARK_NOINLINE void consume(const std::vector<T*>& values) {
	uintptr_t total = 0;
	for(const auto value: values) {
		total += value->payload.value;
	}
	benchmarkSink.fetch_xor(total, std::memory_order_relaxed);
}

BENCHMARK_NOINLINE void consumeRaw(const std::vector<void*>& values) {
	uintptr_t total = 0;
	for(const auto value: values) {
		total += reinterpret_cast<uintptr_t>(value);
	}
	benchmarkSink.fetch_xor(total, std::memory_order_relaxed);
}

template<class T>
double runBatch(size_t count) {
	std::vector<T*> values(count);
	const auto start = Clock::now();
	for(size_t i = 0; i < count; ++i) {
		values[i] = new T(i + 1);
	}
	consume(values);
	for(auto value: values) {
		delete value;
	}
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

template<class T, template<class> class Storage>
double runRawBatch(size_t count) {
	std::vector<void*> values(count);
	const auto start = Clock::now();
	for(size_t i = 0; i < count; ++i) {
		values[i] = Storage<T>::allocate();
	}
	consumeRaw(values);
	for(auto value: values) {
		Storage<T>::deallocate(value);
	}
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

template<class T, class Factory>
double runConstructedBatch(size_t count, Factory factory) {
	std::vector<T*> values(count);
	const auto start = Clock::now();
	for(size_t i = 0; i < count; ++i) {
		values[i] = factory(i);
	}
	uintptr_t total = 0;
	for(const auto value: values) {
		total += reinterpret_cast<uintptr_t>(value);
	}
	benchmarkSink.fetch_xor(total, std::memory_order_relaxed);
	for(auto value: values) {
		delete value;
	}
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

template<class T>
double runChurn(size_t iterations, size_t workingSet) {
	std::vector<T*> values(workingSet);
	for(size_t i = 0; i < workingSet; ++i) {
		values[i] = new T(i + 1);
	}

	const auto start = Clock::now();
	for(size_t i = 0; i < iterations; ++i) {
		const size_t index = i % workingSet;
		delete values[index];
		values[index] = new T(i + workingSet + 1);
	}
	const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

	consume(values);
	for(auto value: values) {
		delete value;
	}
	return elapsed;
}

struct ConcurrentState {
	std::mutex mutex;
	std::condition_variable condition;
	size_t ready = 0;
	size_t done = 0;
	bool start = false;
	bool cleanup = false;
};

template<class T>
void runConcurrentWorker(ConcurrentState& state, size_t iterations, size_t workingSet) {
	std::vector<T*> values(workingSet);
	for(size_t i = 0; i < workingSet; ++i) {
		values[i] = new T(i + 1);
	}

	{
		std::unique_lock<std::mutex> lock(state.mutex);
		++state.ready;
		state.condition.notify_all();
		state.condition.wait(lock, [&state] { return state.start; });
	}

	for(size_t i = 0; i < iterations; ++i) {
		const size_t index = i % workingSet;
		delete values[index];
		values[index] = new T(i + workingSet + 1);
	}
	consume(values);

	{
		std::unique_lock<std::mutex> lock(state.mutex);
		++state.done;
		state.condition.notify_all();
		state.condition.wait(lock, [&state] { return state.cleanup; });
	}

	for(auto value: values) {
		delete value;
	}
}

template<class StartWorkers>
double runConcurrent(size_t threadCount, StartWorkers startWorkers) {
	ConcurrentState state;
	std::vector<std::thread> threads;
	threads.reserve(threadCount);
	startWorkers(state, threads);

	std::unique_lock<std::mutex> lock(state.mutex);
	state.condition.wait(lock, [&state, threadCount] { return state.ready == threadCount; });
	const auto start = Clock::now();
	state.start = true;
	state.condition.notify_all();
	state.condition.wait(lock, [&state, threadCount] { return state.done == threadCount; });
	const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	state.cleanup = true;
	state.condition.notify_all();
	lock.unlock();

	for(auto& thread: threads) {
		thread.join();
	}
	return elapsed;
}

template<class T>
double runSameTypeConcurrent(size_t threadCount, size_t iterations, size_t workingSet) {
	return runConcurrent(threadCount, [threadCount, iterations, workingSet](ConcurrentState& state, std::vector<std::thread>& threads) {
		for(size_t i = 0; i < threadCount; ++i) {
			threads.emplace_back(runConcurrentWorker<T>, std::ref(state), iterations, workingSet);
		}
	});
}

template<class T0, class T1, class T2, class T3>
double runDifferentTypeConcurrent(size_t iterations, size_t workingSet) {
	return runConcurrent(4, [iterations, workingSet](ConcurrentState& state, std::vector<std::thread>& threads) {
		threads.emplace_back(runConcurrentWorker<T0>, std::ref(state), iterations, workingSet);
		threads.emplace_back(runConcurrentWorker<T1>, std::ref(state), iterations, workingSet);
		threads.emplace_back(runConcurrentWorker<T2>, std::ref(state), iterations, workingSet);
		threads.emplace_back(runConcurrentWorker<T3>, std::ref(state), iterations, workingSet);
	});
}

struct Measurement {
	double coldMs;
	double medianMs;
	double minimumMs;
	double maximumMs;
};

template<class Function>
Measurement measure(Function function) {
	const double coldMs = function();
	std::vector<double> samples;
	samples.reserve(SAMPLE_COUNT);
	for(size_t i = 0; i < SAMPLE_COUNT; ++i) {
		samples.push_back(function());
	}
	std::sort(samples.begin(), samples.end());
	return { coldMs, samples[samples.size() / 2], samples.front(), samples.back() };
}

struct NamedMeasurement {
	const char* name;
	Measurement measurement;
};

template<size_t Count>
void printScenario(const char* name, size_t pairs, const std::array<NamedMeasurement, Count>& results) {
	double fastest = results[0].measurement.medianMs;
	for(const auto& result: results) {
		fastest = std::min(fastest, result.measurement.medianMs);
	}

	std::cout << "\n" << name << " (" << pairs << " allocation/deallocation pairs)\n";
	std::cout << std::left << std::setw(31) << "Allocator"
		<< std::right << std::setw(11) << "Cold ms"
		<< std::setw(11) << "Min ms"
		<< std::setw(12) << "Median ms"
		<< std::setw(11) << "Max ms"
		<< std::setw(15) << "M pairs/s"
		<< std::setw(12) << "vs best" << '\n';
	for(const auto& result: results) {
		const auto& value = result.measurement;
		const double throughput = pairs / (value.medianMs * 1000.0);
		std::cout << std::left << std::setw(31) << result.name
			<< std::right << std::fixed << std::setprecision(3)
			<< std::setw(11) << value.coldMs
			<< std::setw(11) << value.minimumMs
			<< std::setw(12) << value.medianMs
			<< std::setw(11) << value.maximumMs
			<< std::setw(15) << throughput
			<< std::setw(11) << value.medianMs / fastest << "x\n";
	}
}

#ifndef _DEBUG
template<class T>
void benchmarkDcppRawType(const char* typeName) {
	const size_t count = std::min<size_t>(250000, std::max<size_t>(32768, 16 * 1024 * 1024 / sizeof(T)));
	const string name = string("Raw DC++ storage: ") + typeName + " (" + std::to_string(sizeof(T)) + " bytes)";
	printScenario(name.c_str(), count, std::array<NamedMeasurement, 4>{{
		{ "C++ new/delete", measure([count] { return runRawBatch<T, DefaultRawStorage>(count); }) },
		{ WINDOWS_ALLOCATOR_NAME, measure([count] { return runRawBatch<T, WindowsRawStorage>(count); }) },
		{ "Legacy FastAlloc global lock", measure([count] { return runRawBatch<T, LegacyRawStorage>(count); }) },
		{ "Optimized FastAlloc production", measure([count] { return runRawBatch<T, ProductionRawStorage>(count); }) }
	}});
}

template<class T, class Factory>
void benchmarkDcppLifecycle(const char* typeName, Factory factory) {
	const size_t count = 50000;
	const Measurement result = measure([=] { return runConstructedBatch<T>(count, factory); });
	std::cout << std::left << std::setw(38) << typeName
		<< std::right << std::setw(9) << sizeof(T)
		<< std::fixed << std::setprecision(3)
		<< std::setw(12) << result.coldMs
		<< std::setw(12) << result.medianMs
		<< std::setw(16) << count / (result.medianMs * 1000.0) << '\n';
}

void benchmarkDcppTypes() {
	benchmarkDcppRawType<User>("User");
	benchmarkDcppRawType<OnlineUser>("OnlineUser");
	benchmarkDcppRawType<SearchResult>("SearchResult");
	benchmarkDcppRawType<QueueItem>("QueueItem");
	benchmarkDcppRawType<DirectoryListing::File>("DirectoryListing::File");
	benchmarkDcppRawType<DirectoryListing::Directory>("DirectoryListing::Directory");
	benchmarkDcppRawType<TTHValue>("TTHValue");

	std::array<uint8_t, TTHValue::BYTES> hashData;
	hashData.fill(0x5a);
	const TTHValue tth(hashData.data());
	const CID cid;
	const HintedUser hintedUser;
	const Style style;
	std::cout << "\nConstructed DC++ objects with production FastAlloc (50000 objects per run)\n";
	std::cout << std::left << std::setw(38) << "Type"
		<< std::right << std::setw(9) << "Bytes"
		<< std::setw(12) << "Cold ms"
		<< std::setw(12) << "Median ms"
		<< std::setw(16) << "M objects/s" << '\n';
	benchmarkDcppLifecycle<User>("User", [=](size_t) { return new User(cid); });
	benchmarkDcppLifecycle<TTHValue>("TTHValue", [=](size_t) { return new TTHValue(tth); });
	benchmarkDcppLifecycle<QueueItem>("QueueItem", [=](size_t i) { return new QueueItem("/fastalloc/" + std::to_string(i), 4096, QueueItem::NORMAL, 0, 0, tth); });
	benchmarkDcppLifecycle<SearchResult>("SearchResult", [=](size_t i) { return new SearchResult(hintedUser, SearchResult::TYPE_FILE, 3, 2, 4096, "file-" + std::to_string(i), "hub", "127.0.0.1", tth, "token", style); });
	benchmarkDcppLifecycle<DirectoryListing::File>("DirectoryListing::File", [=](size_t i) { return new DirectoryListing::File(nullptr, "file-" + std::to_string(i), 4096, tth); });
	benchmarkDcppLifecycle<DirectoryListing::Directory>("DirectoryListing::Directory", [=](size_t i) { return new DirectoryListing::Directory(nullptr, "dir-" + std::to_string(i), false, true); });
}
#endif

template<size_t Size>
void benchmarkBatch() {
	static_assert(sizeof(DefaultNode<Size, 0>) == sizeof(WindowsNode<Size, 0>), "Windows benchmark node size mismatch");
	static_assert(sizeof(DefaultNode<Size, 0>) == sizeof(CurrentNode<Size, 0>), "Current FastAlloc benchmark node size mismatch");
	static_assert(sizeof(DefaultNode<Size, 0>) == sizeof(LegacyNode<Size, 0>), "Legacy FastAlloc benchmark node size mismatch");
	static_assert(sizeof(DefaultNode<Size, 0>) == sizeof(ThreadCachedNode<Size, 0>), "Thread-cached pool benchmark node size mismatch");
	const size_t count = std::max<size_t>(32768, 8 * 1024 * 1024 / Size);
	printScenario(("Single-thread batch, " + std::to_string(Size) + "-byte payload").c_str(), count, std::array<NamedMeasurement, 5>{{
		{ "C++ new/delete", measure([count] { return runBatch<DefaultNode<Size, 0> >(count); }) },
		{ WINDOWS_ALLOCATOR_NAME, measure([count] { return runBatch<WindowsNode<Size, 0> >(count); }) },
		{ "Legacy FastAlloc global lock", measure([count] { return runBatch<LegacyNode<Size, 0> >(count); }) },
		{ "Optimized FastAlloc production", measure([count] { return runBatch<CurrentNode<Size, 0> >(count); }) },
		{ "TLS prototype reference", measure([count] { return runBatch<ThreadCachedNode<Size, 0> >(count); }) }
	}});
}

template<size_t Size>
void benchmarkChurn() {
	const size_t iterations = 1000000;
	const size_t workingSet = 4096;
	printScenario(("Single-thread churn, " + std::to_string(Size) + "-byte payload").c_str(), iterations, std::array<NamedMeasurement, 5>{{
		{ "C++ new/delete", measure([=] { return runChurn<DefaultNode<Size, 1> >(iterations, workingSet); }) },
		{ WINDOWS_ALLOCATOR_NAME, measure([=] { return runChurn<WindowsNode<Size, 1> >(iterations, workingSet); }) },
		{ "Legacy FastAlloc global lock", measure([=] { return runChurn<LegacyNode<Size, 1> >(iterations, workingSet); }) },
		{ "Optimized FastAlloc production", measure([=] { return runChurn<CurrentNode<Size, 1> >(iterations, workingSet); }) },
		{ "TLS prototype reference", measure([=] { return runChurn<ThreadCachedNode<Size, 1> >(iterations, workingSet); }) }
	}});
}

template<size_t Size>
void benchmarkSameTypeConcurrent(size_t threadCount) {
	const size_t iterations = 250000;
	const size_t workingSet = 1024;
	const size_t pairs = threadCount * iterations;
	printScenario((std::to_string(threadCount) + " threads, shared type, " + std::to_string(Size) + "-byte payload").c_str(), pairs, std::array<NamedMeasurement, 5>{{
		{ "C++ new/delete", measure([=] { return runSameTypeConcurrent<DefaultNode<Size, 2> >(threadCount, iterations, workingSet); }) },
		{ WINDOWS_ALLOCATOR_NAME, measure([=] { return runSameTypeConcurrent<WindowsNode<Size, 2> >(threadCount, iterations, workingSet); }) },
		{ "Legacy FastAlloc global lock", measure([=] { return runSameTypeConcurrent<LegacyNode<Size, 2> >(threadCount, iterations, workingSet); }) },
		{ "Optimized FastAlloc production", measure([=] { return runSameTypeConcurrent<CurrentNode<Size, 2> >(threadCount, iterations, workingSet); }) },
		{ "TLS prototype reference", measure([=] { return runSameTypeConcurrent<ThreadCachedNode<Size, 2> >(threadCount, iterations, workingSet); }) }
	}});
}

template<size_t Size>
void benchmarkDifferentTypeConcurrent() {
	const size_t iterations = 250000;
	const size_t workingSet = 1024;
	const size_t pairs = 4 * iterations;
	printScenario(("4 threads, independent types, " + std::to_string(Size) + "-byte payload").c_str(), pairs, std::array<NamedMeasurement, 5>{{
		{ "C++ new/delete", measure([=] { return runDifferentTypeConcurrent<DefaultNode<Size, 3>, DefaultNode<Size, 4>, DefaultNode<Size, 5>, DefaultNode<Size, 6> >(iterations, workingSet); }) },
		{ WINDOWS_ALLOCATOR_NAME, measure([=] { return runDifferentTypeConcurrent<WindowsNode<Size, 3>, WindowsNode<Size, 4>, WindowsNode<Size, 5>, WindowsNode<Size, 6> >(iterations, workingSet); }) },
		{ "Legacy FastAlloc global lock", measure([=] { return runDifferentTypeConcurrent<LegacyNode<Size, 3>, LegacyNode<Size, 4>, LegacyNode<Size, 5>, LegacyNode<Size, 6> >(iterations, workingSet); }) },
		{ "Optimized FastAlloc production", measure([=] { return runDifferentTypeConcurrent<CurrentNode<Size, 3>, CurrentNode<Size, 4>, CurrentNode<Size, 5>, CurrentNode<Size, 6> >(iterations, workingSet); }) },
		{ "TLS prototype reference", measure([=] { return runDifferentTypeConcurrent<ThreadCachedNode<Size, 3>, ThreadCachedNode<Size, 4>, ThreadCachedNode<Size, 5>, ThreadCachedNode<Size, 6> >(iterations, workingSet); }) }
	}});
}

}

TEST(FastAllocBenchmark, DISABLED_comparison)
{
#ifdef _DEBUG
	std::cout << "\nFastAlloc is disabled by _DEBUG; run this benchmark with a release build.\n";
	return;
#else
	const size_t hardwareThreads = std::thread::hardware_concurrency();
	const size_t threadCount = std::max<size_t>(2, std::min<size_t>(4, hardwareThreads ? hardwareThreads : 4));
	std::cout << "\nFastAlloc allocator comparison\n"
		<< "Samples: " << SAMPLE_COUNT << " warm runs after one cold run; concurrent workers: " << threadCount << '\n'
		<< "Run only this benchmark with --gtest_also_run_disabled_tests --gtest_filter=FastAllocBenchmark.*\n";

	benchmarkBatch<64>();
	benchmarkBatch<256>();
	benchmarkChurn<128>();
	benchmarkSameTypeConcurrent<128>(threadCount);
	benchmarkDifferentTypeConcurrent<128>();
	benchmarkDcppTypes();
#endif
}
