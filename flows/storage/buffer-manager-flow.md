# Buffer Manager Flow

## Assumptions
- The BufferManager manages a fixed-size pool of in-memory pages (blocks).
- All disk I/O goes through the BufferManager; no operator accesses BlockFile directly.
- Blocks are identified by block_id. A pin keeps a block in memory; an unpin makes it evictable.
- When memory is full, unpinned blocks are evicted using an LRU policy.

## Diagram

```mermaid
flowchart TD
    A["Operator requests block data"] --> B["BufferManager::Pin(block_id)"]
    B --> C{"Block already\nin memory?"}
    C -- Yes --> D["Increment pin count\nReturn buffer pointer"]
    C -- No --> E["Need to load block from disk"]

    E --> F{"Enough memory\navailable in pool?"}
    F -- Yes --> G["Allocate buffer\nfrom memory pool"]
    F -- No --> H["EvictBlocks():\nfind unpinned blocks (LRU order)"]

    H --> I{"Found evictable\nblock?"}
    I -- Yes --> J{"Block is\ndirty?"}
    J -- Yes --> K["Write block to BlockFile\n(flush dirty page)"]
    K --> L["Mark block as clean\nFree buffer slot"]
    J -- No --> L
    L --> M{"Enough memory\nfreed?"}
    M -- Yes --> G
    M -- No --> I
    I -- No --> N["Throw OutOfMemoryError:\nall blocks pinned"]

    G --> O["BlockFile::ReadBlock(block_id, buffer)"]
    O --> P["Block loaded into memory"]
    P --> D

    D --> Q["Caller uses block data"]
    Q --> R["BufferManager::Unpin(block_id)"]
    R --> S["Decrement pin count"]
    S --> T{"Pin count\n== 0?"}
    T -- Yes --> U["Block added to\nLRU eviction list"]
    T -- No --> V["Block stays pinned"]

    subgraph DirtyWriteBack["Dirty Block Write-Back"]
        DW1["BufferManager::MarkDirty(block_id)"]
        DW1 --> DW2["Block flagged dirty:\nmust be written before eviction"]
        DW2 --> DW3["On eviction or checkpoint:\nBlockFile::WriteBlock(block_id, buffer)"]
    end
```

## Planned Implementation
- `src/storage/buffer_manager.cpp` — BufferManager, Pin(), Unpin(), EvictBlocks()
- `src/storage/block_manager.cpp` — BlockManager, BlockFile (disk I/O)
- `src/storage/buffer/buffer_pool.cpp` — memory pool, LRU eviction tracking
