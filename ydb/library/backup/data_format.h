#pragma once

#include <library/cpp/streams/zstd/zstd.h>

#include <util/folder/path.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/file.h>
#include <util/stream/input.h>
#include <util/stream/output.h>
#include <util/system/file.h>

namespace NYdb {
namespace NBackup {

enum class ECompressionCodec {
    None,
    Zstd,
};

struct TCompressionSettings {
    ECompressionCodec Codec = ECompressionCodec::None;
    // 0 means ZSTD default compression level (3).
    int Level = 0;

    static TCompressionSettings None() {
        return {};
    }

    static TCompressionSettings Zstd(int level = 0) {
        TCompressionSettings settings;
        settings.Codec = ECompressionCodec::Zstd;
        settings.Level = level;
        return settings;
    }

    bool IsCompressed() const {
        return Codec == ECompressionCodec::Zstd;
    }
};

bool TryParseCompression(const TString& compression, TCompressionSettings& out);
TCompressionSettings ParseCompression(const TString& compression);

TString DataFileName(ui32 id, const TCompressionSettings& compression = {});

TVector<TFsPath> CollectDataFiles(const TFsPath& tablePath);

class TTableDataFile: public IOutputStream {
public:
    TTableDataFile(const TFsPath& path, const TCompressionSettings& compression);
    ~TTableDataFile() override;

    void Write(const void* buffer, size_t size);
    i64 GetLength() const;
    void Close();
    const TString& GetPath() const;

private:
    void DoWrite(const void* buffer, size_t size) override;
    void DoFlush() override;
    void DoFinish() override;

    const TString Path_;
    TFile File_;
    THolder<TFileOutput> FileOutput_;
    THolder<TZstdCompress> Compressor_;
    IOutputStream* Output_ = nullptr;
};

class TDataFileLineReader {
public:
    TDataFileLineReader(const TFsPath& path, size_t bufferSize);

    bool ReadLine(TString& line);

private:
    TFileInput FileInput_;
    THolder<TZstdDecompress> Decompressor_;
    IInputStream* Input_ = nullptr;
};

} // namespace NBackup
} // namespace NYdb
