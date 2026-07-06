#pragma once

#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/types/status/status.h>

#include <ydb/library/backup/data_format.h>

#include <util/generic/maybe.h>
#include <util/folder/path.h>
#include <util/stream/str.h>
#include <util/system/file.h>

class TRegExMatch;

namespace NYdb {

inline namespace Dev {
class TDriver;
class TResultSetParser;
class TValue;

namespace NTable {
    class TTableDescription;
}
}

namespace NBackup {

class TYdbErrorException : public yexception {
public:
    TStatus Status;

    TYdbErrorException(const TStatus& status)
        : Status(status) {}

    void LogToStderr() const;
};

void BackupFolder(
    const TDriver& driver,
    const TString& database,
    const TString& relDbPath,
    TFsPath folderPath,
    const TVector<TRegExMatch>& exclusionPatterns,
    bool schemaOnly,
    bool useConsistentCopyTable,
    bool avoidCopy = false,
    bool savePartialResult = false,
    bool preservePoolKinds = false,
    bool ordered = false,
    const TCompressionSettings& compression = {});

void BackupCluster(const TDriver& driver, TFsPath folderPath);
void BackupDatabase(const TDriver& driver, const TString& database, TFsPath folderPath);

// For unit-tests only
TMaybe<TValue> ProcessResultSet(
    TStringStream& ss,
    TResultSetParser& resultSetParser,
    IOutputStream* dataFile = nullptr,
    const NTable::TTableDescription* desc = nullptr);

} // NBackup
} // NYdb
