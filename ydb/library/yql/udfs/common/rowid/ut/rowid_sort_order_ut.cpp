#include <ydb/library/yql/udfs/common/rowid/rowid_keygen.h>

#include <library/cpp/testing/unittest/registar.h>

#include <util/random/random.h>

#include <algorithm>
#include <set>

using namespace NYql::NRowidKeyGen;

namespace {
constexpr ui64 kTestRowPrefix = 0xA5A; // 12 bits

std::array<ui8, RowidLen> MakeColumnKey(ui64 epochSeconds) {
    return MakeColumnKeyRowidBytes(epochSeconds);
}

std::array<ui8, RowidLen> MakeRowKey(ui64 prefix, ui64 epochSeconds) {
    return MakeRowKeyRowidBytes(prefix, epochSeconds, true);
}

ui64 ExtractRowTimestamp(const std::array<ui8, RowidLen>& bytes) {
    const ui64 msb = ReadBe64(bytes.data());
    return (msb & RowKeyTimestampMask) >> RowKeyTimestampShift;
}

ui64 ExtractColumnTimestamp(const std::array<ui8, RowidLen>& bytes) {
    const ui64 msb = ReadBe64(bytes.data());
    return (msb & ColumnKeyTimestampMask) >> ColumnKeyTimestampShift;
}
} // namespace

Y_UNIT_TEST_SUITE(TRowidKeyGenSortOrder) {
    Y_UNIT_TEST(RowKeyUsesBottom12PrefixBits) {
        SetRandomSeed(1);
        const ui64 epochSeconds = 1'700'000'000ull;
        const ui64 rawPrefix = 0x12345ull;
        const ui64 expectedParam = rawPrefix & PrefixParamMask;

        const auto fromRaw = MakeRowKeyRowidBytes(rawPrefix, epochSeconds, true);
        SetRandomSeed(1);
        const auto fromBottomBits = MakeRowKeyRowidBytes(expectedParam, epochSeconds, true);
        UNIT_ASSERT_EQUAL(fromRaw, fromBottomBits);

        constexpr ui64 kSmallPrefix = 7;
        SetRandomSeed(2);
        const auto withSmallPrefix = MakeRowKeyRowidBytes(kSmallPrefix, epochSeconds, true);
        SetRandomSeed(2);
        const auto withZeroPrefix = MakeRowKeyRowidBytes(0, epochSeconds, true);
        UNIT_ASSERT(withSmallPrefix != withZeroPrefix);
        UNIT_ASSERT_EQUAL(ExtractPrefixFromRowidBytes(withSmallPrefix.data()), kSmallPrefix);
    }

    Y_UNIT_TEST(RowKeyEmbedsTimestamp) {
        SetRandomSeed(3);
        const ui64 epochSeconds = 1'700'000'123ull;
        const auto bytes = MakeRowKey(kTestRowPrefix, epochSeconds);
        UNIT_ASSERT_EQUAL(ExtractRowTimestamp(bytes), epochSeconds % TimestampModulus);
        UNIT_ASSERT_EQUAL(ExtractPrefixFromRowidBytes(bytes.data()), kTestRowPrefix);
    }

    Y_UNIT_TEST(ColumnKeyEmbedsTimestamp) {
        SetRandomSeed(4);
        const ui64 epochSeconds = 1'700'000'456ull;
        const auto bytes = MakeColumnKey(epochSeconds);
        UNIT_ASSERT_EQUAL(ExtractColumnTimestamp(bytes), epochSeconds % TimestampModulus);
    }

    Y_UNIT_TEST(ColumnKeysSortByTimestamp) {
        SetRandomSeed(5);
        const ui64 earlier = 1'700'000'000ull;
        const ui64 later = earlier + 10;
        const auto earlierGenerated = MakeColumnKey(earlier);
        const auto laterGenerated = MakeColumnKey(later);
        UNIT_ASSERT(std::memcmp(earlierGenerated.data(), laterGenerated.data(), RowidLen) < 0);
    }

    Y_UNIT_TEST(RowKeysWithSamePrefixSortByTimestamp) {
        SetRandomSeed(6);
        const ui64 earlier = 1'700'000'000ull;
        const ui64 later = earlier + 10;
        const auto earlierGenerated = MakeRowKey(kTestRowPrefix, earlier);
        const auto laterGenerated = MakeRowKey(kTestRowPrefix, later);
        UNIT_ASSERT(std::memcmp(earlierGenerated.data(), laterGenerated.data(), RowidLen) < 0);
    }

    Y_UNIT_TEST(ColumnKeySequenceIsSorted) {
        SetRandomSeed(7);
        const ui64 baseEpochSeconds = 1'700'000'000ull;
        std::vector<std::array<ui8, RowidLen>> generated;
        for (ui64 i = 0; i < 16; ++i) {
            generated.push_back(MakeColumnKey(baseEpochSeconds + i));
        }
        auto sorted = generated;
        std::sort(sorted.begin(), sorted.end());
        UNIT_ASSERT_EQUAL(generated, sorted);
    }

    Y_UNIT_TEST(RowKeySequenceWithFixedPrefixIsSorted) {
        SetRandomSeed(8);
        const ui64 baseEpochSeconds = 1'700'000'000ull;
        std::vector<std::array<ui8, RowidLen>> generated;
        for (ui64 i = 0; i < 16; ++i) {
            generated.push_back(MakeRowKey(kTestRowPrefix, baseEpochSeconds + i));
        }
        auto sorted = generated;
        std::sort(sorted.begin(), sorted.end());
        UNIT_ASSERT_EQUAL(generated, sorted);
    }

    Y_UNIT_TEST(SameTimestampColumnKeysAreDistinct) {
        SetRandomSeed(9);
        const ui64 epochSeconds = 1'700'000'000ull;
        std::set<std::array<ui8, RowidLen>> unique;
        for (int i = 0; i < 32; ++i) {
            unique.insert(MakeColumnKey(epochSeconds));
        }
        UNIT_ASSERT_EQUAL(unique.size(), 32u);
    }

    Y_UNIT_TEST(SameTimestampRowKeysAreDistinct) {
        SetRandomSeed(10);
        const ui64 epochSeconds = 1'700'000'000ull;
        std::set<std::array<ui8, RowidLen>> unique;
        for (int i = 0; i < 32; ++i) {
            unique.insert(MakeRowKey(kTestRowPrefix, epochSeconds));
        }
        UNIT_ASSERT_EQUAL(unique.size(), 32u);
    }

    Y_UNIT_TEST(RowKeyWithoutPrefixUsesRandomPrefixBits) {
        SetRandomSeed(11);
        const ui64 epochSeconds = 1'700'000'000ull;
        std::set<ui64> prefixes;
        for (int i = 0; i < 32; ++i) {
            const auto bytes = MakeRowKeyRowidBytes(0, epochSeconds, false);
            prefixes.insert(ExtractPrefixFromRowidBytes(bytes.data()));
        }
        UNIT_ASSERT_GT(prefixes.size(), 1u);
    }

    Y_UNIT_TEST(RowidLengthIsFourteen) {
        SetRandomSeed(12);
        const auto columnKey = MakeColumnKey(Seconds());
        const auto rowKey = MakeRowKeyRowidBytes(0, Seconds(), false);
        UNIT_ASSERT_EQUAL(columnKey.size(), 14u);
        UNIT_ASSERT_EQUAL(rowKey.size(), 14u);
    }
}
