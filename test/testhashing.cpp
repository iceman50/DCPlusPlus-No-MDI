/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "testbase.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

#include <dcpp/File.h>
#include <dcpp/FileReader.h>
#include <dcpp/TigerTreeHasher.h>
#include <dcpp/Util.h>

using namespace dcpp;

namespace {

class TemporaryHashFile {
public:
	explicit TemporaryHashFile(const ByteVector& data) : path(Util::getTempPath() + "dcpp-parallel-hash-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin") {
		File::deleteFile(path);
		File output(path, File::WRITE, File::CREATE | File::TRUNCATE);
		if(!data.empty()) output.write(data.data(), data.size());
	}

	~TemporaryHashFile() { File::deleteFile(path); }

	TemporaryHashFile(const TemporaryHashFile&) = delete;
	TemporaryHashFile& operator=(const TemporaryHashFile&) = delete;

	string path;
};

ByteVector makeHashData(size_t size) {
	ByteVector data(size);
	uint32_t state = 0x8d12e5a7U;
	for(auto& byte: data) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		byte = static_cast<uint8_t>(state);
	}
	return data;
}

TigerTree makeSerialTree(const ByteVector& data, int64_t blockSize) {
	TigerTree tree(blockSize);
	if(!data.empty()) tree.update(data.data(), data.size());
	tree.finalize();
	return tree;
}

TigerTree hashFileSerially(const string& path, int64_t blockSize) {
	File input(path, File::READ, File::OPEN);
	TigerTree tree(blockSize);
	FileReader(false).read(input, [&](const void* data, size_t size) {
		tree.update(data, size);
		return true;
	});
	tree.finalize();
	return tree;
}

double elapsedMilliseconds(const std::chrono::steady_clock::time_point& start) {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void expectSameTree(const TigerTree& expected, const TigerTree& actual) {
	EXPECT_EQ(expected.getFileSize(), actual.getFileSize());
	EXPECT_EQ(expected.getBlockSize(), actual.getBlockSize());
	EXPECT_EQ(expected.getRoot(), actual.getRoot());
	EXPECT_EQ(expected.getLeaves(), actual.getLeaves());
}

}

TEST(TigerTreeHasherTest, matches_serial_hashes_at_data_and_leaf_boundaries) {
	const int64_t blockSize = 64 * 1024;
	const vector<size_t> sizes = { 0, 1, 1023, 1024, 1025, 65535, 65536, 65537, 1024 * 1024 - 1, 1024 * 1024, 1024 * 1024 + 1 };

	for(const auto size: sizes) {
		SCOPED_TRACE(size);
		const auto data = makeHashData(size);
		TemporaryHashFile temporary(data);
		File input(temporary.path, File::READ, File::OPEN);
		TigerTree parallelTree;
		ByteVector observed;
		std::atomic<size_t> progressed(0);
		const auto success = TigerTreeHasher::hash(input, static_cast<int64_t>(data.size()), blockSize, 4, parallelTree, [&](const void* bytes, size_t count) {
			const auto* first = static_cast<const uint8_t*>(bytes);
			observed.insert(observed.end(), first, first + count);
			return true;
		}, [&](size_t count) { progressed.fetch_add(count, std::memory_order_relaxed); });

		ASSERT_TRUE(success);
		EXPECT_EQ(data, observed);
		EXPECT_EQ(data.size(), progressed.load(std::memory_order_relaxed));
		expectSameTree(makeSerialTree(data, blockSize), parallelTree);
	}
}

TEST(TigerTreeHasherTest, matches_serial_hashes_with_large_tree_blocks) {
	const auto data = makeHashData(5 * 1024 * 1024 + 123);
	TemporaryHashFile temporary(data);
	const vector<int64_t> blockSizes = { 64 * 1024, 1024 * 1024, 2 * 1024 * 1024 };

	for(const auto blockSize: blockSizes) {
		SCOPED_TRACE(blockSize);
		File input(temporary.path, File::READ, File::OPEN);
		TigerTree parallelTree;
		ASSERT_TRUE(TigerTreeHasher::hash(input, static_cast<int64_t>(data.size()), blockSize, 8, parallelTree, {}, {}));
		expectSameTree(makeSerialTree(data, blockSize), parallelTree);
	}
}

TEST(TigerTreeHasherTest, rejects_cancellation_and_file_size_changes_without_partial_results) {
	const auto data = makeHashData(2 * 1024 * 1024 + 17);
	TemporaryHashFile temporary(data);
	const int64_t blockSize = 64 * 1024;
	TigerTree tree;

	{
		File input(temporary.path, File::READ, File::OPEN);
		EXPECT_FALSE(TigerTreeHasher::hash(input, static_cast<int64_t>(data.size()), blockSize, 4, tree, [](const void*, size_t) { return false; }, {}));
	}
	{
		File input(temporary.path, File::READ, File::OPEN);
		EXPECT_FALSE(TigerTreeHasher::hash(input, static_cast<int64_t>(data.size() + 1), blockSize, 4, tree, {}, {}));
	}
	{
		File input(temporary.path, File::READ, File::OPEN);
		EXPECT_FALSE(TigerTreeHasher::hash(input, static_cast<int64_t>(data.size() - 1), blockSize, 4, tree, {}, {}));
	}
	{
		File input(temporary.path, File::READ, File::OPEN);
		EXPECT_FALSE(TigerTreeHasher::hash(input, 0, blockSize, 4, tree, {}, {}));
	}
}

TEST(TigerTreeHasherTest, joins_workers_before_propagating_callback_failures) {
	const auto data = makeHashData(2 * 1024 * 1024);
	TemporaryHashFile temporary(data);
	TigerTree tree;

	{
		File input(temporary.path, File::READ, File::OPEN);
		EXPECT_THROW(TigerTreeHasher::hash(input, static_cast<int64_t>(data.size()), 64 * 1024, 4, tree, [](const void*, size_t) -> bool { throw std::runtime_error("observer failure"); }, {}), std::runtime_error);
	}
	{
		File input(temporary.path, File::READ, File::OPEN);
		EXPECT_THROW(TigerTreeHasher::hash(input, static_cast<int64_t>(data.size()), 64 * 1024, 4, tree, {}, [](size_t) { throw std::runtime_error("progress failure"); }), std::runtime_error);
	}
}

TEST(TigerTreeHasherTest, bounds_workers_by_cpu_leaves_and_memory) {
	EXPECT_EQ(1U, TigerTreeHasher::getWorkerCount(8, 0, 64 * 1024));
	EXPECT_EQ(1U, TigerTreeHasher::getWorkerCount(8, 64 * 1024, 64 * 1024));
	EXPECT_EQ(1U, TigerTreeHasher::getWorkerCount(1, 16 * 1024 * 1024, 64 * 1024));
	EXPECT_EQ(1U, TigerTreeHasher::getWorkerCount(64, 1024LL * 1024LL * 1024LL, 128LL * 1024LL * 1024LL));

	const auto workers = TigerTreeHasher::getWorkerCount(64, 16 * 1024 * 1024, 64 * 1024);
	EXPECT_GE(workers, 1U);
	EXPECT_LE(workers, 64U);
	EXPECT_LE(workers, TigerTree::calcBlocks(16 * 1024 * 1024, 64 * 1024));
	const auto hardwareWorkers = std::thread::hardware_concurrency();
	if(hardwareWorkers != 0) {
		EXPECT_LE(workers, static_cast<size_t>(hardwareWorkers));
	}
}

TEST(TigerTreeHasherTest, rejects_invalid_sizes_and_tree_block_layouts) {
	const auto data = makeHashData(4096);
	TemporaryHashFile temporary(data);
	TigerTree tree;
	const vector<int64_t> invalidBlocks = { 0, 1023, 1536, 3072 };
	for(const auto blockSize: invalidBlocks) {
		SCOPED_TRACE(blockSize);
		File input(temporary.path, File::READ, File::OPEN);
		EXPECT_FALSE(TigerTreeHasher::hash(input, 4096, blockSize, 4, tree, {}, {}));
	}
	File input(temporary.path, File::READ, File::OPEN);
	EXPECT_FALSE(TigerTreeHasher::hash(input, -1, 1024, 4, tree, {}, {}));

	if(std::numeric_limits<size_t>::max() >= (1ULL << 40)) {
		const int64_t largeBlock = 1LL << 40;
		File largeBlockInput(temporary.path, File::READ, File::OPEN);
		ASSERT_TRUE(TigerTreeHasher::hash(largeBlockInput, static_cast<int64_t>(data.size()), largeBlock, 4, tree, {}, {}));
		expectSameTree(makeSerialTree(data, largeBlock), tree);
	}
}

TEST(TigerTreeHasherBenchmark, DISABLED_compare_serial_and_parallel_file_hashing) {
	const auto data = makeHashData(128 * 1024 * 1024);
	TemporaryHashFile temporary(data);
	const auto blockSize = TigerTree::calcBlockSize(static_cast<int64_t>(data.size()), 10);
	const auto workers = TigerTreeHasher::getWorkerCount(8, static_cast<int64_t>(data.size()), blockSize);
	ASSERT_GT(workers, 1U);

	// Warm the filesystem cache so this measures hashing rather than storage-device variance.
	const auto expected = hashFileSerially(temporary.path, blockSize);
	double serialMs = std::numeric_limits<double>::max();
	double parallelMs = std::numeric_limits<double>::max();
	for(size_t iteration = 0; iteration < 3; ++iteration) {
		auto start = std::chrono::steady_clock::now();
		const auto serial = hashFileSerially(temporary.path, blockSize);
		serialMs = std::min(serialMs, elapsedMilliseconds(start));
		expectSameTree(expected, serial);

		File input(temporary.path, File::READ, File::OPEN);
		TigerTree parallel;
		start = std::chrono::steady_clock::now();
		ASSERT_TRUE(TigerTreeHasher::hash(input, static_cast<int64_t>(data.size()), blockSize, workers, parallel, {}, {}));
		parallelMs = std::min(parallelMs, elapsedMilliseconds(start));
		expectSameTree(expected, parallel);
	}

	std::cout << "\nTTH benchmark: serial=" << serialMs << " ms, parallel=" << parallelMs << " ms, workers=" << workers << ", speedup=" << serialMs / parallelMs << "x\n";
}

TEST(TigerTreeHasherBenchmark, DISABLED_compare_serial_and_parallel_directory_hashing) {
	const auto* rootVariable = std::getenv("DCPP_HASH_BENCHMARK_PATH");
	ASSERT_TRUE(rootVariable && *rootVariable) << "Set DCPP_HASH_BENCHMARK_PATH to an existing directory";

	struct BenchmarkFile {
		string path;
		int64_t size;
		int64_t blockSize;
		TigerTree expected;
	};

	const auto root = std::filesystem::u8path(rootVariable);
	ASSERT_TRUE(std::filesystem::is_directory(root)) << rootVariable;
	vector<BenchmarkFile> files;
	for(const auto& entry: std::filesystem::recursive_directory_iterator(root)) {
		if(!entry.is_regular_file()) continue;
		const auto fileSize = entry.file_size();
		ASSERT_LE(fileSize, static_cast<uintmax_t>(std::numeric_limits<int64_t>::max()));
		const auto size = static_cast<int64_t>(fileSize);
		files.push_back({ entry.path().u8string(), size, std::max(TigerTree::calcBlockSize(size, 10), 64LL * 1024LL), TigerTree() });
	}
	std::sort(files.begin(), files.end(), [](const BenchmarkFile& lhs, const BenchmarkFile& rhs) { return lhs.path < rhs.path; });
	ASSERT_FALSE(files.empty()) << rootVariable;

	uint64_t totalBytes = 0;
	for(const auto& file: files) totalBytes += static_cast<uint64_t>(file.size);
	std::cout << "\nTTH directory benchmark: root=" << std::quoted(rootVariable) << ", files=" << files.size() << ", bytes=" << totalBytes << "\n";

	// Populate the reference trees and warm the filesystem cache before timing.
	for(auto& file: files) file.expected = hashFileSerially(file.path, file.blockSize);

	constexpr size_t trials = 2;
	vector<double> bestSerialFileMs(files.size(), std::numeric_limits<double>::max());
	double bestSerialTotalMs = std::numeric_limits<double>::max();
	for(size_t trial = 0; trial < trials; ++trial) {
		const auto totalStart = std::chrono::steady_clock::now();
		for(size_t i = 0; i < files.size(); ++i) {
			const auto start = std::chrono::steady_clock::now();
			const auto tree = hashFileSerially(files[i].path, files[i].blockSize);
			bestSerialFileMs[i] = std::min(bestSerialFileMs[i], elapsedMilliseconds(start));
			expectSameTree(files[i].expected, tree);
		}
		bestSerialTotalMs = std::min(bestSerialTotalMs, elapsedMilliseconds(totalStart));
	}

	const vector<size_t> requestedWorkers = { 2, 4, 8, 16 };
	vector<vector<double>> bestParallelFileMs(requestedWorkers.size(), vector<double>(files.size(), std::numeric_limits<double>::max()));
	vector<double> bestParallelTotalMs(requestedWorkers.size(), std::numeric_limits<double>::max());
	for(size_t config = 0; config < requestedWorkers.size(); ++config) {
		for(size_t trial = 0; trial < trials; ++trial) {
			const auto totalStart = std::chrono::steady_clock::now();
			for(size_t i = 0; i < files.size(); ++i) {
				File input(files[i].path, File::READ, File::OPEN);
				TigerTree tree;
				std::atomic<uint64_t> progressed(0);
				const auto start = std::chrono::steady_clock::now();
				ASSERT_TRUE(TigerTreeHasher::hash(input, files[i].size, files[i].blockSize, requestedWorkers[config], tree, {}, [&](size_t bytes) { progressed.fetch_add(bytes, std::memory_order_relaxed); })) << files[i].path;
				bestParallelFileMs[config][i] = std::min(bestParallelFileMs[config][i], elapsedMilliseconds(start));
				EXPECT_EQ(static_cast<uint64_t>(files[i].size), progressed.load(std::memory_order_relaxed)) << files[i].path;
				expectSameTree(files[i].expected, tree);
			}
			bestParallelTotalMs[config] = std::min(bestParallelTotalMs[config], elapsedMilliseconds(totalStart));
		}
	}

	const auto mebibytes = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
	std::cout << std::fixed << std::setprecision(2);
	std::cout << "TTH_DIRECTORY_SUMMARY\tmode\trequested_workers\tbest_ms\tMiB_per_s\tspeedup\n";
	std::cout << "TTH_DIRECTORY_SUMMARY\tserial\t0\t" << bestSerialTotalMs << '\t' << mebibytes * 1000.0 / bestSerialTotalMs << "\t1.00\n";
	for(size_t config = 0; config < requestedWorkers.size(); ++config) {
		std::cout << "TTH_DIRECTORY_SUMMARY\tparallel\t" << requestedWorkers[config] << '\t' << bestParallelTotalMs[config] << '\t' << mebibytes * 1000.0 / bestParallelTotalMs[config] << '\t' << bestSerialTotalMs / bestParallelTotalMs[config] << '\n';
	}
	std::cout << "TTH_DIRECTORY_FILE\tbytes\tblock_size\tserial_ms";
	for(const auto workers: requestedWorkers) std::cout << "\tparallel_" << workers << "_ms";
	std::cout << "\tpath\n";
	for(size_t i = 0; i < files.size(); ++i) {
		std::cout << "TTH_DIRECTORY_FILE\t" << files[i].size << '\t' << files[i].blockSize << '\t' << bestSerialFileMs[i];
		for(size_t config = 0; config < requestedWorkers.size(); ++config) std::cout << '\t' << bestParallelFileMs[config][i];
		std::cout << '\t' << std::quoted(files[i].path) << '\n';
	}
}
