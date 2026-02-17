#pragma once

#include <ydb/public/lib/ydb_cli/commands/ydb_common.h>
#include <ydb/public/lib/ydb_cli/commands/ydb_command.h>

namespace NYdb {
namespace NConsoleClient {

class TCommandCoordination : public TClientCommandTree {
public:
    TCommandCoordination();
};

}
}
