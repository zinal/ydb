#pragma once

#include <yql/essentials/types/rowid/rowid.h>

#include <util/generic/maybe.h>
#include <util/random/random.h>

#include <algorithm>
#include <array>
#include <cstring>

// Rowid generators for YDB primary keys.
//
// Rowid is a 14-byte opaque value. Internal and external byte orders coincide
// (unlike Uuid, which uses Microsoft GUID mixed-endian layout in YDB).
//
// Layouts follow the pk_generation RFC (same bit fields as UUIDv8 row/column
// keys, but without version/variant nibbles — the random suffix is shorter so
// the whole value fits in 14 bytes).

namespace NYql::NRowidKeyGen {

static constexpr ui32 RowidLen = NKikimr::NRowid::ROWID_LEN;

// Row-key layout:
//   [12 prefix][31 timestamp sec][5 random][64 random]
static constexpr ui32 PrefixBits = 12;
static constexpr ui32 TimestampBits = 31;
static constexpr ui64 TimestampModulus = 1ULL << TimestampBits;
static constexpr ui64 PrefixMsbMask = ((1ULL << PrefixBits) - 1) << (64 - PrefixBits);
static constexpr ui64 PrefixParamMask = (1ULL << PrefixBits) - 1;
static constexpr ui32 RowKeyTimestampShift = 64 - PrefixBits - TimestampBits;
static constexpr ui64 RowKeyTimestampMask = ((1ULL << TimestampBits) - 1) << RowKeyTimestampShift;

// Column-key layout:
//   [31 timestamp sec][1 random][80 random]
static constexpr ui32 ColumnKeyTimestampShift = 64 - TimestampBits;
static constexpr ui64 ColumnKeyTimestampMask = ((1ULL << TimestampBits) - 1) << ColumnKeyTimestampShift;

static constexpr ui64 MaxRowGroupCount = 1'000'000;

inline ui64 ReadBe64(const ui8* data) {
    ui64 value = 0;
    for (ui32 i = 0; i < 8; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

inline void WriteBe64(ui64 value, ui8* data) {
    for (int i = 7; i >= 0; --i) {
        data[i] = static_cast<ui8>(value & 0xff);
        value >>= 8;
    }
}

inline void FillRandomBytes(ui8* data, size_t size) {
    for (size_t offset = 0; offset < size; offset += sizeof(ui64)) {
        const ui64 random = RandomNumber<ui64>();
        std::memcpy(data + offset, &random, std::min(size - offset, sizeof(ui64)));
    }
}

inline ui64 PrefixParamToMsb(ui64 prefix) {
    return (prefix & PrefixParamMask) << (64 - PrefixBits);
}

inline ui64 ExtractPrefixFromRowidBytes(const ui8* data) {
    const ui64 msb = ReadBe64(data);
    return (msb & PrefixMsbMask) >> (64 - PrefixBits);
}

inline ui64 GetRowKeyTimestampCode(ui64 epochSeconds) {
    return (epochSeconds % TimestampModulus) << RowKeyTimestampShift;
}

inline ui64 GetColumnKeyTimestampCode(ui64 epochSeconds) {
    return (epochSeconds % TimestampModulus) << ColumnKeyTimestampShift;
}

inline ui64 UpdateMsbRowKey(ui64 msb, ui64 prefix, ui64 epochSeconds, bool hasPrefix) {
    const ui64 tsCode = GetRowKeyTimestampCode(epochSeconds);
    if (hasPrefix) {
        return (msb & ~(PrefixMsbMask | RowKeyTimestampMask))
            | (PrefixParamToMsb(prefix) | (tsCode & RowKeyTimestampMask));
    }
    return (msb & ~RowKeyTimestampMask) | (tsCode & RowKeyTimestampMask);
}

inline ui64 UpdateMsbColumnKey(ui64 msb, ui64 epochSeconds) {
    const ui64 tsCode = GetColumnKeyTimestampCode(epochSeconds);
    return (msb & ~ColumnKeyTimestampMask) | (tsCode & ColumnKeyTimestampMask);
}

// Build a row-table Rowid key.
//
// Sort order (memcmp): (1) 12-bit random prefix; (2) 31-bit second-granularity
// timestamp; (3) random suffix. Without an explicit prefix, prefix bits stay
// random. With hasPrefix=true, the prefix is fixed (used by newRowGroup).
inline std::array<ui8, RowidLen> MakeRowKeyRowidBytes(
    ui64 prefix, ui64 epochSeconds, bool hasPrefix)
{
    std::array<ui8, RowidLen> result{};
    FillRandomBytes(result.data(), result.size());

    ui64 msb = ReadBe64(result.data());
    msb = UpdateMsbRowKey(msb, prefix, epochSeconds, hasPrefix);
    WriteBe64(msb, result.data());

    return result;
}

// Build a column-table Rowid key.
//
// Sort order (memcmp): 31-bit second-granularity timestamp first, then random
// suffix. No partition prefix — column tables use hash partitioning.
inline std::array<ui8, RowidLen> MakeColumnKeyRowidBytes(ui64 epochSeconds) {
    std::array<ui8, RowidLen> result{};
    FillRandomBytes(result.data(), result.size());

    ui64 msb = ReadBe64(result.data());
    msb = UpdateMsbColumnKey(msb, epochSeconds);
    WriteBe64(msb, result.data());

    return result;
}

} // namespace NYql::NRowidKeyGen
