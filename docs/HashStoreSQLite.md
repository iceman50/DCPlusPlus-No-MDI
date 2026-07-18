# SQLite Hash and Share Cache Storage

This document is the single source of truth for the SQLite-backed local hash
and share-cache storage used by DC++ in this tree.

It merges the former hash-store and share-cache notes and expands them into a
top-to-bottom description of the whole system: what is stored, what is only
cached, how the startup path works, how old `HashIndex.xml` and `HashData.dat`
files are migrated, how the in-memory indexes relate to SQLite, and how upload
requests are protected while the share is refreshing.

## Executive Summary

DC++ uses two SQLite databases in the user configuration directory:

| Database | Owner | Role | Authority |
| --- | --- | --- | --- |
| `HashStore.sqlite3` | `HashManager::HashStore` | Durable storage for TTH roots, file records and full Tiger Tree leaf data | Authoritative for local hash records once accepted or migrated |
| `ShareCache.sqlite3` | `ShareManager` | Optional startup snapshot of the in-memory share tree | Never authoritative; only a validated warm-start cache |

The distinction matters:

* `HashStore.sqlite3` answers "what TTH did this real file have when it was
  hashed with this size and timestamp?" and "what full Tiger Tree belongs to
  this root?"
* `ShareCache.sqlite3` answers "can we restore the previous shared directory
  tree quickly while a real filesystem refresh runs in the background?"
* The hash store can prevent needless rehashing and can serve tree requests.
* The share cache can prevent a slow blocking startup scan, but it is discarded
  whenever its fingerprint or validation rules do not match the current share
  configuration.

Both databases are derived local data. Losing them is inconvenient but not
fatal: files can be scanned and hashed again.

## Main Classes And Files

The relevant code is split across these classes:

| Component | Source | Responsibility |
| --- | --- | --- |
| `HashManager` | `dcpp/HashManager.h`, `dcpp/HashManager.cpp` | Public hash API, async hashing thread, synchronous refresh-time verifier |
| `HashManager::HashStore` | `dcpp/HashManager.h`, `dcpp/HashManager.cpp` | In-memory hash indexes plus durable SQLite hash database |
| `HashManager::Hasher` | `dcpp/HashManager.h`, `dcpp/HashManager.cpp` | Background worker that hashes files and schedules rebuilds |
| `HashLoader` | `dcpp/HashManager.cpp` | Legacy XML parser for `HashIndex.xml` migration |
| `SQLiteDB` | `dcpp/SQLiteDB.h`, `dcpp/SQLiteDB.cpp` | Small hardened RAII wrapper around the bundled SQLite library |
| `SQLiteStatement` | `dcpp/SQLiteDB.h`, `dcpp/SQLiteDB.cpp` | Prepared statement wrapper with typed binds and column reads |
| `SQLiteTransaction` | `dcpp/SQLiteDB.h`, `dcpp/SQLiteDB.cpp` | `BEGIN IMMEDIATE` transaction guard with rollback on destruction |
| `ShareManager` | `dcpp/ShareManager.h`, `dcpp/ShareManager.cpp` | Share tree, share cache, TTH-to-path lookups, file list generation |
| `UploadManager` | `dcpp/UploadManager.cpp` | Upload request preparation and refresh-time TTH verification |

## Terms

### TTH

TTH means Tiger Tree Hash. It is the root hash of a Tiger Tree built from a
file's data. In DC protocol terms it is the stable content identifier used for
search results, queue matching, alternate source matching, tree requests and
file requests such as `TTH/<base32-root>`.

### Tiger Tree

A Tiger Tree is a Merkle tree over a file. DC++ stores enough information to
reconstruct the full tree for a root:

* file size
* tree block size
* leaf hashes for multi-leaf trees
* the root itself

For single-leaf trees, the root plus size and block size are sufficient. The
SQLite hash store therefore stores `NULL` in the `trees.leaves` column for
single-leaf trees.

### Real Path And Virtual Path

The hash store is keyed by real filesystem paths. The share manager exposes
virtual paths to remote users and maps those virtual paths back to real paths
when serving uploads.

Examples:

* real path: `D:\Share\Music\Album\song.flac`
* virtual path: `/Music/Album/song.flac`
* TTH request path: `TTH/ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDEFG`

The hash store does not decide whether a file is shared. It only stores hashes
for real files. The share manager decides which real files are currently shared
and visible to a given hub.

## Storage Files

### `HashStore.sqlite3`

Located in:

```text
<user config directory>\HashStore.sqlite3
```

SQLite may also create:

```text
HashStore.sqlite3-wal
HashStore.sqlite3-shm
```

Those sidecar files are normal when WAL journaling is active.

### `ShareCache.sqlite3`

Located in:

```text
<user config directory>\ShareCache.sqlite3
```

It may also have WAL sidecars.

### Legacy Files

Older builds used:

```text
HashIndex.xml
HashData.dat
```

`HashIndex.xml` contained file and tree metadata. `HashData.dat` contained the
serialized tree leaf data. The SQLite migration path imports valid data from
those two files once, then renames the legacy files to `.migrated` names instead
of deleting them.

## SQLite Wrapper Behavior

All SQLite use goes through `SQLiteDB`, `SQLiteStatement` and
`SQLiteTransaction`.

### Opening A Database

`SQLiteDB::open` uses `sqlite3_open_v2` with:

* `SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE` for normal writable opens
* `SQLITE_OPEN_READONLY` for read-only opens
* `SQLITE_OPEN_FULLMUTEX`
* `SQLITE_OPEN_PRIVATECACHE`

The full mutex mode makes each connection safe even if manager code reaches it
from different paths. Private cache avoids SQLite shared-cache state leaking
between unrelated local databases.

### Connection Hardening

`SQLiteDB::configure` applies a conservative profile:

* 30 second busy timeout
* defensive mode enabled
* trusted schema disabled when supported
* triggers disabled when supported
* views disabled when supported
* foreign keys enabled
* WAL journal mode
* `synchronous=NORMAL`
* temporary storage in memory
* memory mapped I/O disabled
* cell size checks enabled

It also reduces generic SQLite limits:

* maximum value length: 16 MiB
* maximum SQL length: 64 KiB
* maximum columns: 64
* maximum compound-select terms: 4
* attached databases: 0 by default
* LIKE pattern length: 1024
* bound variables: 128

The hash and share-cache databases do not need triggers, views, attached
databases, huge SQL strings or application-defined schema behavior. These
limits reduce the blast radius of a corrupt or hostile local database while
still allowing normal hash and share-cache records.

### Transactions

`SQLiteTransaction` starts a `BEGIN IMMEDIATE` transaction. The transaction
commits only when `commit()` is called. If the object is destroyed while still
active, it rolls back.

This is used for:

* one-time legacy migration
* full hash-store rebuilds
* share-cache snapshot replacement
* write batching in the hash store

## Hash Store: Conceptual Model

The hash store has two layers:

1. In-memory indexes used by the live program.
2. SQLite rows used to persist those indexes across restarts.

The in-memory indexes remain the primary runtime structure. SQLite is the
durable backing store, not a query engine used for every share lookup.

### In-Memory Indexes

`HashManager::HashStore` keeps:

```cpp
unordered_map<string, vector<FileInfo>> fileIndex;
unordered_map<TTHValue, TreeInfo> treeIndex;
```

`fileIndex` maps a real directory path to file records inside that directory.
Each `FileInfo` contains:

* file name
* TTH root
* last write timestamp
* `used` flag

The split between directory path and file name matches the historic hash store
layout and keeps lookups compatible with existing behavior.

`treeIndex` maps a TTH root to `TreeInfo`. Each `TreeInfo` contains:

* file size
* legacy tree data index marker
* block size

In the SQLite backend, the `index` field is no longer a byte offset into
`HashData.dat`. It is kept as compatibility metadata:

* `SMALL_TREE` means a single-leaf tree where no leaf blob is needed.
* `0` means the full tree must be loaded from the SQLite `trees.leaves` blob.

### Why Keep In-Memory Indexes?

The rest of DC++ expects hash lookups to be cheap, synchronous and protected by
the hash manager lock. Keeping the old in-memory structure preserves that
behavior:

* share scanning can ask for TTH roots without running SQL for each file
* queue matching can ask for block sizes quickly
* tree lookup first checks whether the root exists
* rebuild logic can work over known-used hash records
* startup can load once, then avoid database reads except for tree blobs

SQLite mainly replaces the old XML/DAT durability layer.

## Hash Store Schema

`HashStore.sqlite3` contains three `WITHOUT ROWID` tables.

### `trees`

```sql
CREATE TABLE IF NOT EXISTS trees (
    root BLOB PRIMARY KEY NOT NULL CHECK(length(root) = 24),
    size INTEGER NOT NULL CHECK(size >= 0),
    block_size INTEGER NOT NULL CHECK(block_size >= 1024),
    leaves BLOB
) WITHOUT ROWID;
```

Columns:

| Column | Meaning |
| --- | --- |
| `root` | 24-byte binary TTH root |
| `size` | original file size |
| `block_size` | Tiger Tree block size |
| `leaves` | concatenated 24-byte leaf hashes, or `NULL` for single-leaf trees |

Important details:

* `root` is stored as binary, not base32 text.
* `leaves` is binary leaf data, not a serialized XML value.
* `leaves` must either be `NULL` for a single-leaf tree or exactly
  `TigerTree::calcBlocks(size, block_size) * TTHValue::BYTES` bytes.
* The root is verified again when a tree blob is loaded.

### `files`

```sql
CREATE TABLE IF NOT EXISTS files (
    path TEXT PRIMARY KEY NOT NULL,
    size INTEGER NOT NULL CHECK(size >= 0),
    timestamp INTEGER NOT NULL CHECK(timestamp > 0),
    root BLOB NOT NULL CHECK(length(root) = 24),
    FOREIGN KEY(root) REFERENCES trees(root) ON UPDATE CASCADE ON DELETE CASCADE
) WITHOUT ROWID;

CREATE INDEX IF NOT EXISTS idx_hash_files_root ON files(root);
```

Columns:

| Column | Meaning |
| --- | --- |
| `path` | full real filesystem path |
| `size` | file size when hashed |
| `timestamp` | last modified timestamp when hashed |
| `root` | TTH root that points to `trees.root` |

The `files` table answers whether a real file still has a current hash. A row is
accepted only if the caller presents the same path, size and timestamp.

The foreign key prevents a file row from pointing at a missing tree row in new
writes. Startup still validates rows because old databases, manually edited
databases or partially migrated data may not be trustworthy.

### `metadata`

```sql
CREATE TABLE IF NOT EXISTS metadata (
    key TEXT PRIMARY KEY NOT NULL,
    value TEXT NOT NULL
) WITHOUT ROWID;
```

Used for durable state that is not a hash record. The important keys are:

| Key | Meaning |
| --- | --- |
| `legacy_migration_complete` | `1` means the legacy XML/DAT import decision has been completed |
| `legacy_migration_time` | diagnostic timestamp for migration completion |
| `legacy_migrated_files` | diagnostic count of imported file rows |
| `legacy_migrated_trees` | diagnostic count of imported tree rows |
| `legacy_index_file` | diagnostic source file name |
| `legacy_data_file` | diagnostic source file name |

An empty database with only metadata is not considered to contain hash data.
This prevents a metadata-only database from blocking the correct empty-store
startup path.

## Hash Store Schema Versions

The schema version is stored in SQLite `PRAGMA user_version`.

### Version 1

Version 1 was the first SQLite hash-store layout.

### Version 2

Version 2 adds the enforced foreign-key relationship:

```text
files.root -> trees.root
```

When a database with version lower than 2 is opened, migration creates a
replacement `files_v2` table and copies only usable file rows:

* `path` must not be empty
* `size` must be non-negative
* `timestamp` must be positive
* `root` must be 24 bytes
* a matching tree row must exist

Rows that do not satisfy those rules are dropped. That is intentional. A file
record without a valid tree cannot answer tree requests correctly and should be
rehashable instead of trusted.

After copying, the old `files` table is dropped, `files_v2` is renamed to
`files`, the root index is recreated, and `user_version` becomes 2.

For already-newer databases, startup still creates
`idx_hash_files_root` if needed. This is a cheap repair for databases created by
intermediate builds.

## Hash Store Startup

Startup enters through:

```cpp
HashManager::startup(progressF)
```

which starts the hasher thread and calls:

```cpp
HashStore::load(progressF)
```

The load sequence is:

1. Open `HashStore.sqlite3`.
2. Create the schema if missing.
3. Migrate the schema if needed.
4. Optionally run `PRAGMA quick_check` if startup verification is enabled.
5. Decide whether SQLite already contains hash rows.
6. Load SQLite rows, or perform the one-time legacy migration path.

### Decision Tree

#### Case 1: SQLite Contains Tree Or File Rows

If either `trees` or `files` contains rows:

1. SQLite is treated as authoritative.
2. `loadDb` loads valid rows into `treeIndex` and `fileIndex`.
3. If the legacy migration marker is missing, it is backfilled.
4. Old `HashIndex.xml` and `HashData.dat` files are renamed to `.migrated`
   names if they still exist.

This prevents stale legacy files from overwriting newer SQLite data.

#### Case 2: SQLite Is Empty But Migration Is Already Complete

If there are no hash rows but `legacy_migration_complete=1`:

1. The empty SQLite state is treated as intentional.
2. Old legacy files are renamed if they appear.
3. No legacy import is attempted.

This avoids repeatedly importing stale XML/DAT files after the user has already
moved to SQLite.

#### Case 3: SQLite Is Empty And Migration Is Not Complete

If there are no rows and no completion marker:

1. DC++ attempts to load `HashIndex.xml`.
2. If the legacy index is missing, migration is marked complete with zero rows.
3. If the legacy index loads, trees are validated against `HashData.dat`.
4. Valid trees and matching file rows are written into SQLite in one
   transaction.
5. Legacy files are renamed to `.migrated` names after a successful import.

If legacy loading fails, the legacy files are left in place and memory indexes
are cleared. The user can inspect or recover the old files, and shared files can
be rehashed normally.

## Loading SQLite Hash Rows

`loadDb` first reserves in-memory capacity for `treeIndex` when the row count is
reasonable. It does not blindly trust a huge count.

It then scans:

```sql
SELECT root, size, block_size, leaves IS NULL FROM trees
```

For each tree row:

* `root` must be 24 bytes
* `size` must be non-negative
* `block_size` must be at least 1024
* the in-memory index stores `SMALL_TREE` if `leaves IS NULL`, otherwise `0`

It then scans:

```sql
SELECT path, timestamp, root FROM files ORDER BY path
```

For each file row:

* `path` must not be empty
* `timestamp` must not be zero
* `root` must be 24 bytes
* `root` must already exist in `treeIndex`

Rows that fail validation are skipped. If any invalid tree, invalid file or
orphan file row is skipped, a warning is logged.

The ordered file scan keeps records grouped by directory path while rebuilding
the in-memory `fileIndex`.

## Hash Lookup Flow

The public lookup is:

```cpp
HashManager::getTTH(fileName, size, timestamp)
```

It locks the hash manager and calls:

```cpp
HashStore::getTTH(fileName, size, timestamp)
```

The store:

1. Splits the real path into directory path and file name.
2. Finds that directory in `fileIndex`.
3. Finds the file name in the directory's file list.
4. Looks up the stored root in `treeIndex`.
5. Accepts the row only when:
   * the tree exists
   * the stored tree size equals the caller's current file size
   * the stored timestamp equals the caller's current file timestamp
6. Marks the file record as `used`.
7. Returns the root.

If size or timestamp changed, the file row is removed from SQLite and erased
from the in-memory file list. The caller receives `nullopt`, and the file is
scheduled for hashing.

That is the core cache rule: path plus size plus timestamp must match.

## Adding A Hash

When the hasher finishes hashing a file, it calls:

```cpp
HashManager::hashDone(fileName, timestamp, tigerTree, speed, size)
```

That calls:

```cpp
HashStore::addFile(fileName, timestamp, tigerTree, used=true)
```

`addFile` does two things:

1. Ensures the tree exists by calling `addTree`.
2. Replaces any existing file record for the same directory/name.

`addTree` writes the `trees` row if the root is not already known in memory.
`addFile` writes the `files` row every time the file record is updated.

The SQL writes are upserts:

```sql
INSERT INTO trees(root, size, block_size, leaves)
VALUES(?1, ?2, ?3, ?4)
ON CONFLICT(root) DO UPDATE SET
    size=excluded.size,
    block_size=excluded.block_size,
    leaves=excluded.leaves;
```

```sql
INSERT INTO files(path, size, timestamp, root)
VALUES(?1, ?2, ?3, ?4)
ON CONFLICT(path) DO UPDATE SET
    size=excluded.size,
    timestamp=excluded.timestamp,
    root=excluded.root;
```

## Write Batching

Hash writes are not committed one row at a time unless the configured batch size
is 1.

`beginWrite` opens a transaction on the first pending write.
`recordWrite` marks the store dirty and increments a counter.
When the counter reaches `HASH_DB_WRITE_BATCH_SIZE`, `flushWrites` commits.

Default:

```text
HASH_DB_WRITE_BATCH_SIZE = 256
```

Tradeoff:

* larger batches reduce transaction overhead while hashing many files
* larger batches also mean more recently hashed files may need to be rehashed if
  the process exits before the transaction is committed

`HashStore::save` flushes pending writes, runs `PRAGMA optimize`, and performs a
passive WAL checkpoint.

`HashStore::~HashStore` also tries to flush pending writes during cleanup.

## Loading A Full Tree

The in-memory `treeIndex` tells DC++ whether a root is known and what block size
belongs to it. The full leaf data is loaded lazily from SQLite when needed.

The public call is:

```cpp
HashManager::getTree(root, tigerTree)
```

The store:

1. Checks that `root` exists in `treeIndex`.
2. Reads the row from `trees`.
3. Validates size and block size.
4. Computes the expected leaf count.
5. Handles `NULL` leaves only when exactly one leaf is expected.
6. Handles blob leaves only when blob size equals expected size.
7. Reconstructs the `TigerTree`.
8. Recomputes and verifies that the resulting root equals the requested root.

If any check fails, the tree is treated as unavailable. That protects tree
uploads and queue validation from malformed database rows.

## Tree Requests And Downloads

Remote users can request a tree from us. `ShareManager::getTree` resolves the
requested virtual file or TTH root, then asks `HashManager::getTree` for the
full tree.

Downloads also use stored tree data. `QueueManager` may call
`HashManager::getBlockSize` or `HashManager::getTree` when validating existing
partial data and building segments.

This is why the SQLite store persists the full Tiger Tree leaves and not only
the root. A root is enough to identify content, but a full tree is needed for
chunk verification and tree upload compatibility.

## Rebuild

The `/rebuild` path schedules or runs:

```cpp
HashStore::rebuild()
```

Rebuild is a pruning operation. It does not rescan every file from disk. It
works from the in-memory hash records and keeps only records that were marked
`used`.

The `used` flag is set when a current file lookup succeeds. Therefore rebuild
preserves hash records that were actually observed as still relevant during the
current session/share activity.

Rebuild sequence:

1. Flush pending writes.
2. Build a new tree index containing roots referenced by used file records.
3. Load each tree from SQLite and discard trees that fail validation.
4. Build a new file index containing only used file records whose tree survived.
5. Replace the in-memory indexes.
6. In one SQLite transaction:
   * delete all rows from `files`
   * delete all rows from `trees`
   * write the surviving tree rows
   * write the surviving file rows
7. Optionally compact if `HASH_DB_COMPACT_ON_REBUILD` is enabled.

This preserves the public behavior of the old rebuild command while replacing
the old XML/DAT rewrite with a transactionally safe SQLite rewrite.

## Maintenance Operations

The Upload settings page exposes hash database maintenance controls.

### Verify On Startup

Setting:

```text
HASH_DB_VERIFY_STARTUP
```

Default:

```text
false
```

When enabled, startup runs:

```sql
PRAGMA quick_check
```

If it fails, DC++ logs the failure and continues. Bad or missing hashes can be
rehashable derived data.

### Manual Verify

The manual verify action calls:

```sql
PRAGMA quick_check
```

and logs whether the hash database quick check passed.

### Full Check

The full check action calls:

```sql
PRAGMA integrity_check
```

This is slower but more exhaustive than `quick_check`.

### Optimize

The optimize action:

1. flushes pending writes
2. runs `PRAGMA optimize`
3. runs `PRAGMA wal_checkpoint(PASSIVE)`

### Compact

The compact action:

1. flushes pending writes
2. clears cached prepared statements
3. runs `PRAGMA wal_checkpoint(TRUNCATE)`
4. temporarily allows one attached database because SQLite `VACUUM` may need it
5. runs `VACUUM`
6. runs `PRAGMA optimize`

## Legacy XML/DAT Migration

Migration exists for users upgrading from older hash storage.

### Legacy XML Index

`HashLoader` parses `HashIndex.xml`. It recognizes version 2 and version 3
legacy stores.

For tree entries, it reads:

* type
* index
* block size
* size
* root

For file entries, it reads:

* file name/path
* timestamp
* root

Version 2 stored paths in lower case. On Windows, `upgradeFromV2` attempts to
find the actual file with proper casing and rejects ambiguous case-insensitive
duplicates. On non-Windows builds this upgrade path currently returns false,
forcing rehashing instead of trusting lower-case path data.

### Legacy DAT Tree Data

For each legacy tree, migration validates the actual leaf data in
`HashData.dat`:

* `SMALL_TREE` entries are reconstructed directly from size, block size and root
* other entries seek to the stored byte offset
* the expected leaf byte count is calculated
* the tree is rebuilt
* the resulting root must equal the XML root

Invalid trees are discarded.

### Import Transaction

After validation:

1. file records whose trees failed validation are removed from memory
2. SQLite `files` and `trees` tables are cleared
3. valid trees are written
4. valid file rows are written
5. migration metadata is written
6. the transaction commits

Only after the transaction succeeds are the legacy files renamed.

### Legacy File Renaming

The old files are renamed, not deleted:

```text
HashIndex.xml -> HashIndex.xml.migrated
HashData.dat  -> HashData.dat.migrated
```

If the target already exists, a timestamp is appended. This is intentionally
non-destructive and leaves a manual recovery trail.

## Background Hashing

The background hasher owns a queue:

```cpp
map<string, int64_t> w;
```

It is sorted by path to keep deterministic behavior and avoid strange random
share ordering while hashing.

When `HashManager::getTTH` misses, it schedules:

```cpp
hasher.hashFile(fileName, size)
```

The hasher thread:

1. waits for work
2. handles scheduled rebuilds before normal hash work
3. opens the file
4. reads size and timestamp
5. calculates the Tiger Tree block size
6. reads file data through `FileReader`
7. optionally checks matching SFV CRC32
8. throttles if `MAX_HASH_SPEED` is configured
9. honors pause/resume
10. finalizes the tree
11. calls `hashDone` if hashing completed and optional CRC matched

Hashing errors are logged. Failed hashing does not create a database row.

## Synchronous Refresh-Time Verification

In addition to the background hasher, the current implementation has a
synchronous verifier:

```cpp
HashManager::verifyFileTTH(fileName, size, root)
```

This is used by uploads while a share refresh is active.

### Why This Exists

When DC++ starts with a cached share tree, remote users may reconnect and ask
for queued files immediately. Meanwhile, the live filesystem refresh may still
be catching up. A cached virtual path can briefly point at a real path that has
been moved, replaced or removed locally.

Serving bytes from a stale path would be worse than making the peer retry. The
verifier makes the upload path prove that the file currently on disk still
matches the requested TTH before it is exposed to a peer.

### Verification Steps

`verifyFileTTH`:

1. rejects negative expected sizes
2. opens the file shared for reading
3. reads live file size and last modified timestamp
4. rejects if the live size differs from the expected share size
5. checks the short-lived verification cache
6. reads and hashes the full file synchronously on cache miss
7. rejects if the read did not consume the expected byte count
8. rejects if the computed root differs from the requested root
9. saves the verified tree/file row to the durable hash store
10. records a verification-cache success
11. fires `HashManagerListener::TTHDone`

Failures are recorded in the verification cache too, as long as path, size,
timestamp and requested root remain the same.

### Verification Cache

The verifier keeps a small deque of recent proof results. Entries are keyed by:

* real path
* size
* timestamp
* requested root

The cache stores both success and failure results and is capped at 256 entries.

This cache is intentionally separate from the durable hash store. It is only a
short-lived guard against re-reading the same requested file repeatedly while a
live share refresh is still active. If the file changes size or timestamp, the
entry no longer matches and the file must be hashed again.

## Upload Path During Share Refresh

`UploadManager::prepareFile` captures the requested TTH root for normal file
uploads:

* if the request is `TTH/<root>`, the root is parsed from the request
* otherwise the root is obtained from `ShareManager::getTTH`

When `ShareManager::isRefreshing()` is true, file uploads are treated as
candidate paths until verified.

The upload flow is:

1. resolve the requested virtual file to a real path and expected size
2. capture the requested TTH root
3. if the share is refreshing, call `verifyFileTTH` for the resolved real path
4. if that fails, ask `ShareManager::getRealPaths(root, hubUrl)` for other
   currently shared real paths with the same TTH that are visible from the same
   hub
5. try verification for each alternate path
6. if an alternate verifies, upload from that alternate path
7. if no path verifies, send a retryable maxed-out/upload-queue response and
   disconnect

The important behavior is that refresh uncertainty becomes retryable. DC++ does
not serve unverified bytes, and it does not permanently poison the remote
user's queue with a hard "file not available" when the problem may only be a
temporary refresh window.

## Share Cache: Conceptual Model

The share cache is separate from the hash store.

It stores a snapshot of the in-memory share tree:

* virtual directory names
* optional real-name overrides
* files
* file sizes
* file TTH roots when known
* exact real file paths

It does not replace a filesystem refresh forever. It only allows startup to
become responsive immediately when the previous share tree is still compatible
with current settings.

## Share Cache Schema

`ShareCache.sqlite3` has schema version 2.

### `metadata`

```sql
CREATE TABLE IF NOT EXISTS metadata (
    key TEXT PRIMARY KEY NOT NULL,
    value TEXT NOT NULL
) WITHOUT ROWID;
```

Important keys:

| Key | Meaning |
| --- | --- |
| `schema` | share-cache schema version as text |
| `fingerprint` | compact hash of share roots and share-affecting settings |
| `app` | diagnostic application/version string |
| `directories` | diagnostic count saved after snapshot write |
| `files` | diagnostic count saved after snapshot write |

The schema version is also stored in `PRAGMA user_version`.

### `directories`

```sql
CREATE TABLE IF NOT EXISTS directories (
    id INTEGER PRIMARY KEY NOT NULL,
    parent_id INTEGER REFERENCES directories(id) ON DELETE CASCADE,
    name TEXT NOT NULL CHECK(length(name) > 0),
    real_name TEXT
);

CREATE INDEX IF NOT EXISTS idx_share_cache_directories_parent
    ON directories(parent_id);
```

Columns:

| Column | Meaning |
| --- | --- |
| `id` | cache-local directory id |
| `parent_id` | parent directory id, or `NULL` for root virtual shares |
| `name` | virtual directory segment |
| `real_name` | optional real name override |

Directory ids are cache-local. They are not stable public identifiers.

### `files`

```sql
CREATE TABLE IF NOT EXISTS files (
    id INTEGER PRIMARY KEY NOT NULL,
    directory_id INTEGER NOT NULL REFERENCES directories(id) ON DELETE CASCADE,
    name TEXT NOT NULL CHECK(length(name) > 0),
    size INTEGER NOT NULL CHECK(size >= 0),
    tth TEXT CHECK(tth IS NULL OR length(tth) = 39),
    real_path TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_share_cache_files_directory
    ON files(directory_id);

CREATE INDEX IF NOT EXISTS idx_share_cache_files_tth
    ON files(tth);
```

Unlike the hash store, share-cache TTH values are stored as 39-character base32
text. The cache is designed to rebuild a share tree, not to serve full Tiger
Tree leaf data.

## Share Cache Fingerprint

The share cache is accepted only if its fingerprint matches the current share
configuration. The fingerprint uses FNV-style accumulation and includes:

* cache format marker: `ShareCache.v2`
* configured real share roots
* configured virtual share names
* `SHARE_HIDDEN`
* `FOLLOW_LINKS`
* `LIST_DUPES`
* `SHARING_SKIPLIST_REGEX`
* `SHARING_SKIPLIST_EXTENSIONS`
* `SHARING_SKIPLIST_PATHS`
* `SHARING_SKIPLIST_MINSIZE`
* `SHARING_SKIPLIST_MAXSIZE`
* `TEMP_DOWNLOAD_DIRECTORY`
* `TLS_PRIVATE_KEY_FILE`

The fingerprint is not a security boundary. It is an invalidation key. Its job
is to prevent DC++ from reusing a cached tree when the rules for what should be
shared have changed.

## Share Cache Startup

Startup enters through:

```cpp
ShareManager::startupRefresh(progressF)
```

If `loadShareCache()` succeeds:

1. the cached tree is immediately swapped into the live share manager
2. share indexes are rebuilt
3. the XML file list is marked dirty
4. a background full refresh starts

If `loadShareCache()` fails:

1. DC++ runs the normal blocking full share refresh
2. startup behavior falls back to the old safe path

### Reasons The Cache Is Skipped

The share cache is skipped when:

* `SHARE_CACHE` is disabled
* `DONT_DL_ALREADY_SHARED` is enabled
* any configured share root is a UNC path
* `ShareCache.sqlite3` does not exist
* the schema version does not match
* the fingerprint does not match
* validation fails
* any SQLite or C++ exception is thrown during load

`DONT_DL_ALREADY_SHARED` is special. Queue duplicate removal needs a freshly
scanned filesystem view. A stale share snapshot could remove valid queued files
during startup, so the share cache is intentionally bypassed in that mode.

## Share Cache Validation

The loader validates before touching the live share tree. The current share
remains intact if validation fails.

Directory validation:

* ids must be positive
* ids must be unique
* names must be non-empty
* names must not be `.` or `..`
* names must not contain path separators
* names must be at most 4096 bytes
* `real_name`, when present, follows the same segment rules
* root directories must match configured virtual roots
* child parent ids must already exist
* duplicate child names are rejected
* all configured virtual roots must be present

File validation:

* parent directory must exist
* name must be a valid path segment
* size must be non-negative
* TTH text, when present, must be 39 characters and parse as a TTH value
* `real_path` must be present and non-empty
* `real_path` must be at most 32768 bytes
* `real_path` must remain under a configured shared root

After loading, `ShareManager::rebuildIndices` recreates:

* TTH index
* bloom filter
* directory sizes
* duplicate handling according to settings

## Share Cache Saving

Full refreshes save a new snapshot by calling:

```cpp
ShareManager::saveShareCache()
```

If `SHARE_CACHE` is disabled, the save is skipped.

The save process:

1. opens `ShareCache.sqlite3`
2. creates the schema if needed
3. starts a transaction
4. deletes existing `metadata`, `files` and `directories` rows
5. writes metadata and the current fingerprint
6. writes directories recursively with cache-local ids
7. writes files for each directory
8. writes diagnostic directory/file counts
9. commits
10. runs `PRAGMA optimize`
11. runs `PRAGMA wal_checkpoint(PASSIVE)`

Because replacement happens inside one transaction, an interrupted save cannot
leave a partial snapshot that will be accepted as valid on the next startup.

Manual `/refresh` still performs a real filesystem scan. When that scan
finishes successfully, the new live share tree is saved for the next startup.

## How Hash Store And Share Cache Work Together

Startup ordering is important:

1. settings load
2. hash database loads
3. share startup refresh begins
4. share cache may be restored
5. background live refresh reconciles the restored tree with the filesystem

The hash store is needed before share scanning because share refresh calls:

```cpp
HashManager::getTTH(realPath, size, timestamp)
```

For each scanned file, the hash store either returns a current root or schedules
the file for hashing.

The share cache can contain TTH roots from the previous session. Those roots are
useful for immediate search results, file lists and upload path resolution, but
they are not final proof that the same bytes still exist on disk. The live
refresh and the refresh-time upload verifier close that gap.

## Moved Files And Queued Remote Uploads

Consider this scenario:

1. A remote user queued a file from us by TTH.
2. We moved the file locally from one shared directory to another.
3. We start DC++ and auto-connect to a hub.
4. The remote user requests the queued file immediately.
5. Our share cache has loaded, but the live refresh is still running.

The protection layers are:

* the upload request carries or resolves to the requested TTH
* the initially resolved path is verified by hashing before upload
* if that path fails, the share manager searches the current share tree for
  other paths with the same TTH visible from the requesting hub
* alternate paths are verified too
* if nothing verifies, the request receives a retryable queue response

This avoids serving the wrong bytes and also avoids turning a temporary refresh
race into a permanent queue failure for the remote user.

## Hub-Specific Share Visibility

Some hubs can have custom share directories. Therefore TTH-to-path fallback
cannot simply return every local file with a matching TTH.

`ShareManager::getRealPaths(root, hubUrl)`:

1. computes the requesting hub's share access
2. walks the current live share tree
3. selects files whose TTH matches
4. filters them through `isFileAllowed`
5. returns only real paths visible from that hub

This keeps refresh-time recovery from leaking paths or files that should not be
shared on that hub.

## Failure Behavior

### Hash Store

Hash-store failures are usually logged and treated as missing hash data.

Examples:

* database open failure
* schema migration failure
* invalid tree row
* invalid file row
* orphan file row
* tree root mismatch
* failed quick check or integrity check
* stale file row removal failure
* failed legacy migration
* failed legacy rename

When a hash record is skipped, the file can be hashed again if it is still
shared or needed.

### Share Cache

Share-cache failures are logged and fall back to a full refresh.

Examples:

* missing cache file
* schema mismatch
* fingerprint mismatch
* invalid directory row
* invalid file row
* invalid TTH text
* invalid real path
* parent missing
* duplicate directory
* exception during load

The live share tree is only swapped after the entire cache has validated.

### Upload During Refresh

If upload verification fails during refresh:

* DC++ does not upload the candidate file
* DC++ tries alternate shared paths with the same TTH
* if no verified path is found, DC++ responds as temporarily queued/maxed and
  disconnects

This is deliberately retryable.

## Settings Summary

| Setting | Default | Effect |
| --- | --- | --- |
| `HASH_DB_WRITE_BATCH_SIZE` | `256` | number of hash-store row changes per transaction batch |
| `HASH_DB_VERIFY_STARTUP` | `false` | run SQLite quick check when opening the hash database |
| `HASH_DB_COMPACT_ON_REBUILD` | `false` | run compact after hash database rebuild |
| `SHARE_CACHE` | `true` | enable startup share snapshot load/save |
| `DONT_DL_ALREADY_SHARED` | `false` | when true, share cache is skipped at startup |
| `LIST_DUPES` | `true` | affects share-cache fingerprint and index rebuild duplicate behavior |
| `SHARE_HIDDEN` | `false` | affects share-cache fingerprint |
| `FOLLOW_LINKS` | `false` | affects share-cache fingerprint |
| `SHARING_SKIPLIST_*` | varies | affects share-cache fingerprint |

## Invariants

The system depends on these invariants:

* `HashStore.sqlite3` stores durable hash facts, not share policy.
* `ShareCache.sqlite3` stores a startup snapshot, not durable truth.
* A hash file row is current only when path, size and timestamp all match.
* A file row without a valid tree row is unusable.
* A tree blob must reconstruct to the requested root.
* Legacy XML/DAT files never override non-empty SQLite data.
* A completed migration marker prevents stale legacy files from being imported
  later.
* The share cache is accepted only after schema, fingerprint and row validation.
* Uploads during refresh must verify bytes before serving normal files.
* Hub-specific share restrictions still apply to TTH fallback paths.

## Recovery Notes

Because both databases are derived data, the safest manual recovery for a
corrupt local store is usually:

1. close DC++
2. move the affected SQLite file and its WAL/SHM sidecars aside
3. restart DC++
4. allow the share to refresh and files to rehash as needed

For hash migration issues, old `.migrated` files may still exist and can be
inspected manually. They are not deleted by the migration path.

## Test Coverage

The current unit tests cover the major SQLite hash and share-cache paths:

* persisting file and tree rows
* batching writes and flushing on save
* stale file lookup removal
* rebuild pruning
* compact and verify maintenance
* v1-to-v2 schema migration
* migration completion metadata
* foreign-key rejection of orphan file records
* invalid leaf blob handling
* compact-after-rebuild behavior
* legacy XML/DAT migration
* avoiding stale legacy imports after completed empty SQLite setup
* share-cache round trips
* merged share/search/protocol lookup behavior after cache load
* saving cache snapshots after full refresh
* rejecting cache when share settings change
* skipping cache when queued duplicate removal requires a fresh share

The release build command used in this tree is:

```text
scons -j6 mode=release arch=x64
```

Use `mode=debug` for debug builds.

## Practical Mental Model

The easiest way to reason about the system is:

1. `HashStore.sqlite3` remembers hashes for real files.
2. The in-memory hash indexes make those records fast during runtime.
3. A hash record is trusted only while the real file still has the same size and
   timestamp.
4. Full tree data is persisted so DC++ can serve and validate Tiger Tree
   requests.
5. Legacy XML/DAT data is imported once, only when SQLite has no authoritative
   hash rows.
6. `ShareCache.sqlite3` remembers the shape of the last share tree for fast
   startup.
7. The share cache is thrown away whenever settings or validation say it might
   be stale.
8. During the window where a cached share is live and a filesystem refresh is
   still running, uploads prove the requested TTH before serving bytes.

That gives DC++ fast startup for large shares while preserving the important
safety rule: cached metadata can help find candidate files, but only verified
live bytes should be uploaded during a refresh race.
