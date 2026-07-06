#include <ydb/library/backup/data_format.h>

#include <library/cpp/testing/unittest/registar.h>

#include <util/folder/tempdir.h>
#include <util/stream/file.h>

namespace NYdb::NBackup {

Y_UNIT_TEST_SUITE(BackupDataFormat) {

Y_UNIT_TEST(ParseCompression) {
    TCompressionSettings settings;
    UNIT_ASSERT(TryParseCompression("", settings));
    UNIT_ASSERT(!settings.IsCompressed());

    UNIT_ASSERT(TryParseCompression("zstd", settings));
    UNIT_ASSERT(settings.IsCompressed());
    UNIT_ASSERT_VALUES_EQUAL(settings.Level, 0);

    UNIT_ASSERT(TryParseCompression("zstd-7", settings));
    UNIT_ASSERT(settings.IsCompressed());
    UNIT_ASSERT_VALUES_EQUAL(settings.Level, 7);

    UNIT_ASSERT(!TryParseCompression("gzip", settings));
    UNIT_ASSERT(!TryParseCompression("zstd-0", settings));
    UNIT_ASSERT(!TryParseCompression("zstd-23", settings));
}

Y_UNIT_TEST(DataFileName) {
    UNIT_ASSERT_VALUES_EQUAL(DataFileName(0), "data_00.csv");
    UNIT_ASSERT_VALUES_EQUAL(DataFileName(12, TCompressionSettings::Zstd()), "data_12.csv.zst");
}

Y_UNIT_TEST(RoundTripCompressedDataFile) {
    TTempDir tempDir;
    const TFsPath filePath = tempDir.Path() / "data_00.csv.zst";
    const TString payload = "1,\"%D0%9F%D1%80%D0%B8%D0%B2%D0%B5%D1%82\"\n2,\"world\"\n";

    {
        TTableDataFile writer(filePath, TCompressionSettings::Zstd());
        writer.Write(payload.data(), payload.size());
        writer.Close();
    }

    TDataFileLineReader reader(filePath, 65536);
    TString line;
    UNIT_ASSERT(reader.ReadLine(line));
    UNIT_ASSERT_VALUES_EQUAL(line, "1,\"%D0%9F%D1%80%D0%B8%D0%B2%D0%B5%D1%82\"");
    UNIT_ASSERT(reader.ReadLine(line));
    UNIT_ASSERT_VALUES_EQUAL(line, "2,\"world\"");
    UNIT_ASSERT(!reader.ReadLine(line));
}

Y_UNIT_TEST(CollectDataFilesDetectsCompression) {
    TTempDir tempDir;
    const TFsPath tablePath = tempDir.Path();

    TFileOutput(tablePath / "data_00.csv.zst").Write("compressed");
    TFileOutput(tablePath / "data_01.csv.zst").Write("compressed");

    const auto files = CollectDataFiles(tablePath);
    UNIT_ASSERT_VALUES_EQUAL(files.size(), 2u);
    UNIT_ASSERT(files[0].GetName().EndsWith(".zst"));
    UNIT_ASSERT(files[1].GetName().EndsWith(".zst"));
}

Y_UNIT_TEST(CollectDataFilesPrefersPlainCsv) {
    TTempDir tempDir;
    const TFsPath tablePath = tempDir.Path();

    TFileOutput(tablePath / "data_00.csv").Write("plain");

    const auto files = CollectDataFiles(tablePath);
    UNIT_ASSERT_VALUES_EQUAL(files.size(), 1u);
    UNIT_ASSERT_VALUES_EQUAL(files[0].GetName(), "data_00.csv");
}

} // Y_UNIT_TEST_SUITE

} // namespace NYdb::NBackup
