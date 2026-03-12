# Feature: Buffer Manager

## 1. Purpose

The Buffer Manager is the central memory controller for all storage I/O. Every read from or write to the on-disk `BlockFile` goes through the Buffer Manager. It maintains a fixed-size pool of in-memory buffers (pages/blocks), tracks which blocks are pinned (in use by operators), and evicts unpinned blocks using an LRU policy when the pool is full. Operators never touch the `BlockFile` directly; they only hold pinned buffers returned by `BufferManager::Pin()`.

## 2. Responsibilities

- Maintain a fixed-size memory pool of `block_size`-byte buffers (default 256 KiB per block)
- `Pin(block_id)`: load the block into a pool buffer if not already present; increment its pin count; return a pointer to the buffer
- `Unpin(block_id)`: decrement the pin count; when it reaches zero, make the block evictable (add to LRU queue)
- `MarkDirty(block_id)`: flag a block as modified; it must be written to the `BlockFile` before eviction or shutdown
- Evict LRU-ordered unpinned blocks to free pool space when a new block must be loaded
- `AllocateBlock()`: assign a new `BlockId`, allocate a pool buffer, return it pinned and dirty
- `Flush()`: write all dirty blocks to the `BlockFile` (called during checkpoint)
- `Shutdown()`: write all dirty blocks and release all pool memory
- In in-memory mode: skip all `BlockFile` I/O; blocks live only in pool memory

## 3. Non-Responsibilities

- Does not interpret block contents (column data, metadata, etc.)
- Does not manage the WAL (that is `WAL`'s responsibility)
- Does not assign compression or decompression (that is `ColumnChunk`'s job)
- Does not decide which blocks to read (callers provide `BlockId`)
- Does not manage the transaction log

## 4. Architecture Design

```
Operators (ColumnChunk::Scan, ColumnChunk::Flush, ...)
    |
    | Pin(block_id) / Unpin(block_id) / MarkDirty(block_id)
    v
+------------------------------------------+
|             BufferManager                |
|                                          |
|  pool_: unordered_map<BlockId, Frame>    |
|  lru_:  list<BlockId>  (unpinned, LRU)  |
|  total_pool_bytes_ / used_pool_bytes_    |
+------------------------------------------+
    |
    | ReadBlock / WriteBlock
    v
+------------------------------------------+
|              BlockFile                   |
|  (single file on disk, block-addressable)|
|  block_size: 256 KiB                     |
|  offset = block_id * block_size          |
+------------------------------------------+
```

**Frame**: one slot in the pool holding a buffer pointer, pin count, dirty flag, and LRU iterator.

**LRU list**: `std::list<BlockId>` where the front is the most-recently-used unpinned block and the back is the least-recently-used. When a block's pin count drops to zero it is pushed to the front. When evicting, the back is removed.

## 5. Core Data Structures (C++)

```cpp
// src/storage/buffer_manager.hpp
#pragma once
#include <unordered_map>
#include <list>
#include <vector>
#include <memory>
#include <mutex>
#include <cstdint>
#include "common/types.hpp"

namespace cppcoldb {

static constexpr size_t DEFAULT_BLOCK_SIZE       = 256 * 1024; // 256 KiB
static constexpr size_t DEFAULT_BUFFER_POOL_SIZE = 256ULL * 1024 * 1024; // 256 MiB

// A block buffer handle returned by Pin().
// Automatically unpins when destroyed (RAII).
class BufferHandle {
public:
    BufferHandle() = default;
    BufferHandle(class BufferManager* bm, BlockId id, uint8_t* data);
    ~BufferHandle();

    BufferHandle(BufferHandle&&) noexcept;
    BufferHandle& operator=(BufferHandle&&) noexcept;

    uint8_t* Data() const { return data_; }
    BlockId  Id()   const { return block_id_; }
    bool     Valid()const { return data_ != nullptr; }

private:
    BufferManager* bm_       = nullptr;
    BlockId        block_id_ = INVALID_BLOCK;
    uint8_t*       data_     = nullptr;
};

// One slot in the buffer pool.
struct Frame {
    std::unique_ptr<uint8_t[]> buffer;     // heap-allocated block data
    int32_t                    pin_count;  // number of active pins
    bool                       dirty;      // true if modified since last flush
    // Iterator into lru_list_ for O(1) removal when re-pinned.
    std::list<BlockId>::iterator lru_it;
    bool                       in_lru;    // true if currently in lru_list_
};

class BlockFile;

class BufferManager {
public:
    explicit BufferManager(size_t pool_bytes = DEFAULT_BUFFER_POOL_SIZE,
                           size_t block_size = DEFAULT_BLOCK_SIZE,
                           BlockFile* file   = nullptr); // nullptr = in-memory
    ~BufferManager();

    // Pin a block; load from disk if not in pool.
    // Returns a RAII handle; unpins automatically on destruction.
    BufferHandle Pin(BlockId id);

    // Mark a pinned block as modified (will be written before eviction).
    void MarkDirty(BlockId id);

    // Allocate a new block (assigns a new BlockId, pins, marks dirty).
    BufferHandle AllocateBlock();

    // Write all dirty blocks to disk.
    void Flush();

    // Shutdown: flush all dirty blocks, release pool memory.
    void Shutdown();

    size_t BlockSize()  const { return block_size_; }
    bool   IsInMemory() const { return file_ == nullptr; }

private:
    void Unpin(BlockId id);  // called by BufferHandle destructor
    friend class BufferHandle;

    void       EvictBlocks(size_t needed_bytes);
    void       EvictOne();
    BlockId    NextBlockId();

    size_t                                  pool_bytes_;
    size_t                                  block_size_;
    size_t                                  used_bytes_  = 0;
    BlockFile*                              file_;

    std::unordered_map<BlockId, Frame>      pool_;
    std::list<BlockId>                      lru_list_;   // back = LRU candidate
    BlockId                                 next_block_id_ = 0;

    std::mutex                              mu_; // guards pool_, lru_list_, used_bytes_
};

} // namespace cppcoldb
```

```cpp
// src/storage/block_file.hpp
#pragma once
#include <string>
#include <cstdint>
#include "common/types.hpp"

namespace cppcoldb {

// Manages the single on-disk file that stores all column data blocks.
class BlockFile {
public:
    explicit BlockFile(const std::string& path, size_t block_size);
    ~BlockFile();

    void ReadBlock(BlockId id, uint8_t* buffer);
    void WriteBlock(BlockId id, const uint8_t* buffer);
    void Sync();  // fsync

    BlockId AllocateBlock();  // extends the file; returns the new block ID
    size_t  BlockCount() const { return block_count_; }

private:
    int    fd_;
    size_t block_size_;
    size_t block_count_;
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

class BufferManager {
public:
    // Pin a block into pool memory; throws IOError if disk read fails.
    BufferHandle Pin(BlockId id);

    // Mark block as dirty (must be called while block is pinned).
    void MarkDirty(BlockId id);

    // Allocate a brand-new block; returns pinned and dirty handle.
    BufferHandle AllocateBlock();

    // Write all dirty unpinned blocks to disk.
    void Flush();

    // Flush and release all memory. Called at shutdown.
    void Shutdown();

    size_t BlockSize()  const;
    bool   IsInMemory() const;
};

// BufferHandle (RAII pin)
class BufferHandle {
public:
    uint8_t* Data()  const;  // pointer to block's raw bytes
    BlockId  Id()    const;
    bool     Valid() const;
    void     MarkDirty();    // convenience: calls bm_->MarkDirty(id_)
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### Pin
```
Pin(block_id):
  lock(mu_)
  if pool_.contains(block_id):
    frame = pool_[block_id]
    if frame.pin_count == 0:
      // Remove from LRU list (O(1) with iterator)
      lru_list_.erase(frame.lru_it)
      frame.in_lru = false
    frame.pin_count++
    return BufferHandle(this, block_id, frame.buffer.get())

  // Block not in pool; need to load
  EvictBlocks(block_size_)  // make room if needed

  frame = Frame{buffer=new uint8_t[block_size_], pin_count=1, dirty=false}
  if file_ != nullptr:
    file_.ReadBlock(block_id, frame.buffer.get())  // throws IOError on failure
  else:
    memset(frame.buffer.get(), 0, block_size_)     // in-memory: zero-initialize

  pool_[block_id] = move(frame)
  used_bytes_ += block_size_
  return BufferHandle(this, block_id, pool_[block_id].buffer.get())
  Time: O(1) if cached; O(block_size) I/O if loading
```

### Unpin (called by BufferHandle destructor)
```
Unpin(block_id):
  lock(mu_)
  frame = pool_[block_id]
  frame.pin_count--
  if frame.pin_count == 0:
    // Add to front of LRU list (most recently used)
    lru_list_.push_front(block_id)
    frame.lru_it = lru_list_.begin()
    frame.in_lru = true
  Time: O(1)
```

### EvictBlocks
```
EvictBlocks(needed_bytes):
  while used_bytes_ + needed_bytes > pool_bytes_:
    if lru_list_.empty():
      throw RuntimeError("BufferManager: all blocks pinned, out of memory")
    EvictOne()

EvictOne():
  victim_id = lru_list_.back()
  lru_list_.pop_back()
  frame = pool_[victim_id]
  if frame.dirty AND file_ != nullptr:
    file_.WriteBlock(victim_id, frame.buffer.get())
  pool_.erase(victim_id)
  used_bytes_ -= block_size_
  Time: O(1) if block is clean; O(block_size) if dirty write-back
```

### AllocateBlock
```
AllocateBlock():
  lock(mu_)
  id = next_block_id_++
  if file_ != nullptr:
    file_.AllocateBlock()  // extend file to cover new block id
  EvictBlocks(block_size_)
  frame = Frame{buffer=new uint8_t[block_size_], pin_count=1, dirty=true}
  memset(frame.buffer.get(), 0, block_size_)
  pool_[id] = move(frame)
  used_bytes_ += block_size_
  return BufferHandle(this, id, pool_[id].buffer.get())
  Time: O(1) amortized
```

### Flush
```
Flush():
  lock(mu_)
  for (id, frame) in pool_:
    if frame.dirty AND file_ != nullptr:
      file_.WriteBlock(id, frame.buffer.get())
      frame.dirty = false
  file_.Sync()
  Time: O(dirty_blocks * block_size)
```

## 8. Persistence Model

The `BufferManager` itself is not persisted. Dirty blocks are written to `BlockFile` during eviction, `Flush()`, and `Shutdown()`. The `BlockFile` is a single file with fixed-size blocks addressed by `BlockId`:

```
BlockFile layout:
  Byte offset of block N: N * block_size
  Example: block_size = 256 KiB
    block 0: bytes [0, 262144)
    block 1: bytes [262144, 524288)
    ...
```

## 9. Concurrency Model

| Object | Lock | Usage |
|--------|------|-------|
| `BufferManager.mu_` | `std::mutex` | Guards all pool access: Pin, Unpin, AllocateBlock, Flush |
| `BlockFile` | None (single writer) | Called only while BufferManager lock is held |
| `Frame.buffer` | Caller-responsibility | Callers must not mutate buffer after Unpin |

**Deadlock avoidance**: `mu_` is never held while calling into other subsystems. The `BufferHandle` destructor acquires `mu_` briefly to decrement pin count; callers must not hold `mu_` when destroying a `BufferHandle`.

## 10. Configuration

```cpp
struct BufferManagerConfig {
    size_t pool_bytes  = 256ULL * 1024 * 1024; // 256 MiB default
    size_t block_size  = 256 * 1024;           // 256 KiB per block
    // nullptr = in-memory mode (no BlockFile)
    std::string db_path = "";
};
```

## 11. Testing Strategy

- `TestPinLoadsBlock`: pin a block that isn't in pool → block loaded from file, handle valid
- `TestPinCached`: pin same block twice → second pin does not trigger disk read
- `TestUnpinDecrementsCount`: pin, unpin → frame.pin_count == 0, block in LRU
- `TestMarkDirty`: pin, mark dirty, unpin → eviction writes block to file
- `TestLRUEviction`: fill pool to capacity with 3 blocks, pin a 4th → LRU block evicted
- `TestLRUEvictionOrder`: pin A, pin B, unpin B, unpin A, fill pool → A is LRU victim (unpinned last but used less recently)
- `TestAllocateBlock`: allocate new block → valid handle, zero-initialized
- `TestAllPinnedOutOfMemory`: pin every block in pool, request one more → throws RuntimeError
- `TestFlushWritesDirtyBlocks`: mark dirty, call Flush() → verify file content matches buffer
- `TestShutdownFlushes`: dirty block in pool at shutdown → flushed to disk
- `TestInMemoryMode`: no BlockFile → Pin zero-initializes, Flush is no-op, no I/O errors
- `TestConcurrentPin`: two threads pin different blocks simultaneously → no data race (guards by mu_)

## 12. Open Questions

None.
