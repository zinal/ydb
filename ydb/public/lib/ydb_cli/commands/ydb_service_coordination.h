#pragma once

#include <ydb/public/lib/ydb_cli/commands/ydb_common.h>
#include <ydb/public/lib/ydb_cli/commands/ydb_command.h>

namespace NYdb {
namespace NConsoleClient {

class TCommandCoordination : public TClientCommandTree {
public:
    TCommandCoordination();
};

class TCommandCoordCreate : public TYdbSimpleCommand {
public:
    TCommandCoordCreate();
    void Config(TConfig& config) override;
    int Run(TConfig& config) override;

private:
    TString NodeName;
};

class TCommandCoordDrop : public TYdbSimpleCommand {
public:
    TCommandCoordDrop();
    void Config(TConfig& config) override;
    int Run(TConfig& config) override;

private:
    TString NodeName;
};

}
}
