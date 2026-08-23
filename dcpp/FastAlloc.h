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

#if !defined(FAST_ALLOC_H)
#define FAST_ALLOC_H

#include "CriticalSection.h"
#include "debug.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace dcpp {

#ifndef _DEBUG
template<class T>
struct FastAllocStorage {
	union Slot {
		alignas(T) uint8_t storage[sizeof(T)];
		Slot* next;
	};

	struct State {
		FastCriticalSection cs;
		Slot* freeList = nullptr;
		std::vector<std::unique_ptr<Slot[]> > slabs;
	};

	struct LocalCache {
		~LocalCache() { FastAllocStorage<T>::release(*this, count); }

		Slot* freeList = nullptr;
		size_t count = 0;
	};

	enum {
		BLOCK_BYTES = 128 * 1024,
		BATCH_SIZE = 32,
		CACHE_LIMIT = BATCH_SIZE * 2
	};

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

	static void deallocate(void* memory) noexcept {
		LocalCache& cache = getCache();
		Slot* slot = static_cast<Slot*>(memory);
		slot->next = cache.freeList;
		cache.freeList = slot;
		++cache.count;

		if(cache.count > CACHE_LIMIT) {
			release(cache, BATCH_SIZE);
		}
	}

private:
	static State& getState() {
		// The process owns allocator slabs for its lifetime. Keeping this state
		// alive also makes thread-local cache destruction order independent.
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
		Slot* first = slab.get();
		slab[items - 1].next = state.freeList;
		state.slabs.push_back(std::move(slab));
		state.freeList = first;
	}

	static void refill(LocalCache& cache) {
		State& state = getState();
		FastLock lock(state.cs);
		if(!state.freeList) {
			grow(state);
		}

		Slot* first = state.freeList;
		Slot* last = first;
		size_t count = 1;
		while(count < BATCH_SIZE && last->next) {
			last = last->next;
			++count;
		}
		state.freeList = last->next;
		last->next = cache.freeList;
		cache.freeList = first;
		cache.count += count;
	}

	static void release(LocalCache& cache, size_t count) noexcept {
		if(!cache.freeList || count == 0) {
			return;
		}
		if(count > cache.count) {
			count = cache.count;
		}

		Slot* first = cache.freeList;
		Slot* last = first;
		for(size_t i = 1; i < count; ++i) {
			last = last->next;
		}
		cache.freeList = last->next;
		cache.count -= count;

		State& state = getState();
		FastLock lock(state.cs);
		last->next = state.freeList;
		state.freeList = first;
	}
};

/**
 * Fast new/delete replacements for constant sized objects. Storage is kept in
 * per-type slabs and transferred to threads in small batches, providing good
 * reference locality without serializing every allocation on a global lock.
 */
template<class T>
struct FastAlloc {
	// Custom new & delete that (hopefully) use the node allocator
	static void* operator new(size_t s) {
		if(s != sizeof(T))
			return ::operator new(s);
		return FastAllocStorage<T>::allocate();
	}

	// Avoid hiding placement new that's needed by the stl containers...
	static void* operator new(size_t, void* m) {
		return m;
	}
	// ...and the warning about missing placement delete...
	static void operator delete(void*, void*) {
		// ? We didn't allocate so...
	}

	static void operator delete(void* m, size_t s) noexcept {
		if (s != sizeof(T)) {
			::operator delete(m);
		} else if(m != NULL) {
			FastAllocStorage<T>::deallocate(m);
		}
	}
protected:
	~FastAlloc() { }
};
#else
template<class T> struct FastAlloc { };
#endif

} // namespace dcpp

#endif // !defined(FAST_ALLOC_H)
