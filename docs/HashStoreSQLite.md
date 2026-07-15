# HashStore SQLite Backend

DC++ stores the local TTH hash database in `HashStore.sqlite3` in the user configuration folder. SQLite may also create `HashStore.sqlite3-wal` and `HashStore.sqlite3-shm`; those sidecar files are expected when write-ahead logging is active.

The runtime behavior remains the same as the previous `HashIndex.xml` and `HashData.dat` backend. `HashManager::HashStore` still keeps the in-memory file and tree indexes used by sharing, searching, queue matching and TTH lookups. SQLite is only the durable storage layer for those records.

## Schema

The database contains three `WITHOUT ROWID` tables:

* `trees`: one row per TTH root, including file size, block size and the serialized leaf hashes for multi-leaf trees.
* `files`: one row per hashed file path, including file size, timestamp and the root that points back to `trees`.
* `metadata`: small key/value records for database state that should survive upgrades, such as whether legacy hash migration has already completed.

Single-leaf trees store a NULL leaf blob because the root is enough to recreate the tree.

Schema version 2 adds a foreign-key relationship from `files.root` to `trees.root`. When an older version 1 SQLite hash database is opened, DC++ copies only valid file rows into the version 2 table and drops orphaned rows whose tree is missing.

## Migration

On startup, DC++ opens `HashStore.sqlite3` and creates the schema if needed. If the SQLite database is empty and legacy migration has not already completed, it loads the legacy `HashIndex.xml` and `HashData.dat` files, verifies each tree, drops orphaned file records and writes the valid entries into SQLite inside one transaction.

Successful migration writes a `legacy_migration_complete` marker into the metadata table. Existing SQLite databases from earlier builds are also marked complete after they load successfully so stale legacy files cannot be imported over newer SQLite data on a later startup.

After a successful legacy import, `HashIndex.xml` and `HashData.dat` are renamed to `.migrated` files. The same cleanup is also applied when an existing SQLite database is accepted as authoritative or when the migration-complete marker already exists but unrenamed legacy files are still present. The old files are not deleted, which keeps manual recovery possible, but the rename prevents repeated migration work and avoids accidentally treating stale legacy data as active. If migration fails because the old files are corrupt or incomplete, DC++ leaves them in place and logs the failure.

## Rebuild

The `/rebuild` command prunes file records that no longer match the filesystem and rewrites the valid tree/file rows into SQLite inside one transaction. The command preserves the existing public behavior: it can still take time with large shares and it still runs synchronously from the command path.

The Sharing settings page exposes hash database maintenance controls:

* Write batch size controls how many SQLite row changes are grouped into one transaction while files are being hashed. Larger values reduce write overhead but leave more recently hashed files to be rehashed if the process exits before the batch is committed.
* Verify hash database on startup runs SQLite `quick_check` when the database is opened.
* Compact hash database after rebuild runs the compact action after `/rebuild` finishes.
* Verify, Full check, Optimize and Compact buttons run the corresponding maintenance action immediately.

## Hardening

The SQLite wrapper applies conservative defaults when opening the database:

* Foreign SQL extensions are not enabled.
* `trusted_schema` is disabled when supported by the bundled SQLite version.
* Journaling uses WAL with normal sync for a balance of integrity and write cost.
* SQLite limits are reduced for SQL length, expression depth, compound selects, attached databases and other features the hash store does not need.
* Hash writes use cached prepared statements and bounded transactions.
* `PRAGMA optimize` is run during periodic saves and manual optimization.

The bundled SQLite amalgamation is built from the `sqlite/` folder and is linked into both the core library and the Windows client.

## Logging

HashStore writes warnings and errors to the system log when it cannot safely use stored hash data. Logged cases include:

* SQLite open, load, save, checkpoint and migration failures.
* Invalid tree metadata, missing leaf data, malformed leaf blobs and root mismatches.
* Invalid file records and file rows whose root has no matching tree.
* Legacy migration entries that are skipped because the old data cannot be verified.
* Legacy hash database files that are renamed after successful migration or cannot be renamed.
* Failures while removing stale hash database entries after a file changed.
* Failed integrity checks and failed maintenance actions.

When a record is skipped, DC++ leaves sharing/search behavior intact by treating the affected hash as unavailable. If the file is still shared or queued, it can be hashed again normally.
