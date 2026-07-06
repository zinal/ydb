#include "data_format.h"

#include <library/cpp/streams/zstd/zstd.h>

#include <util/stream/file.h>
#include <util/string/printf.h>

namespace NYdb {
namespace NBackup {

namespace {

constexpr TStringBuf ZstdSuffix = ".zst";

bool IsZstdDataFile(const TFsPath& path) {
    return path.GetExtension() == "zst";
}

} // namespace

bool TryParseCompression(const TString& compression, TCompressionSettings& out) {
    if (compression.empty()) {
        out = TCompressionSettings::None();
        return true;
    }

    if (compression == "zstd") {
        out = TCompressionSettings::Zstd();
        return true;
    }

    constexpr TStringBuf prefix = "zstd-";
    if (compression.StartsWith(prefix)) {
        int level = 0;
        if (!TryFromString(compression.substr(prefix.size()), level) || level < 1 || level > 22) {
            return false;
        }
        out = TCompressionSettings::Zstd(level);
        return true;
    }

    return false;
}

TCompressionSettings ParseCompression(const TString& compression) {
    TCompressionSettings settings;
    if (!TryParseCompression(compression, settings)) {
        ythrow yexception() << "Unknown compression codec: " << compression.Quote()
            << ". Available options: zstd, zstd-N (N is compression level in range [1, 22])";
    }
    return settings;
}

TString DataFileName(ui32 id, const TCompressionSettings& compression) {
    TString name = Sprintf("data_%02d.csv", id);
    if (compression.IsCompressed()) {
        name.append(ZstdSuffix);
    }
    return name;
}

TVector<TFsPath> CollectDataFiles(const TFsPath& tablePath) {
    TVector<TFsPath> dataFiles;

    const TFsPath plainFile = tablePath.Child(DataFileName(0));
    const TFsPath compressedFile = tablePath.Child(DataFileName(0, TCompressionSettings::Zstd()));
    const bool useCompression = !plainFile.Exists() && compressedFile.Exists();

    for (ui32 id = 0;; ++id) {
        const TCompressionSettings compression = useCompression ? TCompressionSettings::Zstd() : TCompressionSettings::None();
        const TFsPath dataFile = tablePath.Child(DataFileName(id, compression));
        if (!dataFile.Exists()) {
            break;
        }
        dataFiles.push_back(dataFile);
    }

    return dataFiles;
}

TTableDataFile::TTableDataFile(const TFsPath& path, const TCompressionSettings& compression)
    : Path_(path.GetPath())
    , File_(path, CreateAlways | WrOnly)
{
    FileOutput_ = MakeHolder<TFileOutput>(File_);
    Output_ = FileOutput_.Get();
    if (compression.IsCompressed()) {
        Compressor_ = MakeHolder<TZstdCompress>(Output_, compression.Level);
        Output_ = Compressor_.Get();
    }
}

TTableDataFile::~TTableDataFile() {
    try {
        Close();
    } catch (...) {
    }
}

void TTableDataFile::DoWrite(const void* buffer, size_t size) {
    Output_->Write(buffer, size);
}

void TTableDataFile::DoFlush() {
    Output_->Flush();
}

void TTableDataFile::DoFinish() {
    Output_->Finish();
}

void TTableDataFile::Write(const void* buffer, size_t size) {
    DoWrite(buffer, size);
}

i64 TTableDataFile::GetLength() const {
    return File_.GetLength();
}

void TTableDataFile::Close() {
    if (Compressor_) {
        Compressor_->Finish();
        Compressor_.Destroy();
    }
    if (FileOutput_) {
        FileOutput_->Finish();
        FileOutput_.Destroy();
    }
    File_.Close();
}

const TString& TTableDataFile::GetPath() const {
    return Path_;
}

TDataFileLineReader::TDataFileLineReader(const TFsPath& path, size_t bufferSize)
    : FileInput_(path, bufferSize)
{
    Input_ = &FileInput_;
    if (IsZstdDataFile(path)) {
        Decompressor_ = MakeHolder<TZstdDecompress>(&FileInput_, bufferSize);
        Input_ = Decompressor_.Get();
    }
}

bool TDataFileLineReader::ReadLine(TString& line) {
    return Input_->ReadLine(line);
}

} // namespace NBackup
} // namespace NYdb
