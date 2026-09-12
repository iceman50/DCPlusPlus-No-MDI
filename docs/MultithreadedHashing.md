<!-- Copyright (C) 2026 iceman50 -->

# Multi-threaded Tiger Tree hashing

DC++ hashes shared files with Tiger Tree Hashes (TTH). The implementation keeps file input sequential and parallelizes only independent final tree blocks. This preserves the exact TTH root and leaf layout produced by the original `TigerTree::update` path while allowing Tiger calculations to use more than one CPU core.

## Configuration

`Settings > Experimental > Transfers and hashing > Hashing worker threads` sets the maximum worker count for one file. The default is 2, the minimum is 1, and the maximum is 64. A value of 1 selects the original single-threaded hashing path.

The configured value is an upper bound. Before every file, DC++ reduces it to the number of logical processors reported by the C++ runtime, the number of TTH leaves in the file, and a fixed 256 MiB pipeline-memory budget. A very large TTH block therefore falls back to one worker instead of attempting a dangerous allocation. Changing the setting applies to the next file; a pool already hashing a file is allowed to finish normally.

`Maximum hash speed` remains an aggregate file-read limit. Increasing the worker count does not multiply that limit.

## Data flow and correctness invariants

1. `HashManager::Hasher` opens and validates the file snapshot exactly as before.
2. `FileReader` reads that same handle in file order. On Linux this prevents a path replacement between validation and reading from switching the hash to a different inode. On Windows the original single-thread path retains unbuffered overlapped I/O, while the parallel path reads the already-open write-denying handle.
3. The coordinator performs speed limiting and feeds CRC32 in strict file order. SFV behavior is therefore unchanged.
4. Each bounded work item contains at most one final TTH block. Workers own their buffers and never access the hash database, settings, listeners, or another worker's `TigerTree`.
5. Leaf roots are stored by block index, not completion order. The final `TigerTree` is constructed only after every worker has joined and every expected leaf is present.
6. The result is rejected if cancellation occurs, the byte count differs from the validated size, a worker or callback fails, or the file metadata changes during hashing. Partial trees are never published or written to SQLite.

Hash completion and failure events remain serialized by the existing outer hasher thread, so share-tree consumers continue to receive one terminal event for each hash-job cohort.

## FileReader changes

The cached reader now fills each requested callback block across short operating-system reads instead of exposing arbitrary partial chunks. Only the final block may be short. Callback cancellation is honored immediately, including the Windows overlapped-reader tail path.

An overload accepts an already-open `File`. Parallel hashing uses it on both platforms, and serial hashing uses it on Linux. Linux applies sequential and discard-cache advice when direct reads were requested; failures of those optional hints do not fail hashing. Existing FileReader callers retain their original path-based API.

## Failure and shutdown behavior

The worker pool has bounded input, owns all copied buffers, catches every worker exception, and joins every thread before returning or rethrowing. Cancellation clears queued work, wakes blocked producers and consumers, and waits for any block already executing. No worker captures a listener or outlives `HashManager::Hasher::fastHash`.

Hash database writes, listener delivery, rebuild scheduling, pause state, and per-file terminal-event tracking remain outside the pool. This deliberately limits concurrency to deterministic, side-effect-free Tiger calculations.

## Validation

Unit tests compare serial and parallel roots and complete leaf arrays at empty, 1 KiB, 64 KiB, 1 MiB, and partial-block boundaries. Additional tests cover several final block sizes, oversized-but-valid leaf sizes without oversized allocation, invalid tree layouts, ordered observation, byte progress, cancellation, short reads, size growth/truncation, exception propagation, worker-count bounds, settings persistence, and repeated shutdown-safe worker joins.

`TigerTreeHasherBenchmark.DISABLED_compare_serial_and_parallel_file_hashing` is an opt-in release benchmark. It warms the filesystem cache, alternates serial and parallel hashing of the same 128 MiB file, compares the complete trees after every run, and reports the best elapsed time for each path. It is disabled during normal test runs because timing tests should not gate correctness builds.

`TigerTreeHasherBenchmark.DISABLED_compare_serial_and_parallel_directory_hashing` recursively benchmarks every regular file beneath `DCPP_HASH_BENCHMARK_PATH`. It builds serial reference trees during an unmeasured cache-warming pass, then performs two measured serial passes and two parallel passes at requested worker counts 2, 4, 8, and 16. Every measured tree is compared with its reference, and every parallel run must report exactly the expected byte progress. The output includes aggregate throughput and tab-separated per-file timings.

For example, from PowerShell with a release test build:

```powershell
$env:DCPP_HASH_BENCHMARK_PATH = 'G:\Share'
.\build\release-mingw-x64\test\gtest.exe --gtest_also_run_disabled_tests --gtest_filter=TigerTreeHasherBenchmark.DISABLED_compare_serial_and_parallel_directory_hashing
```
