// Placeholder stub for cppcoldb::engine::execution::Pipeline / PipelineExecutor.
// Also exercises compilation of FakeFileSystem, FakeClock, and FakeLogger
// (the cross-cutting io-port fakes, homed here since pipeline execution is
// where they would first be wired in). No assertions yet.
#include "cppcoldb/engine/execution/pipeline.hpp"
#include "cppcoldb/engine/execution/pipeline_executor.hpp"

#include "helpers/fake_clock.hpp"
#include "helpers/fake_file_system.hpp"
#include "helpers/fake_logger.hpp"

namespace cppcoldb::test {

void PlaceholderTestPipeline() {
    // TODO: add assertions once PipelineExecutor drives real operators end to end.
}

} // namespace cppcoldb::test
