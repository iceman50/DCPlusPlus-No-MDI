/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdinc.h"
#include "TigerTreeHasher.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>

#ifndef _WIN32
#include <sys/resource.h>
#endif

#include "File.h"
#include "FileReader.h"

namespace dcpp {

namespace {

constexpr size_t MAX_HASH_WORKERS = 64;
constexpr size_t FILE_READ_BLOCK_SIZE = 1024 * 1024;
constexpr uint64_t MAX_PIPELINE_MEMORY = 256ULL * 1024ULL * 1024ULL;

/** Calculate leaf storage without the signed addition used by MerkleTree::calcBlocks. */
size_t getBlockCount(int64_t fileSize, int64_t blockSize) noexcept {
	if(fileSize <= 0 || blockSize <= 0) return fileSize == 0 ? 1 : 0;
	const auto blocks = 1ULL + static_cast<uint64_t>(fileSize - 1) / static_cast<uint64_t>(blockSize);
	return blocks > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ? 0 : static_cast<size_t>(blocks);
}

struct HashTask {
	HashTask(size_t aIndex, ByteVector&& aData) : index(aIndex), data(std::move(aData)) { }

	size_t index;
	ByteVector data;
};

/** Owns every worker and guarantees that no callback survives the hash call. */
class HashWorkerPool {
public:
	HashWorkerPool(size_t workerCount, size_t queueLimit, int64_t blockSize, size_t resultCount, const TigerTreeHasher::ProgressCallback& progress) : queueLimit(queueLimit), blockSize(blockSize), leaves(resultCount), progress(progress), inputDone(false), cancelled(false), completed(0) {
		try {
			workers.reserve(workerCount);
			for(size_t i = 0; i < workerCount; ++i) workers.emplace_back([this] { run(); });
		} catch(...) {
			cancel();
			join();
			throw;
		}
	}

	HashWorkerPool(const HashWorkerPool&) = delete;
	HashWorkerPool& operator=(const HashWorkerPool&) = delete;

	~HashWorkerPool() {
		cancel();
		join();
	}

	bool submit(size_t index, ByteVector&& data) {
		std::unique_lock<std::mutex> lock(mutex);
		spaceAvailable.wait(lock, [this] { return cancelled || tasks.size() < queueLimit; });
		if(cancelled || index >= leaves.size()) return false;
		tasks.emplace_back(index, std::move(data));
		workAvailable.notify_one();
		return true;
	}

	void finishInput() noexcept {
		{
			std::lock_guard<std::mutex> lock(mutex);
			inputDone = true;
		}
		workAvailable.notify_all();
	}

	void cancel() noexcept {
		{
			std::lock_guard<std::mutex> lock(mutex);
			cancelled = true;
			tasks.clear();
		}
		workAvailable.notify_all();
		spaceAvailable.notify_all();
	}

	void join() noexcept {
		for(auto& worker: workers) {
			if(worker.joinable()) worker.join();
		}
	}

	void rethrowWorkerError() const {
		std::exception_ptr error;
		{
			std::lock_guard<std::mutex> lock(mutex);
			error = workerError;
		}
		if(error) std::rethrow_exception(error);
	}

	bool succeeded() const noexcept {
		std::lock_guard<std::mutex> lock(mutex);
		return !cancelled && !workerError && completed == leaves.size();
	}

	const vector<TTHValue>& getLeaves() const noexcept { return leaves; }

private:
	void fail(std::exception_ptr error) noexcept {
		{
			std::lock_guard<std::mutex> lock(mutex);
			if(!workerError) workerError = error;
			cancelled = true;
			tasks.clear();
		}
		workAvailable.notify_all();
		spaceAvailable.notify_all();
	}

	void run() noexcept {
#ifdef _WIN32
		::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_IDLE);
#else
		::setpriority(PRIO_PROCESS, 0, 19);
#endif

		for(;;) {
			HashTask task { 0, ByteVector() };
			{
				std::unique_lock<std::mutex> lock(mutex);
				workAvailable.wait(lock, [this] { return cancelled || inputDone || !tasks.empty(); });
				if(cancelled || (inputDone && tasks.empty())) return;
				task = std::move(tasks.front());
				tasks.pop_front();
				spaceAvailable.notify_one();
			}

			try {
				TigerTree blockTree(blockSize);
				blockTree.update(task.data.data(), task.data.size());
				blockTree.finalize();

				{
					std::lock_guard<std::mutex> lock(mutex);
					if(cancelled) return;
					leaves[task.index] = blockTree.getRoot();
					++completed;
				}

				if(progress) progress(task.data.size());
			} catch(...) {
				fail(std::current_exception());
				return;
			}
		}
	}

	size_t queueLimit;
	int64_t blockSize;
	vector<TTHValue> leaves;
	TigerTreeHasher::ProgressCallback progress;
	mutable std::mutex mutex;
	std::condition_variable workAvailable;
	std::condition_variable spaceAvailable;
	std::deque<HashTask> tasks;
	vector<std::thread> workers;
	std::exception_ptr workerError;
	bool inputDone;
	bool cancelled;
	size_t completed;
};

} // namespace

size_t TigerTreeHasher::getWorkerCount(size_t requested, int64_t fileSize, int64_t blockSize) noexcept {
	if(requested <= 1 || fileSize <= 0 || blockSize <= 0 || fileSize <= blockSize) return 1;
	if(static_cast<uint64_t>(blockSize) > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) return 1;
	const auto blockCount = getBlockCount(fileSize, blockSize);
	if(blockCount == 0) return 1;

	auto workers = std::min(requested, MAX_HASH_WORKERS);
	const auto hardwareWorkers = std::thread::hardware_concurrency();
	if(hardwareWorkers != 0) workers = std::min(workers, static_cast<size_t>(hardwareWorkers));
	workers = std::min(workers, blockCount);

	const auto blockBytes = static_cast<uint64_t>(blockSize);
	// The coordinator owns one read buffer and one assembling block. The bounded
	// queue and active workers can each own at most one block per worker.
	const auto coordinatorBytes = blockBytes + FILE_READ_BLOCK_SIZE;
	if(coordinatorBytes >= MAX_PIPELINE_MEMORY || blockBytes > (MAX_PIPELINE_MEMORY - coordinatorBytes) / 2) return 1;
	workers = std::min<uint64_t>(workers, (MAX_PIPELINE_MEMORY - coordinatorBytes) / (blockBytes * 2));
	return std::max<size_t>(workers, 1);
}

bool TigerTreeHasher::hash(File& file, int64_t expectedSize, int64_t blockSize, size_t requestedWorkers, TigerTree& tree, const DataObserver& observer, const ProgressCallback& progress) {
	if(expectedSize < 0 || blockSize < static_cast<int64_t>(TigerTree::BASE_BLOCK_SIZE)) return false;
	const auto blockRatio = static_cast<uint64_t>(blockSize) / TigerTree::BASE_BLOCK_SIZE;
	if(blockSize % TigerTree::BASE_BLOCK_SIZE != 0 || (blockRatio & (blockRatio - 1)) != 0 || static_cast<uint64_t>(blockSize) > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) return false;
	if(expectedSize == 0) {
		if(FileReader(true, FILE_READ_BLOCK_SIZE).read(file, [](const void*, size_t) { return false; }) != 0) return false;
		TigerTree emptyTree(blockSize);
		emptyTree.finalize();
		tree = emptyTree;
		return true;
	}

	const auto workerCount = getWorkerCount(requestedWorkers, expectedSize, blockSize);
	const auto resultCount = getBlockCount(expectedSize, blockSize);
	if(resultCount == 0) return false;
	HashWorkerPool pool(workerCount, workerCount, blockSize, resultCount, progress);
	size_t nextIndex = 0;
	uint64_t bytesRead = 0;
	bool accepted = true;
	ByteVector pendingBlock;
	const auto pendingCapacity = static_cast<size_t>(std::min<uint64_t>(static_cast<uint64_t>(blockSize), static_cast<uint64_t>(expectedSize)));
	pendingBlock.reserve(pendingCapacity);

	FileReader(true, FILE_READ_BLOCK_SIZE).read(file, [&](const void* data, size_t size) {
		if(size > static_cast<uint64_t>(expectedSize) - bytesRead) {
			accepted = false;
			pool.cancel();
			return false;
		}
		if(observer && !observer(data, size)) {
			accepted = false;
			pool.cancel();
			return false;
		}

		bytesRead += size;
		const auto* input = static_cast<const uint8_t*>(data);
		while(size > 0) {
			const auto bytes = std::min(size, static_cast<size_t>(blockSize) - pendingBlock.size());
			pendingBlock.insert(pendingBlock.end(), input, input + bytes);
			if(pendingBlock.size() == static_cast<size_t>(blockSize)) {
				if(!pool.submit(nextIndex++, std::move(pendingBlock))) {
					accepted = false;
					return false;
				}
				pendingBlock = ByteVector();
				pendingBlock.reserve(pendingCapacity);
			}
			input += bytes;
			size -= bytes;
		}
		return true;
	});

	if(accepted && !pendingBlock.empty() && !pool.submit(nextIndex++, std::move(pendingBlock))) accepted = false;
	pool.finishInput();
	pool.join();
	pool.rethrowWorkerError();
	if(!accepted || bytesRead != static_cast<uint64_t>(expectedSize) || nextIndex != resultCount || !pool.succeeded()) return false;

	ByteVector leafData(resultCount * TTHValue::BYTES);
	for(size_t i = 0; i < resultCount; ++i) memcpy(leafData.data() + i * TTHValue::BYTES, pool.getLeaves()[i].data, TTHValue::BYTES);
	tree = TigerTree(expectedSize, blockSize, leafData.data());
	return true;
}

} // namespace dcpp
