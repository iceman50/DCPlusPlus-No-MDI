# Share Cache SQLite Snapshot

DC++ stores an optional startup snapshot of the in-memory shared-file tree in `ShareCache.sqlite3` in the user configuration folder. The snapshot is separate from `HashStore.sqlite3`: the hash database remains the authoritative store for TTH data, while the share cache only avoids a blocking filesystem walk at startup when the previous share tree is still usable.

On startup, DC++ loads the normal settings and hash database first. If the share cache is enabled and the snapshot matches the current share roots and share-affecting settings, the cached tree is loaded immediately, the TTH index and bloom filter are rebuilt in memory, and a full share refresh is started in the background. If the cache is missing, corrupt, from an unsupported schema version, was created with different sharing settings, or queued duplicate removal needs a freshly scanned share, DC++ logs the reason and uses the existing full refresh path.

The cache fingerprint includes the configured share roots, virtual names, duplicate-file policy, hidden/link handling, sharing skiplist settings, size filters, temporary download directory and TLS private key path. Changing any of those values invalidates the snapshot so stale filtering rules are not reused.

The SQLite cache loader validates rows before swapping them into the live share:

* Directory and file names must be non-empty path segments.
* TTH values must be valid base32 TTH roots.
* File sizes must be non-negative.
* Cached real-path overrides must remain under a configured share root.
* Parent rows must exist before child rows are accepted.

The current share remains untouched if validation fails. Full refreshes save a new snapshot in one SQLite transaction, so a failed or interrupted save cannot expose a partial tree as a valid cache.

Manual `/refresh` still performs the normal full filesystem scan. When that scan finishes successfully, DC++ saves a new share-cache snapshot for the next startup.

Manual `/rebuild` still belongs to the hash database path and rebuilds `HashStore.sqlite3` from the current in-memory hash records. It does not trust the share cache for pruning decisions; when queued duplicate removal requires a fresh share scan, startup skips the cache and uses the existing blocking refresh path.
