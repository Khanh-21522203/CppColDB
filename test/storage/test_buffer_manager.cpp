#include "storage/buffer_manager.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <cstring>
#include <thread>

using namespace cppcoldb;

static constexpr size_t BS = 4096; // block size for tests

static void TestInMemoryAllocate() {
    BufferManager bm(BS * 10, BS, nullptr);
    auto h = bm.AllocateBlock();
    assert(h.Valid());
    assert(h.Id() == 0);
    // Zero-filled.
    for (size_t i = 0; i < BS; ++i) assert(h.Data()[i] == 0);
    std::cout << "TestInMemoryAllocate: PASS\n";
}

static void TestInMemoryPinAfterAllocate() {
    BufferManager bm(BS * 10, BS, nullptr);
    auto h1 = bm.AllocateBlock();
    h1.Data()[0] = 42;
    BlockId id = h1.Id();
    h1 = BufferHandle{}; // unpin (move-assign empty)

    // Re-pin the same block.
    auto h2 = bm.Pin(id);
    assert(h2.Data()[0] == 42);
    std::cout << "TestInMemoryPinAfterAllocate: PASS\n";
}

static void TestMarkDirtyDoesNotCrash() {
    // In-memory mode: MarkDirty is a no-op from persistence standpoint.
    BufferManager bm(BS * 10, BS, nullptr);
    auto h = bm.AllocateBlock();
    bm.MarkDirty(h.Id());
    bm.Flush(); // no-op for in-memory
    std::cout << "TestMarkDirtyDoesNotCrash: PASS\n";
}

static void TestLRUEviction() {
    // Pool holds exactly 3 blocks; allocating a 4th should evict LRU.
    BufferManager bm(BS * 3, BS, nullptr);

    auto h0 = bm.AllocateBlock(); h0.Data()[0] = 0; BlockId id0 = h0.Id();
    auto h1 = bm.AllocateBlock(); h1.Data()[0] = 1; BlockId id1 = h1.Id();
    auto h2 = bm.AllocateBlock(); h2.Data()[0] = 2;

    // Unpin in order: h0 (LRU), h1, h2 (MRU).
    h0 = BufferHandle{};
    h1 = BufferHandle{};
    h2 = BufferHandle{};

    // Allocate a 4th block → evicts h0 (LRU).
    auto h3 = bm.AllocateBlock();
    (void)h3;

    // h1 and h2 should still be in pool; h0 evicted.
    // In in-memory mode eviction just drops the frame.
    // Re-pinning an evicted in-memory block → zero-fills (data lost).
    auto h0_repinned = bm.Pin(id0);
    // After eviction in in-memory mode the data is lost; just verify no crash.
    assert(h0_repinned.Valid());
    std::cout << "TestLRUEviction: PASS\n";
}

static void TestAllPinnedOutOfMemory() {
    // Pool holds 2 blocks; pin 2, then try a 3rd → RuntimeError.
    BufferManager bm(BS * 2, BS, nullptr);
    auto h0 = bm.AllocateBlock();
    auto h1 = bm.AllocateBlock();

    bool threw = false;
    try {
        auto h2 = bm.AllocateBlock();
        (void)h2;
    } catch (const RuntimeError&) {
        threw = true;
    }
    assert(threw);
    std::cout << "TestAllPinnedOutOfMemory: PASS\n";
}

static void TestConcurrentPin() {
    BufferManager bm(BS * 20, BS, nullptr);
    // Pre-allocate two blocks.
    auto ha = bm.AllocateBlock(); BlockId idA = ha.Id(); ha = BufferHandle{};
    auto hb = bm.AllocateBlock(); BlockId idB = hb.Id(); hb = BufferHandle{};

    std::thread t1([&]() {
        auto h = bm.Pin(idA);
        h.Data()[0] = 0xAA;
    });
    std::thread t2([&]() {
        auto h = bm.Pin(idB);
        h.Data()[0] = 0xBB;
    });
    t1.join();
    t2.join();

    auto hA = bm.Pin(idA);
    auto hB = bm.Pin(idB);
    assert(hA.Data()[0] == 0xAA);
    assert(hB.Data()[0] == 0xBB);
    std::cout << "TestConcurrentPin: PASS\n";
}

static void TestFlushInMemoryIsNoop() {
    BufferManager bm(BS * 10, BS, nullptr);
    auto h = bm.AllocateBlock();
    h.Data()[0] = 99;
    bm.MarkDirty(h.Id());
    bm.Flush(); // must not throw
    std::cout << "TestFlushInMemoryIsNoop: PASS\n";
}

void RunBufferManagerTests() {
    TestInMemoryAllocate();
    TestInMemoryPinAfterAllocate();
    TestMarkDirtyDoesNotCrash();
    TestLRUEviction();
    TestAllPinnedOutOfMemory();
    TestConcurrentPin();
    TestFlushInMemoryIsNoop();
    std::cout << "\nAll buffer_manager tests passed.\n";
}
