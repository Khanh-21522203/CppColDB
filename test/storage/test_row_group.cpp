#include "storage/column/row_group.hpp"
#include "storage/column/version_info.hpp"
#include "transaction/transaction.hpp"
#include "storage/buffer_manager.hpp"
#include "common/types.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace cppcoldb;

// ---- helpers ---------------------------------------------------------------

static Transaction MakeTx(TransactionId id, timestamp_t start_time) {
    Transaction t;
    t.tx_id      = id;
    t.start_time = start_time;
    return t;
}

// Build a DataChunk with a single INT64 column from values.
static DataChunk MakeSingleColChunk(const std::vector<int64_t>& vals) {
    DataChunk chunk;
    chunk.Initialize({TypeId::INT64});
    DataVector& dv = chunk.columns[0];
    for (size_t i = 0; i < vals.size(); ++i) {
        dv.int_data.push_back(vals[i]);
        dv.validity.set(dv.count);
        dv.count++;
    }
    chunk.count = vals.size();
    return chunk;
}

// ---- VersionInfo tests -----------------------------------------------------

static void TestVersionInfoUnmarkedVisible() {
    VersionInfo vi;
    Transaction tx = MakeTx(1, 100);
    assert(vi.IsVisible(42, tx));
    std::cout << "TestVersionInfoUnmarkedVisible: PASS\n";
}

static void TestVersionInfoMarkDeleteOwner() {
    VersionInfo vi;
    Transaction tx = MakeTx(5, 100);
    vi.MarkDeleted(10, 5);
    assert(!vi.IsVisible(10, tx)); // own delete → not visible
    std::cout << "TestVersionInfoMarkDeleteOwner: PASS\n";
}

static void TestVersionInfoMarkDeleteUncommittedOther() {
    VersionInfo vi;
    Transaction deleter = MakeTx(2, 50);
    Transaction reader  = MakeTx(3, 60);
    vi.MarkDeleted(7, deleter.tx_id);
    // Uncommitted delete by another tx → still visible to reader
    assert(vi.IsVisible(7, reader));
    std::cout << "TestVersionInfoMarkDeleteUncommittedOther: PASS\n";
}

static void TestVersionInfoCommitDeleteBeforeStart() {
    VersionInfo vi;
    Transaction reader = MakeTx(3, 200);
    vi.MarkDeleted(7, 2);
    vi.CommitDelete(7, 100); // committed at t=100, reader started at t=200
    assert(!vi.IsVisible(7, reader)); // row deleted before reader started
    std::cout << "TestVersionInfoCommitDeleteBeforeStart: PASS\n";
}

static void TestVersionInfoCommitDeleteAfterStart() {
    VersionInfo vi;
    Transaction reader = MakeTx(3, 50);
    vi.MarkDeleted(7, 2);
    vi.CommitDelete(7, 100); // committed at t=100, reader started at t=50
    assert(vi.IsVisible(7, reader)); // reader didn't see the delete (started earlier)
    std::cout << "TestVersionInfoCommitDeleteAfterStart: PASS\n";
}

static void TestVersionInfoInsertOwnVisible() {
    VersionInfo vi;
    Transaction tx = MakeTx(4, 100);
    vi.MarkInserted(15, tx.tx_id);
    assert(vi.IsVisible(15, tx)); // own insert → visible
    std::cout << "TestVersionInfoInsertOwnVisible: PASS\n";
}

static void TestVersionInfoInsertUncommittedInvisible() {
    VersionInfo vi;
    Transaction inserter = MakeTx(4, 100);
    Transaction reader   = MakeTx(5, 110);
    vi.MarkInserted(15, inserter.tx_id);
    // Uncommitted insert by another → invisible to reader
    assert(!vi.IsVisible(15, reader));
    std::cout << "TestVersionInfoInsertUncommittedInvisible: PASS\n";
}

static void TestVersionInfoInsertCommittedBeforeStart() {
    VersionInfo vi;
    Transaction reader = MakeTx(5, 200);
    vi.MarkInserted(15, 4);
    vi.CommitInsert(15, 100); // committed at t=100, reader started at t=200
    assert(vi.IsVisible(15, reader)); // reader sees the insert
    std::cout << "TestVersionInfoInsertCommittedBeforeStart: PASS\n";
}

static void TestVersionInfoInsertCommittedAfterStart() {
    VersionInfo vi;
    Transaction reader = MakeTx(5, 50);
    vi.MarkInserted(15, 4);
    vi.CommitInsert(15, 100); // committed at t=100, reader started at t=50
    assert(!vi.IsVisible(15, reader)); // reader started before insert committed
    std::cout << "TestVersionInfoInsertCommittedAfterStart: PASS\n";
}

static void TestVersionInfoClearDelete() {
    VersionInfo vi;
    Transaction tx = MakeTx(5, 200);
    vi.MarkDeleted(3, 2);
    vi.CommitDelete(3, 100);
    assert(!vi.IsVisible(3, tx));
    vi.ClearDeleteMarker(3);
    assert(vi.IsVisible(3, tx)); // marker cleared → visible again
    std::cout << "TestVersionInfoClearDelete: PASS\n";
}

// ---- RowGroup tests --------------------------------------------------------

static void TestRowGroupBasicAppendScan() {
    BufferManager bm;
    RowGroup rg(0, {TypeId::INT64}, bm);

    std::vector<size_t> col_ids = {0};
    Transaction tx = MakeTx(INVALID_TRANSACTION, 0);

    // Append 50 rows
    DataChunk chunk = MakeSingleColChunk(
        std::vector<int64_t>(50, 0));
    for (int i = 0; i < 50; ++i) chunk.columns[0].int_data[i] = i * 2;
    rg.Append(chunk, col_ids);

    assert(rg.RowCount() == 50);

    size_t offset = 0;
    DataChunk out;
    size_t n = rg.Scan(offset, col_ids, out, tx);

    assert(n == 50);
    assert(out.count == 50);
    for (int i = 0; i < 50; ++i) {
        assert(out.columns[0].int_data[i] == i * 2);
    }
    std::cout << "TestRowGroupBasicAppendScan: PASS\n";
}

static void TestRowGroupLargeAppendScan() {
    // Append more than STANDARD_VECTOR_SIZE rows; scan in multiple calls.
    BufferManager bm;
    RowGroup rg(0, {TypeId::INT64}, bm);

    std::vector<size_t> col_ids = {0};
    Transaction tx = MakeTx(INVALID_TRANSACTION, 0);
    constexpr size_t TOTAL = 300;

    // Append in batches of 100 (each < STANDARD_VECTOR_SIZE)
    for (int batch = 0; batch < 3; ++batch) {
        std::vector<int64_t> vals;
        for (int i = 0; i < 100; ++i) vals.push_back(batch * 100 + i);
        rg.Append(MakeSingleColChunk(vals), col_ids);
    }

    assert(rg.RowCount() == TOTAL);

    size_t offset = 0;
    size_t total_read = 0;
    while (offset < TOTAL) {
        DataChunk out;
        size_t n = rg.Scan(offset, col_ids, out, tx);
        if (n == 0) break;
        for (size_t i = 0; i < n; ++i) {
            assert(out.columns[0].int_data[i] == (int64_t)(total_read + i));
        }
        total_read += n;
    }
    assert(total_read == TOTAL);
    std::cout << "TestRowGroupLargeAppendScan: PASS\n";
}

static void TestRowGroupScanMVCCDeleteVisible() {
    // Row 1 deleted by txn 2 at t=100; reader starts at t=200 → row 1 invisible.
    BufferManager bm;
    RowGroup rg(0, {TypeId::INT64}, bm);
    std::vector<size_t> col_ids = {0};

    DataChunk chunk = MakeSingleColChunk({10, 20, 30});
    rg.Append(chunk, col_ids);

    // Simulate: row 1 deleted by another committed transaction.
    rg.GetVersionInfo().MarkDeleted(1, 2);
    rg.GetVersionInfo().CommitDelete(1, 100);

    Transaction reader = MakeTx(3, 200);
    size_t offset = 0;
    DataChunk out;
    size_t n = rg.Scan(offset, col_ids, out, reader);

    assert(n == 2); // row 1 filtered out
    assert(out.columns[0].int_data[0] == 10);
    assert(out.columns[0].int_data[1] == 30);
    std::cout << "TestRowGroupScanMVCCDeleteVisible: PASS\n";
}

static void TestRowGroupScanMVCCDeleteInvisible() {
    // Row 1 deleted by txn 2 but not yet committed; reader at t=200 still sees it.
    BufferManager bm;
    RowGroup rg(0, {TypeId::INT64}, bm);
    std::vector<size_t> col_ids = {0};

    rg.Append(MakeSingleColChunk({10, 20, 30}), col_ids);
    rg.GetVersionInfo().MarkDeleted(1, 2); // uncommitted

    Transaction reader = MakeTx(3, 200);
    size_t offset = 0;
    DataChunk out;
    size_t n = rg.Scan(offset, col_ids, out, reader);

    assert(n == 3); // all rows still visible
    std::cout << "TestRowGroupScanMVCCDeleteInvisible: PASS\n";
}

static void TestRowGroupCommitAppend() {
    // Append with tx_id; rows invisible to other until CommitAppend.
    BufferManager bm;
    RowGroup rg(0, {TypeId::INT64}, bm);
    std::vector<size_t> col_ids = {0};

    Transaction writer = MakeTx(1, 50);
    Transaction reader = MakeTx(2, 60); // starts after writer but before commit

    rg.Append(MakeSingleColChunk({100, 200}), col_ids, writer.tx_id);

    // Reader cannot see uncommitted rows.
    size_t offset = 0;
    DataChunk out;
    size_t n = rg.Scan(offset, col_ids, out, reader);
    assert(n == 0);

    // Commit at t=100; reader started at t=60 → still invisible to reader.
    rg.CommitAppend(100);
    offset = 0;
    DataChunk out2;
    n = rg.Scan(offset, col_ids, out2, reader);
    assert(n == 0);

    // A new reader starting at t=150 sees the committed rows.
    Transaction late_reader = MakeTx(3, 150);
    offset = 0;
    DataChunk out3;
    n = rg.Scan(offset, col_ids, out3, late_reader);
    assert(n == 2);
    assert(out3.columns[0].int_data[0] == 100);
    assert(out3.columns[0].int_data[1] == 200);

    std::cout << "TestRowGroupCommitAppend: PASS\n";
}

static void TestRowGroupRevertAppend() {
    // Append with tx_id then RevertAppend; rows disappear.
    BufferManager bm;
    RowGroup rg(0, {TypeId::INT64}, bm);
    std::vector<size_t> col_ids = {0};
    Transaction tx = MakeTx(1, 50);

    rg.Append(MakeSingleColChunk({7, 8, 9}), col_ids, tx.tx_id);
    assert(rg.RowCount() == 3);

    rg.RevertAppend(tx.tx_id);
    assert(rg.RowCount() == 0);

    // Scan returns nothing.
    Transaction reader = MakeTx(INVALID_TRANSACTION, 0);
    size_t offset = 0;
    DataChunk out;
    size_t n = rg.Scan(offset, col_ids, out, reader);
    assert(n == 0);

    std::cout << "TestRowGroupRevertAppend: PASS\n";
}

static void TestRowGroupFlush() {
    BufferManager bm;
    RowGroup rg(0, {TypeId::INT64}, bm);
    std::vector<size_t> col_ids = {0};
    Transaction tx = MakeTx(INVALID_TRANSACTION, 0);

    std::vector<int64_t> vals;
    for (int i = 0; i < 20; ++i) vals.push_back(i * 5);
    rg.Append(MakeSingleColChunk(vals), col_ids);
    rg.Flush();

    // After flush, pending data is in segments; scan still works.
    size_t offset = 0;
    DataChunk out;
    size_t n = rg.Scan(offset, col_ids, out, tx);
    assert(n == 20);
    assert(out.columns[0].int_data[3] == 15);
    assert(out.columns[0].int_data[7] == 35);

    std::cout << "TestRowGroupFlush: PASS\n";
}

void RunRowGroupTests() {
    TestVersionInfoUnmarkedVisible();
    TestVersionInfoMarkDeleteOwner();
    TestVersionInfoMarkDeleteUncommittedOther();
    TestVersionInfoCommitDeleteBeforeStart();
    TestVersionInfoCommitDeleteAfterStart();
    TestVersionInfoInsertOwnVisible();
    TestVersionInfoInsertUncommittedInvisible();
    TestVersionInfoInsertCommittedBeforeStart();
    TestVersionInfoInsertCommittedAfterStart();
    TestVersionInfoClearDelete();
    TestRowGroupBasicAppendScan();
    TestRowGroupLargeAppendScan();
    TestRowGroupScanMVCCDeleteVisible();
    TestRowGroupScanMVCCDeleteInvisible();
    TestRowGroupCommitAppend();
    TestRowGroupRevertAppend();
    TestRowGroupFlush();
    std::cout << "\nAll RowGroup tests passed.\n";
}
