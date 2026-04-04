# Buffer Pool and Block File Storage

## Purpose

Provide fixed-size block persistence (`BlockFile`) and in-memory block caching/pinning (`BufferManager`) for all segment and catalog block I/O.

## Scope

**In scope:**
- Block file open/read/write/allocate/sync behavior.
- Buffer pool frame lifecycle, LRU eviction, pin/unpin, dirty flush.
- RAII handle semantics via `BufferHandle`.

**Out of scope:**
- Segment compression/encoding.
- WAL file management.

## Primary User Flow

1. Persistent mode creates `BlockFile(path, block_size)`.
2. `BufferManager` pins blocks (`Pin(id)`) for readers/writers.
3. Writers mark modified pages dirty (`BufferHandle::MarkDirty`).
4. `BufferManager::Flush()` writes dirty frames to `BlockFile` and calls `Sync()`.

## System Flow

1. `Pin(id)` returns existing frame if cached; otherwise ensures space and loads block from file (or zeros in memory mode).
2. `Unpin(id)` decrements `pin_count`; zero-pin frames are inserted into front of LRU list.
3. `EnsureSpace` repeatedly evicts LRU tail frames via `EvictOne` when pool is full.
4. `AllocateBlock` increments internal block id counter, optionally extends file with zero block, creates pinned dirty frame.
5. `Flush` writes all dirty frames and calls `file_->Sync()`.

```
Pin(id)
  -> cache hit: pin_count++
  -> cache miss: evict if needed -> read block -> pin_count=1
Unpin(id)
  -> pin_count-- -> if 0, add to LRU
Flush
  -> write dirty frames -> fsync
```

## Data Model

- `BlockFile` (`src/storage/block_file.hpp`):
- `fd_ (int)`
- `block_size_ (size_t)`
- `block_count_ (size_t)`
- `Frame` (`src/storage/buffer_manager.hpp`):
- `buffer (unique_ptr<uint8_t[]>)`
- `pin_count (int32_t)`
- `dirty (bool)`
- `lru_it`, `in_lru`
- `BufferManager` state:
- pool sizing: `pool_bytes_`, `block_size_`, `used_bytes_`
- caches: `pool_ (map<BlockId, Frame>)`, `lru_list_`
- allocation counter: `next_block_id_`
- `BufferHandle` fields: `bm_`, `block_id_`, `data_`

## Interfaces and Contracts

- `BlockFile::ReadBlock/WriteBlock(BlockId, uint8_t*)`
- contract: fixed-size full block I/O; `ReadBlock` throws on invalid `id`.
- `BlockFile::AllocateBlock()`
- contract: appends one zero-filled block and returns its id.
- `BufferManager::Pin(BlockId)`
- contract: returned handle must be released (RAII) to unpin.
- `BufferManager::AllocateBlock()`
- contract: returns pinned dirty zero block.
- `BufferManager::Flush()`
- contract: writes dirty frames to backing file when not in-memory mode.

## Dependencies

**Internal modules:**
- `src/storage/block_file.*` and `src/common/exception.hpp`.

**External services/libraries:**
- POSIX file APIs: `open`, `pread`, `pwrite`, `fstat`, `fsync`, `close`.

## Failure Modes and Edge Cases

- `EvictOne` throws `RuntimeError` when all frames are pinned and eviction is required.
- `ReadBlock` throws `IOError` for out-of-range block ids.
- `AllocateBlock` marks new frame dirty immediately; caller should keep/flush it.
- In-memory mode (`file_ == nullptr`) still allocates blocks in pool but skips disk I/O.

## Observability and Debugging

- No built-in metrics/counters for cache hit rate or eviction count.
- Debug entry points:
- block operations: `src/storage/block_file.cpp`
- cache lifecycle: `src/storage/buffer_manager.cpp`

## Risks and Notes

- `BufferManager::next_block_id_` is not initialized from existing `BlockFile::BlockCount()`, which risks block id reuse after reopen.
- `BlockFile::Sync()` ignores `fsync` return status; sync failures are currently silent.

Changes:

