#include "storage/column/compression.hpp"
#include "storage/column/compression/rle.hpp"
#include "storage/column/compression/bit_packing.hpp"
#include "storage/column/compression/delta.hpp"
#include "storage/column/compression/dictionary.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <vector>
#include <cstring>

using namespace cppcoldb;

static constexpr size_t BUF = 256 * 1024;

// Build an INT64 DataVector with the given values (all non-null).
static DataVector MakeIntVec(const std::vector<int64_t>& vals) {
    DataVector v;
    v.Reset(TypeId::INT64, vals.size());
    v.int_data = vals;
    for (size_t i = 0; i < vals.size(); ++i) v.validity.set(i);
    v.count = vals.size();
    return v;
}

static DataVector MakeStrVec(const std::vector<std::string>& vals) {
    DataVector v;
    v.Reset(TypeId::VARCHAR, vals.size());
    v.str_data = vals;
    for (size_t i = 0; i < vals.size(); ++i) v.validity.set(i);
    v.count = vals.size();
    return v;
}

// ---- SelectCompression ----

static void TestSelectCompressionRLE() {
    auto vec = MakeIntVec(std::vector<int64_t>(100, 42));
    auto c = SelectCompression(vec);
    assert(c.type == CompressionType::RLE);
    std::cout << "TestSelectCompressionRLE: PASS\n";
}

static void TestSelectCompressionBitPacked() {
    std::vector<int64_t> vals(100);
    for (int i = 0; i < 100; ++i) vals[i] = i; // range [0, 99] → 7 bits
    auto vec = MakeIntVec(vals);
    auto c = SelectCompression(vec);
    assert(c.type == CompressionType::BIT_PACKED);
    assert(c.bit_width == 7);
    std::cout << "TestSelectCompressionBitPacked: PASS\n";
}

static void TestSelectCompressionDelta() {
    // Use INT32_MAX as step: range = 99 * INT32_MAX ≈ 2^38 → requires > 32 bits.
    // Each delta = INT32_MAX, fits in int32_t. Monotonic → DELTA chosen.
    std::vector<int64_t> big;
    for (int64_t i = 0; i < 100; ++i) big.push_back(i * (int64_t)INT32_MAX);
    auto vec = MakeIntVec(big);
    auto c = SelectCompression(vec);
    assert(c.type == CompressionType::DELTA);
    std::cout << "TestSelectCompressionDelta: PASS\n";
}

static void TestSelectCompressionUncompressedInt() {
    // Large random-ish range, non-monotonic, > 32 bits.
    auto vec = MakeIntVec({1LL << 40, -(1LL << 40), 0, 1LL << 35, -1LL << 35});
    auto c = SelectCompression(vec);
    assert(c.type == CompressionType::UNCOMPRESSED);
    std::cout << "TestSelectCompressionUncompressedInt: PASS\n";
}

static void TestSelectCompressionDictionary() {
    std::vector<std::string> vals;
    for (int i = 0; i < 500; ++i) vals.push_back("cat_" + std::to_string(i % 50));
    auto vec = MakeStrVec(vals);
    auto c = SelectCompression(vec);
    assert(c.type == CompressionType::DICTIONARY);
    std::cout << "TestSelectCompressionDictionary: PASS\n";
}

static void TestSelectCompressionUncompressedVarchar() {
    // 1001 distinct values exceeds the dictionary threshold (1000) → UNCOMPRESSED.
    // Keep under STANDARD_VECTOR_SIZE (1024).
    std::vector<std::string> vals;
    for (int i = 0; i < 1001; ++i) vals.push_back("str_" + std::to_string(i));
    auto vec = MakeStrVec(vals);
    auto c = SelectCompression(vec);
    assert(c.type == CompressionType::UNCOMPRESSED);
    std::cout << "TestSelectCompressionUncompressedVarchar: PASS\n";
}

// ---- Codec round-trips ----

static void TestRleRoundTrip() {
    // Pattern: [5 nulls][3 x value 99][2 nulls][10 x value 7]
    size_t N = 20;
    std::vector<int64_t> vals(N, 0);
    std::vector<uint8_t> bitmap((N + 7) / 8, 0);
    // rows 5..7 = value 99, rows 10..19 = value 7
    for (int i = 5; i < 8; ++i)  { vals[i] = 99; bitmap[i/8] |= (1u << (i%8)); }
    for (int i = 10; i < 20; ++i) { vals[i] = 7;  bitmap[i/8] |= (1u << (i%8)); }

    std::vector<uint8_t> buf(4096, 0);
    size_t bytes = RleEncode(vals.data(), N, bitmap.data(), buf.data(), buf.size());

    std::vector<int64_t> out_vals(N, 0);
    std::vector<uint8_t> out_bitmap((N + 7) / 8, 0);
    RleDecode(buf.data(), bytes, N, out_vals.data(), out_bitmap.data());

    for (int i = 5; i < 8; ++i)   assert(out_vals[i] == 99);
    for (int i = 10; i < 20; ++i) assert(out_vals[i] == 7);
    // Check nulls: rows 0-4 and 8-9 must be NULL (bit unset).
    for (int i = 0; i < 5; ++i)  assert(!(out_bitmap[i/8] & (1u << (i%8))));
    assert(!(out_bitmap[8/8] & (1u << (8%8)))); // row 8 is null → bit must be 0
    assert(!(out_bitmap[9/8] & (1u << (9%8)))); // row 9 is null → bit must be 0
    std::cout << "TestRleRoundTrip: PASS\n";
}

static void TestBitPackRoundTrip() {
    std::vector<int64_t> vals;
    for (int i = 0; i < 256; ++i) vals.push_back(i);
    std::vector<uint8_t> buf(8192, 0);
    size_t bytes = BitPackEncode(vals.data(), vals.size(), 8, buf.data(), buf.size());
    (void)bytes;
    std::vector<int64_t> out(256);
    BitPackDecode(buf.data(), 256, 8, out.data());
    for (int i = 0; i < 256; ++i) assert(out[i] == i);
    std::cout << "TestBitPackRoundTrip: PASS\n";
}

static void TestBitPackOffset() {
    // Values in [1000, 1010] → range 10, 4 bits sufficient.
    std::vector<int64_t> vals;
    for (int i = 0; i < 11; ++i) vals.push_back(1000 + i);
    std::vector<uint8_t> buf(4096, 0);
    BitPackEncode(vals.data(), vals.size(), 4, buf.data(), buf.size());
    std::vector<int64_t> out(11);
    BitPackDecode(buf.data(), 11, 4, out.data());
    for (int i = 0; i < 11; ++i) assert(out[i] == 1000 + i);
    std::cout << "TestBitPackOffset: PASS\n";
}

static void TestDeltaRoundTrip() {
    std::vector<int64_t> vals;
    for (int i = 0; i < 100; ++i) vals.push_back(1000 + i);
    std::vector<uint8_t> buf(4096);
    DeltaEncode(vals.data(), vals.size(), buf.data(), buf.size());
    std::vector<int64_t> out(100);
    DeltaDecode(buf.data(), 100, out.data());
    for (int i = 0; i < 100; ++i) assert(out[i] == vals[i]);
    std::cout << "TestDeltaRoundTrip: PASS\n";
}

static void TestDictionaryRoundTrip() {
    std::vector<std::string> vals;
    for (int i = 0; i < 500; ++i) vals.push_back("word_" + std::to_string(i % 5));
    std::vector<uint8_t> bitmap((vals.size() + 7) / 8, 0xFF); // all non-null
    std::vector<uint8_t> buf(65536, 0);
    size_t bytes = DictEncode(vals.data(), vals.size(), bitmap.data(), buf.data(), buf.size());
    std::vector<std::string> out(vals.size());
    std::vector<uint8_t> out_bitmap((vals.size() + 7) / 8, 0);
    DictDecode(buf.data(), bytes, vals.size(), out.data(), out_bitmap.data());
    for (size_t i = 0; i < vals.size(); ++i) assert(out[i] == vals[i]);
    std::cout << "TestDictionaryRoundTrip: PASS\n";
}

// ---- Compress/Decompress round-trips ----

static void TestCompressDecompressUncompressedInt() {
    auto vec = MakeIntVec({100, 200, 300});
    std::vector<uint8_t> buf(BUF, 0);
    CompressionChoice c{CompressionType::UNCOMPRESSED, 0};
    size_t bytes = Compress(c, vec, buf.data(), BUF);
    DataVector out;
    Decompress(buf.data(), bytes, TypeId::INT64, out);
    assert(out.count == 3);
    assert(out.int_data[0] == 100);
    assert(out.int_data[1] == 200);
    assert(out.int_data[2] == 300);
    std::cout << "TestCompressDecompressUncompressedInt: PASS\n";
}

static void TestCompressDecompressBitPacked() {
    std::vector<int64_t> vals;
    for (int i = 0; i < 50; ++i) vals.push_back(i);
    auto vec = MakeIntVec(vals);
    CompressionChoice c = SelectCompression(vec);
    assert(c.type == CompressionType::BIT_PACKED);
    std::vector<uint8_t> buf(BUF, 0);
    size_t bytes = Compress(c, vec, buf.data(), BUF);
    DataVector out;
    Decompress(buf.data(), bytes, TypeId::INT64, out);
    for (int i = 0; i < 50; ++i) assert(out.int_data[i] == i);
    std::cout << "TestCompressDecompressBitPacked: PASS\n";
}

static void TestCompressDecompressDelta() {
    std::vector<int64_t> vals;
    for (int64_t i = 0; i < 50; ++i) vals.push_back(i * (int64_t)INT32_MAX);
    auto vec = MakeIntVec(vals);
    CompressionChoice c = SelectCompression(vec);
    assert(c.type == CompressionType::DELTA);
    std::vector<uint8_t> buf(BUF, 0);
    size_t bytes = Compress(c, vec, buf.data(), BUF);
    DataVector out;
    Decompress(buf.data(), bytes, TypeId::INT64, out);
    for (int i = 0; i < 50; ++i) assert(out.int_data[i] == vals[i]);
    std::cout << "TestCompressDecompressDelta: PASS\n";
}

static void TestCompressDecompressDictionary() {
    std::vector<std::string> vals;
    for (int i = 0; i < 50; ++i) vals.push_back("cat_" + std::to_string(i % 5));
    auto vec = MakeStrVec(vals);
    CompressionChoice c = SelectCompression(vec);
    assert(c.type == CompressionType::DICTIONARY);
    std::vector<uint8_t> buf(BUF, 0);
    size_t bytes = Compress(c, vec, buf.data(), BUF);
    DataVector out;
    Decompress(buf.data(), bytes, TypeId::VARCHAR, out);
    for (int i = 0; i < 50; ++i) assert(out.str_data[i] == vals[i]);
    std::cout << "TestCompressDecompressDictionary: PASS\n";
}

static void TestNullBitmapPreserved() {
    DataVector vec;
    vec.Reset(TypeId::INT64, 5);
    vec.int_data = {1, 0, 3, 0, 5};
    vec.validity.set(0); // not null
    // row 1: null
    vec.validity.set(2); // not null
    // row 3: null
    vec.validity.set(4); // not null
    vec.count = 5;

    CompressionChoice c{CompressionType::UNCOMPRESSED, 0};
    std::vector<uint8_t> buf(BUF, 0);
    size_t bytes = Compress(c, vec, buf.data(), BUF);
    DataVector out;
    Decompress(buf.data(), bytes, TypeId::INT64, out);

    assert(!out.IsNull(0)); assert(out.int_data[0] == 1);
    assert( out.IsNull(1));
    assert(!out.IsNull(2)); assert(out.int_data[2] == 3);
    assert( out.IsNull(3));
    assert(!out.IsNull(4)); assert(out.int_data[4] == 5);
    std::cout << "TestNullBitmapPreserved: PASS\n";
}

static void TestSegmentHeaderParsed() {
    auto vec = MakeIntVec({10, 20, 30});
    CompressionChoice c{CompressionType::UNCOMPRESSED, 0};
    std::vector<uint8_t> buf(BUF, 0);
    Compress(c, vec, buf.data(), BUF);

    SegmentHeader hdr{};
    std::memcpy(&hdr, buf.data(), sizeof(hdr));
    assert(hdr.compression_type == static_cast<uint8_t>(CompressionType::UNCOMPRESSED));
    assert(hdr.row_count == 3);
    assert(hdr.null_bitmap_size == 1); // ceil(3/8) = 1
    std::cout << "TestSegmentHeaderParsed: PASS\n";
}

void RunCompressionTests() {
    TestSelectCompressionRLE();
    TestSelectCompressionBitPacked();
    TestSelectCompressionDelta();
    TestSelectCompressionUncompressedInt();
    TestSelectCompressionDictionary();
    TestSelectCompressionUncompressedVarchar();
    TestRleRoundTrip();
    TestBitPackRoundTrip();
    TestBitPackOffset();
    TestDeltaRoundTrip();
    TestDictionaryRoundTrip();
    TestCompressDecompressUncompressedInt();
    TestCompressDecompressBitPacked();
    TestCompressDecompressDelta();
    TestCompressDecompressDictionary();
    TestNullBitmapPreserved();
    TestSegmentHeaderParsed();
    std::cout << "\nAll compression tests passed.\n";
}
