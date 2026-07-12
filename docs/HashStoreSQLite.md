# HashStore SQLite Backend

DC++ stores the local TTH hash database in `HashStore.sqlite3` in the user configuration folder. SQLite may also create `HashStore.sqlite3-wal` and `HashStore.sqlite3-shm`; those sidecar files are expected when write-ahead logging is active.

The runtime behavior remains the same as the previous `HashIndex.xml` and `HashData.dat` backend. `HashManager::HashStore` still keeps the in-memory file and tree indexes used by sharing, searching, queue matching and TTH lookups. SQLite is only the durable storage layer for those records.

## Schema

The database contains two `WITHOUT ROWID` tables:

* `trees`: one row per TTH root, including file size, block size and the serialized leaf hashes for multi-leaf trees.
* `files`: one row per hashed file path, including file size, timestamp and the root that points back to `trees`.

Single-leaf trees store a NULL leaf blob because the root is enough to recreate the tree.

## Migration

On startup, DC++ opens `HashStore.sqlite3` and creates the schema if needed. If the SQLite database is empty, it loads the legacy `HashIndex.xml` and `HashData.dat` files, verifies each tree, drops orphaned file records and writes the valid entries into SQLite inside one transaction.

The legacy files are not deleted by migration. Keeping them makes downgrade or manual recovery easier, but new hash updates are written only to SQLite.

## Rebuild

The `/rebuild` command prunes file records that no longer match the filesystem and rewrites the valid tree/file rows into SQLite inside one transaction. The command preserves the existing public behavior: it can still take time with large shares and it still runs synchronously from the command path.

## Hardening

The SQLite wrapper applies conservative defaults when opening the database:

* Foreign SQL extensions are not enabled.
* `trusted_schema` is disabled when supported by the bundled SQLite version.
* Journaling uses WAL with normal sync for a balance of integrity and write cost.
* SQLite limits are reduced for SQL length, expression depth, compound selects, attached databases and other features the hash store does not need.

The bundled SQLite amalgamation is built from the `sqlite/` folder and is linked into both the core library and the Windows client.

## Logging

HashStore writes warnings and errors to the system log when it cannot safely use stored hash data. Logged cases include:

* SQLite open, load, save, checkpoint and migration failures.
* Invalid tree metadata, missing leaf data, malformed leaf blobs and root mismatches.
* Invalid file records and file rows whose root has no matching tree.
* Legacy migration entries that are skipped because the old data cannot be verified.
* Failures while removing stale hash database entries after a file changed.

When a record is skipped, DC++ leaves sharing/search behavior intact by treating the affected hash as unavailable. If the file is still shared or queued, it can be hashed again normally.
