# Feature: Column Compression

## 1. Purpose

Column Compression selects and applies a compression algorithm when writing a `ColumnSegment` to a block, and decompresses that block back to a flat `DataVector` when reading. Compression is transparent to operators: they always receive uncompressed `DataVector`s. The scheme is chosen per segment at flush time based on the data's value distribution. Supporting algorithms are: RLE (run-length encoding), bit-packing, delta encoding, and dictionary encoding.

## 2. Responsibilities

- `CompressionAnalyzer`: examine a `DataVector` and decide the best `CompressionType` for its values
- `Compress(type, vec, out_buffer)`: encode a `DataVector` into a byte buffer using the chosen scheme, prefixed by the segment header
- `Decompress(buffer, type, row_count, col_type, out_vec)`: read the byte buffer and expand it back into a `DataVector`
- Implement four algorithm modules: `RleCodec`, `BitPackCodec`, `DeltaCodec`, `DictionaryCodec`
- Write a segment header at the start of every block: `[compression_type: uint8][row_count: uint32][data...]`
- Ensure decompression is correct for any block written by the corresponding compressor
- Handle NULL values correctly: the validity bitmask is stored separately in the segment block before the value data

## 3. Non-Responsibilities

- Does not decide when to flush a segment (that is `ColumnChunk`)
- Does not manage buffer pool blocks (that is `BufferManager`)
- Does not perform cross-column compression
- Does not compress floating-point columns with lossy codecs
- Does not implement LZ4/Snappy/Zstandard (only column-oriented codecs)

## 4. Architecture Design

```
ColumnChunk::Flush(values: DataVector)
        |
        v
CompressionAnalyzer::SelectScheme(values, col_type)
        |
        +-- INTEGER types:
        |     All identical? → RLE
        |     Fits in N bits? → BIT_PACKED
        |     Monotonic?     → DELTA
        |     Else           → UNCOMPRESSED
        |
        +-- FLOAT types: → UNCOMPRESSED
        |
        +-- VARCHAR types:
              Low cardinality (< 1000 distinct)? → DICTIONARY
              Else                               → UNCOMPRESSED
        |
        v
Compress(scheme, values) → byte buffer (segment header + data)
        |
        v
BufferManager block (written to BlockFile at checkpoint)


ColumnChunk::Scan(...)
        |
        v
BufferManager::Pin(block_id) → raw bytes
        |
        v
Read segment header: compression_type, row_count
        |
        v
Decompress(compression_type, bytes, row_count, col_type) → DataVector
```

## 5. Core Data Structures (C++)

```cpp
// src/storage/column/compression.hpp
#pragma once
#include <vector>
#include <cstdint>
#include "common/types.hpp"
#include "storage/column/column_segment.hpp"

namespace cppcoldb {

// Segment block header (written at byte 0 of every column segment block).
struct SegmentHeader {
    uint8_t  compression_type; // CompressionType enum value
    uint32_t row_count;        // number of rows stored in this segment
    uint32_t null_bitmap_size; // bytes used by the validity bitmap
    // Followed by: validity bitmap, then compressed value data.
};
static_assert(sizeof(SegmentHeader) == 9, "SegmentHeader size changed");

// Compression analysis result.
struct CompressionChoice {
    CompressionType type;
    uint8_t         bit_width;  // only for BIT_PACKED: bits per value (1-64)
};

// Analyze a DataVector and pick the best compression scheme.
CompressionChoice SelectCompression(const DataVector& vec);

// Compress vec into dst_buffer (must be pre-allocated to block_size bytes).
// Returns the number of bytes written.
size_t Compress(CompressionChoice choice, const DataVector& vec,
                uint8_t* dst_buffer, size_t buffer_size);

// Decompress src_buffer into dst_vec.
// src_buffer must start with a SegmentHeader.
void Decompress(const uint8_t* src_buffer, size_t buffer_size,
                TypeId col_type, DataVector& dst_vec);

} // namespace cppcoldb
```

```cpp
// src/storage/column/compression/rle.hpp
namespace cppcoldb {
// RLE for integer types: [(value: int64_t)(count: uint32_t)] pairs.
size_t RleEncode(const int64_t* values, const bool* nulls,
                 size_t row_count, uint8_t* dst, size_t dst_size);
void   RleDecode(const uint8_t* src, size_t row_count,
                 int64_t* values_out, bool* nulls_out);
}

// src/storage/column/compression/bit_packing.hpp
namespace cppcoldb {
// Bit-packing: each value stored in bit_width bits, packed LSB-first.
size_t BitPackEncode(const int64_t* values, size_t row_count,
                     uint8_t bit_width, uint8_t* dst, size_t dst_size);
void   BitPackDecode(const uint8_t* src, size_t row_count,
                     uint8_t bit_width, int64_t* values_out);
}

// src/storage/column/compression/delta.hpp
namespace cppcoldb {
// Delta: store first value as int64_t, then deltas as int32_t.
size_t DeltaEncode(const int64_t* values, size_t row_count,
                   uint8_t* dst, size_t dst_size);
void   DeltaDecode(const uint8_t* src, size_t row_count, int64_t* values_out);
}

// src/storage/column/compression/dictionary.hpp
namespace cppcoldb {
// Dictionary: build string→int16_t code map; store codes array + dictionary.
size_t DictEncode(const std::string* values, const bool* nulls,
                  size_t row_count, uint8_t* dst, size_t dst_size);
void   DictDecode(const uint8_t* src, size_t row_count,
                  std::string* values_out, bool* nulls_out);
}
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

// Analyze and choose a compression scheme.
CompressionChoice SelectCompression(const DataVector& vec);

// Compress a DataVector into a raw byte buffer (returns bytes written).
size_t Compress(CompressionChoice choice, const DataVector& vec,
                uint8_t* dst_buffer, size_t buffer_size);

// Decompress a raw byte buffer (starting with SegmentHeader) into a DataVector.
void Decompress(const uint8_t* src_buffer, size_t buffer_size,
                TypeId col_type, DataVector& dst_vec);

} // namespace cppcoldb
```

## 7. Internal Algorithms

### SelectCompression
```
SelectCompression(vec):
  if vec is INTEGER type:
    if AllIdentical(vec):
      return {RLE, 0}
    bit_width = MinBitsRequired(vec)   // floor(log2(max - min)) + 1
    if bit_width <= 32:
      return {BIT_PACKED, bit_width}
    if IsMonotonic(vec):
      return {DELTA, 0}
    return {UNCOMPRESSED, 0}

  if vec is FLOAT type:
    return {UNCOMPRESSED, 0}

  if vec is VARCHAR type:
    distinct_count = CountDistinct(vec)
    if distinct_count <= 1000:
      return {DICTIONARY, 0}
    return {UNCOMPRESSED, 0}

AllIdentical(vec): O(n) scan; true if all non-NULL values equal
MinBitsRequired(vec): O(n) find min and max; width = bits needed to store (max - min)
IsMonotonic(vec): O(n) scan; true if values are non-decreasing
CountDistinct(vec): O(n) scan with unordered_set
Time: O(n) for any column type
```

### RLE encode
```
RleEncode(values, nulls, row_count, dst, dst_size):
  out_pos = 0
  i = 0
  while i < row_count:
    if nulls[i]:
      run_len = count consecutive nulls from i
      write tag=NULL_RUN, count=run_len to dst[out_pos]
      i += run_len
    else:
      val = values[i]
      run_len = 1
      while i + run_len < row_count AND not nulls[i+run_len]
            AND values[i+run_len] == val:
        run_len++
      write tag=VALUE_RUN, value=val, count=run_len to dst[out_pos]
      i += run_len
  return out_pos  // bytes written
  Time: O(n)

RleDecode(src, row_count, values_out, nulls_out):
  in_pos = 0; out_i = 0
  while out_i < row_count:
    tag = read from src[in_pos]
    if tag == NULL_RUN:
      count = read uint32; fill nulls_out[out_i..out_i+count] = true; out_i += count
    else:
      value = read int64; count = read uint32
      fill values_out[out_i..out_i+count] = value; out_i += count
  Time: O(n)
```

### Bit-pack encode
```
BitPackEncode(values, row_count, bit_width, dst, dst_size):
  // NOTE: NULL rows are handled by the validity bitmap (written separately by Compress()).
  // values[] for NULL rows may contain arbitrary garbage — they are skipped at decode
  // by checking the validity bitmap. bit_width covers only NON-NULL value range.

  min_val = FindMin(values, row_count)   // O(n) scan over non-null values
  write min_val as int64 at dst[0..7]   // 8 bytes, little-endian
  bit_pos = 64                           // start packing at bit 64 (after min_val)
  for i in 0..row_count:
    delta = values[i] - min_val         // offset-encode: always in [0, 2^bit_width)
    WriteNBits(dst, bit_pos, bit_width, delta)
    bit_pos += bit_width
  return ceil((bit_pos) / 8)            // total bytes written
  Time: O(n)

BitPackDecode(src, row_count, bit_width, values_out):
  min_val = read int64 from src[0..7]
  bit_pos = 64
  for i in 0..row_count:
    delta = ReadNBits(src, bit_pos, bit_width)
    values_out[i] = min_val + static_cast<int64_t>(delta)
    bit_pos += bit_width
  // Caller then applies the validity bitmap to restore NULLs.
  Time: O(n)
```

### WriteNBits / ReadNBits (bit-level helpers)
```
// Pack a w-bit unsigned integer 'value' into 'buf' starting at bit offset 'bit_pos'.
// Bits are packed LSB-first within each byte.
//
// Example: bit_width=3, value=5 (binary 101)
//   If bit_pos=0:  byte[0] gets bits 0-2 set to 101  → byte[0] = 0b????_?101
//   If bit_pos=6:  bits 0-1 go into byte[0] bits 6-7, bit 2 goes into byte[1] bit 0
//
WriteNBits(buf: uint8_t*, bit_pos: size_t, w: uint8_t, value: uint64_t):
  // value must fit in w bits: assert(value < (1ULL << w))
  remaining = w
  while remaining > 0:
    byte_idx  = bit_pos / 8
    bit_in_byte = bit_pos % 8          // which bit within that byte (0=LSB)
    bits_available = 8 - bit_in_byte   // how many bits left in current byte
    bits_to_write = min(remaining, bits_available)

    mask  = (1ULL << bits_to_write) - 1        // e.g. bits_to_write=3 → mask=0b111
    chunk = (value >> (w - remaining)) & mask   // take the next bits_to_write bits
    buf[byte_idx] |= (chunk << bit_in_byte)     // place them at the right position

    bit_pos   += bits_to_write
    remaining -= bits_to_write

// Read a w-bit unsigned integer from 'buf' starting at bit offset 'bit_pos'.
ReadNBits(buf: const uint8_t*, bit_pos: size_t, w: uint8_t) -> uint64_t:
  result    = 0
  bits_read = 0
  while bits_read < w:
    byte_idx    = bit_pos / 8
    bit_in_byte = bit_pos % 8
    bits_available = 8 - bit_in_byte
    bits_to_read   = min(w - bits_read, bits_available)

    mask  = (1ULL << bits_to_read) - 1
    chunk = (buf[byte_idx] >> bit_in_byte) & mask   // extract bits from current byte
    result |= (chunk << bits_read)                  // place at correct position

    bit_pos   += bits_to_read
    bits_read += bits_to_read
  return result

// Concrete example: pack values [5, 3, 7] with bit_width=3 into buf[]
//   min_val=3, so deltas are [2, 0, 4]
//   binary:  2=010  0=000  4=100
//   Packed layout (LSB-first within bytes):
//     bit 0-2:  010  (delta for row 0)
//     bit 3-5:  000  (delta for row 1)
//     bit 6-8:  100  (delta for row 2, spans bytes 0 and 1)
//   buf[0] = 0b01_000_010 = 0x42
//   buf[1] = 0b??????_10  (low 2 bits) → 0b????_??10 = 0x?2 masked
//   After decoding: values_out = [3+2, 3+0, 3+4] = [5, 3, 7] ✓
```

### Delta encode
```
DeltaEncode(values, row_count, dst, dst_size):
  write values[0] as int64 at dst[0]  // first value verbatim
  for i in 1..row_count:
    delta = values[i] - values[i-1]   // assumes monotonic; delta >= 0
    write delta as int32 at dst[8 + (i-1)*4]
  return 8 + (row_count - 1) * 4
  Time: O(n)

DeltaDecode(src, row_count, values_out):
  values_out[0] = read int64 from src[0]
  for i in 1..row_count:
    delta = read int32 from src[8 + (i-1)*4]
    values_out[i] = values_out[i-1] + delta
  Time: O(n)
```

### Dictionary encode
```
DictEncode(values, nulls, row_count, dst, dst_size):
  dict = {} // string -> uint16_t code
  codes = []
  for i in 0..row_count:
    if nulls[i]: codes.append(NULL_CODE=0xFFFF)
    else:
      if values[i] not in dict:
        dict[values[i]] = len(dict)
      codes.append(dict[values[i]])
  write dict size as uint32
  for (str, code) in dict (ordered by code):
    write str_len as uint16, write str bytes
  write codes array as uint16[row_count]
  Time: O(n * avg_str_len)

DictDecode(src, row_count, values_out, nulls_out):
  dict_size = read uint32
  dict = []  // index -> string
  for i in 0..dict_size:
    str_len = read uint16; str = read str_len bytes; dict.append(str)
  for i in 0..row_count:
    code = read uint16
    if code == 0xFFFF: nulls_out[i] = true
    else: values_out[i] = dict[code]; nulls_out[i] = false
  Time: O(n + dict_entries * avg_str_len)
```

## 8. Persistence Model

Each compressed segment is stored as a single block in the `BlockFile`. The block starts with a 9-byte `SegmentHeader` followed by the validity bitmap followed by compressed value data. The `ColumnSegment` metadata (stored in the checkpoint catalog snapshot) records the `block_id`, `compression_type`, and `row_count` so the decompressor can be selected at read time.

```
Block layout:
  [0..8]     SegmentHeader  (compression_type uint8, row_count uint32, null_bitmap_size uint32)
  [9..9+K-1] validity bitmap (K = ceil(row_count / 8) bytes; bit i = 1 → row i is NOT NULL)
  [9+K..]    compressed value data (format depends on compression_type)
```

## 9. Concurrency Model

Compression and decompression functions are stateless and take no locks. They operate on caller-owned buffers. Thread safety is guaranteed by the caller ensuring exclusive access to the target buffer.

## 10. Configuration

```cpp
struct CompressionConfig {
    // Maximum distinct values in a VARCHAR column before falling back to UNCOMPRESSED.
    size_t dictionary_max_cardinality = 1000;

    // Maximum bit-width to use BIT_PACKED instead of UNCOMPRESSED.
    uint8_t bit_pack_max_width = 32;
};
```

## 11. Testing Strategy

- `TestSelectCompressionRLE`: DataVector with all identical int values → RLE chosen
- `TestSelectCompressionBitPacked`: DataVector with range [0, 100] → BIT_PACKED with 7-bit width
- `TestSelectCompressionDelta`: monotonically increasing IDs → DELTA chosen
- `TestSelectCompressionUncompressedInt`: large random int range → UNCOMPRESSED
- `TestSelectCompressionDictionary`: 50 distinct strings in 1000 rows → DICTIONARY
- `TestSelectCompressionUncompressedVarchar`: 2000 distinct strings → UNCOMPRESSED
- `TestRleRoundTrip`: encode then decode 1000 rows with runs → exact values and nulls match
- `TestBitPackRoundTrip`: encode [0..255] with 8-bit width → decode matches
- `TestBitPackOffset`: min_val = 1000; values [1000..1010] → encoded as 4-bit deltas
- `TestDeltaRoundTrip`: monotonic [1000, 1001, ..., 2000] → decode matches
- `TestDictionaryRoundTrip`: 5 distinct strings repeated across 500 rows → decode matches
- `TestNullBitmapPreserved`: mix of NULL and non-NULL rows; compress + decompress → nulls preserved
- `TestCompressDecompressAllTypes`: round-trip for each CompressionType × TypeId combination
- `TestSegmentHeaderParsed`: verify SegmentHeader fields correctly written and read back

## 12. Open Questions

- FLOAT compression: floating-point data is left uncompressed. A future implementation could add XOR-based float compression (e.g., Gorilla-style).
- Large VARCHAR values: strings exceeding a few KB within a column segment may cause the block to overflow `block_size`. Deferred — assumes reasonable VARCHAR sizes.
