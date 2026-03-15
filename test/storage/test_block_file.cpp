#include "storage/block_file.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <cstring>
#include <vector>
#include <unistd.h>

using namespace cppcoldb;

static constexpr size_t BLOCK_SIZE = 4096; // small blocks for tests

static std::string TempPath() {
    char tmpl[] = "/tmp/cppcoldb_test_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd >= 0) close(fd);
    return std::string(tmpl);
}

static void TestBlockFileAllocateAndWrite() {
    auto path = TempPath();
    {
        BlockFile bf(path, BLOCK_SIZE);
        assert(bf.BlockCount() == 0);

        BlockId id = bf.AllocateBlock();
        assert(id == 0);
        assert(bf.BlockCount() == 1);

        std::vector<uint8_t> data(BLOCK_SIZE, 0xAB);
        bf.WriteBlock(id, data.data());
        bf.Sync();
    }
    // Re-open and read back.
    {
        BlockFile bf(path, BLOCK_SIZE);
        assert(bf.BlockCount() == 1);

        std::vector<uint8_t> buf(BLOCK_SIZE, 0);
        bf.ReadBlock(0, buf.data());
        for (auto b : buf) assert(b == 0xAB);
    }
    unlink(path.c_str());
    std::cout << "TestBlockFileAllocateAndWrite: PASS\n";
}

static void TestBlockFileMultipleBlocks() {
    auto path = TempPath();
    {
        BlockFile bf(path, BLOCK_SIZE);
        for (int i = 0; i < 3; ++i) {
            BlockId id = bf.AllocateBlock();
            assert(id == static_cast<BlockId>(i));
            std::vector<uint8_t> data(BLOCK_SIZE, static_cast<uint8_t>(i + 1));
            bf.WriteBlock(id, data.data());
        }
    }
    {
        BlockFile bf(path, BLOCK_SIZE);
        assert(bf.BlockCount() == 3);
        for (int i = 0; i < 3; ++i) {
            std::vector<uint8_t> buf(BLOCK_SIZE, 0);
            bf.ReadBlock(static_cast<BlockId>(i), buf.data());
            for (auto b : buf) assert(b == static_cast<uint8_t>(i + 1));
        }
    }
    unlink(path.c_str());
    std::cout << "TestBlockFileMultipleBlocks: PASS\n";
}

static void TestBlockFilePersistsAcrossReopens() {
    auto path = TempPath();
    {
        BlockFile bf(path, BLOCK_SIZE);
        bf.AllocateBlock();
        std::vector<uint8_t> d(BLOCK_SIZE, 0x55);
        bf.WriteBlock(0, d.data());
    }
    {
        BlockFile bf(path, BLOCK_SIZE);
        std::vector<uint8_t> buf(BLOCK_SIZE);
        bf.ReadBlock(0, buf.data());
        for (auto b : buf) assert(b == 0x55);
    }
    unlink(path.c_str());
    std::cout << "TestBlockFilePersistsAcrossReopens: PASS\n";
}

void RunBlockFileTests() {
    TestBlockFileAllocateAndWrite();
    TestBlockFileMultipleBlocks();
    TestBlockFilePersistsAcrossReopens();
    std::cout << "\nAll block_file tests passed.\n";
}
