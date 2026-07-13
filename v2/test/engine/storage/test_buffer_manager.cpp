// Placeholder stub for cppcoldb::engine::storage::BufferManager.
// Also exercises compilation of FakeBlockManager, FakeBufferManager, and
// FakeBufferHandle (the storage-domain fakes for IBlockManager,
// IBufferManager, and IBufferHandle). No assertions yet.
#include "cppcoldb/engine/storage/buffer_manager.hpp"

#include "helpers/fake_block_manager.hpp"
#include "helpers/fake_buffer_handle.hpp"
#include "helpers/fake_buffer_manager.hpp"

namespace cppcoldb::test {

void PlaceholderTestBufferManager() {
    // TODO: add assertions once BufferManager pin/evict semantics are implemented.
}

} // namespace cppcoldb::test
