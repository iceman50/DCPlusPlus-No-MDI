<!-- Copyright (C) 2026 iceman50 -->

# Local filelist cache and on-demand browsing

## Source files

The cache supports XML, bzip2, and Zstandard input lists, independently of
[ZST1 transfer negotiation](zstd-filelists.md). Negotiated Zstandard full-list
downloads retain their original validated frame as `.xml.zst`; the optional
local cache stores indexed copies of the entries. Locally supplied single-frame
`.xml.zst` lists can also be opened.

## Cache and browsing behavior

Under **Experimental > Transfers and hashing**,
**Cache file lists on disk and load file entries on demand** (`FilelistCache`)
defaults to **off**, independently of transfer compression. It applies to
XML, bzip2, and Zstandard input lists. The first open parses the source in a
stream and builds a SQLite sidecar named `<source>.dcfl`. Directory metadata
stays in memory. File entries are serialized into approximately 512 KiB XML
chunks and stored as independent checksummed Zstandard frames, indexed by
directory. This is an application index, not the optional Zstandard seekable
archive format.

Reopening reads directory metadata from the index. Selecting a directory loads
its file entries; leaving it releases those entries and their UI display cache.
Totals do not need to load file entries. Find walks directories on demand.
ADL matching, queue matching, saving, and recursive downloads visit unloaded
entries too. Changed directories are detached from the immutable cache so later
release/reload cannot lose edits. ADL matching still requires a scan when active
rules exist; matches remain in memory. Queue matching still builds its TTH map.

The source remains authoritative and is retained. Size, high-resolution write
time, source path, cache schema version, and the parser size-limit setting govern
cache reuse. A source change during indexing aborts the index. A transaction and
temporary database prevent publishing an unfinished cache. If an existing cache
cannot be replaced because another window holds it, that session uses its
temporary index. Unusable indexes are rebuilt; if cache creation fails (for
example, a read-only folder), the ordinary loader is used. Corruption encountered
while browsing is reported without exposing a partly loaded directory.

This reduces memory from all file entries to visited/current directory entries,
plus the directory tree. One directory containing millions of files still needs
memory proportional to that directory; this patch does not add row pagination.
The source and its sidecar use additional disk space. Sidecars are disposable and
may be deleted when their lists are closed. Changing this option affects newly
opened lists.

## Build and validation

The bundled Zstandard 1.5.7 library builds statically through SCons on the same
toolchains as the rest of the application; no extra runtime DLL is needed.

`testfilelistcache.cpp` covers multiple chunks, reopening, metadata, eviction,
partial updates, stale/damaged indexes, duplicate entries, and local Zstd lists.
Run these with the existing directory-listing, zlib, ADC, NMDC, queue, share, and
connection tests, plus the Zstandard transfer tests described in the protocol
document.

The combined transfer and cache patch was validated on Windows with MinGW x64
release:

```text
scons tools=mingw arch=x64 mode=release -j8 test build/release-mingw-x64/bin/DCPlusPlus.exe
```

Result: application build succeeded; all 186 enabled tests passed. The eight
existing disabled benchmarks remained disabled. Use the SCons `test` target so
its debug companion is regenerated before the DWARF regression test runs.
